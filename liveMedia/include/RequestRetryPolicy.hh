/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// When an RTSP request fails: retry THAT REQUEST, or rebuild the whole session?
//
// The proxy used to always rebuild. That is enormously more expensive than it looks, and the cost is
// not the handshake -- it is the fan-out clients.
//
// MEASURED, one real recovery on 192.168.11.19 (fixture:
// tetherbox spec/fixtures/live555/recovery_gap_verbose_trace_192.168.11.19_2026-07-20.log):
// camera RST at 13:05:39, video restored at 13:09:58. 259 seconds. The proxy's own protocol work in
// that window totalled ~40 MILLISECONDS. Everything else was this loop, three times over:
//
//     SETUP sent -> no SETUP-ok -> ~10s -> full session reset -> re-DESCRIBE (3ms)
//       -> WAIT for a downstream client to re-subscribe -> SETUP sent -> ...
//
// The re-subscribe waits were 129.7s, 40s and 9.5s. They exist ONLY because the reset dropped the
// fan-out clients: with nobody attached, `createNewStreamSource` is never called, so the proxy has
// no reason to touch upstream and simply idles until a consumer returns. One unanswered SETUP thus
// costs a discarded DESCRIBE *and* a multi-minute client-reconnect wait.
//
// GStreamer's rtspsrc does not work this way. Read from gst-plugins-good/gst/rtsp/gstrtspsrc.c:
//   * SETUP failure          -> retry SETUP (next transport, or the original port pair)
//   * conn dropped mid-request -> reconnect and replay the SAME request (`goto again`, try==0)
//   * response timeout       -> send a keep-alive and `continue`
// In none of those does it discard a completed DESCRIBE, so its downstream clients are never
// dropped and those waits never occur. This header encodes that behaviour, with its numbers.
//
// Header-only and dependency-free so it is unit-testable standalone
// (test/test_request_retry.cpp), matching RTPTcpTeardown.hh / StreamRole.hh / SessionLifecycle.hh.

#ifndef _REQUEST_RETRY_POLICY_HH
#define _REQUEST_RETRY_POLICY_HH

// For the symbolic errno names in classifyRequestFailure. Included HERE rather than left to the
// includer: relying on include order is how SessionLifecycle.hh compiled everywhere until it did
// not, and how StreamFaultCodes.hh broke the box build while passing on macOS.
#include <errno.h>

// Which request failed. They are NOT interchangeable: see decideRequestFailure.
enum RequestKind {
  REQ_DESCRIBE,
  REQ_SETUP,
  REQ_PLAY
};

enum RequestTransport {
  REQ_TRANSPORT_TCP,
  REQ_TRANSPORT_UDP
};

// Greppable transport names for the TX-RESET line's transport= field (the fourth logged
// vocabulary, alongside reason=/cause=/state=). Media carried over the RTSP connection is
// interleaved by definition, so the TCP name says so. Mirrored by FlapDiag::TRANSPORTS in the
// tetherbox repo; the parity spec fails if the two lists disagree in either direction.
//
// Why the field exists: fault tallies could not be SPLIT by transport, so "the stall affects both
// TCP and UDP" in the Merit-Lilin 761 report (2026-07-21, 47 of 49 cameras) could only be
// asserted from separate ad-hoc testing, not from the journal that recorded every incident.
inline char const* requestTransportName(RequestTransport t) {
  switch (t) {
    case REQ_TRANSPORT_TCP: return "tcp-interleaved";
    case REQ_TRANSPORT_UDP: return "udp";
  }
  return "unknown"; // unreachable for a valid enum; never let a new member print as a number
}

// A rejected transport must be REPLACED, not re-sent. GStreamer walks an ordered protocol mask
// (gstrtspsrc.c:8035-8040 -- UDP, UDP_MCAST, TCP) and on GST_RTSP_STS_UNSUPPORTED_TRANSPORT
// advances it before retrying (:8744-8750). We carry two transports, so the "next" one is simply
// the other one.
//
// NOTE this is a fallback after an EXPLICIT REJECTION, which is a different thing from forcing TCP
// pre-emptively. Forcing TCP has already been investigated here and found to be a red herring for
// the wedge (it masks a symptom on one camera, a force_tcp camera still wedges, and it violates the
// ONVIF-standard UDP default). Switching only when the camera has actually said 461 carries none of
// that: the camera has told us the transport is unusable.
inline RequestTransport nextTransportAfterRejection(RequestTransport rejected) {
  return rejected == REQ_TRANSPORT_UDP ? REQ_TRANSPORT_TCP : REQ_TRANSPORT_UDP;
}

// WHY the request failed. The cause changes the correct response, and collapsing them all into
// "it failed" is what produced the always-reset behaviour. Each maps to a specific path read from
// gst-plugins-good/gst/rtsp/gstrtspsrc.c.
enum RequestFailureCause {
  // No request failed. Most session rebuilds are not request failures at all -- an inter-packet gap,
  // an upstream BYE, a subsession timeout. They need a way to say "this field does not apply"; the
  // alternative is picking a plausible-looking cause, which is fabrication by another name and would
  // poison every tally that groups by cause.
  REQ_FAIL_NONE,
  // No response within the deadline. GStreamer: send a keep-alive and `continue` -- the session is
  // kept and the connection is assumed good until proven otherwise (GST_RTSP_ETIMEOUT case).
  REQ_FAIL_TIMEOUT,
  // The server answered, but rejected the transport we asked for.
  // GStreamer: GST_RTSP_STS_UNSUPPORTED_TRANSPORT -> retry SETUP with the next protocol, or with the
  // original port pair. Never a session rebuild.
  REQ_FAIL_TRANSPORT_UNSUPPORTED,
  // 401 (or 404-as-disguised-auth; see classifyRequestFailure). GStreamer's keep-alive path retries
  // auth exactly once -- `if (gst_rtspsrc_setup_auth (src, &message) && !(retry++))`,
  // gstrtspsrc.c:6952; its REQUEST path re-attempts auth per pass of gst_rtspsrc_send, bounded by
  // that loop's `count++ > 8` and the `tried_url_auth` gate (:7501-7505, :7829-7835), which in
  // practice is ~2 attempts (URL credentials, then property credentials). Our budget of 1 is the
  // conservative end of that range: retrying repeatedly against a rejecting server is a lockout risk
  // and cannot succeed, since the credentials do not change between attempts.
  REQ_FAIL_AUTH,
  // The connection went away mid-request (EOF/RST). GStreamer: reconnect and replay the SAME request,
  // guarded by `try == 0` so it happens once, then falls through to an error -- and ONLY when media
  // is NOT interleaved (`(try == 0) && !src->interleaved && src->udp_reconnect`, gstrtspsrc.c:7723-7728):
  // with media riding the dead connection the replay cannot help, which is the same fact our
  // reconnectRequiresFullReset() encodes.
  REQ_FAIL_CONNECTION_CLOSED,
  // The server answered with an error status we cannot recover from by retrying (4xx/5xx other than
  // auth/transport). Retrying an identical request against a definite refusal just burns the window.
  //
  // DELIBERATE NON-PARITY, checked against source: GStreamer additionally retries SETUP once on
  // 400/404/455/451/503 with a "non-compliant" control URL -- plain base+"/"+control concatenation
  // instead of RFC 3986 resolution (try_non_compliant_url, gstrtspsrc.c:8462-8485, :8751-8773) --
  // because some cameras only accept the appended form. live555 does not need that fallback: its
  // constructSubsessionURL NEVER resolves, it always appends (its own ##### note admits as much),
  // so our DEFAULT behaviour is already the form GStreamer's fallback exists to reach. A retry here
  // would re-send a byte-identical URL, which is exactly the pointless repetition this budget
  // exists to prevent.
  REQ_FAIL_ERROR_STATUS,
  // A LOCAL resource was unavailable -- most importantly EADDRINUSE, but also fd and buffer
  // exhaustion. Nothing to do with the peer, and blaming it would be a lie.
  //
  // This is the failure production is ACTUALLY hitting. Measured on 6397272975e8c006c6fc:
  //   TX-RETRY ...: SETUP failed (result -98), retrying the request -- attempt 4 of 20.
  // errno 98 == EADDRINUSE (confirmed against that box's own asm-generic/errno.h:81). live555 binds
  // a specific local RTP port pair per subsession, so a re-SETUP after a reset claims a port the
  // dying session has not released yet. It resolves on its own -- attempt 4 succeeds.
  //
  // GStreamer bounds exactly this case with DEFAULT_RETRY (gstrtspsrc.c:734-736, "Max number of
  // retries when allocating RTP ports"), which is where the 20 below genuinely comes from.
  REQ_FAIL_LOCAL_RESOURCE,
  // The server ANSWERED, but the reply overflowed OUR response buffer and was discarded. The peer
  // is behaving; the defect is our limit -- so this must neither blame the peer
  // (requestFailureBlamesPeer is false for it) nor retry (an identical request truncates
  // identically; budget 0 -> rebuild, which at least re-DESCRIBEs with a fresh buffer). It was
  // recorded as REQ_FAIL_ERROR_STATUS, whose peer-attribution printed ", peer-attributed" for our
  // own buffer size -- the exact misattribution the recording site's comment said it fixed.
  REQ_FAIL_RESPONSE_TRUNCATED,
  // We could not work out what went wrong. This is a DISTINCT outcome, not a bucket to sweep
  // surprises into: labelling an unrecognised errno as (say) REQ_FAIL_ERROR_STATUS would claim the
  // server answered when nothing came back, and a log reading "error-status" for a local bug is how
  // our own fault gets attributed to somebody else's camera. It retries zero times and says so.
  REQ_FAIL_UNKNOWN
};

enum RequestFailureAction {
  REQ_ACTION_RETRY,            // re-send this request on the EXISTING session, immediately
  REQ_ACTION_RETRY_DELAYED,    // re-send, but only after a wait -- an instant retry cannot succeed
  REQ_ACTION_RETRY_RECONNECT,  // re-establish the RTSP connection, then replay the SAME request
  REQ_ACTION_RESET_SESSION     // tear down and rebuild from DESCRIBE
};

// True for every action that re-sends the request, whatever the timing. Call sites branch on "do I
// retry?" far more often than on "how"; without this each one re-lists the retry actions and the day
// a fourth is added, whichever list somebody forgot silently becomes a session rebuild.
//
// EVERY retry flavour -- immediate, delayed, reconnect-and-replay -- preserves the completed
// DESCRIBE and keeps the fan-out clients attached, which is the entire reason retrying beats
// rebuilding: re-establishing the RTSP connection does not touch the downstream side, and nothing
// re-DESCRIBEs. (This once existed as two separately-named test-only predicates that could not
// diverge; the claim lives here now, once, where the retry set is defined.)
inline bool requestActionRetries(RequestFailureAction action) {
  return action == REQ_ACTION_RETRY || action == REQ_ACTION_RETRY_DELAYED ||
         action == REQ_ACTION_RETRY_RECONNECT;
}

// How long to wait for a RESPONSE. GStreamer's DEFAULT_TCP_TIMEOUT, 20s.
//
// CORRECTED. This used to return 5 for UDP, citing GStreamer's DEFAULT_TIMEOUT. That citation was
// wrong and the value it justified was actively harmful: DEFAULT_TIMEOUT (gstrtspsrc.c:741) is the
// UDP DATA-STARVATION timeout that triggers a fallback to TCP -- "Retry TCP transport after UDP
// timeout microseconds" -- not a deadline for an RTSP response. GStreamer waits `tcp_timeout` for a
// response on BOTH transports (gstrtspsrc.c:7578-7579, :7710 pass src->tcp_timeout regardless of how
// media is carried).
//
// So a 5s response deadline on the UDP path was TIGHTER than the reference client, which is the
// precise mistake the paragraph below warns about: ours was ~10s for SETUP, half the reference
// client's, against cameras measured at 9-117ms RTT that nonetheless stall under load. A deadline
// tighter than the reference implementation's converts a merely-slow camera into a torn-down
// session, which is the expensive outcome above.
//
// Deliberately NO transport parameter: the response deadline does not depend on how MEDIA is
// carried (that is the whole correction above), and a signature promising transport-dependence the
// body renounces made every caller build a ternary to feed a discarded argument. The deadline that
// genuinely differs per transport is the DATA-stall one -- udpDataStallTimeoutSecs below, which is
// what DEFAULT_TIMEOUT actually names.
inline unsigned requestTimeoutSecs() {
  return 20;
}

// GStreamer DEFAULT_TIMEOUT, in its REAL role (gstrtspsrc.c:741): how long UDP media may deliver
// nothing before the transport is presumed blocked. Kept separate from requestTimeoutSecs so the two
// deadlines can never again be confused for one another.
inline unsigned udpDataStallTimeoutSecs() { return 5; }

// The retry budget.
//
// CORRECTED CITATION. This said "GStreamer DEFAULT_RETRY", implying 20 was that project's
// request-retry budget. It is not: DEFAULT_RETRY (gstrtspsrc.c:734-736) bounds "Max number of
// retries when allocating RTP ports" -- a LOCAL port-allocation budget, which is why it is cited on
// REQ_FAIL_LOCAL_RESOURCE above, where it actually belongs. GStreamer's own request-retry bound is
// `count++ > 8` in gst_rtspsrc_send (gstrtspsrc.c:7814), with the per-cause `!(retry++)` and
// `try == 0` guards doing the real work.
//
// 20 is retained as the ceiling for the causes that genuinely need many attempts, because the TIME
// window below is what actually bounds the outage. We previously had NO budget at all: the reset
// loop was unbounded, so a camera that would not answer produced resets forever rather than
// escalating.
inline unsigned maxRequestRetries() { return 20; }

// A COUNT alone is not a bound on outage time, and treating it as one is how this fix nearly became
// the bug it replaces: 20 retries x the 20s TCP timeout is 400 SECONDS, which is worse than the 259s
// recovery being fixed. A count is sized for errors that fail in milliseconds, not for twenty
// consecutive full timeouts.
//
// So the real budget is TIME, and the count is only a secondary cap. 60s is chosen to stay well
// under the observed 137s median recovery: past that, rebuilding the session is no longer the more
// expensive option and we should stop retrying and do it.
inline unsigned maxRequestRetryWindowSecs() { return 60; }

// True when retrying has cost more than the window allows, whatever the count says.
inline bool retryWindowExpired(unsigned elapsedSecs) {
  return elapsedSecs >= maxRequestRetryWindowSecs();
}

// A successful request clears the counter. Without this a stream that accumulated 20 failures across
// its whole life could never retry again, and a long-lived proxy would silently degrade into
// reset-only behaviour -- the failure mode being fixed, arriving by a slower route.
inline unsigned attemptsAfterSuccess() { return 0; }

// How many times a given CAUSE may be retried. A single budget for every cause is wrong in both
// directions: retrying a 401 twenty times risks an account lockout and cannot succeed, while
// allowing only one retry for a timeout throws away a session for a camera that was merely slow.
inline unsigned maxRetriesForCause(RequestFailureCause cause) {
  switch (cause) {
    case REQ_FAIL_AUTH:               return 1;  // GStreamer: !(retry++), exactly once
    case REQ_FAIL_CONNECTION_CLOSED:  return 1;  // GStreamer: try == 0 guard, exactly once
    case REQ_FAIL_ERROR_STATUS:       return 0;  // a definite refusal; retrying changes nothing
    case REQ_FAIL_RESPONSE_TRUNCATED: return 0;  // an identical request truncates identically
    case REQ_FAIL_UNKNOWN:            return 0;  // we do not know what happened; do not guess a budget
    case REQ_FAIL_NONE:               return 0;  // nothing failed, so there is nothing to retry
    // GStreamer DEFAULT_RETRY, in its real role: max attempts at allocating RTP ports. Production
    // recovers on attempt 4, so a small budget would convert a self-healing wait into a rebuild.
    case REQ_FAIL_LOCAL_RESOURCE:     return maxRequestRetries();
    case REQ_FAIL_TIMEOUT:            return maxRequestRetries();
    // ONE retry, because there is exactly one other transport to offer. This was
    // maxRequestRetries(), which re-sent the IDENTICAL rejected transport twenty times: the server
    // had already refused it and nothing about the request changed between attempts, so every one
    // was guaranteed to fail while spending the window that should have gone to the other transport.
    case REQ_FAIL_TRANSPORT_UNSUPPORTED: return 1;
  }
  return 0; // unknown cause: do not invent a retry budget for it
}

// ------------------------------------------------------------------------------------------------
// Turning live555's one integer into a cause.
//
// Every decision above is inert until something maps the value production actually receives. Without
// this, the SETUP handler can only say "it failed" -- which is the same flattening that produced
// always-reset, one layer further down.
//
// live555's RTSPClient convention (RTSPClient::handleResponseBytes): resultCode < 0 is -errno, the
// request never got a response at all; resultCode > 0 is the RTSP response status code, which means
// the server RECEIVED the request and wrote a reply.
//
// errno values are NOT portable: ETIMEDOUT is 60 on macOS and 110 on Linux, so this must switch on
// the symbolic names or it would classify correctly on the dev Mac and wrongly on every box.
// ------------------------------------------------------------------------------------------------
inline RequestFailureCause classifyRequestFailure(int resultCode) {
  if (resultCode > 0) {
    // The server answered. 401/407: credentials. 461: it rejected the transport we asked for.
    // GStreamer treats both as retryable in place; anything else is a definite refusal.
    // 404 joins 401/407 because GStreamer puts it in the SAME branch (gstrtspsrc.c:7828-7836,
    // GST_RTSP_STS_NOT_FOUND alongside GST_RTSP_STS_UNAUTHORIZED, both calling
    // gst_rtspsrc_setup_auth and retrying once). Cameras answering 404 to an UNAUTHENTICATED
    // DESCRIBE/SETUP are common; treating it as a flat refusal means credentials are never attached
    // and such a camera is permanently unreachable. The one-retry budget makes this cheap: if it
    // really was a missing resource, the second attempt fails too and we move on.
    if (resultCode == 401 || resultCode == 407 || resultCode == 404) return REQ_FAIL_AUTH;
    if (resultCode == 461) return REQ_FAIL_TRANSPORT_UNSUPPORTED;
    return REQ_FAIL_ERROR_STATUS;
  }

  int const err = -resultCode;

  // A LOCAL resource we could not get. Checked FIRST because these are the errnos most easily
  // mistaken for a peer problem, and the one that matters (EADDRINUSE) is what production is
  // actually failing on -- see REQ_FAIL_LOCAL_RESOURCE. Leaving it unenumerated made it
  // REQ_FAIL_UNKNOWN, whose budget is zero, which would have turned a retry loop that demonstrably
  // recovers into an instant session rebuild.
  if (err == EADDRINUSE || err == EADDRNOTAVAIL || err == EMFILE || err == ENFILE ||
      err == ENOBUFS || err == ENOMEM) {
    return REQ_FAIL_LOCAL_RESOURCE;
  }

  // Nothing came back within the deadline. Ambiguous by nature: the socket may be perfectly fine.
  if (err == ETIMEDOUT || err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS) {
    return REQ_FAIL_TIMEOUT;
  }

  // The connection is provably unusable -- either it died under the request or it never came up.
  // Both need the connection re-established before the request can be replayed.
  if (err == ECONNRESET || err == EPIPE || err == EBADF || err == ENOTCONN ||
      err == ECONNABORTED || err == ECONNREFUSED || err == EHOSTUNREACH || err == ENETUNREACH ||
      err == ENETDOWN || err == ENETRESET) {
    return REQ_FAIL_CONNECTION_CLOSED;
  }

  // Deliberately NOT folded into one of the above. An unenumerated errno is a gap in OUR knowledge,
  // and saying so is the only honest report; guessing would hand a plausible-looking cause to an
  // investigation that then reasons from it.
  return REQ_FAIL_UNKNOWN;
}

// Did we actually lose the connection?
//
// This exists because the keep-alive path conflated "the request failed" with "the peer is gone",
// and rebuilt the whole session on the strength of it. A status code is PROOF the connection is
// alive: the server had to receive the request and write a reply to produce one. A timeout proves
// nothing either way, and GStreamer's answer there is to keep the session and send a keep-alive --
// so it must not be reported as a disconnect either.
inline bool requestFailureLostTheConnection(RequestFailureCause cause) {
  return cause == REQ_FAIL_CONNECTION_CLOSED;
}

// Is the PEER responsible for this failure?
//
// Only an answer we did not like proves the peer did anything at all. A local bind failure is ours;
// a timeout proves nothing about anyone; an unclassified errno is a gap in our knowledge. Naming
// this explicitly is the guard against a log line, a diagnostic tally, or a customer email
// attributing our own port conflict to somebody's camera -- which this investigation has already
// done once, at real cost.
inline bool requestFailureBlamesPeer(RequestFailureCause cause) {
  return cause == REQ_FAIL_ERROR_STATUS || cause == REQ_FAIL_AUTH ||
         cause == REQ_FAIL_TRANSPORT_UNSUPPORTED;
}

// The causes that are provably OUR side: a local port/fd/buffer we could not get, or a response
// our own buffer could not hold. Attribution is THREE-way -- peer-attributed / provably ours /
// unproven -- and this predicate names the ONLY set an acquittal ("NOT the camera's fault") is
// true for. `!requestFailureBlamesPeer` is NOT that set: it also contains connection-closed and
// timeout, where the camera may be the very party that closed or went silent, and printing an
// acquittal there is the same overclaim as "Far-end close PROVEN" for our own recorder.
inline bool requestFailureIsLocalFault(RequestFailureCause cause) {
  return cause == REQ_FAIL_LOCAL_RESOURCE || cause == REQ_FAIL_RESPONSE_TRUNCATED;
}

// True when a retry must change the TRANSPORT rather than re-send the same request. Distinct from
// the retry ACTION: the action says when to re-send, this says what has to be different about it.
inline bool requestFailureNeedsDifferentTransport(RequestFailureCause cause) {
  return cause == REQ_FAIL_TRANSPORT_UNSUPPORTED;
}

// Does this status mean "your SESSION no longer exists here" -- i.e. the connection is alive but
// the thing the keep-alive was keeping alive is already gone?
//
// 454 Session Not Found ends the conversation: re-sending keep-alives against it can never succeed
// (the id it presents is the id the server just disowned), yet the connection-is-alive rule would
// keep the session "KEPT" forever -- a zombie whose logs look healthy. Most likely producer: the
// in-place control reconnect re-presenting a session id the camera expired while the connection was
// down. The only correct response is a rebuild. Enumerated here, not at the call site, so the
// keep-alive path cannot grow an inline status list (one vocabulary, one owner).
inline bool rtspStatusEndsSession(int statusCode) {
  return statusCode == 454; // Session Not Found
}

// Does this status mean "I do not implement that method" rather than "that request failed"?
//
// GStreamer strikes such a method from src->methods and converts the failure into a success
// (gstrtspsrc.c:7917-7934). Its comment names a real device: a HikVision DS-2CD2732F-IS answering
// "551 Option not supported" to PAUSE. We send PAUSE whenever neverPause is false (the stock
// default), so without this a camera that simply does not implement PAUSE fails a request we did
// not need to make, on a session that is otherwise perfectly healthy.
//
// Deliberately NOT "any 4xx/5xx": a 500 is a server fault, not a missing verb, and disabling a
// method on the strength of one would silently stop us using something the camera does support.
inline bool methodRefusalDisablesMethod(int statusCode) {
  return statusCode == 405 ||  // Method Not Allowed
         statusCode == 406 ||  // Not Acceptable
         statusCode == 501 ||  // Not Implemented
         statusCode == 551;    // Option Not Supported (some cameras use this for 501)
}

// How long to wait before re-sending, in microseconds.
//
// A timeout has ALREADY waited out its full deadline, so waiting again just adds dead air. A local
// port conflict is the opposite: the port is held by a socket that is still closing, so an instant
// re-send is guaranteed to fail and would spend the entire budget inside a few milliseconds -- 20
// attempts, all doomed, then the session rebuild we were trying to avoid. 250ms is comfortably
// longer than a close takes to complete and comfortably shorter than the outages being fixed.
inline unsigned requestRetryDelayUs(RequestFailureCause cause) {
  return cause == REQ_FAIL_LOCAL_RESOURCE ? 250000 : 0;
}

// Does a lost connection make the whole session unrebuildable-in-place?
//
// With INTERLEAVED media the answer is yes, and the reconnect-and-replay retry is structurally
// DOOMED: the re-send lazily opens a new connection, and the (correct) rule that a new connection
// invalidates subsessions bound to the old one then resets the session anyway -- so the "retry
// that keeps the session" costs an extra DESCRIBE cycle and keeps nothing. MEASURED on
// 6397272975e8c006c6fc, 2026-07-21: every `cause=connection-closed` SETUP retry was followed by
// `reason=reconnect-replaced-connection` IN THE SAME SECOND, 2304 such resets in six hours
// (fixture: tetherbox spec/fixtures/live555/retry_reconnect_doom_loop_192.168.11.19_2026-07-21.log).
//
// GStreamer's replay guard has always said this: `(try == 0) && !src->interleaved &&
// src->udp_reconnect` (gstrtspsrc.c:7723-7728) -- it never replays when media rode the dead
// connection. Named as a predicate so the decision and the reset-reason label consult ONE rule.
inline bool connectionLossRequiresRebuild(RequestFailureCause cause, bool mediaInterleaved) {
  return cause == REQ_FAIL_CONNECTION_CLOSED && mediaInterleaved;
}

// The full decision, cause-aware. This is the one production should call.
// `mediaInterleaved` = is the MEDIA carried on the RTSP connection (RTP-over-TCP)? It changes only
// the connection-loss verdict, and has no default: the transport is a fact every caller has, and a
// default would let a new call site silently re-open the doomed-replay loop.
inline RequestFailureAction decideRequestFailureFor(RequestKind kind, RequestFailureCause cause,
                                                    unsigned attemptsSoFar, unsigned elapsedSecs,
                                                    bool mediaInterleaved) {
  // DESCRIBE has no established session or attached client to preserve, so there is nothing for a
  // retry to save; its own backoff ladder owns the case.
  if (kind == REQ_DESCRIBE) return REQ_ACTION_RESET_SESSION;

  // Time bound first: it overrides every count, because a count cannot bound outage time.
  if (retryWindowExpired(elapsedSecs)) return REQ_ACTION_RESET_SESSION;

  // A lost connection with interleaved media: a replay cannot rebind the media, so the rebuild the
  // replay would trigger anyway happens ONCE, deliberately, under its own name.
  if (connectionLossRequiresRebuild(cause, mediaInterleaved)) return REQ_ACTION_RESET_SESSION;

  if (attemptsSoFar >= maxRetriesForCause(cause)) return REQ_ACTION_RESET_SESSION;

  // A dropped connection under UDP media: the session genuinely survives a reconnect, so replay the
  // request on the new connection (GStreamer's try==0 rule).
  if (cause == REQ_FAIL_CONNECTION_CLOSED) return REQ_ACTION_RETRY_RECONNECT;

  // A port still held by a closing socket cannot be claimed by trying harder, only by trying later.
  if (cause == REQ_FAIL_LOCAL_RESOURCE) return REQ_ACTION_RETRY_DELAYED;

  return REQ_ACTION_RETRY;
}

// The count/window arithmetic in isolation, kept for the unit tests that exercise it without
// choosing a cause. NOT a second policy: it is defined as the full decision with a TIMEOUT cause,
// which is the cause whose budget it has always described (the full maxRequestRetries() ceiling,
// plain RETRY action) -- test_request_retry pins the equivalence, and test_proxy_wiring bans
// production from calling it (the elapsedSecs=0 default is how the 60s window once became
// unreachable while every unit test stayed green).
//
// DESCRIBE is deliberately NOT retried here: it is the FIRST request of a session, so there is no
// established session to preserve and no downstream client attached yet. Retrying it in place buys
// nothing, and it already has its own backoff ladder. Everything after DESCRIBE has something worth
// keeping, and that is exactly what the old always-reset behaviour threw away.
inline RequestFailureAction decideRequestFailure(RequestKind kind, unsigned attemptsSoFar,
                                                 unsigned elapsedSecs = 0) {
  // Transport is irrelevant for a TIMEOUT (see connectionLossRequiresRebuild), so `false` here is
  // not a hidden choice.
  return decideRequestFailureFor(kind, REQ_FAIL_TIMEOUT, attemptsSoFar, elapsedSecs, false);
}

// A response timeout on an IDLE-but-established session is not a request failure at all: GStreamer
// answers it with a keep-alive and carries on. Tearing down here is what turns a quiet camera into
// an outage.
inline bool timeoutShouldKeepSessionAlive(RequestFailureCause cause) {
  return cause == REQ_FAIL_TIMEOUT;
}

inline char const* requestFailureCauseName(RequestFailureCause cause) {
  switch (cause) {
    case REQ_FAIL_TIMEOUT:               return "timeout";
    case REQ_FAIL_TRANSPORT_UNSUPPORTED: return "transport-unsupported";
    case REQ_FAIL_AUTH:                  return "auth";
    case REQ_FAIL_CONNECTION_CLOSED:     return "connection-closed";
    case REQ_FAIL_ERROR_STATUS:          return "error-status";
    // Distinct from the fallthrough below: "unclassified" means we recognised the failure as one we
    // have no classification for (and logged it as such, so the errno can be added); "unknown" would
    // mean the enum itself held a value no branch covers, i.e. memory corruption or a new cause
    // somebody forgot to name here. Collapsing the two would hide the second behind the first.
    case REQ_FAIL_LOCAL_RESOURCE:        return "local-resource";
    case REQ_FAIL_RESPONSE_TRUNCATED:    return "response-truncated";
    case REQ_FAIL_UNKNOWN:               return "unclassified";
    // Distinct from "unclassified": that means a failure we could not name, this means there was no
    // failure. A reader must be able to tell "we don't know" from "not applicable".
    case REQ_FAIL_NONE:                  return "none";
  }
  return "unknown";
}

// ------------------------------------------------------------------------------------------------
// The budget, as a live object.
//
// The decisions above are pure, so something has to hold the attempt count AND the window start.
// Production held only the count, in a bare unsigned, and that single omission made the whole
// time bound unreachable: the count-only overload was called, elapsedSecs defaulted to 0, and
// retryWindowExpired() could never once return true in production while the unit tests "proved" the
// worst case was 60 seconds. The real worst case was 20 retries x 20s = 400s -- worse than the 259s
// outage the policy was written to fix.
//
// Keeping both fields together in one object, with the decision as a method, is what makes that
// class of mistake impossible: there is no way to consult the budget without the clock.
// ------------------------------------------------------------------------------------------------
struct RequestRetryState {
  unsigned attempts;      // consecutive failures of the CURRENT request, FOR countedCause
  // Which cause `attempts` is counting. The count budgets are PER CAUSE (maxRetriesForCause), so a
  // cause-blind counter silently spends one cause's budget on another's failures: SETUP times out
  // once (attempts=1), the camera then answers 461 -- and 1 >= the transport budget of 1, so the
  // session reset instead of ever offering the other transport, the one action guaranteed to change
  // the outcome. GStreamer's per-cause guards (`!(retry++)`, `try == 0`) are independent state for
  // the same reason. The TIME window is deliberately NOT per cause: it bounds the whole outage,
  // whatever mix of causes filled it, so cause ping-pong cannot retry forever.
  RequestFailureCause countedCause;
  long windowStartSec;    // monotonic seconds at the FIRST failure of this run
  bool windowOpen;        // false until the first failure, so an idle session has no elapsed time

  // Born empty. Without this the struct is an aggregate and a member declaration leaves the fields
  // holding whatever was on the stack -- a garbage `attempts` would silently disable retrying
  // for the life of the session, and a garbage `windowStartSec` would expire the window instantly.
  // Neither would be visible in any log.
  RequestRetryState() { clear(); }

  // Start (or restart) with an empty budget. No clock parameter: windowStartSec is unreadable while
  // the window is closed (elapsedSince gates on windowOpen, and noteFailure restamps it on open),
  // so taking one only obliged every caller to perform a clock read nothing could ever observe.
  void clear() {
    attempts = attemptsAfterSuccess();
    countedCause = REQ_FAIL_NONE;
    windowStartSec = 0;
    windowOpen = false;
  }

  void noteFailure(long nowSec, RequestFailureCause cause) {
    if (!windowOpen) { windowStartSec = nowSec; windowOpen = true; }
    if (cause != countedCause) { countedCause = cause; attempts = attemptsAfterSuccess(); }
    ++attempts;
  }

  // A success clears everything, including the window: the next incident gets a full fresh budget
  // rather than inheriting a clock started during the previous one.
  void noteSuccess() { clear(); }

  // The attempt count AS SEEN BY a given cause's budget: failures counted against a different cause
  // do not spend this one's.
  unsigned attemptsFor(RequestFailureCause cause) const {
    return cause == countedCause ? attempts : 0;
  }

  // How long this run of retries has been going. Measured as a DIFFERENCE against the clock we were
  // given, never as an absolute: monotonicSeconds() is uptime-based, so on a box up for 40 days it
  // is ~3.5 million, and treating that as "elapsed" would expire every window on its first retry --
  // silently disabling the retry path on exactly the long-running boxes it exists for.
  unsigned elapsedSince(long nowSec) const {
    if (!windowOpen || nowSec <= windowStartSec) return 0;
    return (unsigned)(nowSec - windowStartSec);
  }

  RequestFailureAction decide(RequestKind kind, RequestFailureCause cause, long nowSec,
                              bool mediaInterleaved) const {
    return decideRequestFailureFor(kind, cause, attemptsFor(cause), elapsedSince(nowSec),
                                   mediaInterleaved);
  }
};

#endif

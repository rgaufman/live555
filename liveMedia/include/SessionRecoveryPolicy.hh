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
// When should a proxied back-end session be re-established, and what should be REPORTED when it is?
//
// Pure and dependency-free so the policy can be unit-tested without sockets, timers or an event
// loop (test/test_session_recovery.cpp). The caller supplies observations; this decides.
//
// TWO DEFECTS THIS EXISTS TO FIX, both measured across a ~50-camera fleet over 30 minutes:
//
//  1. DOUBLE-REPORT. The teardown classifier can PROVE a far-end close (a captured FIN or RST). That
//     close ends the session — but nothing told the inactivity timer, so ~18s later the timer fired
//     on the already-dead session and logged a second, different fault ("peer went silent without
//     closing; nobody is proven at fault"). Measured: 90 of 236 proven closes (38%) produced such a
//     line, so only 58 of 148 idle-reports actually meant what the code said. Any rate derived from
//     that count was inflated, and the second line CONTRADICTS the first: one says the peer provably
//     closed, the other says nobody is proven at fault.
//
//  2. DEAD AIR. On a proven close there is nothing left to wait for — the far end is gone. Yet
//     recovery waited out the full inter-packet-gap interval anyway. Measured: median 26.8s from a
//     proven close to the stream being live again (n=97, max 235.1s). That is recording gap with
//     nothing to gain.
//
// The rule is therefore: PROOF SHORT-CIRCUITS THE TIMER. A timer exists to infer a fault we cannot
// observe directly; once the fault IS observed, the inference is redundant and reporting it as an
// independent finding is wrong.
#ifndef _SESSION_RECOVERY_POLICY_HH
#define _SESSION_RECOVERY_POLICY_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif
#ifndef _RESET_REASONS_HH
#include "ResetReasons.hh" // sessionRecoveryResetReason() maps actions onto this vocabulary
#endif
// For NULL (sessionRecoveryResetReason returns it for WAIT). Own line, outside every other
// include's guard: hiding a second include inside another header's #ifndef block skips it whenever
// that header was already seen -- the SessionLifecycle.hh/stddef.h defect, not repeated here.
#include <stddef.h>

// What the caller should do at this check.
enum SessionRecoveryAction {
  // Nothing to do: the stream is progressing, or the gap has not yet reached the limit.
  SESSION_RECOVERY_WAIT,
  // Re-establish NOW because a far-end close was PROVEN. Must NOT emit an inactivity/idle fault:
  // the close was already reported, with better evidence, by the teardown classifier.
  SESSION_RECOVERY_RESET_PROVEN_CLOSE,
  // Re-establish because no data has arrived for the whole gap interval and nobody is proven at
  // fault. This is the timer's genuine purpose: the ONLY detector for a peer that stops sending
  // while holding its socket open (such a socket is never readable, so no read-path guard can see
  // it). Emit the idle fault here — this is the case the code's wording actually describes.
  SESSION_RECOVERY_RESET_IDLE,
  // Re-establish AND switch the media transport to RTP-over-TCP, because a UDP session completed
  // its handshake and then NEVER delivered a single packet: the RTSP exchange runs over TCP, so a
  // successful SETUP/PLAY proves nothing about the UDP media path, and a firewall/NAT that drops
  // the RTP simply produces silence forever. The old rule ("never received data -> still
  // connecting -> WAIT") was correct for TCP media and an INFINITE black-hole for this case.
  //
  // GStreamer parity: udpsrc is armed with DEFAULT_TIMEOUT (5s); on that first timeout it issues a
  // reconnect with `cur_protocols = GST_RTSP_LOWER_TRANS_TCP` and warns "Could not receive any UDP
  // packets for %.4f seconds, maybe your firewall is blocking it. Retrying using a tcp connection."
  // -- and ONLY when pads never activated, i.e. zero media ever arrived (gstrtspsrc.c:5310-5311,
  // :7032-7118, first-timeout latch :10693-10700; verified against main 2026-07-21).
  //
  // This is NOT the pre-emptive force-TCP red herring RequestRetryPolicy.hh bans: nothing switches
  // on suspicion. The trigger is a positive symptom -- a working control path and a provably-silent
  // media path -- and the switch happens once, on evidence.
  SESSION_RECOVERY_RESET_RETRY_TCP
};

// Sentinel for "no packet count has been observed yet" (the value the session layer resets to).
#define PACKET_COUNT_UNKNOWN ((unsigned)~0)

// Did the received-packet total actually ADVANCE since the last check?
//
// THE DEFECT THIS FIXES: the shipped test was `newTotal != previousTotal`, i.e. "changed", while the
// code around it treated the result as "progress". Those are not the same thing: a DECREASE is also
// a change, and would reset the stall clock — pushing the inactivity watchdog out by a full interval
// on a stream that is not making progress at all.
//
// Whether a decrease actually occurs in production is MEASURED, not assumed: see the GAPCHK
// instrumentation in ProxyRTSPClient::checkInterPacketGaps_, whose captured output is the fixture
// this behaviour is tested against. Do not restate a mechanism here that the fixture does not show.
//
// Regardless of frequency, the comparison must match the question being asked. The question is "did
// more data arrive", so the test is an INCREASE — naming a check for what it measures rather than
// for what we hope it catches.
//
// A decrease deliberately returns False (not progress). The caller must still ADOPT the new value,
// or the stale high-water mark would make every later check compare against a number that can never
// be reached again, and the watchdog would then fire forever.
inline Boolean packetProgressMade(unsigned previousTotal, unsigned newTotal) {
  // First observation after a reset: start the clock rather than judging a delta we cannot compute.
  if (previousTotal == PACKET_COUNT_UNKNOWN) return True;

  return newTotal > previousTotal;
}

// Does opening a NEW RTSP connection require throwing away the current session state?
//
// WHY THE RULE EXISTS: with RTP-over-TCP the media is interleaved on the RTSP connection itself
// (RFC 2326 sec 10.12). If that connection is replaced after subsessions have been SETUP, those
// subsessions still reference the OLD socket, so the session is inconsistent and only a full
// re-DESCRIBE can rebuild it. That part is correct and must be kept.
//
// THE DEFECT: the check was `connectSucceeded && doneDescribe && rtpOverTcp` — with no test for
// whether anything is actually BOUND to the old connection. Between a completed DESCRIBE and the
// first SETUP, nothing is interleaved yet, so the premise does not hold; but the check fired anyway
// and discarded a DESCRIBE that had just succeeded.
//
// MEASURED, from a real captured trace (spec/fixtures/diagnostics/live555_gapchk_reconnect_cycle.txt
// in the tetherbox repo): every reconnect performed TWO complete DESCRIBE/SETUP/PLAY handshakes. The
// first DESCRIBE (CSeq 7) returned 200 OK, subsessions were built, createNewStreamSource then opened
// a new socket, this check fired, and the whole thing ran again as CSeq 9..12. That redundant cycle
// is the dead air between a proven far-end close and the stream being live again.
//
// `numSetupsSent` counts SETUP requests SENT (not completed), and is zeroed by reset(). Sent is the
// correct moment for this question: RTP-over-TCP interleaved channel ids are claimed inside
// sendSetupCommand as the request goes out, so a subsession is bound to the connection from that
// instant. Counting completions would under-report — a SETUP in flight when the socket is replaced
// has already consumed channel ids on the old connection. (The caller's field was named
// `fNumSetupsDone` until 2026-07-20; the value was always "sent", only the name said otherwise.)
// 🚨 `connectAttemptStarted` is NOT "the connect succeeded". RTSPClient::connectToServer returns
//    -1 = failed, 0 = PENDING (EINPROGRESS/EWOULDBLOCK on a non-blocking socket), 1 = succeeded
//    immediately. Sockets are non-blocking by default, so for any real camera the normal return is
//    0. An earlier version of this predicate took `res == 0` and called it "connectSucceeded",
//    which both misnamed the common case AND skipped the reset on `res == 1` — the immediate-success
//    path, where a connection genuinely HAS been replaced. Pass `res >= 0`: a new connection is being
//    established, whether or not it has completed yet.
inline Boolean reconnectRequiresFullReset(Boolean connectAttemptStarted, Boolean doneDescribe,
                                          Boolean rtpOverTcp, unsigned numSetupsSent) {
  if (!connectAttemptStarted) return False; // connect failed; the caller's error path owns it
  if (!rtpOverTcp) return False;       // UDP media is not carried on the RTSP connection
  if (!doneDescribe) return False;     // no session described yet: nothing to invalidate

  // Nothing has been SETUP on the old connection, so nothing references it. Rebuilding here would
  // discard a DESCRIBE that just succeeded and buy nothing.
  return numSetupsSent > 0 ? True : False;
}

// Observations at one check. `secsSinceProgress` is MONOTONIC elapsed since payload last advanced;
// deriving it from anything else (a count of checks, a byte total) would silently stop being a
// duration — see the eviction bug where a "seconds behind" value was computed from queued bytes and
// so was bounded by SO_SNDBUF, making the threshold arithmetically unreachable.
struct SessionRecoveryInput {
  Boolean provenCloseSeen;   // teardown classifier captured a FIN/RST for THIS session
  Boolean everReceivedData;  // a session that never delivered anything is still connecting
  long secsSinceProgress;    // MONOTONIC seconds since payload last advanced (MonotonicTime.hh)
  unsigned gapLimitSecs;     // the -D inter-packet-gap limit (0 = gap checking disabled)
  // Is the MEDIA carried on the RTSP connection (RTP-over-TCP)? Decides whether "handshake done,
  // zero packets ever" can mean a blocked UDP path (see SESSION_RECOVERY_RESET_RETRY_TCP). For TCP
  // media the handshake and the media share one proven-working socket, so that inference is invalid.
  Boolean mediaOverTCP;
  // How long a UDP session may hold at zero-packets-ever before the transport is presumed blocked.
  // The caller passes udpDataStallTimeoutSecs() (RequestRetryPolicy.hh -- GStreamer's
  // DEFAULT_TIMEOUT, 5s, in its real role); it is an INPUT rather than read here so this header
  // stays dependency-free and the tests can exercise the boundary directly. 0 disables the fallback.
  unsigned udpNoDataLimitSecs;
  // Is ANY downstream consumer attached? The packet total only advances when a consumer pulls
  // frames, so with zero consumers every timer input above measures nothing (measured: healthy
  // NVR streams reset at exactly the 15s mark, 894/30min on ea9f, 2026-07-21). Defaults True so
  // existing call sites and tests keep their meaning; the gap-check call site sets it for real.
  // KNOWN LIMIT: with no consumer the caller cannot safely gather provenCloseSeen either (the
  // rtpSource read is the ROUND-5 use-after-free), so today a consumer-less proven close is acted
  // on one window after a client returns. The policy still orders proof above this rule so the
  // day the gather is safe, no policy change is needed.
  Boolean consumerAttached = True;
};

inline SessionRecoveryAction sessionRecoveryDecide(SessionRecoveryInput const& in) {
  // A proven close short-circuits everything, INCLUDING a disabled gap check: proof does not depend
  // on the timer being switched on. It also short-circuits `everReceivedData` — a peer that accepts
  // the connection and then resets it before sending a byte has still provably closed, and waiting
  // on it accomplishes nothing.
  if (in.provenCloseSeen) return SESSION_RECOVERY_RESET_PROVEN_CLOSE;

  // No consumer, no measurement, no timer verdict (see consumerAttached above).
  if (!in.consumerAttached) return SESSION_RECOVERY_WAIT;

  if (in.gapLimitSecs == 0) return SESSION_RECOVERY_WAIT; // gap checking disabled

  // Never time out a session that has not yet delivered anything: that is a connect/DESCRIBE still
  // in flight, not a stalled stream, and resetting it would restart the handshake in a loop.
  //
  // EXCEPT over UDP, where that reasoning is a black-hole: the handshake runs over TCP, so its
  // success proves nothing about the media path, and a firewalled RTP port produces exactly this
  // shape -- forever. secsSinceProgress is stamped at PLAY-ok, so for a zero-packet session it
  // measures "media has been due for this long". See SESSION_RECOVERY_RESET_RETRY_TCP.
  if (!in.everReceivedData) {
    if (!in.mediaOverTCP && in.udpNoDataLimitSecs > 0 &&
        in.secsSinceProgress >= (long)in.udpNoDataLimitSecs) {
      return SESSION_RECOVERY_RESET_RETRY_TCP;
    }
    return SESSION_RECOVERY_WAIT;
  }

  // Strictly ">=": the limit is the point at which the gap is a fault, and an exactly-at-limit gap
  // is the boundary case the -D value names. Comparing elapsed TIME, never a count of checks, so
  // the verdict cannot depend on how often this runs.
  if (in.secsSinceProgress >= (long)in.gapLimitSecs) return SESSION_RECOVERY_RESET_IDLE;

  return SESSION_RECOVERY_WAIT;
}

// Should this action emit the "peer went silent" idle fault line?
// Exactly one action may: collapsing the two reset reasons into one log line is what produced the
// contradictory double-report in the first place.
inline Boolean sessionRecoveryReportsIdle(SessionRecoveryAction a) {
  return a == SESSION_RECOVERY_RESET_IDLE;
}

// Should this action re-establish the back-end session?
inline Boolean sessionRecoveryResets(SessionRecoveryAction a) {
  return a == SESSION_RECOVERY_RESET_PROVEN_CLOSE || a == SESSION_RECOVERY_RESET_IDLE ||
         a == SESSION_RECOVERY_RESET_RETRY_TCP;
}

// Should the rebuild switch the media transport to RTP-over-TCP first? Exactly one action may:
// a transport switch is a durable, session-scoped decision, and letting a second verdict imply it
// would be the reason-collapsing defect again, one field over.
inline Boolean sessionRecoverySwitchesToTCP(SessionRecoveryAction a) {
  return a == SESSION_RECOVERY_RESET_RETRY_TCP;
}

// The reset reason each action carries into the TX-RESET line -- TOTAL over the reset actions, and
// living HERE rather than as a call-site ternary. The ternary mapped "switches to TCP?" to one
// reason and everything else to inter-packet-gap, so a PROVEN-close reset was tallied as an idle
// timeout: the reason field re-merged exactly the two populations this policy was written to keep
// apart, and a fourth action would have silently inherited whichever arm it fell into. NULL for
// WAIT, which resets nothing and must never reach scheduleReset.
inline char const* sessionRecoveryResetReason(SessionRecoveryAction a) {
  switch (a) {
    case SESSION_RECOVERY_RESET_PROVEN_CLOSE: return RESET_REASON_FAR_END_CLOSE_PROVEN;
    case SESSION_RECOVERY_RESET_IDLE:         return RESET_REASON_INTER_PACKET_GAP;
    case SESSION_RECOVERY_RESET_RETRY_TCP:    return RESET_REASON_UDP_NO_INITIAL_DATA;
    case SESSION_RECOVERY_WAIT:               return NULL;
  }
  return NULL;
}

#endif

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
// Does PRODUCTION actually use the policies the other tests prove correct?
//
// Why this file exists
// --------------------
// Every other suite here is a pure unit test of a header. That is the right shape for the logic,
// but it has one blind spot big enough to have swallowed a real defect: a header can be perfect,
// fully tested, and NEVER CALLED. RequestRetryPolicy.hh declared a 60-second retry window and a
// test asserted the worst case was 60s -- while ProxyServerMediaSession.cpp called the count-only
// overload, passed no elapsed time at all, and could retry for 400 seconds. The suite was green and
// the production path was worse than the 259s bug it was written to fix.
//
// So this suite asserts the CALL SITES, by reading the production source as text. That is a blunt
// instrument and it is deliberately chosen: the alternative is linking liveMedia.a and standing up
// a UsageEnvironment, a TaskScheduler and a live socket to observe one branch, which is why nobody
// ever wrote that test. A grep-level contract catches the specific failure mode that actually
// happened -- "the policy is not wired in" -- at a cost of a few seconds.
//
// It is NOT a substitute for the logic tests. It proves the wire is connected; test_request_retry
// proves what travels down it.
//
// Being source-text based, it fails on a rename. That is intended: renaming
// decideRequestFailureFor and leaving this file untouched should stop the build, because the whole
// point is that somebody notices when the connection between policy and production changes.

#include "check.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Read a whole source file. A missing or unreadable file is a hard failure, never an empty string:
// an empty buffer would make every "must contain" assertion below fail loudly but every "must NOT
// contain" assertion pass silently, which is the worst possible outcome for a contract test.
//
// Paths below are written relative to test/ ("../liveMedia/..."), but the build driver on the box
// runs the gate binaries from the TREE ROOT, where the same file is "liveMedia/...". Try both rather
// than pin a cwd: a contract test that only runs from one directory is a test that silently does not
// run, and this one failed the whole deploy gate for a file that was present the entire time.
static char* readSource(char const* path) {
  FILE* f = fopen(path, "rb");
  if (f == NULL && strncmp(path, "../", 3) == 0) f = fopen(path + 3, "rb");
  if (f == NULL) {
    fprintf(stderr, "FATAL: cannot open %s (tried both test/- and tree-root-relative)\n", path);
    exit(2);
  }
  fseek(f, 0, SEEK_END);
  long const size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    fprintf(stderr, "FATAL: %s is empty\n", path);
    exit(2);
  }
  char* buf = (char*)malloc((size_t)size + 1);
  size_t const got = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[got] = '\0';
  return buf;
}

// Text of a single function body, from its definition to the next line that starts in column 0 with
// a non-brace character (i.e. the next top-level definition). Crude, but these are plain C++
// functions at file scope and it keeps an assertion about continueAfterSETUP from being satisfied
// by a line in continueAfterPLAY.
static char* functionBody(char const* src, char const* signature) {
  char const* start = strstr(src, signature);
  if (start == NULL) {
    fprintf(stderr, "FATAL: no definition of %s -- was it renamed?\n", signature);
    exit(2);
  }
  char const* p = start;
  while (*p != '\0') {
    char const* nl = strchr(p, '\n');
    if (nl == NULL) { p = p + strlen(p); break; }
    char const next = nl[1];
    // A new top-level definition begins at column 0 with something that is not '}' or whitespace.
    if (nl > start + 1 && next != '\0' && next != '}' && next != ' ' && next != '\t' &&
        next != '\n' && next != '#') {
      p = nl;
      break;
    }
    p = nl + 1;
  }
  size_t const len = (size_t)(p - start);
  char* body = (char*)malloc(len + 1);
  memcpy(body, start, len);
  body[len] = '\0';
  return body;
}

static bool contains(char const* haystack, char const* needle) {
  return strstr(haystack, needle) != NULL;
}

static unsigned countOccurrences(char const* haystack, char const* needle) {
  unsigned n = 0;
  size_t const len = strlen(needle);
  for (char const* p = haystack; (p = strstr(p, needle)) != NULL; p += len) ++n;
  return n;
}

// A copy of `src` with every comment and string literal blanked to spaces, offsets preserved.
//
// Text-scanning a source file for CODE is only sound if prose is excluded, and getting this wrong
// has now produced three false positives in this very file: a `scheduleReset()` inside a log
// message read as an anonymous call site; a `#include` inside a comment read as a second include in
// a guard block; and a comment saying `NOT "connect-failed"` read as the call site it was
// documenting. Each time the test reported a defect that did not exist -- which for a contract test
// is the same sin as missing one.
//
// So there is ONE implementation -- one lexer, one set of escape/quote rules -- and every
// code-level check goes through it. (There were briefly TWO: the scheduleReset-counting test carried
// its own inline string/comment walk, i.e. a second copy of exactly the logic that had already
// produced the three false positives. A fix to one walker would not have reached the other.)
//
// By default it blanks COMMENTS ONLY, deliberately leaving string literals intact: several checks
// here are specifically about which literal a call site passes (e.g. the reset reason), so blanking
// string contents would make those assertions unable to see the very thing they test. The one check
// that must also ignore strings -- counting anonymous scheduleReset() calls, which a log message can
// imitate -- passes blankStrings=true to get the same lexer with literals blanked too (the quotes
// themselves are kept, so offsets and line structure are preserved either way).
static char* stripCommentsEx(char const* src, bool blankStrings) {
  size_t const len = strlen(src);
  char* out = (char*)malloc(len + 1);
  memcpy(out, src, len + 1);

  bool inString = false, inChar = false, inLine = false, inBlock = false;
  for (size_t i = 0; i < len; ++i) {
    char const c = out[i];
    if (inLine) {
      if (c == '\n') inLine = false; else out[i] = ' ';
      continue;
    }
    if (inBlock) {
      if (c == '*' && out[i + 1] == '/') { out[i] = ' '; out[i + 1] = ' '; ++i; inBlock = false; }
      else if (c != '\n') out[i] = ' ';
      continue;
    }
    // Inside a literal: track it so a '//' or '/*' in a string cannot be mistaken for a comment.
    // The characters are left alone unless the caller asked for literals to be blanked as well.
    if (inString || inChar) {
      char const quote = inString ? '"' : '\'';
      if (c == '\\' && i + 1 < len) {
        if (blankStrings) { out[i] = ' '; if (out[i + 1] != '\n') out[i + 1] = ' '; }
        ++i;
        continue;
      }
      if (c == quote) { inString = inChar = false; continue; }
      if (blankStrings && c != '\n') out[i] = ' ';
      continue;
    }
    if (c == '/' && out[i + 1] == '/') { out[i] = out[i + 1] = ' '; ++i; inLine = true; continue; }
    if (c == '/' && out[i + 1] == '*') { out[i] = out[i + 1] = ' '; ++i; inBlock = true; continue; }
    if (c == '"') { inString = true; continue; }
    if (c == '\'') { inChar = true; continue; }
  }
  return out;
}

static char* stripComments(char const* src) { return stripCommentsEx(src, false); }

// -------------------------------------------------------------------------------------------
// The SETUP failure path
// -------------------------------------------------------------------------------------------
static void testSetupFailureUsesTheCauseAwareTimeBoundedDecision() {
  // Stripped: this file's own comments name the very identifiers the must-not-contain checks scan
  // for, and unstripped scans have already produced three false positives (see stripCommentsEx).
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);
  // The failure path lives in handleSetupFailure, shared by the response path and the deadline path.
  // Both are checked: the entry point must DELEGATE (so the split cannot be used to slip a second,
  // divergent decision into continueAfterSETUP), and the shared function must hold the real policy.
  char* entry = functionBody(src, "void ProxyRTSPClient::continueAfterSETUP(");
  check(contains(entry, "handleSetupFailure"),
        "the SETUP response path delegates failure handling to the one shared decision");
  check(contains(entry, "unscheduleDelayedTask(fRequestDeadlineTask)"),
        "a SETUP response cancels the response deadline, so a late success is not retried by a "
        "timer that was still armed");
  free(entry);

  char* body = functionBody(src, "void ProxyRTSPClient::handleSetupFailure(");

  check(contains(body, "cause"),
        "SETUP failure acts on a CAUSE (a 401 must not get the timeout budget of 20 retries)");
  check(contains(body, "decideRequestFailureFor") || contains(body, ".decide("),
        "SETUP failure uses the cause-aware decision, not the flattened 'it failed' overload");

  // The precise defect: calling the count-only overload means elapsed time is never consulted, so
  // the 60s window cannot bound anything and 20 x 20s = 400s is reachable.
  check(!contains(body, "decideRequestFailure(REQ_SETUP"),
        "SETUP failure does NOT use the count-only overload (it cannot bound outage TIME)");

  // The cause must come from what the failure SITE recorded, never from decoding a negative
  // resultCode -- that number is `-errno` sampled at an arbitrary moment (RTSPClient.cpp reaches
  // handleRequestError from six places, one of them a pure response-PARSE failure where no syscall
  // ran at all). Reading it as a diagnosis is what turned a parse error into "EADDRINUSE".
  check(contains(body, "cause") && !contains(body, "classifyRequestFailure(resultCode)"),
        "the cause is taken from the recorded failure site, not decoded from a stale errno");

  check(contains(body, "monotonicSeconds"),
        "SETUP failure measures elapsed time from a MONOTONIC clock, so an NTP step cannot "
        "fabricate or hide an expired retry window");

  // Attribution in the retry line is THREE-way. A bare "NOT the camera's fault" for every
  // non-peer-attributed cause printed an acquittal for connection-closed -- where the camera may
  // be the very party that closed. Only the provably-local set may be acquitted; the rest is
  // "unproven", never a verdict.
  check(contains(body, "requestFailureIsLocalFault"),
        "the retry line only acquits the camera for provably-LOCAL causes; unproven stays unproven");
  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// No consumer, no measurement: the gap watchdog must not judge an unwatched stream
// -------------------------------------------------------------------------------------------
static void testGapWatchdogHoldsItsClockWhileNobodyConsumes() {
  // The packet total the watchdog reads (receptionStatsDB().totNumPacketsReceived()) advances only
  // when a DOWNSTREAM consumer pulls frames. With -P keeping the upstream PLAYING after the last
  // client leaves, nobody pulls, the counter freezes, and the watchdog misread "consumption
  // stopped" as "camera went silent" -- resetting a HEALTHY stream every 15s. Proven on
  // ea9f2eb99b8f8880ff04 (2026-07-21): tcpdump showed the NVR delivering 30-290 pkt/s with
  // max-gap 0.74s while every UDP session was torn down at the 15s mark (894 resets/30min,
  // footage gaps at every reset); a manual proxy with a client attached ran clean for 48s, and
  // the same proxy reset exactly 15s after its only client left. The fix: while referenceCount()
  // is zero the progress clock is HELD -- no measurement, no verdict (a proven FIN/RST still
  // resets: proof does not need the timer).
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);
  char* body = functionBody(src, "void ProxyRTSPClient::checkInterPacketGaps_()");
  char const* hold = strstr(body, "referenceCount() == 0");
  char const* decide = strstr(body, "sessionRecoveryDecide");
  check(hold != NULL, "the watchdog checks whether ANY consumer is pulling before judging silence");
  check(hold != NULL && decide != NULL && hold < decide,
        "the no-consumer clock hold happens BEFORE the recovery decision, so an unwatched healthy "
        "stream can never accumulate a bogus silence verdict");

  // THE 15.000s false-reset: the check runs every fInterPacketGapMaxTime and the progress stamp is
  // updated only INSIDE the check -- so if secsSinceProgress is derived BEFORE this check's
  // restamp, it always equals one full interval (>= the limit) and the policy resets a stream
  // that just delivered 4000 packets. Measured: every UDP session on ea9f/a3e5 torn down at
  // PLAY+15.000s (+-120us) regardless of data; -D 10 worked on pre-policy builds because they
  // compared COUNTS, not a stale time derivation. The restamp must precede the derivation.
  char const* restamp = strstr(body, "fLastPacketProgressSec = nowSec");
  char const* derive = strstr(body, "nowSec - fLastPacketProgressSec");
  check(restamp != NULL && derive != NULL && restamp < derive,
        "progress restamps the clock BEFORE seconds-since-progress is derived -- deriving first "
        "makes every on-interval check read one full interval and reset a healthy stream");
  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// The reset line names its transport
// -------------------------------------------------------------------------------------------
static void testResetLineCarriesTheTransport() {
  // Without transport= on the TX-RESET line, no tally can split faults TCP vs UDP -- the
  // Merit-Lilin 761 report (2026-07-21) needed exactly "does the stall affect both transports?"
  // and the journal could not answer. Fourth field, fourth vocabulary, one source
  // (requestTransportName in RequestRetryPolicy.hh), mirrored in FlapDiag like the other three.
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);
  char* body = functionBody(src, "void ProxyRTSPClient::scheduleReset(");
  check(contains(body, "transport="),
        "the reset line carries a transport= field");
  check(contains(body, "requestTransportName"),
        "the transport name comes from the one vocabulary, not a call-site literal");
  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// A deadline expiry is not yet a retry
// -------------------------------------------------------------------------------------------
static void testDeadlineExpiryDoesNotPreCountARetry() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);
  // requestDeadlineExpired logs BEFORE handleSetupFailure decides. Stamping that line TX-RETRY
  // counted a retry that (a) had not happened yet and (b) sometimes never happened (budget spent
  // -> reset). And when a retry DID follow, handleSetupFailure logged its own TX-RETRY -- every
  // timeout retry double-counted. The expiry line is prose; the OUTCOME line carries the token.
  char* body = functionBody(src, "void ProxyRTSPClient::requestDeadlineExpired()");
  check(!contains(body, "TX_CODE_RETRY"),
        "the deadline-expiry line carries no TX-RETRY -- the decision has not been made yet, and "
        "the outcome line (retry or reset) is the one that tallies");
  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// Session rebuild must clear the budget
// -------------------------------------------------------------------------------------------
static void testResetClearsTheRetryBudget() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw); // scan CODE, not prose that may name the same identifiers
  free(raw);
  char* body = functionBody(src, "void ProxyRTSPClient::reset()");

  // Without this, a session that spent its budget is rebuilt already exhausted: the new session's
  // first SETUP failure resets immediately, with zero retries, permanently -- the always-reset
  // behaviour returning by a slower route. reset() cleared five other fields and missed this one.
  check(contains(body, "fRequestRetry"),
        "reset() clears the retry budget, so a rebuilt session does not inherit exhaustion");

  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// An ANSWERED keep-alive is not a lost connection
// -------------------------------------------------------------------------------------------
static void testAnsweredKeepaliveIsNotReportedAsConnectionLost() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw); // scan CODE, not prose that may name the same identifiers
  free(raw);
  char* body = functionBody(src, "void ProxyRTSPClient::continueAfterLivenessCommand(");

  // live555's own comment in this function states that resultCode > 0 means the server RESPONDED.
  // The code then ignored its own distinction and treated every non-zero result as a dead
  // connection: SE_CONNECTION_LOST plus a full rebuild. A camera answering 501 Not Implemented to
  // GET_PARAMETER therefore had its session torn down on every keepalive interval, forever, over a
  // perfectly healthy TCP connection.
  check(contains(body, "requestFailureLostTheConnection") ||
        contains(body, "classifyRequestFailure"),
        "the keepalive path distinguishes 'server answered with an error' from 'connection died'");

  // A keep-alive TIMEOUT must not rebuild the session either. GStreamer answers a keep-alive that
  // gets no reply by sending ANOTHER keep-alive and carrying on -- gstrtspsrc.c:6760-6765 and
  // :6908-6913, `case GST_RTSP_ETIMEOUT: ... send keep-alive ... continue`. Nothing is torn down,
  // because no response is not evidence the peer is gone.
  //
  // Our first cut kept the session only when `resultCode > 0`, i.e. only when the server ANSWERED.
  // A timeout is resultCode < 0, so it fell through to the full rebuild -- meaning one slow camera
  // answering OPTIONS late still cost a complete outage cycle, which is the thing being fixed.
  // RequestRetryPolicy.hh defined timeoutShouldKeepSessionAlive() for exactly this and NOTHING
  // called it: a tested policy with no call site, the same shape as the unbounded-retry bug.
  check(contains(body, "timeoutShouldKeepSessionAlive"),
        "a keep-alive TIMEOUT keeps the session (GStreamer sends another keep-alive and continues)");

  // A lost CONTROL connection with UDP media reconnects in place once before rebuilding: the
  // session outlives the connection (RFC 2326 sec 1.4), and GStreamer treats this as routine
  // (gstrtspsrc.c:6914-6926). The policy lives in RTSPKeepAlive.hh; this asserts it is consulted.
  check(contains(body, "keepAliveLossReconnectsInPlace"),
        "a UDP session survives a control-connection loss via one in-place reconnect");

  // And only a method-refusal status (405/406/501/551) may demote a keep-alive method: a 500 is a
  // server fault, not a missing verb, and an answer proves the connection alive either way.
  check(contains(body, "methodRefusalDisablesMethod"),
        "method demotion is gated on the method-refusal statuses, not on any answered error");

  // A 454 on a live connection means the SESSION is already gone on the far end; the
  // connection-is-alive rule must not keep it as a zombie.
  check(contains(body, "rtspStatusEndsSession"),
        "an answered Session-Not-Found rebuilds instead of being 'KEPT' forever");

  // Only a PROVEN connection loss may carry the connection-lost reason (or record
  // SE_CONNECTION_LOST). Production showed `reason=keepalive-connection-lost cause=local-resource`
  // -- a reason asserting a condition its own cause field disclaimed.
  check(contains(body, "RESET_REASON_KEEPALIVE_FAILED"),
        "an unproven keep-alive failure resets under its own name, never as a connection loss");
  free(body);

  // A keep-alive that gets NO reply must not kill the chain: without an armed deadline nothing
  // re-schedules until a response handler runs, so the session silently stopped being kept alive --
  // and the timeout branch above was a call site no producer could reach.
  check(contains(src, "livenessDeadlineExpired"),
        "a no-reply deadline is armed on every keep-alive (the timeout branch has a real producer)");

  free(src);
}

// -------------------------------------------------------------------------------------------
// Digest auth goes through the tolerant parser
// -------------------------------------------------------------------------------------------
static void testDigestAuthGoesThroughTheTolerantParser() {
  // The WWW-Authenticate header must be parsed tolerantly (DigestAuthParams.hh), not through one
  // exact sscanf shape. A camera emitting `stale="TRUE"` quoted, or reordering parameters, or
  // adding qop=/algorithm= -- all legal -- otherwise reads as a rejected credential, and a routine
  // nonce rotation on a long-lived session costs the session.
  char* raw = readSource("../liveMedia/RTSPClient.cpp");
  char* src = stripComments(raw);
  free(raw);
  char* auth = functionBody(src, "Boolean RTSPClient::handleAuthenticationFailure(");
  check(contains(auth, "digestAuthIndicatesStaleNonce"),
        "stale-nonce detection goes through the tolerant parser, not a fixed sscanf shape");
  check(contains(auth, "digestAuthParam"),
        "realm/nonce extraction goes through the tolerant parser too");
  free(auth);
  free(src);
}

// -------------------------------------------------------------------------------------------
// Every reset call site is named
// -------------------------------------------------------------------------------------------
static void testEveryScheduleResetCallSiteNamesItself() {
  char* src = readSource("../liveMedia/ProxyServerMediaSession.cpp");

  // A captured 259s recovery could not answer "which code path rebuilt this session?", because
  // every caller emitted an identical line. An unnamed scheduleReset() is that blind spot
  // reappearing, so a bare call is a failure -- the default argument exists for compilation, not
  // for use.
  // Scan CODE only. A naive strstr also matches "scheduleReset()" inside a log message and inside a
  // comment, and it did: the first run of this test reported an anonymous call site that was really
  // the text of a debug line. A contract test that miscounts is precisely the silent-misreport
  // failure it was written to catch, so it scans the shared lexer's output with strings blanked as
  // well as comments -- the ONE walker, not a private second copy of its trickiest logic.
  char* code = stripCommentsEx(src, /*blankStrings*/ true);
  unsigned bare = 0, named = 0;
  for (char const* p = code; (p = strstr(p, "scheduleReset(")) != NULL; ) {
    char const* arg = p + strlen("scheduleReset(");
    if (*arg == ')') ++bare;  // scheduleReset() -- anonymous, cannot name its own cause
    else ++named;
    p = arg;
  }
  check(bare == 0, "no scheduleReset() call site is anonymous -- every rebuild names its cause");
  check(named >= 7, "all known reset call sites still pass a reason");

  free(code);
  free(src);
}

// -------------------------------------------------------------------------------------------
// One include guard must protect exactly ONE include
// -------------------------------------------------------------------------------------------
static void testNoIncludeIsHiddenBehindAnotherHeadersGuard() {
  // live555's house style wraps each include in the guard macro of the header it pulls in:
  //     #ifndef _RTSP_CLIENT_HH
  //     #include "RTSPClient.hh"
  //     #endif
  // Adding a SECOND include inside an existing block is silently catastrophic: the macro belongs to
  // the FIRST header, so once anything else in the translation unit has included that first header,
  // the whole block is skipped and the extra includes never happen. The types they declare then
  // vanish, and only in the build orders where that other include came first.
  //
  // Exactly that was done here: RequestRetryPolicy.hh and RTSPKeepAlive.hh were added inside
  // `#ifndef _SESSION_LIFECYCLE_HH`, so any TU reaching SessionLifecycle.hh first would lose
  // RequestRetryState and RtspLivenessMethod. Nothing triggered it locally, which is the point --
  // this is the same shape as the StreamFaultCodes.hh break that compiled on macOS and killed the
  // box build.
  // Scanned across EVERY header this campaign has touched, not just the one that broke first: the
  // defect recurred in SessionLifecycle.hh (an <stddef.h> hidden inside the _BOOLEAN_HH block --
  // in the very file whose comment narrates this failure mode) while this test was watching only
  // ProxyServerMediaSession.hh. A contract test scoped to one file polices one file.
  static char const* const headersToScan[] = {
    "../liveMedia/include/ProxyServerMediaSession.hh",
    "../liveMedia/include/SessionLifecycle.hh",
    "../liveMedia/include/SessionRecoveryPolicy.hh",
    "../liveMedia/include/RequestRetryPolicy.hh",
    "../liveMedia/include/ResetReasons.hh",
    "../liveMedia/include/StreamRole.hh",
    "../liveMedia/include/RTSPKeepAlive.hh",
    "../liveMedia/include/DigestAuthParams.hh",
    "../liveMedia/include/MonotonicTime.hh",
  };
  for (unsigned h = 0; h < sizeof(headersToScan)/sizeof(headersToScan[0]); ++h) {
    char* raw = readSource(headersToScan[h]);
    char* src = stripComments(raw);
    free(raw);

    unsigned offenders = 0;
    char const* p = src;
    // Skip the file's own include guard (the first "#ifndef _" is the header protecting itself and
    // legitimately wraps every include in the file).
    p = strstr(p, "#ifndef _");
    if (p != NULL) p += 9;
    while (p != NULL && (p = strstr(p, "#ifndef _")) != NULL) {
      char const* end = strstr(p, "#endif");
      if (end == NULL) break;

      unsigned includes = 0;
      for (char const* q = p; q < end; ++q) {
        if (strncmp(q, "#include", 8) == 0) ++includes;
      }
      if (includes > 1) {
        fprintf(stderr, "  offending guard block in %s\n", headersToScan[h]);
        ++offenders;
      }
      p = end + 6;
    }

    check(offenders == 0,
          "every inner #ifndef guard protects exactly one #include "
          "(a second one inside the block is skipped whenever the first header was already seen)");

    free(src);
  }
}

// -------------------------------------------------------------------------------------------
// A requested port that will not bind must be FATAL — for every port, with no fallback
// -------------------------------------------------------------------------------------------
static void testTheProxyNeverBindsAPortItWasNotAskedFor() {
  char* src = readSource("../proxyServer/live555ProxyServer.cpp");

  // Upstream retried the requested port, then 554, then 8554. A proxy that binds a DIFFERENT port
  // than it was asked for is not degraded, it is INVISIBLE: alive and streaming, so no supervisor
  // restarts it, while every consumer connects to the requested port, times out, and reports the
  // CAMERA as dead. The fallback also made 554/8554 the most collision-prone ports of all, since
  // every proxy that lost a race piled onto them.
  //
  // The rule is port-agnostic on purpose: a duplicate 8554 is exactly as fatal as a duplicate 11503.
  check(!contains(src, "Trying instead with the standard port numbers"),
        "the 554/8554 fallback is gone -- binding a port we were not asked for is never acceptable");
  // The real invariant, and tighter than grepping for port literals (554 is also the legitimate
  // DEFAULT when no -p is given, so a literal match there is a false positive): we attempt the bind
  // exactly ONCE. A second createRTSPServer call is a retry, and a retry on a different port is the
  // silent misbind by definition.
  unsigned attempts = 0;
  char const* q = src;
  while ((q = strstr(q, "createRTSPServer(rtspServerPortNum)")) != NULL) { ++attempts; q += 20; }
  checkEq(attempts, 1, "the RTSP port is bound in exactly one attempt -- no retry, no other port");

  // And the failure must be loud and immediate, naming the port so the conflict is actionable.
  check(contains(src, "FATAL: could not bind the requested RTSP port"),
        "a bind failure says FATAL and names the port");

  // Exactly ONE decision. This had briefly become two overlapping blocks (one gated on whether -p
  // was explicit, one not), which is how a special case sneaks back in: the day someone deletes the
  // 'wrong' one, the fallback returns for half the cases.
  unsigned bindChecks = 0;
  char const* p = src;
  while ((p = strstr(p, "rtspServer == NULL")) != NULL) { ++bindChecks; p += 18; }
  checkEq(bindChecks, 1,
          "there is exactly one bind-failure decision, with no special case for how the port "
          "was chosen");

  free(src);
}

// -------------------------------------------------------------------------------------------
// A reset reason must name the CONDITION, not the function it happens to sit in
// -------------------------------------------------------------------------------------------
static void testResetReasonsDescribeWhatActuallyHappened() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);
  char* body = functionBody(src, "int ProxyRTSPClient::connectToServer(");

  // THE BUG: this call site was named "connect-failed", but it fires when reconnectRequiresFullReset
  // sees res >= 0 -- i.e. when the connect SUCCEEDED (or is pending). The real condition is "a new
  // RTSP connection replaced the old one while subsessions were bound to it", which is a legitimate
  // rebuild, not a failure.
  //
  // Cost, measured: 183 of 217 resets in a 90-minute production window carried reason=connect-failed
  // and were read as connection failures. A name that lies is worse than no name, because the whole
  // point of naming the call sites was to stop guessing which path fired. This is the rule in
  // doc/troubleshooting/hard-rules.md -- name a thing for what it MEASURES.
  check(!contains(body, "connect-failed"),
        "the reset that fires on a SUCCESSFUL connect is not called 'connect-failed'");
  check(contains(body, "RESET_REASON_RECONNECT_REPLACED_CONNECTION"),
        "it is named for its actual condition, via the ResetReasons.hh vocabulary: a new "
        "connection invalidated bound subsessions");

  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// The response deadline must be APPLIED, not merely defined
// -------------------------------------------------------------------------------------------
static void testTheResponseDeadlineIsActuallyArmed() {
  char* src = readSource("../liveMedia/ProxyServerMediaSession.cpp");

  // requestTimeoutSecs() was defined, documented at length, and unit-tested -- with ZERO production
  // callers. So the header's central claim ("ours was ~10s, half the reference client's, which turns
  // a merely-slow camera into a torn-down session") described a fix that did not exist in the
  // binary, and test_request_retry computed a worst case from a constant production never read.
  //
  // Same shape as the retry-window bug and the unwired timeoutShouldKeepSessionAlive: a policy that
  // is correct, tested, and not plugged in. Three times is a pattern, so it gets a test.
  check(contains(src, "requestTimeoutSecs"),
        "the 20s response deadline is read by production, not just by its own test");

  free(src);
}

// -------------------------------------------------------------------------------------------
// Reset reasons are a fixed VOCABULARY, and it must not blur into the cause vocabulary
// -------------------------------------------------------------------------------------------
static void testResetReasonsComeFromTheOneVocabulary() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  // Every reason must be a RESET_REASON_* constant. A string literal at a call site is how a
  // near-duplicate spelling ("setup-failed" vs "setup-failure") gets introduced -- the log tally
  // parses these strings, so a variant is a fault it silently cannot see, and nothing catches it.
  check(strstr(src, "scheduleReset(\"") == NULL,
        "no scheduleReset() passes a string literal -- reasons come from ResetReasons.hh");

  // And the two vocabularies must stay apart. requestFailureCauseName() feeds the `cause=` field;
  // passing it as the `reason` made that field hold reset reasons on some branches and cause names
  // on others, so no tally could aggregate it.
  check(strstr(src, "scheduleReset(requestFailureCauseName") == NULL,
        "a failure CAUSE is never passed as a reset REASON -- they are separate fields");

  free(src);

  // Every constant the header declares must actually be used. An unused reason is either a rebuild
  // path that silently vanished, or a name invented for a case that does not exist -- and both look
  // identical in a log that never prints it. The corpus is the proxy PLUS SessionRecoveryPolicy.hh:
  // the gap-check reasons are consumed through that policy's action→reason mapping rather than
  // spelled at the call site (one mapping, total over the actions).
  char* hdr = readSource("../liveMedia/include/ResetReasons.hh");
  char* body = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* policy = readSource("../liveMedia/include/SessionRecoveryPolicy.hh");
  unsigned declared = 0, unused = 0;
  char const* p = hdr;
  while ((p = strstr(p, "#define RESET_REASON_")) != NULL) {
    p += strlen("#define ");
    char name[128];
    unsigned n = 0;
    while (n < sizeof(name) - 1 && p[n] != ' ' && p[n] != '\t' && p[n] != '\0') { name[n] = p[n]; ++n; }
    name[n] = '\0';
    ++declared;
    if (strstr(body, name) == NULL && strstr(policy, name) == NULL) {
      fprintf(stderr, "  unused reset reason: %s\n", name);
      ++unused;
    }
  }
  check(declared >= 10, "the reason vocabulary is declared in ResetReasons.hh");
  checkEq(unused, 0, "every declared reset reason is actually used by a call site");

  free(hdr);
  free(body);
  free(policy);
}

// -------------------------------------------------------------------------------------------
// The "no packet count yet" sentinel has ONE spelling
// -------------------------------------------------------------------------------------------
static void testThePacketCountSentinelHasOneSpelling() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  // The sentinel is PACKET_COUNT_UNKNOWN (SessionRecoveryPolicy.hh). Spelling it as a bare `~0` at
  // some sites means a reader must prove the two are the same value to know it is one sentinel, and
  // a change to the macro silently misses the literal sites. Three assignments, one name.
  check(!contains(src, "fTotNumPacketsReceived(~0)") && !contains(src, "fTotNumPacketsReceived = ~0"),
        "the packet-count sentinel is never spelled as a bare ~0");
  checkEq(countOccurrences(src, "fTotNumPacketsReceived(PACKET_COUNT_UNKNOWN)")
            + countOccurrences(src, "fTotNumPacketsReceived = PACKET_COUNT_UNKNOWN"),
          3, "all three sentinel assignments (constructor, reset, post-PLAY) use PACKET_COUNT_UNKNOWN");

  free(src);
}

// -------------------------------------------------------------------------------------------
// The gap watchdog is re-armed by exactly ONE chain of code
// -------------------------------------------------------------------------------------------
static void testTheGapWatchdogIsReArmedInExactlyOnePlace() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  // The "re-arm a SINGLE chain" rule (unschedule any pending gap check, then schedule the next) is
  // an invariant, and an invariant duplicated at two call sites must stay byte-identical forever --
  // the not-PLAYING early return and the function tail each carried a copy. One helper, one place a
  // future edit can disarm or double-arm the watchdog.
  checkEq(countOccurrences(src, "checkInterPacketGaps, this"), 1,
          "the gap-check task is scheduled from exactly one place (the shared re-arm helper)");

  free(src);
}

// -------------------------------------------------------------------------------------------
// "Trust a positive status, otherwise ask the client" is derived in ONE place
// -------------------------------------------------------------------------------------------
static void testTheFailureCauseHasOneDerivation() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  // The central rule of the failure-cause story: a POSITIVE resultCode is the real RTSP status
  // (classify it); a NEGATIVE one is `-errno` sampled at an arbitrary moment (ask the client what
  // its failure site recorded). That rule lived as a copy-pasted ternary at two call sites, and the
  // planned PLAY retry would have made a third copy. It now lives in RTSPClient::failureCauseFor(),
  // next to lastFailureCause() where the convention is defined.
  check(!contains(src, "classifyRequestFailure"),
        "no call site re-derives the failure cause inline -- the rule lives in failureCauseFor()");
  checkEq(countOccurrences(src, "failureCauseFor(resultCode)"), 3,
          "all three failure paths (keep-alive, SETUP, DESCRIBE-fail log) take the cause from the one "
          "shared derivation -- bump this only when a NEW path is wired through failureCauseFor, never "
          "to silence an inline re-derivation (the no-classifyRequestFailure check above guards that)");

  free(src);
}

// -------------------------------------------------------------------------------------------
// A server-initiated request is answered 200 OK, never 405
// -------------------------------------------------------------------------------------------
static void testServerInitiatedRequestsAreAnsweredOk() {
  char* raw = readSource("../liveMedia/RTSPClient.cpp");
  char* src = stripComments(raw); // scan CODE: the comment explaining the 405 defect names "405"
  free(raw);
  char* body = functionBody(src, "void RTSPClient::handleIncomingRequest()");

  // ONVIF NVRs and Axis-style servers ping their CLIENTS with OPTIONS/SET_PARAMETER as the server's
  // own liveness check. GStreamer answers any unknown server request with 200 OK
  // (gstrtspsrc.c:6232-6249, verified against current main 2026-07-21). live555 answered 405 Method
  // Not Allowed, which such a server may read as a broken client -- and tear the session down from
  // ITS side, a disconnect nothing on our side can explain.
  check(contains(body, "200 OK"),
        "a server-initiated request is acknowledged with 200 OK (GStreamer parity)");
  check(!contains(body, "405"),
        "no 405 is sent for a server ping -- refusing a liveness check invites a far-end teardown");
  free(body);
  free(src);
}

// -------------------------------------------------------------------------------------------
// A rejected transport is REPLACED on retry, and UDP black-holes fall back to TCP
// -------------------------------------------------------------------------------------------
static void testTransportFallbacksAreWiredIn() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  // 461: the server already refused this transport, and nothing about an identical re-send can
  // succeed. GStreamer advances its protocol mask before retrying (gstrtspsrc.c:8744-8750). The
  // policy helpers existed (nextTransportAfterRejection, requestFailureNeedsDifferentTransport) --
  // this asserts production actually consults them, because a tested-but-unwired policy is the
  // recurring defect of this whole campaign.
  char* body = functionBody(src, "void ProxyRTSPClient::handleSetupFailure(");
  check(contains(body, "requestFailureNeedsDifferentTransport"),
        "the SETUP retry path asks whether the transport must change (461 -> switch, not repeat)");
  // The doom-loop regression, measured live (2304 doubled rebuilds in 6h on 6397272975e8c006c6fc):
  // a connection loss under interleaved media must reach the decision AS a transport fact, and the
  // reset it produces must carry its own name -- both via the ONE predicate, so label and verdict
  // cannot drift.
  check(contains(body, "fStreamRTPOverTCP"),
        "the transport reaches the retry decision (interleaved connection loss must not replay)");
  check(contains(body, "RESET_REASON_SETUP_INTERLEAVED_CONNECTION_LOST") &&
        contains(body, "connectionLossRequiresRebuild"),
        "the interleaved-connection-loss rebuild is named by the predicate the decision used");
  free(body);

  // UDP that never delivers a packet is a black-hole (the handshake runs over TCP and proves
  // nothing about the media path). The gap check must feed the policy the transport and the UDP
  // no-data limit, so SESSION_RECOVERY_RESET_RETRY_TCP is reachable in production.
  check(contains(src, "udpDataStallTimeoutSecs"),
        "the UDP no-data limit is read by production, not just by its own test");
  check(contains(src, "sessionRecoverySwitchesToTCP"),
        "the gap check acts on the switch-to-TCP verdict");

  free(src);
}

// -------------------------------------------------------------------------------------------
// The audited race guards stay wired: disown, cancel-not-forget, resend-vs-new, restamp, arm-first
// -------------------------------------------------------------------------------------------
static void testTheRaceGuardsStayWired() {
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  // Deadline expiry DISOWNS the timed-out SETUP before acting, so its late response cannot cancel
  // the retry's deadline or complete against the already-consumed queue.
  char* body = functionBody(src, "void ProxyRTSPClient::requestDeadlineExpired()");
  check(contains(body, "changeResponseHandler(fGuardedSetupCSeq, NULL)"),
        "the timed-out SETUP is disowned before the retry is sent");
  free(body);

  // The guarded-cseq assignment must tolerate SYNCHRONOUS send-failure reentry: an unconditional
  // assignment clobbers the guard the nested retry just stored.
  body = functionBody(src, "void ProxyRTSPClient::sendSetupWithDeadline(");
  check(contains(body, "if (setupCSeq != 0)"),
        "a synchronous send failure cannot clobber the replacement SETUP's disown guard");
  free(body);

  // doReset CANCELS a pending reset task (a cleared-but-queued task once tore down the session that
  // had just been rebuilt); the fired-path wrapper NULLs the handle first so the cancel is a no-op.
  body = functionBody(src, "void ProxyRTSPClient::doReset()");
  check(contains(body, "unscheduleDelayedTask(fResetTask)"),
        "doReset cancels a pending reset task, never merely forgets it");
  free(body);

  // A RE-send must not count as a new SETUP: fNumSetupsSent feeds reconnectRequiresFullReset, and
  // inflating it revives the double-DESCRIBE-cycle defect. The one increment lives in sendNewSetup.
  checkEq(countOccurrences(src, "++fNumSetupsSent"), 1,
          "exactly one site increments the sent-SETUP counter (sendNewSetup; resends bypass it)");
  body = functionBody(src, "void ProxyRTSPClient::resendSetup()");
  check(contains(body, "sendSetupWithDeadline") && !contains(body, "sendNewSetup"),
        "a delayed re-send goes out with a deadline but is never counted as a new SETUP");
  // The abandoned-retry no-op must not wear TX_CODE_RETRY: TX-RETRY tallies as "a request was
  // re-sent, session kept", and nothing is re-sent here -- the queue emptied and the task is
  // discarded, which is bookkeeping (TX-CLEANUP's bucket), not a retry.
  check(!contains(body, "TX_CODE_RETRY"),
        "an ABANDONED delayed retry is not logged as a retry (nothing was re-sent)");
  free(body);

  // PLAY-ok restamps the stall clock (a resume after a PAUSE otherwise counts the whole pause as
  // upstream silence) -- and a PLAY-ok a PAUSE superseded must not re-enter PLAYING at all.
  body = functionBody(src, "void ProxyRTSPClient::continueAfterPLAY(");
  check(contains(body, "fLastPacketProgressSec = monotonicSeconds()"),
        "PLAY-ok restarts the stall clock from now");
  check(contains(body, "!fLastCommandWasPLAY"),
        "a stale PLAY-ok after a PAUSE does not mark LIVE or arm the watchdog");
  free(body);

  // The liveness no-reply deadline is armed BEFORE the send: the send can fail synchronously (the
  // handler runs inside the call), and arming after left a deadline guarding nothing -- a false
  // "got no reply" 20s later that also pushed the pending keep-alive past the session timeout.
  body = functionBody(src, "void ProxyRTSPClient::sendLivenessCommand(");
  char const* arm = strstr(body, "livenessDeadlineExpired");
  char const* sw = strstr(body, "switch (method)");
  check(arm != NULL && sw != NULL && arm < sw,
        "the liveness deadline is armed before the send, so a synchronous failure disarms it");
  // And the *_PARAMETER methods are gated on a REAL session id, not on SETUPs merely sent: without
  // one the send fails synchronously with a stale errno as its cause.
  check(contains(body, "haveRTSPSessionId"),
        "session-scoped keep-alive methods require an actual session id");
  free(body);

  // The silence-escalation threshold comes from the tested policy, not an inline literal.
  body = functionBody(src, "void ProxyRTSPClient::livenessDeadlineExpired()");
  check(contains(body, "livenessSilenceEscalates"),
        "keep-alive silence escalation consults the tested threshold");
  free(body);

  free(src);
}

// -------------------------------------------------------------------------------------------
// The DESCRIBE -> first-SETUP gap (the setup-interleaved-connection-lost rebuild loop)
// -------------------------------------------------------------------------------------------
static void testSetupNeverOpensItsOwnConnection() {
  // Captured 2026-07-21 on 6397272975e8c006c6fc against 192.168.11.19 (tetherbox fixtures
  // bare_setup_ignored_pcap_dialog_* and cam_keepalive_probe_*):
  //   - the camera FINs an idle control connection ~10s after the last request (4x in the pcap);
  //   - a SETUP sent as the FIRST command on a fresh connection is silently ignored, then FIN.
  // live555's sendRequest transparently REOPENS a closed connection and sends the queued request
  // on it. For a SETUP that produces exactly the ignored bare-SETUP above: a rebuild loop that
  // never converges (DESCRIBE 200 on one connection, camera FINs it during the wait for the first
  // downstream client, SETUP rides a virgin reopened connection, silence, FIN, rebuild, repeat).
  // The same capture shows DESCRIBE+SETUP+PLAY back-to-back on ONE connection succeeding 4/4, so:
  // no connection at SETUP time means rebuild, never a transparent reopen.
  char* raw = readSource("../liveMedia/ProxyServerMediaSession.cpp");
  char* src = stripComments(raw);
  free(raw);

  char* body = functionBody(src, "void ProxyRTSPClient::sendSetupWithDeadline(");
  check(contains(body, "socketNum() < 0"),
        "sendSetupWithDeadline checks whether the control connection is still open");
  check(contains(body, "RESET_REASON_SETUP_NO_CONNECTION"),
        "a missing control connection at SETUP time rebuilds, under its own countable reason");
  char const* guard = strstr(body, "socketNum() < 0");
  char const* send = strstr(body, "sendSetupCommand");
  check(guard != NULL && send != NULL && guard < send,
        "the connection check runs BEFORE the send (after it, RTSPClient has already reopened)");
  free(body);

  // The other half of the same capture: pre-session keep-alive cadence. Without it the connection
  // the guard above protects is ALWAYS gone by the time the first downstream client asks (first
  // keep-alive 27-55s for the default 60s session timeout vs the 10s idle FIN), so the guard would
  // just convert the doomed bare-SETUP into an equally endless rebuild loop.
  body = functionBody(src, "void ProxyRTSPClient::scheduleLivenessCommand()");
  check(contains(body, "haveRTSPSessionId"),
        "the keep-alive cadence knows whether a session exists yet: pre-session it must outrun "
        "the camera's CONNECTION idle timeout, not the advertised session timeout");
  free(body);

  free(src);
}

int main() {
  testResetReasonsComeFromTheOneVocabulary();
  testServerInitiatedRequestsAreAnsweredOk();
  testDigestAuthGoesThroughTheTolerantParser();
  testTransportFallbacksAreWiredIn();
  testTheRaceGuardsStayWired();
  testThePacketCountSentinelHasOneSpelling();
  testTheGapWatchdogIsReArmedInExactlyOnePlace();
  testTheFailureCauseHasOneDerivation();
  testTheProxyNeverBindsAPortItWasNotAskedFor();
  testResetReasonsDescribeWhatActuallyHappened();
  testTheResponseDeadlineIsActuallyArmed();
  testNoIncludeIsHiddenBehindAnotherHeadersGuard();
  testSetupFailureUsesTheCauseAwareTimeBoundedDecision();
  testResetClearsTheRetryBudget();
  testAnsweredKeepaliveIsNotReportedAsConnectionLost();
  testEveryScheduleResetCallSiteNamesItself();
  testSetupNeverOpensItsOwnConnection();
  testDeadlineExpiryDoesNotPreCountARetry();
  testResetLineCarriesTheTransport();
  testGapWatchdogHoldsItsClockWhileNobodyConsumes();

  return checkSummary("test_proxy_wiring");
}

// Unit tests for RequestRetryPolicy.hh -- retry the failed REQUEST, not the whole SESSION.
//
// WHY (measured, fixture at tetherbox spec/fixtures/live555/
// recovery_gap_verbose_trace_192.168.11.19_2026-07-20.log):
//
// One real recovery on 192.168.11.19 took 259 seconds. The proxy's own protocol work in that window
// totalled about 40 milliseconds. The rest was this loop, three times:
//
//     SETUP sent -> no SETUP-ok -> ~10s later a full session reset
//     -> re-DESCRIBE (3ms) -> WAIT for a downstream client to re-subscribe -> SETUP sent -> ...
//
// The downstream re-subscribe waits were 129.7s, 40s and 9.5s. They exist ONLY because the session
// was torn down: tearing it down drops the fan-out clients, and nothing happens again until one of
// them comes back and issues a fresh SETUP. So one unanswered SETUP costs a discarded DESCRIBE AND a
// multi-minute client-reconnect wait.
//
// GStreamer's rtspsrc does not do this. Read from gst-plugins-good/gst/rtsp/gstrtspsrc.c:
//   * SETUP failure  -> retry SETUP (next transport, or the original port pair). Session kept.
//   * conn dropped mid-request -> reconnect and replay the SAME request (`goto again`, try==0 guard).
//   * response timeout -> send keep-alive and `continue`. Session kept.
//   * budget: DEFAULT_RETRY 20. Timeouts: DEFAULT_TCP_TIMEOUT 20s, DEFAULT_TIMEOUT (UDP) 5s.
//
// It never discards a completed DESCRIBE, so its downstream clients are never dropped and those
// waits never happen. This policy encodes that shape.

#include "RequestRetryPolicy.hh"
#include "SessionLifecycle.hh" // a retry must not move the session; a reset must

#include "check.hh"

#include <errno.h> // errno VALUES differ per platform -- assert on the symbols, never the numbers
#include <stdio.h>
#include <string.h>

// --- timeouts must match the values GStreamer ships -------------------------

static void testTimeoutsMatchGStreamer() {
  // 20s. Ours was ~10s, i.e. half, against cameras measured at 117ms RTT that
  // nonetheless stall under load. A deadline tighter than the reference client's
  // turns a slow camera into a torn-down session.
  // ONE deadline, no transport parameter. This used to be requestTimeoutSecs(transport) asserting
  // 20 for TCP and (after a corrected citation) 20 for UDP too -- the parameter promised a
  // per-transport distinction the body renounced, and GStreamer passes tcp_timeout for responses on
  // BOTH transports (gstrtspsrc.c:7578-7579, :7710). The per-transport deadline that genuinely
  // exists is the DATA-stall one below.
  checkEq(requestTimeoutSecs(), 20,
          "the response deadline matches GStreamer DEFAULT_TCP_TIMEOUT (20s), for every transport");
  checkEq(udpDataStallTimeoutSecs(), 5,
          "the 5s DEFAULT_TIMEOUT keeps its real meaning: UDP delivering no DATA");

  // CORRECTED. DEFAULT_RETRY is not a request-retry budget: gstrtspsrc.c:734-736 defines it as "Max
  // number of retries when allocating RTP ports". GStreamer's request bound is `count++ > 8`
  // (gstrtspsrc.c:1814). 20 is kept as our ceiling, but it is cited where it belongs -- on the local
  // port-allocation cause, which is what production actually retries.
  checkEq(maxRequestRetries(), 20, "the ceiling for causes that need many attempts");
  checkEq(maxRetriesForCause(REQ_FAIL_LOCAL_RESOURCE), 20,
          "RTP-port allocation is the case GStreamer's DEFAULT_RETRY(20) actually bounds");
}

// --- the core rule: retry the request, keep the session ---------------------

static void testFailedRequestRetriesRatherThanResets() {
  // First failure must NOT reset. Resetting here is what discards the DESCRIBE
  // and drops every downstream client.
  check(decideRequestFailure(REQ_SETUP, /*attempts so far*/ 0) == REQ_ACTION_RETRY,
        "a first SETUP failure retries the request");
  check(decideRequestFailure(REQ_SETUP, 1) == REQ_ACTION_RETRY,
        "a second SETUP failure still retries");
  check(decideRequestFailure(REQ_SETUP, 19) == REQ_ACTION_RETRY,
        "the last attempt inside the budget still retries");

  // Only once the budget is genuinely spent does the session get rebuilt.
  check(decideRequestFailure(REQ_SETUP, 20) == REQ_ACTION_RESET_SESSION,
        "SETUP gives up and resets the session only after the full budget");
  check(decideRequestFailure(REQ_SETUP, 999) == REQ_ACTION_RESET_SESSION,
        "past the budget it stays RESET, never wraps back to retrying");
}

static void testPlayFollowsTheSameRule() {
  check(decideRequestFailure(REQ_PLAY, 0) == REQ_ACTION_RETRY,
        "a PLAY failure retries rather than resetting");
  check(decideRequestFailure(REQ_PLAY, 20) == REQ_ACTION_RESET_SESSION,
        "PLAY resets once the budget is spent");
}

static void testDescribeResetsImmediately() {
  // DESCRIBE is the FIRST request of a session; there is no session state worth
  // preserving and no downstream client attached yet, so retrying it in place
  // buys nothing. Its existing backoff ladder owns this case.
  check(decideRequestFailure(REQ_DESCRIBE, 0) == REQ_ACTION_RESET_SESSION,
        "a DESCRIBE failure is a session-level problem, not a request retry");
}

// --- a retry must preserve exactly what a reset would destroy ---------------

static void testRetryPreservesTheHandshake() {
  // Every retry flavour keeps the DESCRIBE and the fan-out clients; only a reset destroys them
  // (that destruction is what cost the 129.7s / 40s / 9.5s re-subscribe waits). The claim lives on
  // requestActionRetries itself -- it briefly had two separately-named aliases that could not
  // diverge, two names for one measurement.
  check(requestActionRetries(REQ_ACTION_RETRY),
        "retrying keeps the completed DESCRIBE and the fan-out clients (the whole point)");
  check(requestActionRetries(REQ_ACTION_RETRY_DELAYED) &&
        requestActionRetries(REQ_ACTION_RETRY_RECONNECT),
        "the delayed and reconnect-and-replay flavours preserve them too");
  check(!requestActionRetries(REQ_ACTION_RESET_SESSION),
        "a session reset is the one action that discards them");
}

// --- a success must clear the counter ---------------------------------------

static void testSuccessResetsTheBudget() {
  // Without this, a stream that fails 20 times over its whole life can never
  // retry again, and a long-lived proxy degrades into reset-only behaviour.
  checkEq(attemptsAfterSuccess(), 0, "a successful request clears the retry count");
}

// --- the count-only overload must be a VIEW of the full decision ------------

static void testCountOnlyOverloadIsAViewOfTheFullDecision() {
  // decideRequestFailure() exists so the pure count/window arithmetic stays testable in isolation,
  // but it must never become a SECOND policy: production calls decideRequestFailureFor (the
  // proxy-wiring suite enforces that), and a timeout is the cause whose budget the count-only
  // overload has always described (full maxRequestRetries() ceiling, plain RETRY action). Pin the
  // equivalence across the whole input space, so an edit to either function that makes them
  // disagree fails here rather than shipping two decisions that drift.
  RequestKind const kinds[] = { REQ_DESCRIBE, REQ_SETUP, REQ_PLAY };
  unsigned const attempts[] = { 0, 1, 19, 20, 21, 999 };
  unsigned const elapsed[] = { 0, 30, 59, 60, 300 };
  for (unsigned k = 0; k < sizeof(kinds)/sizeof(kinds[0]); ++k) {
    for (unsigned a = 0; a < sizeof(attempts)/sizeof(attempts[0]); ++a) {
      for (unsigned e = 0; e < sizeof(elapsed)/sizeof(elapsed[0]); ++e) {
        check(decideRequestFailure(kinds[k], attempts[a], elapsed[e])
                == decideRequestFailureFor(kinds[k], REQ_FAIL_TIMEOUT, attempts[a], elapsed[e],
                                           /*mediaInterleaved -- irrelevant for TIMEOUT*/ false),
              "the count-only overload equals the full decision with a TIMEOUT cause "
              "(one policy, two views -- never two policies)");
      }
    }
  }
}

// --- reachability: every declared action must be attainable -----------------

static void testEveryActionIsReachable() {
  bool sawRetry = false, sawReset = false;
  for (unsigned n = 0; n <= 25; ++n) {
    RequestFailureAction a = decideRequestFailure(REQ_SETUP, n);
    if (a == REQ_ACTION_RETRY) sawRetry = true;
    if (a == REQ_ACTION_RESET_SESSION) sawReset = true;
  }
  check(sawRetry, "RETRY is reachable");
  check(sawReset, "RESET_SESSION is reachable");
}

// --- the WORST CASE must beat the bug it replaces ---------------------------

static void testWorstCaseIsBoundedByTimeNotCount() {
  // THE TRAP THIS CATCHES: 20 retries x a 20s TCP timeout is 400 seconds. The recovery being fixed
  // was 259s. A count-only budget therefore lets the fix produce a WORSE outage than the defect,
  // and nothing in the count-based tests above would have shown it. GStreamer's 20 is sized for
  // transport errors that fail in milliseconds, not for twenty consecutive full timeouts.
  unsigned const countOnlyWorstCase = maxRequestRetries() * requestTimeoutSecs();
  check(countOnlyWorstCase > 259,
        "sanity: a count-only budget really can exceed the 259s recovery it replaces "
        "(if this ever fails the constants moved and this test needs rethinking)");

  // The real bound is TIME, and it must be comfortably under the measured 137s median recovery --
  // past that, rebuilding the session is no longer the more expensive option.
  check(maxRequestRetryWindowSecs() < 137,
        "worst-case retry time must stay below the 137s median recovery it is meant to avoid");
  checkEq(maxRequestRetryWindowSecs(), 60, "worst-case retry window is 60s");
}

static void testTimeWindowOverridesTheCount() {
  // Inside the window, the count governs.
  check(decideRequestFailure(REQ_SETUP, 0, /*elapsed*/ 0) == REQ_ACTION_RETRY,
        "a fresh failure inside the window retries");
  check(decideRequestFailure(REQ_SETUP, 5, 30) == REQ_ACTION_RETRY,
        "still retrying half way through the window");

  // Past the window it stops, even with plenty of count left. This is the case a count-only budget
  // gets wrong.
  check(decideRequestFailure(REQ_SETUP, 1, 60) == REQ_ACTION_RESET_SESSION,
        "the window expiring stops retries even on attempt 1 of 20");
  check(decideRequestFailure(REQ_SETUP, 0, 300) == REQ_ACTION_RESET_SESSION,
        "long past the window it rebuilds regardless of count");

  check(retryWindowExpired(59) == false, "59s is inside the window");
  check(retryWindowExpired(60), "60s is the boundary and counts as expired");
}

// --- state-machine flow: a retry is NOT a reset -----------------------------

static void testRetryDoesNotDisturbTheSessionState() {
  // A retry must leave the session exactly where a reset would have moved it FROM. This is the
  // property the whole fix depends on: the captured trace shows resets going
  // DESCRIBED -> RESETTING -> INIT, which is what discards the DESCRIBE and drops the fan-out
  // clients. A retry has to stay in DESCRIBED.
  SessionState state = SL_DESCRIBED;

  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    check(decideRequestFailure(REQ_SETUP, attempt, attempt * 2) == REQ_ACTION_RETRY,
          "consecutive SETUP failures keep retrying");

    // A SETUP failure is an OBSERVATION in the lifecycle, not a transition -- see
    // SessionLifecycle.hh. Retrying must not add one.
    SessionState before = state;
    SessionTransition t = sessionTransition(state, SE_SETUP_FAILED);
    check(t.legal, "SETUP-failed is legal while DESCRIBED");
    state = t.nextState;
    check(state == before, "a SETUP failure must not move the session out of DESCRIBED");
  }

  // And the eventual success completes the handshake from that same state, with no rebuild anywhere.
  SessionTransition ok = sessionTransition(state, SE_SETUP_OK);
  check(ok.legal, "SETUP-ok is legal after repeated failures");
  check(ok.nextState == SL_READY, "a successful retry reaches READY without passing through RESETTING");
}

static void testResetPathStillReachesInitWhenBudgetIsSpent() {
  // The escape hatch must still work: once retries are exhausted the session really is rebuilt,
  // exactly as before. Losing this would trade a slow recovery for no recovery.
  check(decideRequestFailure(REQ_SETUP, maxRequestRetries(), 0) == REQ_ACTION_RESET_SESSION,
        "budget spent -> reset");

  SessionTransition started = sessionTransition(SL_DESCRIBED, SE_RESET_STARTED);
  check(started.legal && started.nextState == SL_RESETTING, "reset from DESCRIBED is legal");
  SessionTransition done = sessionTransition(started.nextState, SE_RESET_COMPLETED);
  check(done.legal && done.nextState == SL_INIT, "reset completes back to INIT");
}

// --- EVERY failure condition, and the response GStreamer gives it -----------

struct FailureCase {
  RequestKind kind;
  RequestFailureCause cause;
  unsigned attempts;
  unsigned elapsed;
  RequestFailureAction expected;
  char const* why;
};

static void testEveryFailureConditionHasTheRightResponse() {
  // Each row cites the gstrtspsrc.c behaviour it mirrors. A table rather than prose so a new failure
  // cause cannot be added without deciding, explicitly, what happens for it.
  static FailureCase const cases[] = {
    // --- timeout: the camera is slow, not gone. Keep the session, keep the clients. -------------
    {REQ_SETUP, REQ_FAIL_TIMEOUT, 0, 0, REQ_ACTION_RETRY,
     "first SETUP timeout retries; this is the exact case that cost 259s by resetting"},
    {REQ_SETUP, REQ_FAIL_TIMEOUT, 3, 12, REQ_ACTION_RETRY,
     "still retrying a slow camera inside the window"},
    {REQ_PLAY, REQ_FAIL_TIMEOUT, 0, 0, REQ_ACTION_RETRY,
     "PLAY timeout retries too"},

    // --- transport rejected: try again, do not rebuild (STS_UNSUPPORTED_TRANSPORT) --------------
    {REQ_SETUP, REQ_FAIL_TRANSPORT_UNSUPPORTED, 0, 0, REQ_ACTION_RETRY,
     "GStreamer retries SETUP with the next protocol / original ports"},
    // SUPERSEDED ASSERTION, kept as its corrected form rather than deleted. This used to expect
    // RETRY at attempts=5, on the assumption that transport rejections shared the 20-retry ceiling.
    // Reading gstrtspsrc.c showed the remedy is to ask for a DIFFERENT transport, and we have
    // exactly two — so after one switch there is nothing left to try, and further "retries" would
    // re-send a transport the camera has already refused.
    {REQ_SETUP, REQ_FAIL_TRANSPORT_UNSUPPORTED, 5, 20, REQ_ACTION_RESET_SESSION,
     "both transports have now been refused -- retrying either again cannot succeed"},
    {REQ_SETUP, REQ_FAIL_TRANSPORT_UNSUPPORTED, 1, 0, REQ_ACTION_RESET_SESSION,
     "the single transport switch is spent after one attempt"},

    // --- auth: retry EXACTLY once, then stop (setup_auth && !(retry++)) -------------------------
    {REQ_SETUP, REQ_FAIL_AUTH, 0, 0, REQ_ACTION_RETRY,
     "attach credentials and retry once"},
    {REQ_SETUP, REQ_FAIL_AUTH, 1, 1, REQ_ACTION_RESET_SESSION,
     "a SECOND auth failure must not retry: credentials did not change, and hammering a rejecting "
     "server risks an account lockout"},

    // --- connection died mid-request: reconnect FIRST, then replay (goto again, try==0) ---------
    {REQ_SETUP, REQ_FAIL_CONNECTION_CLOSED, 0, 0, REQ_ACTION_RETRY_RECONNECT,
     "re-sending on a dead socket is pointless; the connection must come back first"},
    {REQ_SETUP, REQ_FAIL_CONNECTION_CLOSED, 1, 2, REQ_ACTION_RESET_SESSION,
     "GStreamer reconnects once and then errors out; so do we"},

    // --- definite refusal: retrying an identical request cannot help ----------------------------
    {REQ_SETUP, REQ_FAIL_ERROR_STATUS, 0, 0, REQ_ACTION_RESET_SESSION,
     "a 4xx/5xx refusal is not a transient; burning the retry window on it delays real recovery"},

    // --- DESCRIBE is always a session-level rebuild ---------------------------------------------
    {REQ_DESCRIBE, REQ_FAIL_TIMEOUT, 0, 0, REQ_ACTION_RESET_SESSION,
     "no session or client exists yet, so a retry saves nothing"},
    {REQ_DESCRIBE, REQ_FAIL_AUTH, 0, 0, REQ_ACTION_RESET_SESSION,
     "same for auth at DESCRIBE time"},

    // --- the time window overrides every cause --------------------------------------------------
    {REQ_SETUP, REQ_FAIL_TIMEOUT, 0, 60, REQ_ACTION_RESET_SESSION,
     "window expired: stop retrying even on the first attempt"},
    {REQ_SETUP, REQ_FAIL_TRANSPORT_UNSUPPORTED, 0, 120, REQ_ACTION_RESET_SESSION,
     "window expired beats a cause that would otherwise retry freely"},

    // --- count exhaustion still works -----------------------------------------------------------
    {REQ_SETUP, REQ_FAIL_TIMEOUT, 20, 30, REQ_ACTION_RESET_SESSION,
     "budget spent inside the window -> rebuild"},
  };

  unsigned const n = sizeof(cases) / sizeof(cases[0]);
  for (unsigned i = 0; i < n; ++i) {
    FailureCase const& c = cases[i];
    RequestFailureAction got =
      decideRequestFailureFor(c.kind, c.cause, c.attempts, c.elapsed,
                              /*mediaInterleaved*/ false); // the table pins the UDP-media shapes;
                                                          // the interleaved overrides have their own test
    if (got != c.expected) {
      fprintf(stderr, "FAIL: [%s attempts=%u elapsed=%us] %s (got %d, want %d)\n",
              requestFailureCauseName(c.cause), c.attempts, c.elapsed, c.why,
              (int)got, (int)c.expected);
      ++g_checkFailures;
    }
  }
}

static void testAuthAndConnectionBudgetsAreDeliberatelyTiny() {
  // These two differ from the general budget on purpose. If someone "simplifies" them to share the
  // 20-retry budget, a rejected login is retried 20 times and a dead connection is reconnected 20
  // times inside one window.
  checkEq(maxRetriesForCause(REQ_FAIL_AUTH), 1, "auth retries exactly once");
  checkEq(maxRetriesForCause(REQ_FAIL_CONNECTION_CLOSED), 1, "reconnect-and-replay happens once");
  checkEq(maxRetriesForCause(REQ_FAIL_ERROR_STATUS), 0, "a definite refusal is never retried");
  checkEq(maxRetriesForCause(REQ_FAIL_TIMEOUT), maxRequestRetries(),
          "a slow camera gets the full budget");
}

static void testAnIdleTimeoutKeepsTheSessionAlive() {
  // GStreamer answers a response timeout with a keep-alive and `continue`. Tearing down instead is
  // how a merely-quiet camera becomes an outage.
  check(timeoutShouldKeepSessionAlive(REQ_FAIL_TIMEOUT),
        "a response timeout is answered with a keep-alive, not a teardown");
  check(!timeoutShouldKeepSessionAlive(REQ_FAIL_CONNECTION_CLOSED),
        "a closed connection is NOT something a keep-alive can paper over");
}

static void testEveryCauseIsNamedAndDecided() {
  // A cause with no name prints as an integer in a log; a cause with no decision falls through to
  // whatever the last branch happened to be. Both are the silent-misreport failure mode.
  static RequestFailureCause const all[] = {
    REQ_FAIL_TIMEOUT, REQ_FAIL_TRANSPORT_UNSUPPORTED, REQ_FAIL_AUTH,
    REQ_FAIL_CONNECTION_CLOSED, REQ_FAIL_ERROR_STATUS, REQ_FAIL_RESPONSE_TRUNCATED,
    REQ_FAIL_LOCAL_RESOURCE, REQ_FAIL_UNKNOWN
  };
  for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
    check(strcmp(requestFailureCauseName(all[i]), "unknown") != 0,
          "every failure cause has a greppable name");
    // Must not crash or fall through; any of the three actions is a decision.
    RequestFailureAction a = decideRequestFailureFor(REQ_SETUP, all[i], 0, 0, false);
    check(requestActionRetries(a) || a == REQ_ACTION_RESET_SESSION,
          "every cause yields a defined action");
  }
}

// ---------------------------------------------------------------------------------------------
// Turning live555's `resultCode` into a cause.
//
// Every one of the decisions above is worthless unless something maps the ONE integer production
// actually receives onto a RequestFailureCause. Without it, `continueAfterSETUP` can only say "it
// failed" -- which is exactly the flattening that produced always-reset, arriving one layer down.
//
// live555's RTSPClient convention (see RTSPClient::handleResponseBytes): resultCode < 0 is
// -errno (the request never got a response), resultCode > 0 is the RTSP response status code.
// ---------------------------------------------------------------------------------------------
static void testResultCodesClassifyIntoCauses() {
  // SYMBOLIC errno only. ETIMEDOUT is 60 on macOS and 110 on Linux; hardcoding the numbers would
  // make this suite pass on the dev Mac while the box classified a timeout as something else
  // entirely -- a green test certifying the opposite of production behaviour.
  //
  // No response at all. ETIMEDOUT / EWOULDBLOCK are the deadline expiring.
  check(classifyRequestFailure(-ETIMEDOUT) == REQ_FAIL_TIMEOUT,
        "a timed-out request is a timeout, not a generic failure");
  check(classifyRequestFailure(-EWOULDBLOCK) == REQ_FAIL_TIMEOUT,
        "a would-block deadline is a timeout");

  // The socket died under the request. GStreamer reconnects and replays; it does not rebuild.
  check(classifyRequestFailure(-ECONNRESET) == REQ_FAIL_CONNECTION_CLOSED,
        "a reset connection is a closed connection, not a timeout");
  check(classifyRequestFailure(-EPIPE) == REQ_FAIL_CONNECTION_CLOSED,
        "a broken pipe is a closed connection");
  check(classifyRequestFailure(-EBADF) == REQ_FAIL_CONNECTION_CLOSED,
        "a closed fd is a closed connection");

  // The server ANSWERED. That is a completely different world: the connection is provably alive.
  check(classifyRequestFailure(401) == REQ_FAIL_AUTH,
        "401 is auth -- retried once with credentials, never twenty times");
  check(classifyRequestFailure(461) == REQ_FAIL_TRANSPORT_UNSUPPORTED,
        "461 Unsupported Transport retries SETUP with another transport");
  // SUPERSEDED: this asserted 404 was "a definite refusal". gstrtspsrc.c:7828-7836 handles
  // GST_RTSP_STS_NOT_FOUND in the same branch as GST_RTSP_STS_UNAUTHORIZED — cameras that answer
  // 404 to an UNAUTHENTICATED request are common, and a flat refusal means credentials are never
  // attached and the camera is permanently unreachable. One retry, with auth.
  check(classifyRequestFailure(404) == REQ_FAIL_AUTH,
        "404 may be auth in disguise -- retried ONCE with credentials, as GStreamer does");
  check(classifyRequestFailure(454) == REQ_FAIL_ERROR_STATUS,
        "a 454 Session Not Found IS a definite refusal; retrying it burns the window for nothing");
  check(classifyRequestFailure(500) == REQ_FAIL_ERROR_STATUS,
        "a 5xx is a definite refusal");

  // The one that must never quietly become something else: an errno we did not enumerate. Folding it
  // into REQ_FAIL_ERROR_STATUS would claim the server answered when nothing came back at all, and a
  // log reading "error-status" for a local bug is how our fault gets attributed to their camera.
  check(classifyRequestFailure(-4095) == REQ_FAIL_UNKNOWN,
        "an unrecognised errno is reported AS unclassified, not folded into a real cause");
  checkEq(maxRetriesForCause(REQ_FAIL_UNKNOWN), 0,
          "an unclassified failure gets no invented retry budget");
  check(strcmp(requestFailureCauseName(REQ_FAIL_UNKNOWN), "unclassified") == 0,
        "an unclassified failure says so in the log, so the errno can be added");
}

// ---------------------------------------------------------------------------------------------
// EADDRINUSE: the failure production is ACTUALLY hitting.
//
// This is not hypothetical. Measured on 6397272975e8c006c6fc (49 Merit-Lilin cameras), 90 minutes
// of journal from the deployed retry build:
//
//   TX-RETRY ...: SETUP failed (result -98), retrying the request -- attempt 4 of 20.
//   TX-RESET ... reason=connect-failed state=DESCRIBED   x183
//
// errno 98, confirmed against that box's own /usr/include/asm-generic/errno.h line 81, is
// EADDRINUSE. live555 binds a SPECIFIC local RTP port pair per subsession (GroupsockHelper.cpp
// bind() error paths), so a re-SETUP after a reset tries to claim a port the dying session has not
// released yet. It clears on its own -- the production logs show attempt 4 succeeding.
//
// Two things follow, and the first is a bug in this very file's first draft:
//
//   1. classifyRequestFailure did not enumerate EADDRINUSE, so it returned REQ_FAIL_UNKNOWN, whose
//      budget is ZERO. Shipping that would have converted a retry loop that DEMONSTRABLY RECOVERS
//      into an instant session rebuild -- making production strictly worse while every test passed.
//      This is precisely why the unclassified branch must be loud rather than convenient.
//   2. A local port conflict is OURS. Reporting it as a camera fault would blame a device that did
//      nothing wrong, in an investigation already burned once by exactly that.
// ---------------------------------------------------------------------------------------------
static void testAddressInUseIsRetryableAndNotBlamedOnTheCamera() {
  check(classifyRequestFailure(-EADDRINUSE) == REQ_FAIL_LOCAL_RESOURCE,
        "EADDRINUSE is a LOCAL resource conflict, not an unclassified mystery");

  // The budget must be real. Zero retries here is the regression described above.
  check(maxRetriesForCause(REQ_FAIL_LOCAL_RESOURCE) >= 4,
        "a local port conflict gets enough retries to outlast the socket that holds the port "
        "(production recovers on attempt 4)");

  RequestRetryState s;
  s.clear();
  for (unsigned i = 0; i < 4; ++i) {
    check(s.decide(REQ_SETUP, REQ_FAIL_LOCAL_RESOURCE, (long)i, false) == REQ_ACTION_RETRY_DELAYED,
          "a port held by a closing socket is retried AFTER A DELAY, never instantly");
    s.noteFailure((long)i, REQ_FAIL_LOCAL_RESOURCE);
  }
  s.noteSuccess();
  checkEq(s.attempts, 0, "the eventual success clears the budget");

  // An instant re-send cannot succeed: the port is held until the old socket finishes closing, so
  // hammering it just burns the whole budget inside a few milliseconds and then rebuilds anyway.
  check(requestRetryDelayUs(REQ_FAIL_LOCAL_RESOURCE) > 0,
        "a local-resource retry waits; a zero delay spends 20 attempts in microseconds");
  checkEq(requestRetryDelayUs(REQ_FAIL_TIMEOUT), 0,
          "a timeout already waited for its deadline -- it must NOT wait again");

  // Never blame the peer for our own port.
  check(!requestFailureLostTheConnection(REQ_FAIL_LOCAL_RESOURCE),
        "a local bind failure says nothing about the connection");
  check(!requestFailureBlamesPeer(REQ_FAIL_LOCAL_RESOURCE),
        "a local port conflict is OURS -- it must never be attributed to the camera");
  check(requestFailureBlamesPeer(REQ_FAIL_ERROR_STATUS),
        "an error status genuinely did come from the peer");
  check(!requestFailureBlamesPeer(REQ_FAIL_UNKNOWN),
        "an unclassified failure blames nobody");
  // A truncated response is OUR buffer limit: the server answered and behaved. Recording it as
  // ERROR_STATUS printed ", peer-attributed" for our own limit.
  check(!requestFailureBlamesPeer(REQ_FAIL_RESPONSE_TRUNCATED),
        "a response WE could not hold never blames the peer");
  checkEq(maxRetriesForCause(REQ_FAIL_RESPONSE_TRUNCATED), 0,
          "an identical request truncates identically -- rebuild, do not retry");

  // And it must be greppable as itself, so the next person sees "local-resource", not "unclassified"
  // and not "the camera did not answer".
  check(strcmp(requestFailureCauseName(REQ_FAIL_LOCAL_RESOURCE), "local-resource") == 0,
        "the log names the real cause");

  // Attribution is THREE-way, not two-way. "not peer-attributed" covers two different verdicts:
  // provably OURS (local-resource, response-truncated) and merely UNPROVEN (connection-closed,
  // timeout -- a camera FIN is the camera acting, we just cannot prove who closed). Production
  // printed ", NOT the camera's fault" for cause=connection-closed, an affirmative acquittal
  // silence cannot support -- the same overclaim class as "Far-end close PROVEN" for our own
  // recorder. requestFailureIsLocalFault names the only set the acquittal is true for.
  check(requestFailureIsLocalFault(REQ_FAIL_LOCAL_RESOURCE),
        "a local port/fd/buffer failure is provably ours");
  check(requestFailureIsLocalFault(REQ_FAIL_RESPONSE_TRUNCATED),
        "a response our buffer could not hold is provably ours");
  check(!requestFailureIsLocalFault(REQ_FAIL_CONNECTION_CLOSED),
        "a closed connection is UNPROVEN, not an acquittal -- the camera may be the one closing");
  check(!requestFailureIsLocalFault(REQ_FAIL_TIMEOUT),
        "silence is unproven, not an acquittal");
  check(!requestFailureIsLocalFault(REQ_FAIL_UNKNOWN),
        "an unclassified failure is exactly the case where no verdict may be printed");
  // The two verdict sets must never overlap: a cause that is both peer-attributed and provably
  // ours is a contradiction, and the log line picks between them with a ternary chain.
  for (int c = REQ_FAIL_NONE; c <= REQ_FAIL_UNKNOWN; ++c) {
    RequestFailureCause const cause = (RequestFailureCause)c;
    check(!(requestFailureBlamesPeer(cause) && requestFailureIsLocalFault(cause)),
          "no cause is simultaneously peer-attributed and provably ours");
  }
}

static void testTransportsHaveGreppableNames() {
  // The TX-RESET line carries transport= so fault tallies can be SPLIT by transport. Without it,
  // "the stall affects both TCP and UDP" could only be asserted from memory -- a 47-camera
  // manufacturer report (Merit-Lilin 761, 2026-07-21) needed exactly that split and the journal
  // could not provide it. Media over the RTSP connection is interleaved by definition, so the TCP
  // name says so -- "tcp" alone would leave a reader wondering which TCP mode was in play.
  check(strcmp(requestTransportName(REQ_TRANSPORT_TCP), "tcp-interleaved") == 0,
        "TCP media transport names itself as interleaved");
  check(strcmp(requestTransportName(REQ_TRANSPORT_UDP), "udp") == 0,
        "UDP media transport is greppable as udp");
}

static void testTheOtherLocalResourceErrnosAreCoveredToo() {
  // EADDRINUSE was found in production. These are the same class of local exhaustion and would
  // otherwise each arrive as an unclassified zero-budget reset the first time they happen -- the
  // failure mode we just caught, waiting to recur under a different errno.
  check(classifyRequestFailure(-EADDRNOTAVAIL) == REQ_FAIL_LOCAL_RESOURCE,
        "EADDRNOTAVAIL is a local address problem");
  check(classifyRequestFailure(-EMFILE) == REQ_FAIL_LOCAL_RESOURCE,
        "EMFILE (per-process fd limit) is ours, not the camera's");
  check(classifyRequestFailure(-ENFILE) == REQ_FAIL_LOCAL_RESOURCE,
        "ENFILE (system-wide fd limit) is ours");
  check(classifyRequestFailure(-ENOBUFS) == REQ_FAIL_LOCAL_RESOURCE,
        "ENOBUFS is local buffer exhaustion");
  check(classifyRequestFailure(-ENOMEM) == REQ_FAIL_LOCAL_RESOURCE,
        "ENOMEM is ours");
}

static void testAnAnsweredRequestIsNeverCalledAConnectionLoss() {
  // The bug this pins: a keep-alive that gets an RTSP ERROR STATUS back was being recorded as
  // SE_CONNECTION_LOST and used to justify a full session rebuild. But a status code is PROOF the
  // connection is alive -- the server had to receive the request and write a reply to produce it.
  // A camera that answers 501 to GET_PARAMETER would therefore have its session rebuilt on every
  // keepalive interval, forever, with a perfectly healthy TCP connection underneath.
  check(!requestFailureLostTheConnection(classifyRequestFailure(501)),
        "an answered request did NOT lose the connection, whatever the status says");
  check(!requestFailureLostTheConnection(classifyRequestFailure(401)),
        "a 401 is an answer, not a disconnect");
  // Symbolic, never the number: errno VALUES are platform-specific. ECONNRESET is 54 on macOS and
  // 104 on Linux, so the literal -54 asserted a truth on the dev Mac and a falsehood on the box it
  // ships to -- it failed the deploy gate on the target platform while passing locally. The
  // production classifier already switches on the symbols; the test must speak the same language.
  check(requestFailureLostTheConnection(classifyRequestFailure(-ECONNRESET)),
        "an ECONNRESET really did lose the connection");
  check(requestFailureLostTheConnection(classifyRequestFailure(-EPIPE)),
        "an EPIPE really did lose the connection");
  // A timeout is ambiguous by nature -- no answer came, but nothing proved the socket dead either.
  // GStreamer's answer is to keep the session and send a keep-alive, so it must NOT be reported as
  // a connection loss.
  check(!requestFailureLostTheConnection(REQ_FAIL_TIMEOUT),
        "a timeout is not evidence of a lost connection (GStreamer keeps the session)");
}

// ---------------------------------------------------------------------------------------------
// The retry BUDGET as a live object.
//
// The decision functions above are pure, which means somebody has to hold the attempt count and the
// window start. Production held only the count, in a bare `unsigned`, and that omission is a bug in
// two independent ways -- both pinned here.
// ---------------------------------------------------------------------------------------------
static void testRetryStateClearsOnSessionRebuild() {
  // THE BUG: `reset()` cleared fNumSetupsSent, fNextDESCRIBEDelay, fDoneDESCRIBE ... and left the
  // attempt counter alone. Only a SUCCESS cleared it. So the instant one budget was spent, the
  // rebuilt session started life already exhausted: its very first SETUP failure reset again, and
  // again, with zero retries -- silently reverting to the exact always-reset behaviour this whole
  // policy exists to remove, and doing so PERMANENTLY until a SETUP happened to succeed.
  RequestRetryState s;
  s.clear();
  for (unsigned i = 0; i < maxRequestRetries(); ++i) s.noteFailure(0, REQ_FAIL_TIMEOUT);
  check(s.attempts == maxRequestRetries(), "budget is spent after the full run of failures");
  check(s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, 0, false) == REQ_ACTION_RESET_SESSION,
        "a spent budget resets, as designed");

  s.clear(); // the rebuild
  check(s.attempts == 0, "a session rebuild clears the retry budget");
  check(s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, 1000, false) == REQ_ACTION_RETRY,
        "the rebuilt session can retry again -- it does not inherit the old session's exhaustion");
}

static void testRetryStateBoundsByElapsedTimeNotJustCount() {
  // THE BUG: production called the count-only overload and passed no elapsed time at all, so
  // maxRequestRetryWindowSecs() was dead code and the 400s worst case was fully reachable in
  // production while the tests "proved" it was bounded at 60s.
  RequestRetryState s;
  s.clear();

  // Two slow timeouts, 20s apart, well inside the count budget.
  s.noteFailure(0, REQ_FAIL_TIMEOUT);
  check(s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, 20, false) == REQ_ACTION_RETRY,
        "still retrying 20s in");
  s.noteFailure(20, REQ_FAIL_TIMEOUT);
  check(s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, 40, false) == REQ_ACTION_RETRY,
        "still retrying 40s in");
  s.noteFailure(40, REQ_FAIL_TIMEOUT);

  // Now past the window. The COUNT still permits 17 more retries; the clock does not.
  check(s.attempts < maxRequestRetries(),
        "the count budget is deliberately NOT the thing that stops this");
  check(s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, 61, false) == REQ_ACTION_RESET_SESSION,
        "past the window we rebuild, even with count budget to spare");
  checkEq(s.elapsedSince(61), 61, "elapsed is measured from the FIRST failure, not the last");
}

static void testRetryStateSurvivesAMonotonicClockThatDoesNotStartAtZero() {
  // monotonicSeconds() is uptime-based: on a box up for 40 days it is ~3.5 million. Storing the
  // start as 0 and computing `now - 0` would make every first retry look like it had already been
  // going for 40 days, and the window would expire instantly -- disabling the retry path entirely
  // on exactly the long-running boxes this was written for.
  RequestRetryState s;
  long const uptime = 3456789L;
  s.clear();
  s.noteFailure(uptime, REQ_FAIL_TIMEOUT);
  checkEq(s.elapsedSince(uptime + 5), 5, "elapsed is relative to the clock we were given");
  check(s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, uptime + 5, false) == REQ_ACTION_RETRY,
        "a large monotonic clock does not fabricate an expired window");
}

static void testTheCapturedOutageReplayedEndToEnd() {
  // The actual 192.168.11.19 recovery, driven through the policy as a sequence rather than as
  // isolated predicates: three unanswered SETUPs, each ~10s apart. Under the OLD behaviour each one
  // rebuilt the session, discarding a completed DESCRIBE and dropping the fan-out clients, and the
  // re-subscribe waits (129.7s + 40s + 9.5s) made the recovery 259s.
  RequestRetryState s;
  s.clear();
  long const setupFailureAt[] = {0, 10, 20};
  for (unsigned i = 0; i < 3; ++i) {
    RequestFailureAction a = s.decide(REQ_SETUP, REQ_FAIL_TIMEOUT, setupFailureAt[i], false);
    check(a == REQ_ACTION_RETRY, "each unanswered SETUP retries in place");
    check(requestActionRetries(a),
          "the DESCRIBE and the fan-out clients stay -- no re-subscribe wait is incurred");
    s.noteFailure(setupFailureAt[i], REQ_FAIL_TIMEOUT);
  }
  // The fourth SETUP succeeds, as it did.
  s.noteSuccess();
  checkEq(s.attempts, 0, "success clears the budget for the next incident");

  // And the whole thing is bounded: had the camera never answered, we would have rebuilt at 60s,
  // not at 400s and not after three multi-minute client waits.
  check(maxRequestRetryWindowSecs() < 137, "worst case stays under the observed median recovery");
}

// ---------------------------------------------------------------------------------------------
// The attempt counter is PER CAUSE, like the budgets it is compared against
// ---------------------------------------------------------------------------------------------
static void testAttemptCounterIsPerCause() {
  // THE TRAP: a cause-blind counter compared against per-cause budgets spends one cause's budget on
  // another's failures. Concrete: SETUP times out once (attempts=1), the camera then answers 461 --
  // and 1 >= the transport budget of 1 meant straight to RESET, so the OTHER transport (the one
  // action guaranteed to change the outcome) was never offered. GStreamer's per-cause guards
  // (`!(retry++)`, `try == 0`) are independent state for the same reason.
  RequestRetryState s;
  s.clear();
  s.noteFailure(0, REQ_FAIL_TIMEOUT); // a prior, unrelated failure of the same request
  check(s.decide(REQ_SETUP, REQ_FAIL_TRANSPORT_UNSUPPORTED, 1, false) != REQ_ACTION_RESET_SESSION,
        "a prior timeout does not eat the 461 transport-switch budget");
  s.noteFailure(1, REQ_FAIL_TRANSPORT_UNSUPPORTED);
  check(s.decide(REQ_SETUP, REQ_FAIL_TRANSPORT_UNSUPPORTED, 2, false) == REQ_ACTION_RESET_SESSION,
        "the second rejection resets: there is no third transport to offer");

  // The TIME window is deliberately NOT per cause: it bounds the whole outage whatever mix of
  // causes filled it, so cause ping-pong cannot retry forever.
  RequestRetryState w;
  w.clear();
  w.noteFailure(0, REQ_FAIL_TIMEOUT);
  w.noteFailure(30, REQ_FAIL_LOCAL_RESOURCE); // cause change resets the COUNT...
  checkEq(w.attemptsFor(REQ_FAIL_LOCAL_RESOURCE), 1, "...to counting the new cause");
  checkEq(w.attemptsFor(REQ_FAIL_TIMEOUT), 0, "the old cause's spent count is not consulted");
  check(w.decide(REQ_SETUP, REQ_FAIL_LOCAL_RESOURCE, 61, false) == REQ_ACTION_RESET_SESSION,
        "...but the 60s window still expired: elapsed time is cause-blind by design");
}

// ---------------------------------------------------------------------------------------------
// A lost connection under INTERLEAVED media rebuilds -- replaying on a new connection cannot help
// ---------------------------------------------------------------------------------------------
static void testConnectionLossUnderInterleavedMediaRebuildsInsteadOfDoomedReplay() {
  // MEASURED IN PRODUCTION, 6397272975e8c006c6fc, 2026-07-21 (fixture:
  // tetherbox spec/fixtures/live555/retry_reconnect_doom_loop_192.168.11.19_2026-07-21.log):
  //
  //   TX-RETRY ... SETUP failed (cause=connection-closed), retrying -- attempt 1 of 1 ... KEPT.
  //   TX-RESET ... reason=reconnect-replaced-connection state=DESCRIBED     <- SAME SECOND
  //
  // 2304 such resets in six hours. The replay is structurally DOOMED on interleaved media: the
  // re-send lazily opens a new connection, and connectToServer's reconnectRequiresFullReset rule
  // (correct!) then resets the session because subsessions were bound to the OLD connection. So the
  // "retry that keeps the session" costs an extra DESCRIBE cycle and keeps nothing -- every camera
  // closure pays a doubled rebuild.
  //
  // GStreamer's replay guard is `(try == 0) && !src->interleaved && src->udp_reconnect`
  // (gstrtspsrc.c:7723-7728, verified 2026-07-21): it NEVER replays when media rode the dead
  // connection. The policy must know the transport for exactly this decision.
  check(decideRequestFailureFor(REQ_SETUP, REQ_FAIL_CONNECTION_CLOSED, 0, 0,
                                /*mediaInterleaved*/ true) == REQ_ACTION_RESET_SESSION,
        "interleaved media + lost connection -> rebuild NOW (a replay cannot rebind the media)");
  check(decideRequestFailureFor(REQ_SETUP, REQ_FAIL_CONNECTION_CLOSED, 0, 0,
                                /*mediaInterleaved*/ false) == REQ_ACTION_RETRY_RECONNECT,
        "UDP media keeps GStreamer's reconnect-and-replay: the session genuinely survives it");
  check(decideRequestFailureFor(REQ_SETUP, REQ_FAIL_TIMEOUT, 0, 0, true) == REQ_ACTION_RETRY,
        "the transport rule is scoped to connection loss -- a timeout retries either way");
  check(connectionLossRequiresRebuild(REQ_FAIL_CONNECTION_CLOSED, true),
        "the predicate names the rule so the reason label and the decision cannot drift");
  check(!connectionLossRequiresRebuild(REQ_FAIL_CONNECTION_CLOSED, false),
        "UDP media does not require the rebuild");
  check(!connectionLossRequiresRebuild(REQ_FAIL_TIMEOUT, true),
        "only a connection loss triggers it");
}

// ---------------------------------------------------------------------------------------------
// 454 Session Not Found ends the session, whatever else the connection proves
// ---------------------------------------------------------------------------------------------
static void testSessionNotFoundEndsTheSession() {
  // An answered 454 proves the connection is alive AND the session is already gone on the far end.
  // The connection-is-alive rule ("an answer keeps the session") must not apply: keep-alives
  // against a disowned session id can never succeed, and a keep-the-session verdict here is a
  // zombie stream whose logs look healthy forever.
  check(rtspStatusEndsSession(454), "454 Session Not Found ends the session");
  check(!rtspStatusEndsSession(200), "a success does not");
  check(!rtspStatusEndsSession(500), "a server error proves the peer alive; the session may be fine");
  check(!rtspStatusEndsSession(551), "a method refusal demotes the method, not the session");
}

// ---------------------------------------------------------------------------------------------
// GStreamer parity: a rejected TRANSPORT must be replaced, not re-sent identically
// ---------------------------------------------------------------------------------------------
static void testRejectedTransportSwitchesInsteadOfRepeating() {
  // GStreamer keeps an ordered protocol mask (gstrtspsrc.c:8035-8040: UDP, UDP_MCAST, TCP) and on
  // GST_RTSP_STS_UNSUPPORTED_TRANSPORT does `mask++` then `goto retry` (:8744-8750) -- it asks for a
  // DIFFERENT transport.
  //
  // We classified 461 as REQ_FAIL_TRANSPORT_UNSUPPORTED with a 20-retry budget and then re-sent the
  // IDENTICAL rejected transport up to twenty times. The server already said no, and nothing about
  // the request changed between attempts, so all twenty were guaranteed to fail before the session
  // was rebuilt anyway. A retry that CANNOT succeed is worse than no retry: it spends the time
  // window that would otherwise have gone to a transport the camera actually accepts.
  check(nextTransportAfterRejection(REQ_TRANSPORT_UDP) == REQ_TRANSPORT_TCP,
        "a camera that rejects UDP is asked for TCP next");
  check(nextTransportAfterRejection(REQ_TRANSPORT_TCP) == REQ_TRANSPORT_UDP,
        "and vice versa -- we try the other one, never the same one again");

  // Two transports means at most one useful retry. Handing the full 20-retry ceiling to a cause
  // whose entire remedy is "ask differently" is how the window gets spent on repetition.
  checkEq(maxRetriesForCause(REQ_FAIL_TRANSPORT_UNSUPPORTED), 1,
          "one retry: there is exactly one other transport to try");

  check(requestFailureNeedsDifferentTransport(REQ_FAIL_TRANSPORT_UNSUPPORTED),
        "the transport rejection is marked as requiring a DIFFERENT transport");
  check(!requestFailureNeedsDifferentTransport(REQ_FAIL_TIMEOUT),
        "a timeout does not mean the transport was wrong -- re-send it unchanged");
  check(!requestFailureNeedsDifferentTransport(REQ_FAIL_LOCAL_RESOURCE),
        "a local port conflict is not a transport problem");
}

static void testNotFoundIsTreatedAsAPossibleAuthChallenge() {
  // gstrtspsrc.c:7828-7836 handles GST_RTSP_STS_NOT_FOUND in the SAME branch as
  // GST_RTSP_STS_UNAUTHORIZED: both call gst_rtspsrc_setup_auth and retry once. Not tidiness --
  // cameras that answer 404 to an UNAUTHENTICATED DESCRIBE/SETUP are common, and they never receive
  // credentials if 404 is treated as a flat refusal.
  //
  // We mapped only 401/407 to REQ_FAIL_AUTH, so 404 fell through to REQ_FAIL_ERROR_STATUS with a
  // budget of ZERO: credentials were never attached and such a camera stayed permanently unreachable.
  check(classifyRequestFailure(404) == REQ_FAIL_AUTH,
        "404 is retried once WITH credentials, as GStreamer does -- some cameras hide auth behind it");
  checkEq(maxRetriesForCause(REQ_FAIL_AUTH), 1,
          "still exactly once: repeating a rejected credential risks lockout and cannot succeed");

  check(classifyRequestFailure(400) == REQ_FAIL_ERROR_STATUS, "400 stays a real refusal");
  check(classifyRequestFailure(500) == REQ_FAIL_ERROR_STATUS, "500 stays a real refusal");
}

static void testRefusedOptionalMethodsDisableTheMethodInsteadOfFailing() {
  // gstrtspsrc.c:7917-7934: 406 / 501 / 405 / 551 strike the method from src->methods and turn the
  // failure into GST_RTSP_OK. The comment there names a real device -- a HikVision DS-2CD2732F-IS
  // answering "551 Option not supported" to PAUSE.
  //
  // We send PAUSE whenever neverPause is false (the stock default), so a camera refusing PAUSE fails
  // a request we did not need to make, on a session that is otherwise perfectly healthy.
  check(methodRefusalDisablesMethod(405), "405 Method Not Allowed disables the method");
  check(methodRefusalDisablesMethod(406), "406 Not Acceptable disables the method");
  check(methodRefusalDisablesMethod(501), "501 Not Implemented disables the method");
  check(methodRefusalDisablesMethod(551), "551 Option Not Supported disables it (the Hikvision case)");

  // NOT any error. A 500 is a server fault, not "I do not implement this verb", and disabling a
  // method on the strength of one would silently stop us using a verb the camera does support.
  check(!methodRefusalDisablesMethod(500), "500 is a server error, not an unsupported method");
  check(!methodRefusalDisablesMethod(404), "404 is an auth challenge here, not a method refusal");
  check(!methodRefusalDisablesMethod(200), "a success is not a refusal");
}

int main() {
  testRejectedTransportSwitchesInsteadOfRepeating();
  testAttemptCounterIsPerCause();
  testConnectionLossUnderInterleavedMediaRebuildsInsteadOfDoomedReplay();
  testSessionNotFoundEndsTheSession();
  testNotFoundIsTreatedAsAPossibleAuthChallenge();
  testRefusedOptionalMethodsDisableTheMethodInsteadOfFailing();
  testAddressInUseIsRetryableAndNotBlamedOnTheCamera();
  testTheOtherLocalResourceErrnosAreCoveredToo();
  testResultCodesClassifyIntoCauses();
  testAnAnsweredRequestIsNeverCalledAConnectionLoss();
  testRetryStateClearsOnSessionRebuild();
  testRetryStateBoundsByElapsedTimeNotJustCount();
  testRetryStateSurvivesAMonotonicClockThatDoesNotStartAtZero();
  testTheCapturedOutageReplayedEndToEnd();
  testEveryFailureConditionHasTheRightResponse();
  testAuthAndConnectionBudgetsAreDeliberatelyTiny();
  testAnIdleTimeoutKeepsTheSessionAlive();
  testEveryCauseIsNamedAndDecided();
  testWorstCaseIsBoundedByTimeNotCount();
  testTimeWindowOverridesTheCount();
  testRetryDoesNotDisturbTheSessionState();
  testResetPathStillReachesInitWhenBudgetIsSpent();
  testTimeoutsMatchGStreamer();
  testCountOnlyOverloadIsAViewOfTheFullDecision();
  testFailedRequestRetriesRatherThanResets();
  testPlayFollowsTheSameRule();
  testDescribeResetsImmediately();
  testRetryPreservesTheHandshake();
  testSuccessResetsTheBudget();
  testEveryActionIsReachable();
  testTransportsHaveGreppableNames();

  return checkSummary("test_request_retry");
}

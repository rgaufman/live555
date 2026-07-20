// Full RTSP session-lifecycle coverage for SessionLifecycle.hh.
//
// Scope: every state, every event, every transition — legal and illegal — plus the complete
// end-to-end sequences a proxy actually runs (cold start, multi-track setup, pause/resume,
// reconnect after a proven close, DESCRIBE backoff on an unreachable camera).
//
// The two production defects this model exists to catch are asserted directly, from the REAL
// observed sequences rather than invented ones:
//   - doubleResetIsRejected()      — two doReset calls back-to-back (observed: two `doReset` lines,
//                                    then a 99s gap before the stream came back).
//   - reconnectDoesNotDiscardDescribe() — a reset scheduled between a successful DESCRIBE and the
//                                    first SETUP, which made every reconnect run TWO complete
//                                    DESCRIBE/SETUP/PLAY cycles. Captured evidence:
//                                    spec/fixtures/diagnostics/live555_gapchk_reconnect_cycle.txt
//                                    (tetherbox repo) — CSeq 7 abandoned, CSeq 9-12 succeeding.
//
// Cross-checked against RFC 2326 Appendix A (client state machine: Init/Ready/Playing under
// SETUP/PLAY/PAUSE/TEARDOWN) and against GStreamer's rtspsrc, which makes the same describe-phase
// split and likewise retries a failed DESCRIBE with backoff rather than tearing the session down.
#include <stdio.h>

#include "SessionLifecycle.hh"

static int checks = 0, failures = 0;

static void ok(char const* what, bool cond) {
  ++checks;
  if (cond) {
    printf("  ok:   %s\n", what);
  } else {
    printf("  FAIL: %s\n", what);
    ++failures;
  }
}

// Apply an event and assert it was legal, returning the new state. Used to walk realistic
// sequences: any illegal step inside a sequence that production really runs is itself a failure.
static SessionState step(SessionState from, SessionEvent e, char const* what) {
  SessionTransition t = sessionTransition(from, e);
  ok(what, t.legal == True);
  return t.nextState;
}

static void expectLegal(SessionState from, SessionEvent e, SessionState to, char const* what) {
  SessionTransition t = sessionTransition(from, e);
  ok(what, t.legal == True && t.nextState == to && t.reason == NULL);
}

static void expectIllegal(SessionState from, SessionEvent e, char const* what) {
  SessionTransition t = sessionTransition(from, e);
  // An illegal transition must NOT move the state, and must carry a reason. A rejection with no
  // explanation is unactionable in a log, and one that also mutates state leaves the caller's
  // bookkeeping diverged in a second, quieter way.
  ok(what, t.legal == False && t.reason != NULL && t.nextState == from);
}

// ---------------------------------------------------------------------------
// The happy path: cold start through to media flowing.
// ---------------------------------------------------------------------------
static void coldStart() {
  printf("cold start (INIT -> PLAYING):\n");
  SessionState s = SL_INIT;
  s = step(s, SE_DESCRIBE_SENT, "INIT accepts DESCRIBE-sent");
  ok("  -> DESCRIBING", s == SL_DESCRIBING);
  s = step(s, SE_DESCRIBE_OK, "DESCRIBING accepts DESCRIBE-ok");
  ok("  -> DESCRIBED", s == SL_DESCRIBED);
  s = step(s, SE_SETUP_OK, "DESCRIBED accepts SETUP-ok");
  ok("  -> READY (RFC 2326 A.2 Ready)", s == SL_READY);
  s = step(s, SE_PLAY_OK, "READY accepts PLAY-ok");
  ok("  -> PLAYING (RFC 2326 A.2 Playing)", s == SL_PLAYING);
  ok("media is expected only while PLAYING", sessionExpectsMedia(s) == True);
}

// ---------------------------------------------------------------------------
// live555 sets tracks up ONE AT A TIME, so later SETUPs land in READY and — when a client is
// already attached — in PLAYING. Neither may knock the session out of playback.
// ---------------------------------------------------------------------------
static void multiTrackSetup() {
  printf("multi-track setup:\n");
  expectLegal(SL_READY, SE_SETUP_OK, SL_READY, "2nd track SETUP from READY stays READY");
  expectLegal(SL_PLAYING, SE_SETUP_OK, SL_PLAYING,
              "SETUP of a later track does NOT interrupt playback");
  expectLegal(SL_PLAYING, SE_PLAY_OK, SL_PLAYING, "re-PLAY while playing is idempotent");
}

// ---------------------------------------------------------------------------
// PAUSE/resume. RFC 2326 A.2: PAUSE is Playing -> Ready. -P (never-pause) means we mostly do not
// issue it, but a client can, and the model must not treat the resume as a fresh session.
// ---------------------------------------------------------------------------
static void pauseResume() {
  printf("pause/resume:\n");
  // SE_PAUSE_SENT, not _OK: the proxy's PAUSE carries no response handler, so the event marks the
  // SEND -- the moment we stop expecting media -- never a server acknowledgement.
  expectLegal(SL_PLAYING, SE_PAUSE_SENT, SL_READY, "PAUSE moves PLAYING -> READY");
  expectLegal(SL_READY, SE_PAUSE_SENT, SL_READY, "PAUSE from READY is a no-op, not an error");
  expectLegal(SL_READY, SE_PLAY_OK, SL_PLAYING, "resume: PLAY from READY returns to PLAYING");
  expectIllegal(SL_DESCRIBED, SE_PAUSE_SENT, "PAUSE before any SETUP is rejected");
  expectIllegal(SL_INIT, SE_PAUSE_SENT, "PAUSE from INIT is rejected");
}

// ---------------------------------------------------------------------------
// REGRESSION: the double reset. Observed as two `doReset` lines back-to-back followed by a 99s
// outage — a direct reset and an already-scheduled one both ran, the second tearing down the
// session the first had just rebuilt.
// ---------------------------------------------------------------------------
static void doubleResetIsRejected() {
  printf("regression: double reset:\n");
  SessionState s = step(SL_PLAYING, SE_RESET_STARTED, "first reset from PLAYING is legal");
  ok("  -> RESETTING", s == SL_RESETTING);
  expectIllegal(s, SE_RESET_STARTED, "SECOND reset while one is in progress is REJECTED");
  s = step(s, SE_RESET_COMPLETED, "reset completion returns to INIT");
  ok("  -> INIT", s == SL_INIT);
  expectIllegal(SL_INIT, SE_RESET_COMPLETED, "reset-completed with no reset in progress is rejected");
  expectIllegal(SL_PLAYING, SE_RESET_COMPLETED, "reset-completed while PLAYING is rejected");
}

// ---------------------------------------------------------------------------
// REGRESSION: the discarded DESCRIBE. A reset fired between a successful DESCRIBE and the first
// SETUP threw away a session that was already half-built, so every reconnect ran two full cycles.
// The model catches it twice over: the responses from the abandoned session are illegal while
// RESETTING, and a DESCRIBE sent on top of an existing described session is illegal outright.
// ---------------------------------------------------------------------------
static void reconnectDoesNotDiscardDescribe() {
  printf("regression: reconnect must not discard a good DESCRIBE:\n");
  expectIllegal(SL_DESCRIBED, SE_DESCRIBE_SENT,
                "re-DESCRIBE on an already-described session is REJECTED (would discard it)");
  expectIllegal(SL_READY, SE_DESCRIBE_SENT, "re-DESCRIBE from READY is rejected");
  expectIllegal(SL_PLAYING, SE_DESCRIBE_SENT, "re-DESCRIBE from PLAYING is rejected");

  // The abandoned session's responses arriving mid-reset — the exact CSeq-7 tail in the captured
  // trace. Each is reported, not silently applied to the NEW session's bookkeeping.
  expectIllegal(SL_RESETTING, SE_DESCRIBE_OK, "stale DESCRIBE-ok during a reset is reported");
  expectIllegal(SL_RESETTING, SE_SETUP_OK, "stale SETUP-ok during a reset is reported");
  expectIllegal(SL_RESETTING, SE_PLAY_OK, "stale PLAY-ok during a reset is reported");

  // And the legal shape of a reconnect: reset -> completed -> ONE new DESCRIBE cycle.
  SessionState s = SL_PLAYING;
  s = step(s, SE_CONNECTION_LOST, "connection loss is recorded");
  s = step(s, SE_RESET_STARTED, "which triggers a reset");
  ok("  -> RESETTING", s == SL_RESETTING);
  s = step(s, SE_RESET_COMPLETED, "reset completes");
  s = step(s, SE_DESCRIBE_SENT, "exactly ONE new DESCRIBE after the reset");
  s = step(s, SE_DESCRIBE_OK, "which succeeds");
  s = step(s, SE_SETUP_OK, "and is followed straight by SETUP -- no second DESCRIBE");
  s = step(s, SE_PLAY_OK, "and PLAY");
  ok("  reconnect ends PLAYING after ONE cycle", s == SL_PLAYING);
}

// ---------------------------------------------------------------------------
// DESCRIBE failure: retry with backoff, do NOT reset. A reset here would clear the 1->256s backoff
// ladder and hammer an unreachable camera at 1Hz forever. Measured on the live unit, the ladder does
// climb (1s, 2s, 4s, 8s observed across proxy logs), so the retry path must stay outside the reset.
// ---------------------------------------------------------------------------
static void describeFailureRetries() {
  printf("DESCRIBE failure retries without resetting:\n");
  SessionState s = step(SL_INIT, SE_DESCRIBE_SENT, "DESCRIBE sent");
  SessionTransition t = sessionTransition(s, SE_DESCRIBE_FAILED);
  ok("DESCRIBE failure is legal while DESCRIBING", t.legal == True);
  ok("  -> back to INIT (retry), NOT RESETTING (which would clear the backoff ladder)",
     t.nextState == SL_INIT);

  // Repeated failures walk the same one-state loop, so the backoff counter is never disturbed.
  s = t.nextState;
  for (int i = 0; i < 4; ++i) {
    s = step(s, SE_DESCRIBE_SENT, "retry DESCRIBE sent");
    SessionTransition f = sessionTransition(s, SE_DESCRIBE_FAILED);
    ok("retry failure stays on the INIT<->DESCRIBING retry loop",
       f.legal == True && f.nextState == SL_INIT);
    s = f.nextState;
  }

  expectIllegal(SL_INIT, SE_DESCRIBE_OK, "DESCRIBE-ok with nothing in flight is rejected");
  expectIllegal(SL_INIT, SE_DESCRIBE_FAILED, "DESCRIBE-failure with nothing in flight is rejected");
  expectIllegal(SL_DESCRIBED, SE_DESCRIBE_OK, "a second DESCRIBE-ok is rejected");
}

// ---------------------------------------------------------------------------
// Ordering violations: a response that cannot have been earned by anything we sent. RFC 2326 A.2
// makes PLAY legal only from Ready; a PLAY that "succeeds" with no SETUP done means our state and
// the server's have diverged.
// ---------------------------------------------------------------------------
static void orderingViolations() {
  printf("ordering violations:\n");
  expectIllegal(SL_INIT, SE_SETUP_OK, "SETUP-ok before any DESCRIBE is rejected");
  expectIllegal(SL_DESCRIBING, SE_SETUP_OK, "SETUP-ok while DESCRIBE is still in flight is rejected");
  expectIllegal(SL_INIT, SE_PLAY_OK, "PLAY-ok with no SETUP is rejected (RFC 2326 A.2)");
  expectIllegal(SL_DESCRIBED, SE_PLAY_OK, "PLAY-ok straight after DESCRIBE is rejected");
  expectIllegal(SL_INIT, SE_SETUP_FAILED, "SETUP failure before any DESCRIBE is rejected");
  expectIllegal(SL_DESCRIBED, SE_PLAY_FAILED, "PLAY failure with no SETUP is rejected");
}

// ---------------------------------------------------------------------------
// Failure paths that DO rebuild: a half-set-up or unplayable session is not usable.
// ---------------------------------------------------------------------------
static void failuresThatRebuild() {
  printf("failures are OBSERVATIONS; the rebuild is a separate action:\n");
  // A failure records what the PEER said. The rebuild is what WE decide to do about it, and it
  // arrives separately as SE_RESET_STARTED. Collapsing the two would move the state to RESETTING
  // here, so the reset that legitimately follows a moment later would be flagged as a double-reset
  // — the detector would fire on every ordinary reconnect and be worthless.
  expectLegal(SL_DESCRIBED, SE_SETUP_FAILED, SL_DESCRIBED, "SETUP failure does not itself reset");
  expectLegal(SL_READY, SE_SETUP_FAILED, SL_READY, "later-track SETUP failure does not itself reset");
  expectLegal(SL_READY, SE_PLAY_FAILED, SL_READY, "PLAY failure does not itself reset");
  expectLegal(SL_PLAYING, SE_PLAY_FAILED, SL_PLAYING, "PLAY failure while playing does not itself reset");

  // ...and the reset that follows each of them is legal, not a double-reset.
  expectLegal(SL_DESCRIBED, SE_RESET_STARTED, SL_RESETTING, "reset after a SETUP failure is legal");
  expectLegal(SL_PLAYING, SE_RESET_STARTED, SL_RESETTING, "reset after a PLAY failure is legal");
}

// The full trigger -> action sequence, asserted end to end. This is the shape production runs on
// every reconnect, and it must produce ZERO illegal transitions.
static void failureThenResetProducesNoFalseAlarm() {
  printf("failure -> scheduled reset -> rebuild raises no false alarm:\n");
  SessionState s = SL_PLAYING;
  s = step(s, SE_CONNECTION_LOST, "keep-alive failure recorded");
  ok("  connection loss alone does NOT move the state", s == SL_PLAYING);
  s = step(s, SE_RESET_STARTED, "the reset it triggers is legal, not a double-reset");
  ok("  -> RESETTING", s == SL_RESETTING);
  s = step(s, SE_RESET_COMPLETED, "reset completes");
  s = step(s, SE_DESCRIBE_SENT, "one DESCRIBE follows");
  s = step(s, SE_DESCRIBE_OK, "which succeeds");
  s = step(s, SE_SETUP_OK, "SETUP");
  s = step(s, SE_PLAY_OK, "PLAY");
  ok("  back to PLAYING with no illegal transition anywhere", s == SL_PLAYING);
}

// ---------------------------------------------------------------------------
// Connection loss is legal from every live state — the peer may vanish at any moment. That is the
// environment's prerogative, not a defect in us, and reporting it as an illegal transition would
// bury the real signal under noise on exactly the flaky links this proxy exists to survive.
// ---------------------------------------------------------------------------
static void connectionLossFromEveryLiveState() {
  printf("connection loss from every live state:\n");
  expectLegal(SL_INIT, SE_CONNECTION_LOST, SL_INIT, "loss from INIT");
  expectLegal(SL_DESCRIBING, SE_CONNECTION_LOST, SL_DESCRIBING, "loss from DESCRIBING");
  expectLegal(SL_DESCRIBED, SE_CONNECTION_LOST, SL_DESCRIBED, "loss from DESCRIBED");
  expectLegal(SL_READY, SE_CONNECTION_LOST, SL_READY, "loss from READY");
  expectLegal(SL_PLAYING, SE_CONNECTION_LOST, SL_PLAYING, "loss from PLAYING");
  expectLegal(SL_RESETTING, SE_CONNECTION_LOST, SL_RESETTING, "loss during a reset is not a second reset");
}

// ---------------------------------------------------------------------------
// Terminal state. A callback firing on a destroyed session is a use-after-free that would otherwise
// be reported as whatever it corrupted.
// ---------------------------------------------------------------------------
static void closedIsTerminal() {
  printf("CLOSED is terminal:\n");
  expectLegal(SL_PLAYING, SE_CLOSED, SL_CLOSED, "close from PLAYING");
  expectLegal(SL_INIT, SE_CLOSED, SL_CLOSED, "close from INIT");
  expectLegal(SL_RESETTING, SE_CLOSED, SL_CLOSED, "close during a reset");
  expectLegal(SL_CLOSED, SE_CLOSED, SL_CLOSED, "double close is idempotent, not an error");
  expectIllegal(SL_CLOSED, SE_DESCRIBE_OK, "DESCRIBE-ok after close is rejected");
  expectIllegal(SL_CLOSED, SE_PLAY_OK, "PLAY-ok after close is rejected");
  expectIllegal(SL_CLOSED, SE_RESET_STARTED, "reset after close is rejected");
  expectIllegal(SL_CLOSED, SE_CONNECTION_LOST, "connection-loss after close is rejected");
}

// ---------------------------------------------------------------------------
// "Media expected" gates the inter-packet gap check. Silence while not PLAYING is normal, and
// treating it as a fault is how a reset storm starts on a camera nobody is watching.
// ---------------------------------------------------------------------------
static void mediaExpectation() {
  printf("media expectation:\n");
  ok("PLAYING expects media", sessionExpectsMedia(SL_PLAYING) == True);
  ok("READY does not (set up, not started)", sessionExpectsMedia(SL_READY) == False);
  ok("DESCRIBED does not", sessionExpectsMedia(SL_DESCRIBED) == False);
  ok("RESETTING does not", sessionExpectsMedia(SL_RESETTING) == False);
  ok("INIT does not", sessionExpectsMedia(SL_INIT) == False);
  ok("CLOSED does not", sessionExpectsMedia(SL_CLOSED) == False);
}

// ---------------------------------------------------------------------------
// TOTALITY. Every (state, event) pair must produce a decided verdict — never a fall-through that is
// silently treated as legal, and never an illegal verdict with no reason. This is the check that
// makes adding a new state or event safe: forget to handle it and this fails immediately.
// ---------------------------------------------------------------------------
static void everyPairIsDecided() {
  printf("totality: every (state,event) pair is decided:\n");
  SessionState const states[] = {SL_INIT, SL_DESCRIBING, SL_DESCRIBED, SL_READY,
                                 SL_PLAYING, SL_RESETTING, SL_CLOSED};
  SessionEvent const events[] = {SE_DESCRIBE_SENT, SE_DESCRIBE_OK, SE_DESCRIBE_FAILED,
                                 SE_SETUP_OK, SE_SETUP_FAILED, SE_PLAY_OK, SE_PLAY_FAILED,
                                 SE_PAUSE_SENT, SE_RESET_STARTED, SE_RESET_COMPLETED,
                                 SE_CONNECTION_LOST, SE_CLOSED};
  int decided = 0, undecidedOrUnexplained = 0, unnamed = 0;
  for (unsigned i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
    for (unsigned j = 0; j < sizeof(events)/sizeof(events[0]); ++j) {
      SessionTransition t = sessionTransition(states[i], events[j]);
      if (t.legal == True) {
        if (t.reason != NULL) ++undecidedOrUnexplained; // a legal transition must carry no reason
      } else {
        // Illegal must explain itself AND leave the state alone.
        if (t.reason == NULL || t.nextState != states[i]) ++undecidedOrUnexplained;
      }
      // "unhandled event/state combination" is the fall-through sentinel: reaching it means a pair
      // escaped every branch, which is the bug this whole test guards against.
      if (t.reason != NULL &&
          t.reason[0] == 'u' && t.reason[1] == 'n' && t.reason[2] == 'h') ++undecidedOrUnexplained;
      ++decided;
    }
  }
  for (unsigned i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
    char const* n = sessionStateName(states[i]);
    if (n[0] == 'U') ++unnamed; // "UNKNOWN-STATE"
  }
  for (unsigned j = 0; j < sizeof(events)/sizeof(events[0]); ++j) {
    char const* n = sessionEventName(events[j]);
    if (n[0] == 'U') ++unnamed; // "UNKNOWN-EVENT"
  }
  printf("  (%d pairs evaluated)\n", decided);
  ok("all 84 (state,event) pairs evaluated", decided == 84);
  ok("no pair falls through, and every rejection explains itself and preserves state",
     undecidedOrUnexplained == 0);
  ok("every state and event has a name (a log must never print an enum value)", unnamed == 0);
}

int main() {
  printf("== session lifecycle (SessionLifecycle.hh) ==\n");
  coldStart();
  multiTrackSetup();
  pauseResume();
  doubleResetIsRejected();
  reconnectDoesNotDiscardDescribe();
  describeFailureRetries();
  orderingViolations();
  failuresThatRebuild();
  failureThenResetProducesNoFalseAlarm();
  connectionLossFromEveryLiveState();
  closedIsTerminal();
  mediaExpectation();
  everyPairIsDecided();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

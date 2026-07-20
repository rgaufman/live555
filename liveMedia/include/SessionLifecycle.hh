// RTSP session lifecycle: the legal states, the events that move between them, and — the point of
// this file — an explicit verdict on every transition that is NOT legal.
//
// Why this exists
// ---------------
// The proxy's session handling was spread across a dozen callbacks that each mutated a few flags
// (fDoneDESCRIBE, fNumSetupsSent, fLastCommandWasPLAY, fHavePlayedBefore). No single place knew
// what state the session was in, so nothing could notice a sequence that made no sense. Two real
// defects hid in exactly that gap:
//   - every reconnect ran TWO complete DESCRIBE/SETUP/PLAY cycles, because a reset was scheduled
//     between a successful DESCRIBE and the first SETUP and nobody could see the DESCRIBE was being
//     discarded (evidence: a captured GAPCHK trace showing CSeq 7 abandoned, CSeq 9-12 succeeding);
//   - a reset ran twice back-to-back, because a direct reset and a scheduled one both fired.
// Both are illegal transitions in the model below, and both would have announced themselves the
// first time they happened instead of being found weeks later in a log.
//
// Relationship to the RFC
// -----------------------
// RFC 2326 Appendix A defines the client state machine over three states — Init, Ready, Playing —
// driven by SETUP / PLAY / PAUSE / TEARDOWN. DESCRIBE is deliberately stateless there ("does not
// change the state"). That is correct for a client that already has an SDP. A PROXY does not: it
// must hold a described session before it can SETUP, and its whole recovery story is "go back and
// DESCRIBE again". So the model below is the RFC machine with the describe phase made explicit
// (INIT -> DESCRIBING -> DESCRIBED == the RFC's Init), plus a RESETTING state for teardown-and-
// rebuild. The RFC states map as:
//     RFC Init    == SL_INIT / SL_DESCRIBING / SL_DESCRIBED
//     RFC Ready   == SL_READY  (>=1 SETUP done, or PAUSEd after playing)
//     RFC Playing == SL_PLAYING
// GStreamer's rtspsrc makes the same split for the same reason (its describe phase is separate from
// its Ready/Playing states, and a failed DESCRIBE retries rather than erroring the pipeline).
//
// This header is pure: no live555 types, no allocation, no I/O — so it is unit-testable standalone
// (test/test_session_lifecycle.cpp) and the same decision runs in the test and in production.

#ifndef _SESSION_LIFECYCLE_HH
#define _SESSION_LIFECYCLE_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif
// NULL is used below. Relying on the INCLUDER to have pulled it in first works only by luck of
// include order: this header compiled everywhere until a test included it before <stdio.h> and it
// failed with "use of undeclared identifier 'NULL'". A header must stand on its own. (Same family as
// the StreamFaultCodes.hh break, where `== NULL` compiled on macOS via a transitive <cstddef> that
// GCC on the target box does not provide.)
//
// OUTSIDE the _BOOLEAN_HH guard above, deliberately: it briefly sat inside that block, so any TU
// that had already included Boolean.hh skipped <stddef.h> entirely -- the exact one-guard-per-
// include defect this tree's own wiring test polices, reintroduced by the file narrating it.
// <stddef.h> has its own internal guard; it needs no wrapper here.
#include <stddef.h>

enum SessionState {
  SL_INIT,        // nothing sent yet, or fully reset
  SL_DESCRIBING,  // DESCRIBE in flight
  SL_DESCRIBED,   // have an SDP; no SETUP done yet (RFC 2326 "Init")
  SL_READY,       // >=1 SETUP done, or PAUSEd after playing (RFC 2326 "Ready")
  SL_PLAYING,     // PLAY confirmed (RFC 2326 "Playing")
  SL_RESETTING,   // teardown-and-rebuild in progress; only a reset-completion may leave this state
  SL_CLOSED       // session object is being destroyed; terminal
};

enum SessionEvent {
  SE_DESCRIBE_SENT,
  SE_DESCRIBE_OK,
  SE_DESCRIBE_FAILED,
  SE_SETUP_OK,
  SE_SETUP_FAILED,
  SE_PLAY_OK,
  SE_PLAY_FAILED,
  // "SENT", not "OK": the proxy's PAUSE is fire-and-forget (no response handler), so no
  // acknowledgement ever exists to observe. The event records what we have COMMITTED to -- we
  // stopped expecting media -- and an event named "_OK" here would be a name promising an ACK the
  // code cannot produce (hard-rules.md: name a thing for what it MEASURES).
  SE_PAUSE_SENT,
  SE_RESET_STARTED,
  SE_RESET_COMPLETED,
  SE_CONNECTION_LOST,
  SE_CLOSED
};

struct SessionTransition {
  SessionState nextState;
  // False when the event cannot legally occur in the current state. The caller must LOG this (with
  // a stable fault code, outside debug mode) rather than swallow it: an illegal transition means our
  // own bookkeeping has diverged from what the session is actually doing, which is precisely the
  // class of defect that stays invisible until a customer reports dropped video.
  Boolean legal;
  // Static, greppable explanation of why it is illegal. NULL when legal. Never a formatted string:
  // this header allocates nothing.
  char const* reason;
};

inline char const* sessionStateName(SessionState s) {
  switch (s) {
    case SL_INIT:       return "INIT";
    case SL_DESCRIBING: return "DESCRIBING";
    case SL_DESCRIBED:  return "DESCRIBED";
    case SL_READY:      return "READY";
    case SL_PLAYING:    return "PLAYING";
    case SL_RESETTING:  return "RESETTING";
    case SL_CLOSED:     return "CLOSED";
  }
  return "UNKNOWN-STATE"; // never silently defaults into a real state name
}

inline char const* sessionEventName(SessionEvent e) {
  switch (e) {
    case SE_DESCRIBE_SENT:   return "DESCRIBE-sent";
    case SE_DESCRIBE_OK:     return "DESCRIBE-ok";
    case SE_DESCRIBE_FAILED: return "DESCRIBE-failed";
    case SE_SETUP_OK:        return "SETUP-ok";
    case SE_SETUP_FAILED:    return "SETUP-failed";
    case SE_PLAY_OK:         return "PLAY-ok";
    case SE_PLAY_FAILED:     return "PLAY-failed";
    case SE_PAUSE_SENT:      return "PAUSE-sent";
    case SE_RESET_STARTED:   return "reset-started";
    case SE_RESET_COMPLETED: return "reset-completed";
    case SE_CONNECTION_LOST: return "connection-lost";
    case SE_CLOSED:          return "closed";
  }
  return "UNKNOWN-EVENT";
}

inline SessionTransition sessionTransition(SessionState state, SessionEvent event) {
  SessionTransition t;
  t.nextState = state;
  t.legal = True;
  t.reason = NULL;

  // Terminal first: once closed, nothing else may happen. Anything arriving here is a callback
  // firing on a session that was already destroyed — a use-after-free waiting to be reported as
  // something else entirely.
  if (state == SL_CLOSED) {
    if (event == SE_CLOSED) return t; // idempotent double-close is tolerated, not an error
    t.legal = False;
    t.reason = "event arrived on an already-CLOSED session";
    return t;
  }

  if (event == SE_CLOSED) { t.nextState = SL_CLOSED; return t; }

  // A lost connection is legal from any live state: the peer may vanish at any moment, and that is
  // the environment's prerogative, not a bug in us.
  //
  // It deliberately does NOT move to RESETTING. Losing the connection is an OBSERVATION; the
  // rebuild is a separate ACTION (SE_RESET_STARTED), and conflating the two would make the reset
  // that legitimately follows look like a second reset -- turning the double-reset detector into a
  // generator of false alarms on every ordinary reconnect. Same reasoning for the *_FAILED events
  // below: they record what the peer said, not what we decided to do about it.
  if (event == SE_CONNECTION_LOST) return t;

  // A reset may be started from any live state EXCEPT one already resetting. That exception is the
  // whole point: a second reset while one is in flight is the double-doReset defect, and it tore
  // down a session that had just been rebuilt.
  if (event == SE_RESET_STARTED) {
    if (state == SL_RESETTING) {
      t.legal = False;
      t.reason = "reset requested while a reset is already in progress (double-reset)";
      return t;
    }
    t.nextState = SL_RESETTING;
    return t;
  }

  if (state == SL_RESETTING) {
    if (event == SE_RESET_COMPLETED) { t.nextState = SL_INIT; return t; }
    // Anything else is a callback from the session we just abandoned. Reporting it is how the
    // "two full DESCRIBE/SETUP/PLAY cycles per reconnect" defect becomes visible at the moment it
    // happens rather than in a hand-read trace weeks later.
    t.legal = False;
    t.reason = "response from the abandoned session arrived during a reset";
    return t;
  }

  if (event == SE_RESET_COMPLETED) {
    t.legal = False;
    t.reason = "reset completed while no reset was in progress";
    return t;
  }

  switch (event) {
    case SE_DESCRIBE_SENT:
      // Re-DESCRIBING from DESCRIBED/READY/PLAYING would silently discard a session we already
      // built — the exact shape of the double-cycle defect.
      if (state != SL_INIT && state != SL_DESCRIBING) {
        t.legal = False;
        t.reason = "DESCRIBE sent while a described session already exists (would discard it)";
        return t;
      }
      t.nextState = SL_DESCRIBING;
      return t;

    case SE_DESCRIBE_OK:
      if (state != SL_DESCRIBING) {
        t.legal = False;
        t.reason = "DESCRIBE response with no DESCRIBE in flight";
        return t;
      }
      t.nextState = SL_DESCRIBED;
      return t;

    case SE_DESCRIBE_FAILED:
      if (state != SL_DESCRIBING) {
        t.legal = False;
        t.reason = "DESCRIBE failure with no DESCRIBE in flight";
        return t;
      }
      // Stay in INIT and retry with backoff (scheduleDESCRIBECommand). Deliberately NOT a reset:
      // a reset would clear the backoff ladder and hammer an unreachable camera at 1Hz forever.
      t.nextState = SL_INIT;
      return t;

    case SE_SETUP_OK:
      // Legal from DESCRIBED (first track) and from READY/PLAYING (subsequent tracks of a
      // multi-track session, which live555 sets up one at a time).
      if (state != SL_DESCRIBED && state != SL_READY && state != SL_PLAYING) {
        t.legal = False;
        t.reason = "SETUP succeeded before any DESCRIBE";
        return t;
      }
      // A SETUP on an already-playing session does not stop playback.
      t.nextState = (state == SL_PLAYING) ? SL_PLAYING : SL_READY;
      return t;

    case SE_SETUP_FAILED:
      if (state != SL_DESCRIBED && state != SL_READY && state != SL_PLAYING) {
        t.legal = False;
        t.reason = "SETUP failure before any DESCRIBE";
        return t;
      }
      // Observation only -- the caller schedules the rebuild, which arrives as SE_RESET_STARTED.
      return t;

    case SE_PLAY_OK:
      if (state != SL_READY && state != SL_PLAYING) {
        t.legal = False;
        t.reason = "PLAY succeeded with no SETUP done (RFC 2326 A.2: PLAY is only legal from Ready)";
        return t;
      }
      t.nextState = SL_PLAYING;
      return t;

    case SE_PLAY_FAILED:
      if (state != SL_READY && state != SL_PLAYING) {
        t.legal = False;
        t.reason = "PLAY failure with no SETUP done";
        return t;
      }
      // Observation only -- see SE_SETUP_FAILED.
      return t;

    case SE_PAUSE_SENT:
      // RFC 2326 A.2: PAUSE moves Playing -> Ready. From Ready it is a no-op, not an error.
      // The event marks the SEND (the PAUSE has no response handler), so this models the moment we
      // stop expecting media, not the server's agreement -- which is exactly what the gap watchdog
      // needs to know.
      if (state != SL_PLAYING && state != SL_READY) {
        t.legal = False;
        t.reason = "PAUSE sent for a session that was never set up";
        return t;
      }
      t.nextState = SL_READY;
      return t;

    // Handled above; listed so a new enum value cannot fall through a default and be silently
    // treated as legal.
    case SE_RESET_STARTED:
    case SE_RESET_COMPLETED:
    case SE_CONNECTION_LOST:
    case SE_CLOSED:
      break;
  }

  t.legal = False;
  t.reason = "unhandled event/state combination";
  return t;
}

// Is this state one in which media should be flowing? Used to decide whether a silence gap is
// meaningful: no packets while not PLAYING is expected, not a fault.
inline Boolean sessionExpectsMedia(SessionState state) {
  return state == SL_PLAYING ? True : False;
}

#endif

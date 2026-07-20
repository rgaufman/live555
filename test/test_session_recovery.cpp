// Regression tests for the back-end session recovery policy (SessionRecoveryPolicy.hh).
//
// These encode the behaviour we WANT, and were written before the policy was wired in, against the
// two defects measured across a ~50-camera fleet over 30 minutes:
//
//   1. 90 of 236 proven far-end closes (38%) were followed ~18.25s later by an inactivity fault on
//      the session the close had already killed — one failure, two log lines, and the second line
//      contradicted the first ("nobody is proven at fault" about a close we had PROVEN).
//   2. Median 26.8s (max 235.1s, n=97) from a proven close to the stream being live again, because
//      recovery waited out the full inter-packet-gap interval even though the far end was gone.
//
// The invariant under test: PROOF SHORT-CIRCUITS THE TIMER. A timer infers a fault it cannot
// observe; once the fault is observed, the inference is redundant and must not be reported as an
// independent finding.
#include <stdio.h>
#include <string.h>

#include "SessionRecoveryPolicy.hh"

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

static SessionRecoveryInput mk(Boolean proven, Boolean everData, long since, unsigned limit) {
  SessionRecoveryInput in;
  in.provenCloseSeen = proven;
  in.everReceivedData = everData;
  in.secsSinceProgress = since;
  in.gapLimitSecs = limit;
  // Defaults keep every pre-existing check meaning what it meant: TCP media never takes the
  // UDP-black-hole branch, whatever the other fields say.
  in.mediaOverTCP = True;
  in.udpNoDataLimitSecs = 5;
  return in;
}

// A UDP-media session: same observations, transport flag flipped.
static SessionRecoveryInput mkUdp(Boolean proven, Boolean everData, long since, unsigned limit,
                                  unsigned udpLimit) {
  SessionRecoveryInput in = mk(proven, everData, since, limit);
  in.mediaOverTCP = False;
  in.udpNoDataLimitSecs = udpLimit;
  return in;
}

int main() {
  printf("== SessionRecoveryPolicy regression tests ==\n\n");

  printf("[DEFECT 1] a PROVEN close must not also be reported as an inactivity fault\n");
  {
    // The exact production shape: TX-RST at 03:56:42, TX-IDLE at 03:57:00 on the same dead session.
    SessionRecoveryAction a = sessionRecoveryDecide(mk(True, True, 18, 15));
    ok("proven close past the gap limit resets as a PROVEN CLOSE, not as idle",
       a == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
    ok("and emits NO idle fault line (the contradictory second report)",
       !sessionRecoveryReportsIdle(a));
    ok("but it DOES re-establish the session", sessionRecoveryResets(a));
  }

  printf("[NO-CONSUMER] with nobody consuming, progress is unmeasurable -- no verdict\n");
  {
    // The packet total only advances when a downstream consumer pulls frames, so with zero
    // consumers the timer inputs measure nothing (ea9f 2026-07-21: healthy NVR streams reset at
    // exactly the 15s mark, 894/30min). The rule lives HERE, not as call-site code: no consumer,
    // no timer verdict. mk() leaves consumerAttached at its default (True) so every pre-existing
    // check keeps its meaning.
    SessionRecoveryInput in = mk(False, True, 900, 15); // 60x past the limit, stream healthy
    in.consumerAttached = False;
    ok("a consumer-less session is never reset on the timer, however stale the clock looks",
       sessionRecoveryDecide(in) == SESSION_RECOVERY_WAIT);

    SessionRecoveryInput proven = mk(True, True, 1, 15);
    proven.consumerAttached = False;
    ok("but PROOF still beats everything: a captured FIN/RST resets even with no consumer",
       sessionRecoveryDecide(proven) == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
  }

  printf("[DEFECT 2] a PROVEN close must not wait out the gap interval\n");
  {
    // 1 second in, far below the 15s limit: with the far end provably gone there is nothing to wait
    // for, and every second waited is recording gap.
    SessionRecoveryAction a = sessionRecoveryDecide(mk(True, True, 1, 15));
    ok("proven close resets immediately, long before the gap limit",
       a == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
    ok("resetting at 0s elapsed is still immediate",
       sessionRecoveryDecide(mk(True, True, 0, 15)) == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
  }

  printf("[DEFECT 2] proof does not depend on the timer being enabled or on prior data\n");
  {
    ok("a proven close resets even when gap checking is DISABLED (-D 0)",
       sessionRecoveryDecide(mk(True, True, 0, 0)) == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
    // A peer that accepts the connection then resets before sending a byte has still provably
    // closed; waiting on it accomplishes nothing.
    ok("a proven close resets even if the session never delivered data",
       sessionRecoveryDecide(mk(True, False, 0, 15)) == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
  }

  printf("[GENUINE IDLE] the timer keeps its real job: a silent-but-open peer\n");
  {
    // This is the ONLY detector for a peer that stops sending while holding the socket open: such a
    // socket is never readable, so no read-path guard can ever see it.
    SessionRecoveryAction a = sessionRecoveryDecide(mk(False, True, 15, 15));
    ok("no proof + gap reached -> reset as IDLE", a == SESSION_RECOVERY_RESET_IDLE);
    ok("and this one DOES emit the idle fault", sessionRecoveryReportsIdle(a));
    ok("exactly at the limit counts as reached (boundary the -D value names)",
       sessionRecoveryDecide(mk(False, True, 15, 15)) == SESSION_RECOVERY_RESET_IDLE);
    ok("well past the limit still reports idle",
       sessionRecoveryDecide(mk(False, True, 900, 15)) == SESSION_RECOVERY_RESET_IDLE);
  }

  printf("[NO FALSE RESET] a healthy or still-connecting stream is left alone\n");
  {
    ok("inside the gap window -> wait",
       sessionRecoveryDecide(mk(False, True, 14, 15)) == SESSION_RECOVERY_WAIT);
    ok("no progress yet but never received data -> still connecting, do NOT reset",
       sessionRecoveryDecide(mk(False, False, 3600, 15)) == SESSION_RECOVERY_WAIT);
    ok("gap checking disabled -> never reset on time alone",
       sessionRecoveryDecide(mk(False, True, 86400, 0)) == SESSION_RECOVERY_WAIT);
    ok("waiting never emits a fault line",
       !sessionRecoveryReportsIdle(SESSION_RECOVERY_WAIT));
    ok("waiting never re-establishes", !sessionRecoveryResets(SESSION_RECOVERY_WAIT));
  }

  printf("[UDP BLACK-HOLE] UDP media that NEVER arrives rebuilds as TCP (GStreamer parity)\n");
  {
    // GStreamer: udpsrc timeout (DEFAULT_TIMEOUT 5s) -> gst_rtspsrc_reconnect with
    // `cur_protocols = TCP` and the warning "maybe your firewall is blocking it. Retrying using a
    // tcp connection." -- fired ONLY when pads never activated, i.e. zero media EVER arrived
    // (gstrtspsrc.c:5310-5311, :7032-7118, latch at :10693-10700; verified 2026-07-21).
    //
    // Our previous behaviour was strictly worse than either resetting or waiting: the
    // "never received data -> still connecting -> WAIT" rule, correct for TCP where SETUP/PLAY
    // succeeding proves the path works, black-holed FOREVER on a UDP session whose handshake
    // succeeds over TCP while the media path is firewalled.
    SessionRecoveryAction a = sessionRecoveryDecide(mkUdp(False, False, 5, 15, 5));
    ok("UDP + never any data + limit reached -> reset AND retry as TCP",
       a == SESSION_RECOVERY_RESET_RETRY_TCP);
    ok("that action re-establishes the session", sessionRecoveryResets(a));
    ok("and switches the transport", sessionRecoverySwitchesToTCP(a));
    ok("but does NOT emit the idle fault (silence was never 'went silent' -- nothing ever arrived)",
       !sessionRecoveryReportsIdle(a));
    ok("exactly at the UDP limit counts as reached (>=, the boundary the limit names)",
       sessionRecoveryDecide(mkUdp(False, False, 5, 15, 5)) == SESSION_RECOVERY_RESET_RETRY_TCP);
    ok("below the UDP limit -> still waiting",
       sessionRecoveryDecide(mkUdp(False, False, 4, 15, 5)) == SESSION_RECOVERY_WAIT);
    ok("TCP media with the same observations keeps the still-connecting WAIT",
       sessionRecoveryDecide(mk(False, False, 3600, 15)) == SESSION_RECOVERY_WAIT);
    ok("once data HAS arrived, UDP silence follows the normal idle rules, never the TCP switch",
       sessionRecoveryDecide(mkUdp(False, True, 900, 15, 5)) == SESSION_RECOVERY_RESET_IDLE);
    ok("a proven close still short-circuits everything",
       sessionRecoveryDecide(mkUdp(True, False, 5, 15, 5)) == SESSION_RECOVERY_RESET_PROVEN_CLOSE);
    ok("gap checking disabled -> the mechanism that runs this check is off, so WAIT",
       sessionRecoveryDecide(mkUdp(False, False, 3600, 0, 5)) == SESSION_RECOVERY_WAIT);
    ok("udpNoDataLimitSecs 0 disables the TCP fallback explicitly",
       sessionRecoveryDecide(mkUdp(False, False, 3600, 15, 0)) == SESSION_RECOVERY_WAIT);
  }

  printf("[REACHABILITY] every declared action must be attainable\n");
  {
    // A verdict that cannot occur is a guard that silently does nothing — the same class of defect
    // as the eviction threshold that was arithmetically unreachable.
    bool sawWait = false, sawProven = false, sawIdle = false, sawRetryTcp = false;
    for (int proven = 0; proven < 2; ++proven) {
      for (int data = 0; data < 2; ++data) {
        for (int tcp = 0; tcp < 2; ++tcp) {
          for (long secs = 0; secs <= 20; secs += 5) {
            SessionRecoveryInput in = mk(proven ? True : False, data ? True : False, secs, 15);
            in.mediaOverTCP = tcp ? True : False;
            SessionRecoveryAction a = sessionRecoveryDecide(in);
            if (a == SESSION_RECOVERY_WAIT) sawWait = true;
            if (a == SESSION_RECOVERY_RESET_PROVEN_CLOSE) sawProven = true;
            if (a == SESSION_RECOVERY_RESET_IDLE) sawIdle = true;
            if (a == SESSION_RECOVERY_RESET_RETRY_TCP) sawRetryTcp = true;
          }
        }
      }
    }
    ok("WAIT is reachable", sawWait);
    ok("RESET_PROVEN_CLOSE is reachable", sawProven);
    ok("RESET_IDLE is reachable", sawIdle);
    ok("RESET_RETRY_TCP is reachable", sawRetryTcp);
  }

  printf("[EXCLUSIVITY] the reset reasons must never double-report\n");
  {
    // Collapsing the reasons into one log line is exactly what produced the double-report.
    ok("at most one action reports idle",
       (sessionRecoveryReportsIdle(SESSION_RECOVERY_RESET_IDLE) ? 1 : 0) +
       (sessionRecoveryReportsIdle(SESSION_RECOVERY_RESET_PROVEN_CLOSE) ? 1 : 0) +
       (sessionRecoveryReportsIdle(SESSION_RECOVERY_RESET_RETRY_TCP) ? 1 : 0) +
       (sessionRecoveryReportsIdle(SESSION_RECOVERY_WAIT) ? 1 : 0) == 1);
    ok("every reset-flavoured action re-establishes the session",
       sessionRecoveryResets(SESSION_RECOVERY_RESET_IDLE) &&
       sessionRecoveryResets(SESSION_RECOVERY_RESET_PROVEN_CLOSE) &&
       sessionRecoveryResets(SESSION_RECOVERY_RESET_RETRY_TCP));
    ok("exactly one action switches the transport",
       (sessionRecoverySwitchesToTCP(SESSION_RECOVERY_RESET_RETRY_TCP) ? 1 : 0) +
       (sessionRecoverySwitchesToTCP(SESSION_RECOVERY_RESET_IDLE) ? 1 : 0) +
       (sessionRecoverySwitchesToTCP(SESSION_RECOVERY_RESET_PROVEN_CLOSE) ? 1 : 0) +
       (sessionRecoverySwitchesToTCP(SESSION_RECOVERY_WAIT) ? 1 : 0) == 1);
  }

  printf("[REASONS] every reset action carries its OWN reason into the TX-RESET tally\n");
  {
    // A call-site ternary briefly mapped every non-TCP-switching reset to inter-packet-gap, so a
    // PROVEN-close reset was tallied as an idle timeout -- re-merging in the reason field exactly
    // the two populations this policy exists to keep apart. The mapping is policy, so it lives
    // here, total over the actions, and each reset action gets a DISTINCT name.
    char const* const provenR = sessionRecoveryResetReason(SESSION_RECOVERY_RESET_PROVEN_CLOSE);
    char const* const idleR   = sessionRecoveryResetReason(SESSION_RECOVERY_RESET_IDLE);
    char const* const tcpR    = sessionRecoveryResetReason(SESSION_RECOVERY_RESET_RETRY_TCP);
    ok("a proven close resets under its own name",
       provenR != NULL && strcmp(provenR, "far-end-close-proven") == 0);
    ok("an idle timeout resets under the inter-packet-gap name",
       idleR != NULL && strcmp(idleR, "inter-packet-gap") == 0);
    ok("the UDP black-hole resets under its own name",
       tcpR != NULL && strcmp(tcpR, "udp-no-initial-data") == 0);
    ok("the three reset reasons are pairwise distinct",
       strcmp(provenR, idleR) != 0 && strcmp(provenR, tcpR) != 0 && strcmp(idleR, tcpR) != 0);
    ok("WAIT resets nothing and carries no reason",
       sessionRecoveryResetReason(SESSION_RECOVERY_WAIT) == NULL);
  }

  printf("[RECONNECT] replacing the RTSP connection must not discard a fresh DESCRIBE\n");
  {
    // MEASURED in spec/fixtures/diagnostics/live555_gapchk_reconnect_cycle.txt: every reconnect ran
    // TWO full DESCRIBE/SETUP/PLAY cycles. DESCRIBE (CSeq 7) succeeded, subsessions were built, then
    // createNewStreamSource opened a new socket and this check discarded the lot; CSeq 9..12 then
    // repeated the whole handshake. That redundant cycle IS the dead air.
    ok("after DESCRIBE but before any SETUP, a new connection does NOT force a reset",
       !reconnectRequiresFullReset(True, True, True, 0));
    ok("once a SETUP has completed, a new connection DOES force a reset (subsessions are bound)",
       reconnectRequiresFullReset(True, True, True, 1));
    ok("many SETUPs done -> still forces a reset",
       reconnectRequiresFullReset(True, True, True, 2));
  }

  printf("[RECONNECT] the other preconditions are preserved exactly\n");
  {
    ok("a FAILED connect never triggers this reset (the caller's error path owns it)",
       !reconnectRequiresFullReset(False, True, True, 5));
    ok("UDP media is not carried on the RTSP connection, so no reset",
       !reconnectRequiresFullReset(True, True, False, 5));
    ok("no DESCRIBE yet -> nothing to invalidate",
       !reconnectRequiresFullReset(True, False, True, 5));
  }

  printf("[RECONNECT] reachability: the reset must still be attainable\n");
  {
    // Narrowing a guard risks making it dead. It must still fire for the case it exists to catch.
    bool sawReset = false, sawNoReset = false;
    for (int setups = 0; setups < 3; ++setups) {
      if (reconnectRequiresFullReset(True, True, True, (unsigned)setups)) sawReset = true;
      else sawNoReset = true;
    }
    ok("the reset still fires for a mid-stream reconnect", sawReset);
    ok("and is correctly skipped pre-SETUP", sawNoReset);
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

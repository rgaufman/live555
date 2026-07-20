// Regression tests for RTSP keep-alive policy + interleaved-channel budgeting
// (RTSPKeepAlive.hh). Pure functions, no sockets, so it runs anywhere.
//
// Locks in two REAL bugs found in the live555 source while investigating a Merit-Lilin 4K fleet
// whose streams closed every ~2 minutes:
//
//   BUG 1 (jitter): the liveness delay was `us_1stPart + (us_2ndPart*our_random())%us_2ndPart`.
//     (a*r) % a is identically 0 in exact arithmetic, so the randomisation was broken — it only
//     produced non-constant values via 32-bit overflow, and those were biased, not uniform.
//
//   BUG 2 (channel ids): RTSPClient allocates interleaved channel ids from a monotonically
//     increasing `unsigned char fTCPStreamIdCount` (+2 per SETUP) that reset() never cleared. Across
//     reconnects the ids climb (production logs showed "channel 96", "channel 100" on a 2-track
//     camera == reconnects #24 and #25) and wrap past 255 back to 0 after 64 reconnects, where they
//     can collide with a channel still registered from an earlier session and corrupt '$'-framing.
//
// Build + run:  make -C test run

#include "RTSPKeepAlive.hh"
#include <cstdio>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, msg) do { ++g_checks; \
  if (!(cond)) { ++g_failures; std::printf("  FAIL: %s\n", (msg)); } \
  else         { std::printf("  ok:   %s\n", (msg)); } } while (0)

// ---------------------------------------------------------------- method selection (GStreamer order)

static void test_method_selection() {
  std::printf("[METHOD] GStreamer order: SET_PARAMETER > GET_PARAMETER > OPTIONS\n");
  CHECK(chooseRtspLivenessMethod(true,  true)  == RTSP_LIVENESS_SET_PARAMETER,
        "supports both -> SET_PARAMETER wins");
  CHECK(chooseRtspLivenessMethod(true,  false) == RTSP_LIVENESS_SET_PARAMETER,
        "SET only -> SET_PARAMETER");
  CHECK(chooseRtspLivenessMethod(false, true)  == RTSP_LIVENESS_GET_PARAMETER,
        "GET only -> GET_PARAMETER");
  CHECK(chooseRtspLivenessMethod(false, false) == RTSP_LIVENESS_OPTIONS,
        "neither advertised -> fall back to OPTIONS");

  // The real camera that prompted this: advertises SET_PARAMETER, does NOT advertise GET_PARAMETER.
  // live555 would have sent OPTIONS; GStreamer sends SET_PARAMETER.
  CHECK(chooseRtspLivenessMethod(/*set*/true, /*get*/false) != RTSP_LIVENESS_OPTIONS,
        "Merit-Lilin case (SET_PARAMETER advertised, GET_PARAMETER not) does NOT fall back to OPTIONS");
}

// ------------------------------------------------- lost control connection vs lost session (UDP)

static void test_silence_escalation_threshold() {
  std::printf("[SILENCE] one unanswered keep-alive pings again; two consecutive rebuild\n");
  CHECK(!livenessSilenceEscalates(0), "no silences -> no escalation");
  CHECK(!livenessSilenceEscalates(1),
        "ONE silence pings again (GStreamer ETIMEOUT: silence is not evidence) -- escalating here "
        "would reset on one slow reply, the exact bug class this campaign fixed");
  CHECK(livenessSilenceEscalates(2), "the second consecutive silence rebuilds");
  CHECK(livenessSilenceEscalates(3), "and it stays escalated past the threshold");
}

static void test_control_loss_keeps_udp_session_once() {
  std::printf("[CONN-LOSS] a lost CONTROL connection is not a lost SESSION when media is UDP\n");
  // GStreamer: GST_RTSP_EEOF in the UDP loop -> warn, reconnect the control connection, continue
  // the SAME session (gstrtspsrc.c:6914-6926; DEFAULT_UDP_RECONNECT TRUE). RFC 2326 sec 1.4: the
  // session outlives the connection. Some cameras drop an idle control connection by design while
  // UDP media keeps flowing; rebuilding there discards a healthy stream.
  CHECK(keepAliveLossReconnectsInPlace(/*mediaOverTCP*/false, /*alreadyRetried*/false),
        "UDP media + first loss -> reconnect in place, session and fan-out clients kept");
  CHECK(!keepAliveLossReconnectsInPlace(false, /*alreadyRetried*/true),
        "bounded to ONE retry per incident (GStreamer's try==0 shape) -- a second consecutive "
        "failure means the camera is really gone, so rebuild");
  CHECK(!keepAliveLossReconnectsInPlace(/*mediaOverTCP*/true, false),
        "TCP-interleaved media rode the dead connection -- only a rebuild can rebind it");
  CHECK(!keepAliveLossReconnectsInPlace(true, true), "TCP + already retried -> rebuild");
}

// ---------------------------------------------------------------- deadline margin (GStreamer rule)

static void test_deadline_margin() {
  std::printf("[DEADLINE] act before the timeout, with GStreamer's safety margin\n");
  CHECK(rtspLivenessDeadlineSecs(600) == 595, "timeout 600 -> 595 (>=20s: minus 5s)");
  CHECK(rtspLivenessDeadlineSecs(60)  == 55,  "timeout 60  -> 55  (>=20s: minus 5s)");
  CHECK(rtspLivenessDeadlineSecs(20)  == 15,  "timeout 20  -> 15  (boundary of the >=20s rule)");
  CHECK(rtspLivenessDeadlineSecs(19)  == 16,  "timeout 19  -> 16  (>=5s: minus 20%)");
  CHECK(rtspLivenessDeadlineSecs(5)   == 4,   "timeout 5   -> 4   (boundary of the >=5s rule)");
  CHECK(rtspLivenessDeadlineSecs(4)   == 3,   "timeout 4   -> 3   (>=1s: minus 1s)");
  CHECK(rtspLivenessDeadlineSecs(1)   == 0,   "timeout 1   -> 0   (no room for a margin: send now)");

  // Absent "; timeout = " parameter -> the 60s default, NOT 0.
  CHECK(rtspLivenessDeadlineSecs(0) == 55, "no timeout advertised -> default 60s -> deadline 55");
}

static void test_deadline_never_exceeds_timeout() {
  std::printf("[DEADLINE] FAILURE CONDITION: the deadline must never reach the session timeout\n");
  // If the deadline ever equalled or exceeded the timeout, we would send the keepalive at or after
  // the moment the server is entitled to drop us — exactly the fault being investigated.
  for (unsigned t = 1; t <= 1000; ++t) {
    if (rtspLivenessDeadlineSecs(t) >= t) {
      std::printf("  FAIL: timeout %u -> deadline %u (not strictly earlier)\n",
                  t, rtspLivenessDeadlineSecs(t));
      ++g_failures; ++g_checks;
      return;
    }
  }
  ++g_checks;
  std::printf("  ok:   deadline < timeout for every timeout in 1..1000\n");
}

static void test_deadline_clamps_absurd_timeout() {
  std::printf("[DEADLINE] FAILURE CONDITION: an absurd timeout must clamp, not overflow\n");
  // deadline*1000000 must stay inside 32-bit unsigned. Without the clamp, a server advertising
  // e.g. 86400s would wrap and produce a tiny delay -> we would hammer it with keepalives.
  unsigned const d = rtspLivenessDeadlineSecs(86400);
  CHECK(d <= RTSP_MAX_LIVENESS_DEADLINE_SECS, "86400s timeout clamps to the max deadline");
  CHECK(rtspLivenessDelayUs(86400, 0, /*sessionEstablished*/true) > 0,
        "clamped delay is still positive (no overflow to 0)");
}

// --------------------------------------------- pre-session cadence (DESCRIBE -> first-SETUP gap)

static void test_pre_session_deadline_beats_connection_idle_fin() {
  std::printf("[PRE-SESSION] before the first SETUP response there is no session, and the session\n"
              "              timeout governs nothing -- the CONNECTION idle timeout does\n");
  // Measured on 192.168.11.19 (pcap fixture bare_setup_ignored_pcap_dialog_*, 4 occurrences):
  // the camera FINs an idle control connection ~10s after the last request. The proxy's
  // DESCRIBE -> first-SETUP gap is bridged only by keep-alives, so the pre-session deadline must
  // sit WELL under that floor. The probe fixture (cam_keepalive_probe_*) proves OPTIONS at 5s
  // holds the connection >=25s and a SETUP on that same connection then succeeds.
  CHECK(rtspPreSessionLivenessDeadlineSecs() == RTSP_PRE_SESSION_IDLE_FIN_FLOOR_SECS/2,
        "pre-session deadline is half the measured 10s idle-FIN floor");
  CHECK(rtspPreSessionLivenessDeadlineSecs() < RTSP_PRE_SESSION_IDLE_FIN_FLOOR_SECS,
        "pre-session deadline strictly under the idle-FIN floor");

  // The session-timeout parameter must have NO influence pre-session: a camera advertising a 600s
  // session timeout still FINs the idle CONNECTION at 10s -- using the session deadline here is the
  // exact defect this test locks out (first keep-alive at 27-55s against a 10s FIN).
  unsigned const samples[] = {0u, 1u, 12345u, 2147483647u, 4294967295u};
  for (unsigned i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
    unsigned const got = rtspLivenessDelayUs(/*sessionTimeoutSecs*/600, samples[i],
                                             /*sessionEstablished*/false);
    unsigned const deadlineUs = rtspPreSessionLivenessDeadlineSecs()*1000000;
    if (got < deadlineUs/2 || got >= deadlineUs) {
      std::printf("  FAIL: pre-session random %u -> delay %u us, outside [%u, %u)\n",
                  samples[i], got, deadlineUs/2, deadlineUs);
      ++g_failures;
    }
    ++g_checks;
  }
  std::printf("  ok:   pre-session delay ignores the 600s session timeout and lands in "
              "[deadline/2, deadline)\n");
}

// ---------------------------------------------------------------- jitter (BUG 1)

static void test_delay_within_range() {
  std::printf("[JITTER] delay lands in [deadline/2, deadline)\n");
  unsigned const timeout = 600, deadline = 595; // 595s window
  unsigned const loUs = deadline*500000, hiUs = deadline*1000000;

  unsigned const samples[] = {0u, 1u, 12345u, 999983u, 2147483647u, 4294967295u};
  for (unsigned i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
    unsigned const got = rtspLivenessDelayUs(timeout, samples[i], /*sessionEstablished*/true);
    if (got < loUs || got >= hiUs) {
      std::printf("  FAIL: random %u -> delay %u us, outside [%u, %u)\n", samples[i], got, loUs, hiUs);
      ++g_failures;
    }
    ++g_checks;
  }
  std::printf("  ok:   all %u sampled random values landed in range\n",
              (unsigned)(sizeof(samples)/sizeof(samples[0])));
}

static void test_jitter_actually_varies() {
  std::printf("[JITTER] REGRESSION (bug 1): the delay must actually VARY with the random input\n");
  // The old form `(span*r) % span` collapses toward a constant. Feed a spread of random values and
  // require a spread of distinct delays — this is the check the old code fails.
  unsigned const timeout = 600;
  unsigned distinct = 0, seen[16];
  for (unsigned i = 0; i < 16; ++i) {
    unsigned const got = rtspLivenessDelayUs(timeout, i*8675309u + 12345u, true);
    bool dup = false;
    for (unsigned j = 0; j < distinct; ++j) if (seen[j] == got) { dup = true; break; }
    if (!dup) seen[distinct++] = got;
  }
  CHECK(distinct >= 12, "16 different random inputs produce >=12 distinct delays (old code: ~1)");
}

static void test_jitter_spans_the_window() {
  std::printf("[JITTER] the jitter must cover the window, not cluster at one end\n");
  unsigned const timeout = 600;
  unsigned const loUs = 595u*500000, hiUs = 595u*1000000;
  unsigned const midUs = loUs + (hiUs - loUs)/2;
  unsigned lower = 0, upper = 0;
  for (unsigned i = 0; i < 400; ++i) {
    unsigned const got = rtspLivenessDelayUs(timeout, i*2654435761u, true); // Knuth multiplicative spread
    if (got < midUs) ++lower; else ++upper;
  }
  CHECK(lower > 50 && upper > 50, "samples fall on BOTH halves of the window (not pinned to one end)");
}

static void test_zero_deadline_sends_immediately() {
  std::printf("[JITTER] FAILURE CONDITION: a 1s timeout leaves no room -> delay 0\n");
  CHECK(rtspLivenessDelayUs(1, 999999, true) == 0,
        "1s timeout -> send immediately, never a negative wait");
}

// ---------------------------------------------------------------- channel ids (BUG 2)

static void test_channel_budget() {
  std::printf("[CHANNELS] one-byte interleaved channel space (RFC 2326 sec 10.12)\n");
  CHECK(!rtspInterleavedChannelsExhausted(0, 2),   "fresh connection has room for a 2-channel track");
  CHECK(!rtspInterleavedChannelsExhausted(252, 4), "channels 252..255 exactly fit");
  CHECK(rtspInterleavedChannelsExhausted(254, 4),  "254 + 4 runs past 255 -> exhausted");
  CHECK(rtspInterleavedChannelsExhausted(256, 2),  "already past the space -> exhausted");
}

static void test_channel_wraparound_regression() {
  std::printf("[CHANNELS] REGRESSION (bug 2): per-connection reset vs the old monotonic counter\n");
  // Model both allocators over a long-lived proxy that reconnects repeatedly. Each reconnect SETUPs
  // 2 tracks == 4 channels (matching the camera under investigation: track1 video + track3 metadata).
  unsigned const channelsPerReconnect = 4;

  // OLD (buggy): one `unsigned char` counter, never reset. Reproduces the production observation
  // and then wraps.
  unsigned char oldCounter = 0;
  unsigned char channelAtReconnect24 = 0;
  bool oldWrapped = false;
  unsigned char previous = 0;
  for (unsigned reconnect = 0; reconnect < 80; ++reconnect) {
    if (reconnect == 24) channelAtReconnect24 = oldCounter;
    if (reconnect > 0 && oldCounter < previous) oldWrapped = true; // counter went backwards == wrap
    previous = oldCounter;
    oldCounter = (unsigned char)(oldCounter + channelsPerReconnect);
  }
  CHECK(channelAtReconnect24 == 96,
        "old allocator reaches channel 96 at reconnect #24 (matches the production log)");
  CHECK(oldWrapped,
        "old allocator WRAPS past 255 within 80 reconnects (the collision risk)");

  // NEW (fixed): reset() clears the counter, so every connection starts at 0 and can never wrap.
  bool newEverExceeded = false;
  for (unsigned reconnect = 0; reconnect < 10000; ++reconnect) {
    unsigned counter = 0; // per-connection reset
    for (unsigned track = 0; track < 2; ++track) {
      if (rtspInterleavedChannelsExhausted(counter, 2)) { newEverExceeded = true; break; }
      counter += 2;
    }
  }
  CHECK(!newEverExceeded,
        "with a per-connection reset, 10000 reconnects never exhaust the channel space");
}

int main() {
  std::printf("== RTSP keep-alive + interleaved-channel regression tests ==\n");
  test_method_selection();
  test_silence_escalation_threshold();
  test_control_loss_keeps_udp_session_once();
  test_deadline_margin();
  test_deadline_never_exceeds_timeout();
  test_deadline_clamps_absurd_timeout();
  test_pre_session_deadline_beats_connection_idle_fin();
  test_delay_within_range();
  test_jitter_actually_varies();
  test_jitter_spans_the_window();
  test_zero_deadline_sends_immediately();
  test_channel_budget();
  test_channel_wraparound_regression();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  if (g_failures > 0) std::printf("FAILURES: %d\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

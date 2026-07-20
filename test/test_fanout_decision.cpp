// Regression tests for the RTP-over-TCP fan-out decision (RTPFanoutDecision.hh) — the
// decoupling logic. Pure function, no sockets, so it runs anywhere. Locks in the two real bugs the
// first cut had: (1) the fit check must use HALF the doubled Linux SO_SNDBUF, and (2) eviction needs
// sustained lag, not a single sample.
//
// Build + run:  make -C test run

#include "RTPFanoutDecision.hh"
#include <cstdio>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, msg) do { ++g_checks; \
  if (!(cond)) { ++g_failures; std::printf("  FAIL: %s\n", (msg)); } \
  else         { std::printf("  ok:   %s\n", (msg)); } } while (0)

static void test_normal_send() {
  std::printf("[SEND] empty buffer, small packet -> SEND\n");
  unsigned strikes = 0; long blocked = 0;
  FanoutDecision d = fanoutDecide(/*haveBufState*/true, /*sndbuf*/200000, /*queued*/0,
                                  /*packetSize*/1400, /*rate*/50000, strikes, 1, blocked);
  CHECK(d.send && !d.drop && !d.evict, "small packet into an empty buffer sends");
}

static void test_unknown_platform_sends() {
  std::printf("[NON-LINUX] no send-buffer state -> SEND (normal path)\n");
  unsigned strikes = 0; long blocked = 0;
  FanoutDecision d = fanoutDecide(false, 0, 0, 60000, 0, strikes, 1, blocked);
  CHECK(d.send && !d.drop, "no buffer state -> take the normal send path");
}

static void test_doubled_sndbuf_regression() {
  std::printf("[RULE #1] fit check uses HALF the doubled SO_SNDBUF (the decoupling-break regression)\n");
  // Linux reports SO_SNDBUF doubled: reported 200000 => ~100000 usable. A 98 KB frame:
  //   old (full-number) check: 200000 free >= need -> SEND, then only ~100000 fits -> BLOCKING send.
  //   new (half) check: usable 100000 < 98000+4+4096 -> DROP (no block). This is the fix.
  unsigned strikes = 0; long blocked = 0;
  FanoutDecision d = fanoutDecide(true, /*sndbuf reported*/200000, /*queued*/0,
                                  /*packetSize*/98000, /*rate*/0, strikes, 1, blocked);
  CHECK(d.usableFree == 100000, "usableFree = sndbuf/2 - queued (honest, not the doubled number)");
  CHECK(d.drop && !d.send, "a frame that fits the DOUBLED number but not the usable half is DROPPED, not blocked");

  // And a frame that genuinely fits the usable half still sends.
  strikes = 0; blocked = 0;
  FanoutDecision d2 = fanoutDecide(true, 200000, 0, 40000, 0, strikes, 1, blocked);
  CHECK(d2.send && !d2.drop, "a frame that fits the usable half still sends");
}

// A consumer whose buffer is full: 4KB usable against a 60KB frame, so nothing fits.
static FanoutDecision refuse(unsigned& strikes, long now, long& blockedSince) {
  return fanoutDecide(true, /*sndbuf*/200000, /*queued*/96000, /*packetSize*/60000,
                      /*rate*/16000, strikes, now, blockedSince);
}

static void test_evict_needs_sustained_lag() {
  std::printf("[RULE #2] eviction needs FANOUT_EVICT_STRIKES consecutive samples, not one\n");
  unsigned strikes = 0; long blocked = 0;

  FanoutDecision a = refuse(strikes, 1000, blocked);
  CHECK(a.drop && !a.evict, "first refusal: DROP but do NOT evict");
  CHECK(blocked == 1000, "first refusal stamps the blocked-since clock");

  // Still inside the window: no eviction no matter how many samples arrive.
  for (int i = 0; i < 50; ++i) refuse(strikes, 1002, blocked);
  CHECK(strikes == 0, "50 samples 2s in: still inside the window, no strikes");

  // Past the window, strikes accumulate; eviction needs the full streak.
  FanoutDecision b = refuse(strikes, 1006, blocked);
  CHECK(!b.evict && strikes == 1, "past the window: strike 1, not yet evicted");
  refuse(strikes, 1007, blocked);
  FanoutDecision d = refuse(strikes, 1008, blocked);
  CHECK(d.evict && d.drop && !d.send, "third consecutive over-window sample: EVICT");
}

static void test_burst_does_not_evict_healthy_consumer() {
  std::printf("[RULE #2] a one-off burst then recovery must NOT evict a healthy consumer\n");
  unsigned strikes = 0; long blocked = 0;
  refuse(strikes, 1000, blocked);
  CHECK(blocked != 0, "burst started the clock");
  // Next sample drains enough for the frame to fit -> the clock must be cleared, not merely paused.
  FanoutDecision recov = fanoutDecide(true, 200000, 2000, 1400, 16000, strikes, 1001, blocked);
  CHECK(!recov.evict && blocked == 0, "a single accepted frame clears the blocked-since clock");
  // ...and a much later refusal starts a FRESH window rather than inheriting the old timestamp.
  FanoutDecision later = refuse(strikes, 9999, blocked);
  CHECK(!later.evict && blocked == 9999, "a later stall starts a new window (no stale carry-over)");
}

// ---------------------------------------------------------------- the anti-masking regression

static void test_evict_is_time_based_not_buffer_based() {
  std::printf("[RULE #2] REGRESSION: eviction must fire regardless of SO_SNDBUF vs bitrate\n");
  // THE BUG THIS PINS: eviction used to trigger on `behindMs > 5000`, where
  // behindMs = queued / rate. `queued` cannot exceed the send buffer, so that value is capped at
  // sndbuf/rate -- and whenever sndbuf/rate < 5s it could NEVER cross the threshold, no matter how
  // long the consumer had been dead. It was also inverted: a FASTER stream shrinks the ceiling, so
  // eviction was least reachable exactly for the high-bitrate consumers it exists to shed.
  //
  // Measured on a real box: ~212KB usable at ~100KB/s capped behindMs at ~2.1s, and a fully stalled
  // consumer survived a 25s soak un-evicted.
  //
  // It is ALSO the shape of a testing mistake worth pinning: the first instinct on seeing "evict
  // never fired" was to turn the bitrate UP until it did. That would have masked the defect --
  // and for THIS bug it would have made things strictly worse. So the case below is deliberately
  // one where the byte-based rule is arithmetically incapable of evicting.
  unsigned strikes = 0; long blocked = 0;
  int const sndbuf = 425000;        // ~212KB usable after the Linux doubling
  double const rate = 100000;       // 100KB/s
  int const queued = 212000;        // buffer FULL -- cannot grow further, this is the ceiling
  unsigned const frame = 60000;

  FanoutDecision first = fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 500, blocked);
  CHECK(first.behindMs < FANOUT_DRIFT_EVICT_MS,
        "the byte-derived estimate is BELOW the old threshold and cannot rise (buffer is full)");
  CHECK(first.drop && !first.evict, "first refusal drops, does not evict");

  // Time passes well beyond the eviction window. Under the OLD byte-based rule this consumer was
  // un-evictable forever; under the time-based rule it must go.
  fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 510, blocked);
  fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 511, blocked);
  FanoutDecision out = fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 512, blocked);
  CHECK(out.evict, "a consumer blocked for >5s IS evicted even though behindMs never crossed 5000");

  // And the rule must hold across bitrates -- including ones where the byte estimate looks fine.
  for (int i = 0; i < 3; ++i) {
    unsigned s2 = 0; long b2 = 0;
    double const rates[] = {10000, 1000000, 50000000}; // 10KB/s .. 50MB/s
    // k=0 stamps the clock; k=1 is still inside the 5s window; k>=2 are over it and accumulate the
    // strikes. FANOUT_EVICT_STRIKES of those are needed, hence 6 samples, not 4.
    FanoutDecision r;
    for (int k = 0; k < 6; ++k) {
      r = fanoutDecide(true, sndbuf, queued, frame, rates[i], s2, 100 + k*3, b2);
    }
    if (!r.evict) { std::printf("  FAIL: rate %.0f B/s did not evict\n", rates[i]); ++g_failures; }
    ++g_checks;
  }
  std::printf("  ok:   eviction fires at 10KB/s, 1MB/s and 50MB/s alike (bitrate-independent)\n");
}

static void test_evict_on_time_behind_with_a_large_buffer() {
  std::printf("[RULE #2] REGRESSION: a consumer SECONDS behind must evict even when frames still FIT a big buffer\n");
  // THE BUG THIS PINS: the kernel autotunes SO_SNDBUF to MEGABYTES, not the 50 KB the proxy asks
  // for. Eviction armed its clock only on !fits, but on a multi-MB buffer a consumer sitting
  // seconds behind still has room for every frame, so !fits never trips and it is never evicted.
  // The other tests only exercise a FULL buffer (usable≈0 -> !fits), missing this regime entirely.
  unsigned strikes = 0; long blocked = 0;
  int const sndbuf = 8000000;   // ~4 MB usable after the Linux doubling
  double const rate = 250000;   // 250 KB/s
  int const queued = 3000000;   // 3 MB ≈ 12 s behind, but NOT buffer-full
  unsigned const frame = 64000; // usable is ~1 MB, so this comfortably FITS

  FanoutDecision first = fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 500, blocked);
  CHECK(first.behindMs > FANOUT_DRIFT_EVICT_MS,
        "the consumer is measurably >5s behind in TIME (behindMs derived from a large, un-capped queue)");
  CHECK(first.send, "the frame still FITS the big buffer, so it is not dropped for lack of room");

  // Time passes well beyond the eviction window while the consumer stays this far behind. It must be
  // evicted on TIME-behind, not left forever because each frame happens to fit.
  fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 510, blocked);
  fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 511, blocked);
  FanoutDecision out = fanoutDecide(true, sndbuf, queued, frame, rate, strikes, 512, blocked);
  CHECK(out.evict, "a consumer >5s behind on a large buffer IS evicted even though every frame fits");
}

static void test_oversized_frame_never_evicts_a_healthy_consumer() {
  std::printf("[OVERSIZE] a frame too big for an EMPTY buffer is OUR sizing, not a slow consumer\n");
  // Same shape as the eviction bug, mirrored: a condition that can never SUCCEED. If one RTP packet
  // exceeds usable-free even when nothing is queued, every frame is refused forever — so a perfectly
  // healthy consumer accrues blocked-time, gets evicted, reconnects, fails identically, and is
  // evicted again. A permanent eviction loop caused by our own buffer sizing, and made FASTER to hit
  // by the switch to time-based eviction.
  unsigned strikes = 0; long blocked = 0;
  int const sndbuf = 200000;      // 100000 usable
  unsigned const huge = 150000;   // bigger than usable even when empty

  FanoutDecision d = fanoutDecide(true, sndbuf, /*queued*/0, huge, 50000, strikes, 1000, blocked);
  CHECK(d.oversizedFrame, "a frame that cannot fit an EMPTY buffer is reported as oversized");
  CHECK(d.drop && !d.send, "the frame is still dropped (never a partial or blocking send)");
  CHECK(!d.evict, "an oversized frame does NOT evict");
  CHECK(blocked == 0, "an oversized frame does NOT start the blocked clock");

  // Hammer it well past the eviction window: still no eviction, because nothing is the consumer's fault.
  for (long t = 1001; t < 1100; ++t) {
    FanoutDecision r = fanoutDecide(true, sndbuf, 0, huge, 50000, strikes, t, blocked);
    if (r.evict) { std::printf("  FAIL: evicted at t=%ld on an oversized frame\n", t); ++g_failures; break; }
  }
  ++g_checks;
  std::printf("  ok:   100s of oversized frames never evict the consumer\n");

  // Sanity: the SAME consumer with a normal frame still works, so the guard is not a blanket excuse.
  strikes = 0; blocked = 0;
  FanoutDecision ok = fanoutDecide(true, sndbuf, 0, 1400, 50000, strikes, 2000, blocked);
  CHECK(ok.send && !ok.oversizedFrame, "a normal frame on the same socket still sends");
}

static void test_every_threshold_is_reachable() {
  std::printf("[REACHABILITY] every decision outcome must be attainable — the anti-masking guard\n");
  // Generalises the eviction bug: a threshold nothing can ever cross is dead code that LOOKS live.
  // Assert each outcome can actually be produced, so a future change that makes one unreachable
  // fails here instead of silently disabling a mechanism in production.
  unsigned strikes; long blocked;
  bool sawSend = false, sawDrop = false, sawWarn = false, sawEvict = false, sawOversize = false;

  strikes = 0; blocked = 0;
  sawSend = fanoutDecide(true, 200000, 0, 1400, 50000, strikes, 1, blocked).send;

  strikes = 0; blocked = 0;
  sawWarn = fanoutDecide(true, 200000, 40000, 1400, 100000, strikes, 1, blocked).driftWarn;

  strikes = 0; blocked = 0;
  sawDrop = fanoutDecide(true, 200000, 96000, 60000, 16000, strikes, 1, blocked).drop;

  strikes = 0; blocked = 0;
  for (long t = 1; t <= 20 && !sawEvict; ++t) {
    sawEvict = fanoutDecide(true, 200000, 96000, 60000, 16000, strikes, t, blocked).evict;
  }

  strikes = 0; blocked = 0;
  sawOversize = fanoutDecide(true, 200000, 0, 150000, 50000, strikes, 1, blocked).oversizedFrame;

  CHECK(sawSend,     "SEND is reachable");
  CHECK(sawDrop,     "DROP is reachable");
  CHECK(sawWarn,     "DRIFT-WARN is reachable");
  CHECK(sawEvict,    "EVICT is reachable (this is the one that was silently unreachable)");
  CHECK(sawOversize, "OVERSIZED is reachable");
}

static void test_drift_warn() {
  std::printf("[WARN] 100ms < behind < 5000ms -> advisory drift warning, still sends if it fits\n");
  unsigned strikes = 0; long blocked = 0;
  // rate 100000 B/s, queued 40000 -> behindMs = 400 (>100, <5000). packet fits usable (100000).
  FanoutDecision d = fanoutDecide(true, 200000, 40000, 1400, 100000, strikes, 1, blocked);
  CHECK(d.driftWarn, "400ms behind raises a drift warning");
  CHECK(d.send && !d.drop && !d.evict, "still sends (fits) while warning");
  CHECK(blocked == 0, "a consumer that still fits is never marked blocked");
}

int main() {
  std::printf("== RTPFanoutDecision regression tests ==\n\n");
  test_normal_send();
  test_unknown_platform_sends();
  test_doubled_sndbuf_regression();
  test_evict_needs_sustained_lag();
  test_burst_does_not_evict_healthy_consumer();
  test_evict_is_time_based_not_buffer_based();
  test_evict_on_time_behind_with_a_large_buffer();
  test_oversized_frame_never_evicts_a_healthy_consumer();
  test_every_threshold_is_reachable();
  test_drift_warn();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

// Regression tests for the TIME-based stream-stall detector (StreamStallDetector.hh).
//
// This detector exists because the older dispatch-COUNTING guard is structurally blind to a peer
// that stops sending while holding the socket open: no data means no readability, no readability
// means no dispatches, and a counter of dispatches therefore never advances. That blind spot was
// found by SIMULATION, not by reading the code, and the guard's misleading name ("stall guard" for
// what is really a CPU busy-loop guard) is what made the wrong behaviour look correct.
//
// The tests below pin BOTH halves: that this detector sees the silent case, and that the counting
// guard provably cannot — so nobody re-merges them or re-assumes the counter covers it.
//
// Build + run:  make -C test run

#include "StreamStallDetector.hh"
#include <cstdio>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, msg) do { ++g_checks; \
  if (!(cond)) { ++g_failures; std::printf("  FAIL: %s\n", (msg)); } \
  else         { std::printf("  ok:   %s\n", (msg)); } } while (0)

static void test_reports_after_the_limit() {
  std::printf("[STALL] a stream with no progress for the limit is reported ONCE\n");
  StreamStallVerdict a = streamStallCheck(true, false, /*lastProgress*/100, /*now*/104);
  CHECK(!a.stalled, "4s idle with a 5s limit: not yet a stall");

  StreamStallVerdict b = streamStallCheck(true, false, 100, 105);
  CHECK(b.stalled && b.idleSecs == 5, "5s idle: stall reported, with the elapsed time");

  StreamStallVerdict c = streamStallCheck(true, /*alreadyReported*/true, 100, 500);
  CHECK(!c.stalled, "already reported: does NOT repeat every tick (onset only)");
}

static void test_control_only_socket_is_never_a_stall() {
  std::printf("[STALL] a socket that never carried a frame is a CONTROL channel, not a stalled stream\n");
  // An RTSP control connection is legitimately idle between keepalives. Reporting it as a stalled
  // stream would generate a fault for every healthy idle session on the box.
  StreamStallVerdict v = streamStallCheck(/*everSawFrame*/false, false, 100, 100000);
  CHECK(!v.stalled, "idle for ~28 hours but never framed: still not a stall");
}

static void test_progress_resets_the_window() {
  std::printf("[STALL] any payload progress restarts the window\n");
  StreamStallVerdict v = streamStallCheck(true, false, /*lastProgress*/1000, /*now*/1002);
  CHECK(!v.stalled, "progress 2s ago: healthy");
  CHECK(v.idleSecs == 2, "idle time is measured from the last progress, not from stream start");
}

// ---------------------------------------------------------------- the structural blind spot

static void test_counting_guard_cannot_see_a_silent_peer() {
  std::printf("[BLIND SPOT] REGRESSION: the dispatch COUNTER can never fire on a silent peer\n");
  unsigned const limit = 2000; // PACKET_DATA_STALL_LIMIT

  // A peer that stops sending produces NO readability, so NO dispatches — forever.
  CHECK(!busyLoopGuardWouldTrip(0, limit),
        "silent peer: zero dispatches, counting guard cannot trip no matter how long it waits");

  // The time-based detector sees exactly that case.
  StreamStallVerdict v = streamStallCheck(true, false, 0, 3600);
  CHECK(v.stalled, "the TIME-based detector DOES report the same silent peer");

  // And the counter still does its own job: a dead-but-READABLE socket spins and must be caught.
  CHECK(busyLoopGuardWouldTrip(limit, limit),
        "dead-but-readable peer: the counting guard trips (that is what it is FOR)");

  std::printf("  note: these are two different faults. Do not merge the triggers -- the counter must\n"
              "        run in the read path to bound CPU; the timer must run off it to see silence.\n");
}

static void test_limit_is_measured_in_time_not_samples() {
  std::printf("[STALL] REGRESSION: the threshold is ELAPSED TIME, not a count of checks\n");
  // If this were ever reimplemented as "N consecutive checks", the answer would depend on how often
  // the timer happens to run. It must not: one check 10s later is a stall; a thousand checks inside
  // the window are not.
  StreamStallVerdict once = streamStallCheck(true, false, 0, 10);
  CHECK(once.stalled, "a SINGLE check 10s after the last progress is a stall");

  bool anyStalled = false;
  for (long t = 0; t < 1000; ++t) {
    if (streamStallCheck(true, false, 0, 1).stalled) anyStalled = true; // 1s idle, checked 1000x
  }
  CHECK(!anyStalled, "a THOUSAND checks inside the window are still not a stall");
}

int main() {
  std::printf("== StreamStallDetector regression tests ==\n\n");
  test_reports_after_the_limit();
  test_control_only_socket_is_never_a_stall();
  test_progress_resets_the_window();
  test_counting_guard_cannot_see_a_silent_peer();
  test_limit_is_measured_in_time_not_samples();
  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  if (g_failures) std::printf("FAILURES: %d\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

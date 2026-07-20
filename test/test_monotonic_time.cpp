// Unit tests for MonotonicTime.hh -- the one seconds counter every duration gate reads.
//
// Why this exists: monotonicSeconds() is free to change its underlying clock source (e.g. to
// CLOCK_MONOTONIC_COARSE, a cached read ~4-5x cheaper than CLOCK_MONOTONIC, on hot per-packet
// paths). Any such change must preserve the two properties every caller depends on:
//
//   1. It never moves backwards. Every stall check and rate limiter subtracts two readings; a
//      backwards step would hide a real stall or mute a rate limiter (see the header's NTP notes).
//   2. It measures the SAME timeline as CLOCK_MONOTONIC, at second granularity. A clock that is
//      cheap but drifts from the monotonic timeline would silently stretch or shrink every
//      timeout in the tree.

#include "MonotonicTime.hh"
#include "check.hh"

#include <time.h>

static void testNeverMovesBackwards() {
  long prev = monotonicSeconds();
  for (int i = 0; i < 100000; ++i) {
    long const now = monotonicSeconds();
    if (now < prev) {
      check(false, "monotonicSeconds() moved backwards between two consecutive calls");
      return;
    }
    prev = now;
  }
  check(true, "monotonicSeconds() never moves backwards across 100k consecutive calls");
}

static void testTracksTheMonotonicTimeline() {
#if defined(CLOCK_MONOTONIC)
  // A coarse/cached source may lag the precise clock by one tick, which at second granularity is
  // at most a 1-second disagreement at a second boundary. More than that means the function is
  // reading a different timeline entirely (wall clock, CPU time, ...), which would rescale every
  // timeout computed from it.
  struct timespec ts;
  check(clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "CLOCK_MONOTONIC is readable on this host");
  long const mono = (long)ts.tv_sec;
  long const ours = monotonicSeconds();
  long const diff = ours >= mono ? ours - mono : mono - ours;
  check(diff <= 1,
        "monotonicSeconds() agrees with CLOCK_MONOTONIC to within 1s (same timeline, coarser read)");
#endif
}

int main() {
  printf("== MonotonicTime regression tests ==\n");
  testNeverMovesBackwards();
  testTracksTheMonotonicTimeline();

  return checkSummary("test_monotonic_time");
}

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
// A seconds counter that only ever moves forward, for measuring ELAPSED TIME.
//
// 🚨 USE THIS, NOT time(NULL), FOR EVERY DURATION / TIMEOUT / RATE-LIMIT GATE.
//
// time(NULL) returns wall-clock, which NTP adjusts. Every guard that subtracts two wall-clock
// readings is therefore wrong across a clock step, in BOTH directions:
//
//   * A BACKWARD step makes `now - then` negative. Rate limiters stop logging until the clock
//     catches up, and an elapsed-time stall check silently hides a stall that is really happening.
//   * A FORWARD step fabricates elapsed time. A jump larger than the stall threshold instantly
//     satisfies it on EVERY stream at once, so a single NTP correction produces a burst of stall
//     faults, peer probes and consumer evictions that describe nothing that occurred.
//
// These units run NTP, so steps are routine rather than theoretical, and the resulting faults are
// indistinguishable in the log from real ones — which is precisely the "must not be POSSIBLE to
// misreport" failure. CLOCK_MONOTONIC is immune: it measures elapsed time and is never stepped.
//
// Wall-clock is still correct for TIMESTAMPS (when did this happen, for correlation with journald).
// It is never correct for DURATIONS. Keep the two uses separate.
#ifndef _MONOTONIC_TIME_HH
#define _MONOTONIC_TIME_HH

#include <time.h>

// Seconds since an unspecified fixed point. Only DIFFERENCES are meaningful.
inline long monotonicSeconds() {
#if defined(CLOCK_MONOTONIC_COARSE)
  // Linux: same monotonic timeline, read from a cached timestamp instead of a TSC read + scaling —
  // ~4-5x cheaper, at 1-4ms resolution. Every caller truncates to WHOLE SECONDS, three orders of
  // magnitude coarser than that, so the precision given up here is precision nobody consumed. This
  // matters because the hottest callers are per-packet (RTPInterface's noteProgress and sendPacket
  // fan-out — order 10k calls/s on a ~50-camera box). test_monotonic_time pins that this source
  // stays on the CLOCK_MONOTONIC timeline and never moves backwards.
  struct timespec tsc;
  if (clock_gettime(CLOCK_MONOTONIC_COARSE, &tsc) == 0) return (long)tsc.tv_sec;
#endif
#if defined(CLOCK_MONOTONIC)
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) return (long)ts.tv_sec;
#endif
  // No monotonic clock available. Wall-clock is the only option left; it is worse, not equivalent,
  // so this fallback is deliberate and narrow rather than an unnoticed default.
  return (long)time(NULL);
}

#endif

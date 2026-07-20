// Rate-limited logger.
//
// Two variants:
//   - rateLimitedLog: global window across all events using the same state.
//     Use when you want a single aggregate view.
//   - rateLimitedLogPerKey: independent window per key (typically socket fd).
//     Use when you want per-socket diagnostics — each socket's events throttle
//     independently, so a stall on socket A doesn't suppress logs for socket B.
//
// Both return N>0 if the caller should log now (N = events accumulated since
// the previous log, including this one), or 0 to suppress. Logs at most once
// every `windowSecs` wall-clock seconds.

#ifndef _RATE_LIMITED_LOG_HH
#define _RATE_LIMITED_LOG_HH

#include <time.h>
#include <map>

#include "MonotonicTime.hh" // monotonicSeconds() -- elapsed-time windows must not follow NTP steps

inline unsigned long rateLimitedLog(time_t& lastSec, unsigned long& pending,
                                    time_t windowSecs) {
  ++pending;
  // Monotonic: this is an elapsed-time window. With wall-clock, a backward NTP step suppresses all
  // logging until the clock catches up -- silencing the diagnostics exactly when something odd is
  // happening -- and a forward step releases a burst that misrepresents when events occurred.
  long now = monotonicSeconds();
  if (lastSec == 0 || now - lastSec >= windowSecs) {
    lastSec = now;
    unsigned long n = pending;
    pending = 0;
    return n;
  }
  return 0;
}

struct RateLimitEntry {
  time_t lastSec;
  unsigned long pending;
  RateLimitEntry() : lastSec(0), pending(0) {}
};

// The tracker map is bounded by the number of DISTINCT keys ever seen (for fd keys, the highest live
// fd — tens per proxy, not millions). If a closed fd is recycled for a new socket the new socket
// inherits the old entry, but this is self-correcting and harmless: the very next call sees the stale
// `lastSec` as expired and logs once (with a possibly-inflated count) then resets. (A per-consumer
// entry that teardown reclaims automatically would be cleaner, but that needs the state to live on the
// tcpStreamRecord/SocketDescriptor rather than in these fd-keyed statics — a larger refactor.)
inline unsigned long rateLimitedLogPerKey(std::map<int, RateLimitEntry>& tracker,
                                          int key, time_t windowSecs) {
  RateLimitEntry& e = tracker[key];
  ++e.pending;
  // Monotonic: this is an elapsed-time window. With wall-clock, a backward NTP step suppresses all
  // logging until the clock catches up -- silencing the diagnostics exactly when something odd is
  // happening -- and a forward step releases a burst that misrepresents when events occurred.
  long now = monotonicSeconds();
  if (e.lastSec == 0 || now - e.lastSec >= windowSecs) {
    e.lastSec = now;
    unsigned long n = e.pending;
    e.pending = 0;
    return n;
  }
  return 0;
}

// Standard trailer for a rate-limited log line: append " (N noun in last Ws)" when more than one
// event was coalesced, then a newline. Templated on the stream type so this header stays free of any
// live555 dependency while still working with UsageEnvironment's operator<<. Replaces the identical
// hand-written suffix that was repeated at every rate-limited log site (so they can't drift apart).
template<class Stream>
inline void logRateLimitSuffix(Stream& out, unsigned long n, char const* noun, time_t windowSecs) {
  if (n > 1) out << " (" << (unsigned)n << " " << noun << " in last " << (unsigned)windowSecs << "s)";
  out << "\n";
}

#endif

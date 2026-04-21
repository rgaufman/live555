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

inline unsigned long rateLimitedLog(time_t& lastSec, unsigned long& pending,
                                    time_t windowSecs) {
  ++pending;
  time_t now = time(NULL);
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

// The tracker map grows unbounded over process lifetime. For live555 use
// this is fine — tens of sockets per proxy lifetime, not millions.
inline unsigned long rateLimitedLogPerKey(std::map<int, RateLimitEntry>& tracker,
                                          int key, time_t windowSecs) {
  RateLimitEntry& e = tracker[key];
  ++e.pending;
  time_t now = time(NULL);
  if (e.lastSec == 0 || now - e.lastSec >= windowSecs) {
    e.lastSec = now;
    unsigned long n = e.pending;
    e.pending = 0;
    return n;
  }
  return 0;
}

#endif

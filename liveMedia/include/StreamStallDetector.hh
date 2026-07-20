// Time-based stream-stall detection, as pure functions so the policy can be unit-tested without
// sockets, a scheduler, or a camera (see test/test_stream_stall.cpp).
//
// WHY THIS EXISTS SEPARATELY FROM THE BUSY-LOOP GUARD: RTPInterface has two guards that are easy to
// confuse, and conflating them has already cost real time.
//
//   fPacketStallCount  — a CPU BUSY-LOOP guard. Counts read DISPATCHES with no payload progress. Its
//                        job is to stop a core spinning on a dead-but-readable socket, and it must
//                        run synchronously in the read path (that is where a valid stall-peek and a
//                        safe teardown are available). It is NOT a stall detector: a peer that stops
//                        sending while holding the socket open generates NO readability, hence no
//                        dispatches, so this counter is structurally blind to the most obvious kind
//                        of stall. It was named "stall guard" for a long time and that misnaming
//                        produced a wrong test expectation and sent an investigation the wrong way.
//
//   this file          — the actual STALL detector. Asks the question an operator asks: how LONG
//                        has nothing arrived? Driven by a timer, precisely because there is no
//                        dispatch to hang off. Detects and reports only; recovery stays with the -D
//                        inactivity reset at the ProxyRTSPClient layer.
//
// Rule of thumb this encodes: if the question is "how long has it been wrong?", measure elapsed
// time. A counter of events cannot answer it when the fault's signature is the ABSENCE of events.

#ifndef _STREAM_STALL_DETECTOR_HH
#define _STREAM_STALL_DETECTOR_HH

// How long a '$'-framed stream may make NO payload progress before we call it stalled. Deliberately
// under the -D inactivity default so the precise signal ("this stream stopped") lands before the
// blunt one ("no packets, resetting"), while staying long enough that a slow keyframe or a
// scheduling hiccup on a loaded box cannot trip it.
//
// NOTE: -D is per-invocation configurable, so this ordering is a convention, not an invariant. If -D
// is set at or below this value the inactivity reset wins and this detector may never report. That
// is acceptable (recovery still happens) but it means the precise signal is lost, so prefer -D
// comfortably above this.
static long const STREAM_STALL_SECS = 5;

struct StreamStallVerdict {
  bool stalled;   // report a stall now
  long idleSecs;  // how long since the last payload progress
};

// `everSawFrame` gates the whole thing: a socket that has never carried a '$'-framed packet is a
// pure RTSP control channel, which is legitimately idle for long stretches and must never be
// reported as a stalled stream. `alreadyReported` latches, so a persistent stall logs its ONSET
// once rather than once per tick.
inline StreamStallVerdict streamStallCheck(bool everSawFrame, bool alreadyReported,
                                           long lastProgressSec, long nowSec,
                                           long limitSecs = STREAM_STALL_SECS) {
  StreamStallVerdict v;
  v.idleSecs = nowSec - lastProgressSec;
  v.stalled = false;

  if (!everSawFrame) return v;   // control-only socket: never a stream stall
  if (alreadyReported) return v; // onset already reported; do not repeat every tick
  if (v.idleSecs < limitSecs) return v;

  v.stalled = true;
  return v;
}

// Models the busy-loop guard's reachability, so the distinction above is asserted rather than merely
// commented. `readableDispatches` is how many times the socket was dispatched with no progress —
// which for a SILENT peer is zero, forever, no matter how long it has been dead.
inline bool busyLoopGuardWouldTrip(unsigned readableDispatches, unsigned limit) {
  return readableDispatches >= limit;
}

#endif

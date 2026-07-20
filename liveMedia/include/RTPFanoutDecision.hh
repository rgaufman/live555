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
// The per-consumer RTP-over-TCP fan-out decision, as a PURE function so it can be
// unit-tested (test/test_fanout_decision.cpp) — the decoupling logic that keeps one slow downstream
// consumer from ever blocking the single-threaded event loop / starving the upstream camera read.
//
// For each downstream consumer, given its kernel send-buffer state this frame, decide: SEND, DROP the
// frame for THAT consumer only (buffer can't hold the whole interleaved frame), WARN (drifting), or
// EVICT (hopelessly behind -> drop it so it reconnects fresh). Dropping a WHOLE frame keeps framing in
// sync (like UDP loss), so a chronically slow consumer degrades only its own stream.
//
// Two correctness rules this encodes (both were real bugs in the first cut):
//   1. USABLE free space = SO_SNDBUF/2 - queued on Linux. Linux reports SO_SNDBUF DOUBLED (half is
//      kernel bookkeeping), so only ~half holds payload. Basing the fit check on the full reported
//      number let a large frame pass the check, then only half fit at send() -> a partial/blocking
//      send that stalls the whole loop — exactly the decoupling failure this is meant to prevent.
//   2. EVICT only after >= FANOUT_EVICT_STRIKES CONSECUTIVE over-threshold samples. A single I-frame
//      burst briefly fills a low-bitrate consumer's buffer (a spot "6000 ms behind" against a coarse
//      1 s average rate) and it would drain in well under a second; evicting on one sample would kick
//      a healthy client. ("One measurement is not a conclusion.")

#ifndef _RTP_FANOUT_DECISION_HH
#define _RTP_FANOUT_DECISION_HH

// Framing + policy constants (shared with the send path and the unit test — one definition each).
static unsigned const RTP_TCP_FRAMING_HEADER_BYTES = 4;      // RFC 2326 sec 10.12: '$'+channel+2-byte-size
static unsigned const FANOUT_DROP_CUSHION_BYTES    = 4096;   // margin over the honest usable-free for skb overhead
static double   const FANOUT_DRIFT_WARN_MS         = 100.0;  // >100ms behind -> WARN (advisory, rate-limited)
static double   const FANOUT_DRIFT_EVICT_MS        = 5000.0; // (advisory only; see FANOUT_EVICT_SECS)
// How long a consumer may be UNABLE TO ACCEPT A FRAME before we evict it. This is MONOTONIC elapsed
// time, deliberately NOT "seconds' worth of queued bytes" — the latter is capped by SO_SNDBUF and so
// is unreachable whenever sndbuf/rate is under the threshold. See the note in fanoutDecide().
static long     const FANOUT_EVICT_SECS            = 5;
static unsigned const FANOUT_EVICT_STRIKES         = 3;      // consecutive over-EVICT samples before we actually evict

struct FanoutDecision {
  bool   send;       // ok to send this frame to this consumer now
  bool   drop;       // skip this consumer for this frame (buffer can't hold a whole frame, or evicting)
  bool   driftWarn;  // >WARN_MS behind (advisory; caller logs rate-limited)
  bool   evict;      // hopelessly behind for FANOUT_EVICT_STRIKES samples -> caller evicts + reconnects it
  double behindMs;   // estimated time-behind (0 if send-rate unknown) -- ADVISORY, see fanoutDecide
  int    usableFree; // honest usable free bytes (-1 if the platform can't report it)
  bool   oversizedFrame; // frame cannot fit an EMPTY buffer: our sizing, not a slow consumer
};

// Decide the action for ONE consumer. `evictStrikes` is this consumer's running count of consecutive
// over-EVICT samples (caller stores it per consumer); this function updates it. Pure: no syscalls, no
// globals — the caller passes the already-read socket numbers.
//   haveBufState : did the platform report send-buffer state? (Linux yes; else no -> always SEND)
//   sndbuf       : SO_SNDBUF as reported (doubled on Linux)
//   queued       : TIOCOUTQ bytes queued to this consumer
//   packetSize   : this frame's RTP payload size
//   sendBytesPerSec : coarse ~1s send-rate estimate (0 => time-behind unknown; ADVISORY only)
//   nowSec          : current MONOTONIC seconds (caller passes it, so this stays pure/testable)
//   blockedSinceSec : per-consumer; 0 while healthy, else when it first refused a frame (updated here)
inline FanoutDecision fanoutDecide(bool haveBufState, int sndbuf, int queued,
                                   unsigned packetSize, double sendBytesPerSec,
                                   unsigned& evictStrikes, long nowSec, long& blockedSinceSec) {
  FanoutDecision d;
  d.send = true; d.drop = false; d.driftWarn = false; d.evict = false;
  d.behindMs = 0.0; d.usableFree = -1; d.oversizedFrame = false;

  if (!haveBufState) return d; // non-Linux / unknown: take the normal send path (as before)

  // Rule 1: honest usable free space (half the doubled SO_SNDBUF, minus what's queued).
  int usable = sndbuf / 2 - queued;
  if (usable < 0) usable = 0;
  d.usableFree = usable;

  // ADVISORY ONLY. This is an estimate of how far behind the consumer is, and it is fundamentally
  // capped: `queued` cannot exceed the socket's send buffer, so this value can never exceed
  // sndbuf/rate no matter how long the consumer has been dead. Good enough to warn with; NOT usable
  // as an eviction trigger (see below).
  if (sendBytesPerSec > 0 && queued > 0) {
    d.behindMs = (queued / sendBytesPerSec) * 1000.0;
  }
  if (d.behindMs > FANOUT_DRIFT_WARN_MS) d.driftWarn = true;

  // Frame-atomic fit: need room for the whole interleaved frame + a cushion, else DROP for this
  // consumer only (never a partial/blocking send).
  unsigned need = packetSize + RTP_TCP_FRAMING_HEADER_BYTES + FANOUT_DROP_CUSHION_BYTES;
  bool const fits = (unsigned)usable >= need;
  if (!fits) { d.drop = true; d.send = false; }

  // OVERSIZED FRAME: could this frame fit even into a COMPLETELY EMPTY buffer? If not, the consumer
  // is not slow — our send buffer is too small for this stream's packet size, and no amount of
  // draining will ever let it through. Blaming the consumer here would evict it, let it reconnect,
  // fail identically, and evict it again: a permanent eviction loop for a perfectly healthy client,
  // caused entirely by our own sizing. So flag it, and do NOT let it start or advance the blocked
  // clock. (This became easier to hit, not harder, once eviction was measured in elapsed time.)
  d.oversizedFrame = (need > (unsigned)(sndbuf / 2));
  if (d.oversizedFrame) return d; // dropped this frame, but nobody is at fault and nothing is evicted

  // Rule 2: eviction is WALL-CLOCK, and "behind" must be the UNION of both tests, because each one
  // alone is unreachable in the other's buffer regime:
  //   - behindMs is byte-derived and `queued` plateaus at the buffer size, so on a SMALL buffer it
  //     can never cross the window (a faster stream only lowers that ceiling).
  //   - !fits never trips on a LARGE buffer (the kernel autotunes SO_SNDBUF to MBs), where a
  //     consumer seconds behind still leaves room for every frame.
  // blockedSinceSec is 0 while healthy, stamped when either holds, cleared when neither does.
  bool const behind = !fits || d.behindMs > FANOUT_DRIFT_EVICT_MS;
  if (!behind) {
    blockedSinceSec = 0; // draining again
    evictStrikes = 0;
  } else {
    if (blockedSinceSec == 0) blockedSinceSec = nowSec; // first sample behind: start the clock
    else if (nowSec - blockedSinceSec >= (long)FANOUT_EVICT_SECS) {
      // A streak, not one sample: a consumer that recovers resets the clock above.
      ++evictStrikes;
      if (evictStrikes >= FANOUT_EVICT_STRIKES) {
        d.evict = true; d.drop = true; d.send = false;
      }
    }
  }
  return d;
}

#endif

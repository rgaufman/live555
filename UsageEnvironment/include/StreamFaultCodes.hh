// Stable, machine-greppable fault codes for every stream failure we classify.
//
// WHY: log tallies used to grep the PROSE of each message ("stalled mid-packet", "closed
// gracefully"). That couples every diagnostic and dashboard to wording nobody thinks of as an API —
// reword a log line for clarity and the fault silently stops being counted, which reads exactly like
// "the fault stopped happening". These codes are the contract; the prose around them is free to
// change.
//
// RULES:
//   * A code is permanent. Never reword or repurpose one — add a new code instead.
//   * Every classified fault log line must carry exactly one code, near the front.
//   * Codes are greppable as whole tokens: "TX-FIN" never appears inside another code.
//
// Any log-analysis tooling that tallies these codes holds a mirror of this list. The two must agree:
// a code added here but missing from the consumer is a fault nothing counts, and the consumer will
// silently under-report rather than fail. Assert that agreement in BOTH directions in the consumer's
// own test suite, so adding a code here cannot quietly go uncounted there.
//
// LIVES IN UsageEnvironment/ ON PURPOSE. Emitters exist in liveMedia (RTPInterface,
// ProxyServerMediaSession) AND in BasicUsageEnvironment (BasicTaskScheduler's orphan-fd guard), and
// BasicUsageEnvironment sits BELOW liveMedia — it must not include a liveMedia header. This file has
// no dependencies of its own, so the lowest common layer is its correct home. Hardcoding the literal
// in the lower layer instead would reintroduce exactly the drift this file exists to prevent.

#ifndef _STREAM_FAULT_CODES_HH
#define _STREAM_FAULT_CODES_HH

// ---- upstream (the camera / back-end side) ----
#define TX_CODE_FIN     "TX-FIN"     // far end sent FIN mid-packet -- PROVEN graceful close
#define TX_CODE_RST     "TX-RST"     // far end sent RST mid-packet -- PROVEN abortive close
#define TX_CODE_STALL   "TX-STALL"   // stalled mid-packet, far end NOT proven at fault (e.g. path loss)
#define TX_CODE_DESYNC  "TX-DESYNC"  // interleaved '$' framing lost; socket still healthy
#define TX_CODE_IDLE    "TX-IDLE"    // no packets for the -D interval -> session reset (peer went silent)

// ---- our own socket bookkeeping ----
// A state the code did not anticipate. ALWAYS logged, never verbosity-gated: an unexpected condition
// that returns silently is indistinguishable from one that never happened, which is how a fault
// becomes invisible. If you find yourself adding an `if (x == NULL) return;` on a path that cannot
// then do its job, log this instead of returning quietly.
#define TX_CODE_UNEXPECTED "TX-UNEXPECTED"

#define TX_CODE_EBADF   "TX-EBADF"   // stale read handler deregistered (EBADF busy-loop guard)
#define TX_CODE_ORPHAN  "TX-ORPHAN"  // orphaned ready socket cleared
#define TX_CODE_REENTER "TX-REENTER" // socket marked for deletion but still registered
// Routine per-teardown socket cleanup (expected once or twice per reconnect). Split from TX-EBADF
// deliberately: the routine emitter fires 1-3x per teardown, so counting it under the busy-loop
// guard's code buried the anomaly inside reconnect churn -- one name, two verdicts, distribution
// hidden. TX-EBADF now measures only what its consumer key (ebadf_busyloop) says.
#define TX_CODE_CLEANUP "TX-CLEANUP"

// ---- reachability at the INSTANT of a fault ----
// Emitted right after a classified fault, answering the question a periodic ping cannot: was the
// peer still on the network at that instant? "Still answering" means it was present and chose to
// abort/stop; "did not answer" means it left the network. A sampled measurement taken seconds later
// cannot distinguish these.
#define TX_CODE_PEER_UP   "TX-PEER-UP"   // peer answered TCP at the moment of the fault
#define TX_CODE_PEER_GONE "TX-PEER-GONE" // peer did NOT answer within the probe deadline

// ---- downstream (fan-out to local consumers -- NOT a camera fault) ----
#define TX_CODE_DRIFT   "TX-DRIFT"   // a consumer is falling behind (advisory)
#define TX_CODE_DROP    "TX-DROP"    // send buffer full -> frame dropped for that consumer
#define TX_CODE_EVICT   "TX-EVICT"   // consumer too far behind to recover -> evicted

// A downstream consumer's socket closed. This is ROUTINE -- our recorder or
// detector restarted, or a viewer closed a tab -- and it is emphatically NOT a
// camera fault, so it must never share a code with one.
//
// Measured on box 6397272975e8c006c6fc, teardowns in one 60-minute window by
// peer address: 166 from 127.0.0.1 (our own consumers) against 20 from the
// worst actual camera. Every one of those 166 was logged as TX-FIN/TX-RST with
// "Far-end close PROVEN", and bin/check_flap then tallied them into a verdict
// of "REAL upstream stalls ... FIX the camera link" -- blaming the customer's
// network for our own recorder restarting. See test/test_stream_role.cpp.
#define TX_CODE_CLIENT_GONE "TX-CLIENT-GONE"

// The socket's direction could not be established. Deliberately its OWN code:
// folding an unknown role into the upstream vocabulary is precisely the bug
// above, and folding it into the downstream one would hide real camera faults
// instead. An unclassifiable teardown must be countable as unclassifiable.
#define TX_CODE_ROLE_UNKNOWN "TX-ROLE-UNKNOWN"

// A session reset was requested, naming the call site that asked. NOT a fault in itself -- a reset is
// the correct response to a lost upstream -- but it is the most expensive thing the proxy does
// (a completed DESCRIBE is discarded and the whole handshake restarts), and until this existed the
// log could not say WHICH of the five-plus call sites fired. A captured 259s recovery on
// 192.168.11.19 showed three back-to-back resets from state DESCRIBED that matched no logged cause.
#define TX_CODE_RESET   "TX-RESET"

// A failed RTSP request is being RE-SENT on the existing session, instead of the session being torn
// down and rebuilt. Not a fault: it is the cheap path. Emitted so the retry loop is visible and
// bounded in the log rather than silent -- and so its rate can be compared against TX-RESET, which
// is what tells you whether retries are actually saving sessions. See RequestRetryPolicy.hh.
#define TX_CODE_RETRY   "TX-RETRY"

// The back-end DESCRIBE failed, and the retry ladder is backing off. Before this code existed the
// path emitted NOTHING journal-worthy: a camera that is off, unreachable, or rejecting credentials
// walked the 1s->256-511s backoff in total silence, so "camera down for six hours" was
// indistinguishable in the journal from "proxy idle" -- silence reading as health, the exact
// failure this whole file's preamble describes.
#define TX_CODE_DESCRIBE_FAIL "TX-DESCRIBE-FAIL"

// The server answered a keep-alive with 454 Session Not Found: connection alive, session GONE on
// the far end. Its own code, not TX-UNEXPECTED: this is an anticipated, policy-handled condition
// (rtspStatusEndsSession), and tagging anticipated conditions TX-UNEXPECTED trains operators to
// ignore the one code that must always mean "our bookkeeping diverged".
#define TX_CODE_SESSION_GONE "TX-SESSION-GONE"

// The session's media transport is being switched (UDP -> RTP-over-TCP, or after a 461 rejection).
// Its own code, not TX-RETRY: TX-RETRY is counted as the cheap-path acknowledgement ("rising is the
// fix working"), and a fleet of firewalled-UDP cameras cycling through TCP fallbacks must not read
// as good news. A transport switch is a durable, session-scoped decision worth counting as itself.
#define TX_CODE_TCP_FALLBACK "TX-TCP-FALLBACK"

// ---- positive acknowledgements ----
// Not a fault, but it belongs in the same namespace for the same reason: it is the ONE line that
// proves the upstream stream is actually live, so it must be greppable as a token rather than as
// prose, and it must survive the log-routing split below.
#define TX_CODE_LIVE    "TX-LIVE"    // PLAY succeeded -> upstream stream is confirmed flowing

// Log routing: is this line important enough for the systemd journal?
//
// Every line the library emits used to go to stderr, so a verbose run buried the handful of lines an
// operator needs under everything else. TetherBox worked around that OUTSIDE the process, with
// `2>&1 | tee file | grep -E 'TX-|...'` in the service command -- which put a grep between the proxy
// and its own journal. Anything that did not match the pattern (a crash message, an assertion, a
// libc error) was silently discarded, and the pipeline itself became a component that could fail.
//
// The decision belongs here, next to the codes it is made from: a line beginning with a TX- token is
// journal-worthy (faults AND the TX-LIVE acknowledgement); everything else is verbose detail that a
// caller may redirect to a file or discard. Because it keys off the same codes the tallies use,
// there is exactly one list to keep in step rather than two.
inline bool isJournalWorthyLogLine(char const* lineStart) {
  // A null pointer cannot be classified. Send it to the journal: over-reporting is recoverable, and
  // silently routing an unclassifiable line to a stream that may be /dev/null is not.
  //
  // `!lineStart`, not `== NULL`: this header must stay dependency-free (it is included from the
  // lowest layer, BasicUsageEnvironment, and from liveMedia). NULL is a macro from <cstddef>, which
  // clang happens to pull in transitively on macOS and GCC on the target does not — so `== NULL`
  // compiled locally and broke the build on the box.
  if (!lineStart) return true;
  return lineStart[0] == 'T' && lineStart[1] == 'X' && lineStart[2] == '-';
}

#endif

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
// THE canonical list of reasons a proxy session gets torn down and rebuilt.
//
// Why this file exists
// --------------------
// These strings are a VOCABULARY, not incidental log text. `bin/lib/flap_diag.rb` parses them,
// tallies them, and decides from them whether a fault belongs to the camera or to us. A reason that
// is spelled slightly differently in two places is two faults in every report; a reason invented at
// a call site is a fault the tooling silently cannot see.
//
// Two rules follow, and both were broken before this file existed:
//
//   1. A reason is a CONSTANT FROM HERE. Never a string literal at the call site. A literal is how
//      a near-duplicate ("setup-failed" vs "setup-failure") gets introduced, and nothing catches it.
//
//   2. A reason names WHY THE SESSION IS BEING REBUILT. It is NOT the failure cause.
//      Those are two different vocabularies and they were briefly merged: scheduleReset() was being
//      passed requestFailureCauseName(cause), so `reason=` sometimes held a reset reason
//      ("play-failed") and sometimes a cause ("timeout", "local-resource"). A field whose vocabulary
//      depends on which branch produced it cannot be tallied, and any count built from it is wrong.
//      The cause travels in its own `cause=` field, from RequestRetryPolicy.hh's
//      requestFailureCauseName(). One field, one vocabulary.
//
// Adding a reason: add it HERE, then add it to FlapDiag::RESET_REASONS in the tetherbox repo. The
// parity spec (spec/lib/fault_code_parity_spec.rb) fails if the two lists disagree in either
// direction -- so an unlisted reason breaks the build rather than quietly under-reporting.
//
// Naming convention, so nobody has to invent one: lowercase, hyphen-separated, describing the
// CONDITION DETECTED -- never the function the call happens to sit in. `connect-failed` violated
// that (it fired on a SUCCESSFUL connect) and cost 183 of 217 resets in a production window being
// read as connection failures. See doc/troubleshooting/hard-rules.md rule 6.

#ifndef _RESET_REASONS_HH
#define _RESET_REASONS_HH

// A new RTSP connection replaced the previous one while subsessions were still interleaved on it,
// so those bindings are stale. A legitimate rebuild following a SUCCESSFUL connect -- this is the
// one that used to be called "connect-failed".
#define RESET_REASON_RECONNECT_REPLACED_CONNECTION "reconnect-replaced-connection"

// The keep-alive method we used was refused, and it was OPTIONS -- the last fallback. Every RTSP
// server implements OPTIONS, so a refusal here is genuinely anomalous. (A refusal of
// SET_PARAMETER/GET_PARAMETER is NOT a reset: we demote that method and carry on.)
#define RESET_REASON_KEEPALIVE_OPTIONS_REFUSED "keepalive-options-refused"

// The keep-alive failed in a way that proves the connection is gone (not a timeout, which is
// answered with another keep-alive, and not an error status, which proves the peer is alive).
#define RESET_REASON_KEEPALIVE_CONNECTION_LOST "keepalive-connection-lost"

// The keep-alive failed WITHOUT proof the connection is gone -- a locally-failed send
// (cause=local-resource), or a failure we could not classify. Distinct from
// keepalive-connection-lost BY DESIGN: production showed `reason=keepalive-connection-lost
// cause=local-resource`, i.e. the reason asserting a condition the cause field disclaimed. The
// reason claims only what was measured; the cause= field carries the specifics.
#define RESET_REASON_KEEPALIVE_FAILED "keepalive-failed"

// Consecutive keep-alives got NO REPLY within the response deadline. Deliberately distinct from
// keepalive-connection-lost: silence proves nothing about the connection (the socket may be fine),
// so this reason claims only what was measured -- the camera stopped answering us. One unanswered
// keep-alive is retried in place (GStreamer's ETIMEOUT rule); this reason fires when the retry also
// went unanswered.
#define RESET_REASON_KEEPALIVE_UNANSWERED "keepalive-unanswered"

// The server answered a keep-alive with 454 Session Not Found: the connection is alive and the
// session is ALREADY GONE on the far end (typical producer: a control-connection reconnect
// re-presented a session id the camera expired while the connection was down). Re-sending
// keep-alives against it can never succeed; only a rebuild can.
#define RESET_REASON_SESSION_NOT_FOUND "session-not-found"

// PLAY failed. Deliberately not retried in place -- see continueAfterPLAY.
#define RESET_REASON_PLAY_FAILED "play-failed"

// SETUP failed and there is nothing queued to retry, so there is no request left to re-send.
#define RESET_REASON_SETUP_NOTHING_QUEUED "setup-failed-nothing-queued"

// SETUP kept failing until the retry TIME window expired. Distinct from exhausting the count: this
// one says we spent the budget waiting, which is the bound that actually caps outage time.
#define RESET_REASON_SETUP_RETRY_WINDOW_EXPIRED "setup-retry-window-expired"

// SETUP failed with a cause whose retry budget is spent (or zero). The specific cause travels in the
// `cause=` field -- do NOT encode it here, or this field stops being a fixed vocabulary.
#define RESET_REASON_SETUP_RETRIES_EXHAUSTED "setup-retries-exhausted"

// The connection died during SETUP while the media was INTERLEAVED on it: a replay on a new
// connection cannot rebind the media (the new-connection rule would reset anyway), so the session
// rebuilds directly, under its own name. Before this existed, the doomed replay fired
// `reconnect-replaced-connection` in the same second -- 2304 doubled rebuilds in six hours on
// 6397272975e8c006c6fc (fixture: retry_reconnect_doom_loop_192.168.11.19_2026-07-21.log).
#define RESET_REASON_SETUP_INTERLEAVED_CONNECTION_LOST "setup-interleaved-connection-lost"

// A SETUP was about to be sent but the control connection was already gone. live555's sendRequest
// would transparently reopen a connection and send the SETUP as its FIRST command -- which at least
// Tapo-class cameras silently ignore (no response, FIN at ~10s idle; measured 4x on 192.168.11.19,
// tetherbox fixtures bare_setup_ignored_pcap_dialog_* and cam_keepalive_probe_*, 2026-07-21). The
// same capture shows DESCRIBE+SETUP+PLAY back-to-back on ONE connection succeeding every time, so
// the recoverable move is a rebuild, never the transparent reopen.
#define RESET_REASON_SETUP_NO_CONNECTION "setup-no-connection"

// No RTP arrived for the -D inter-packet-gap interval: the upstream went silent while we believed it
// was PLAYing.
#define RESET_REASON_INTER_PACKET_GAP "inter-packet-gap"

// The teardown classifier PROVED the far end closed (captured FIN/RST), so the session is being
// rebuilt on evidence, not on a timer inference. Distinct from inter-packet-gap BY DESIGN: the
// policy that decides these (SessionRecoveryPolicy.hh) exists to keep the proven-close population
// and the timer-inference population apart, and labelling this reset "inter-packet-gap" re-merged
// in the reason tally exactly what the policy had just separated.
#define RESET_REASON_FAR_END_CLOSE_PROVEN "far-end-close-proven"

// A UDP session completed its handshake and then NEVER delivered a packet: the media path is
// presumed blocked (firewall/NAT -- the handshake runs over TCP and proves nothing about UDP), and
// the rebuild switches the session to RTP-over-TCP. GStreamer parity; see
// SESSION_RECOVERY_RESET_RETRY_TCP in SessionRecoveryPolicy.hh. Distinct from inter-packet-gap,
// which is silence AFTER data flowed: these have different causes and different remedies, and one
// name for both would hide the firewall population inside the flapping-camera population.
#define RESET_REASON_UDP_NO_INITIAL_DATA "udp-no-initial-data"

// Not all subsessions were SETUP within the subsession timer, so the session is incomplete.
#define RESET_REASON_SUBSESSION_TIMEOUT "subsession-timeout"

// The upstream source closed (e.g. RTCP BYE): the server told us the stream ended.
#define RESET_REASON_STREAM_SOURCE_CLOSED "stream-source-closed"

#endif

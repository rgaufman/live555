// RTSP 'liveness' (keep-alive) policy + interleaved-channel budgeting.
//
// Pure functions, no sockets and no liveMedia link, so the policy can be unit-tested directly
// (test/test_rtsp_keepalive.cpp) instead of only being observable by watching a camera drop.
//
// The policy follows GStreamer's rtspsrc, which is the most widely-deployed RTSP client and handles
// more of the real-world camera matrix than live555's original:
//
//   * METHOD (gstrtspsrc.c gst_rtspsrc_send_keep_alive_internal, :6286-6291): prefer SET_PARAMETER, else
//     GET_PARAMETER, else OPTIONS — whichever the server advertised in its "Public:"/"Allow:"
//     header. live555 historically sent only OPTIONS (its GET_PARAMETER path is #ifdef-disabled
//     because GET_PARAMETER crashes some cameras, and it never considered SET_PARAMETER at all).
//     Real observed case: a Merit-Lilin 4K advertises "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY,
//     SET_PARAMETER" — it supports SET_PARAMETER and NOT GET_PARAMETER.
//
//   * TIMING (gstrtspconnection.c gst_rtsp_connection_next_timeout_usec): act BEFORE the session
//     timeout, with a safety margin — 5s early if the timeout is >= 20s, 20% early if >= 5s, 1s
//     early if >= 1s. live555 instead picked a random point in [timeout/2, timeout-1).
//
// We keep live555's randomisation ON TOP of GStreamer's deadline: a box running ~94 proxies that
// all reconnect together would otherwise synchronise every keepalive into one burst. So the delay
// is a random point in [deadline/2, deadline), where `deadline` is GStreamer's margin-adjusted
// value rather than the raw timeout.

#ifndef _RTSP_KEEP_ALIVE_HH
#define _RTSP_KEEP_ALIVE_HH

// Used when the server sent no "; timeout = " parameter with its Session header (live555's
// long-standing default; RFC 2326 sec 12.37 also cites 60s as the default session timeout).
unsigned const RTSP_DEFAULT_SESSION_TIMEOUT_SECS = 60;

// Deadlines are converted to MICROseconds in a 32-bit unsigned, which overflows past ~4294s. A
// server advertising a longer timeout than this gets clamped rather than wrapping to a tiny delay
// (a wrap would make us hammer the server with keepalives).
unsigned const RTSP_MAX_LIVENESS_DEADLINE_SECS = 3600;

enum RtspLivenessMethod {
  RTSP_LIVENESS_SET_PARAMETER,
  RTSP_LIVENESS_GET_PARAMETER,
  RTSP_LIVENESS_OPTIONS
};

// GStreamer's preference order. OPTIONS is the fallback because every RTSP server implements it,
// but some cameras do not treat a bare OPTIONS as session activity, which is why a server that
// advertises a *_PARAMETER method gets that instead.
inline RtspLivenessMethod chooseRtspLivenessMethod(bool serverSupportsSetParameter,
                                                   bool serverSupportsGetParameter) {
  if (serverSupportsSetParameter) return RTSP_LIVENESS_SET_PARAMETER;
  if (serverSupportsGetParameter) return RTSP_LIVENESS_GET_PARAMETER;
  return RTSP_LIVENESS_OPTIONS;
}

// GStreamer's margin rule: how long after the last message we may wait before we MUST have sent a
// keepalive. Always strictly less than the session timeout (except a 0/1s timeout, where there is
// no room for a margin and we send immediately).
inline unsigned rtspLivenessDeadlineSecs(unsigned sessionTimeoutSecs) {
  unsigned t = sessionTimeoutSecs == 0 ? RTSP_DEFAULT_SESSION_TIMEOUT_SECS : sessionTimeoutSecs;
  if (t > RTSP_MAX_LIVENESS_DEADLINE_SECS) t = RTSP_MAX_LIVENESS_DEADLINE_SECS;

  if (t >= 20) return t - 5;
  if (t >= 5)  return t - t/5;
  if (t >= 1)  return t - 1;
  return 0;
}

// Before the first SETUP's RESPONSE there is no RTSP session, so the advertised session timeout
// governs nothing; what the keep-alive must outrun in the DESCRIBE -> first-SETUP gap is the
// camera's CONNECTION idle timeout. Measured on 192.168.11.19 (2026-07-21, tetherbox fixtures
// bare_setup_ignored_pcap_dialog_* and cam_keepalive_probe_*): the camera FINs an idle control
// connection ~10s after the last request, while the session-timeout-derived first keep-alive would
// land at 27-55s -- always too late, so every in-process rebuild lost its connection before the
// first downstream client could trigger SETUP. The probe verified that OPTIONS at a 5s cadence
// holds the connection open (>=25s observed) and a SETUP on that same connection then succeeds.
//
// The floor is a MEASURED per-vendor minimum, not a spec value: nothing obliges a camera to keep an
// idle pre-session connection at all, so the deadline sits at half the worst floor seen.
unsigned const RTSP_PRE_SESSION_IDLE_FIN_FLOOR_SECS = 10;
inline unsigned rtspPreSessionLivenessDeadlineSecs() {
  return RTSP_PRE_SESSION_IDLE_FIN_FLOOR_SECS/2;
}

// Delay before the next liveness command, in microseconds: a random point in [deadline/2, deadline).
// `randomValue` is supplied by the caller (our_random()) so this stays pure and testable.
// `sessionEstablished` selects which deadline applies -- the session-timeout margin once a session
// id exists, the connection-idle floor above before one does. No default: every call site must say
// which phase it is in.
//
// NOTE the historical bug this replaces. live555 computed the jitter as
//     us_1stPart + (us_2ndPart*our_random())%us_2ndPart
// but (a*r) % a is identically 0 in exact arithmetic — the randomisation only produced anything at
// all via 32-bit overflow, and what it produced was heavily biased rather than uniform. The correct
// form is `randomValue % span`.
inline unsigned rtspLivenessDelayUs(unsigned sessionTimeoutSecs, unsigned randomValue,
                                    bool sessionEstablished) {
  unsigned const deadlineSecs = sessionEstablished
    ? rtspLivenessDeadlineSecs(sessionTimeoutSecs)
    : rtspPreSessionLivenessDeadlineSecs();
  if (deadlineSecs == 0) return 0; // no room to wait — send now

  // The jitter span [deadline/2, deadline) has width deadline/2 == halfUs, and halfUs >= 500000
  // here (deadlineSecs == 0 already returned above), so the modulus is never by zero.
  unsigned const halfUs = deadlineSecs*500000; // deadline/2, in microseconds
  return halfUs + randomValue%halfUs;
}

// How many CONSECUTIVE unanswered keep-alives (each having waited out its full response deadline)
// before the session rebuilds. ONE silence is answered with another keep-alive -- GStreamer's
// ETIMEOUT rule: silence is not evidence of anything -- and the SECOND consecutive silence
// escalates: the camera has now ignored us for 2x the deadline, and with no downstream client
// attached nothing else would ever rebuild the stream. A pure predicate rather than an inline
// `>= 2` at the call site, because flipping that literal to `>= 1` (instant reset on one slow
// reply -- the exact bug class this campaign fixed) would otherwise pass every suite.
unsigned const RTSP_LIVENESS_SILENCES_BEFORE_RESET = 2;
inline bool livenessSilenceEscalates(unsigned consecutiveSilences) {
  return consecutiveSilences >= RTSP_LIVENESS_SILENCES_BEFORE_RESET;
}

// A lost CONTROL connection is not a lost SESSION when the media is UDP.
//
// RFC 2326 sec 1.4: an RTSP session outlives the TCP connection that carries its requests -- and
// some cameras drop an idle control connection by design while UDP media keeps flowing. GStreamer
// treats exactly this as routine: on GST_RTSP_EEOF in the UDP loop it warns, reconnects the
// control connection, and CONTINUES the same session (gstrtspsrc.c:6914-6926, with
// DEFAULT_UDP_RECONNECT TRUE at :325; verified against current main 2026-07-21). Rebuilding
// instead throws away the DESCRIBE and the fan-out clients over a media path that never stopped.
//
// Bounded to ONE in-place retry per incident (GStreamer's `try == 0` shape): the next keep-alive
// re-opens the connection lazily and re-presents the same session id; if THAT also fails, the
// camera really is gone and the session must be rebuilt. Never for TCP-interleaved media -- there
// the media rode the dead connection, and only a rebuild can rebind it.
inline bool keepAliveLossReconnectsInPlace(bool mediaOverTCP, bool alreadyRetriedThisIncident) {
  return !mediaOverTCP && !alreadyRetriedThisIncident;
}

// RFC 2326 sec 10.12: the interleaved channel identifier is a SINGLE BYTE, so only 0..255 exist.
unsigned const RTSP_MAX_INTERLEAVED_CHANNEL = 255;

// True if allocating `numChannelsNeeded` more channels starting at `nextChannelId` would run past
// the one-byte channel space.
//
// This matters because live555's RTSPClient allocates channel ids from a monotonically increasing
// `unsigned char fTCPStreamIdCount` (+2 per SETUP) that reset() did NOT clear. A proxy that
// reconnects to a camera all day therefore marches the channel id upward — a 2-track camera
// reconnecting every ~2 minutes reaches channel 96 after 24 reconnects (observed in production) and
// WRAPS PAST 255 back to 0 after 64. A wrapped id can then collide with a channel still registered
// from an earlier session, which corrupts the '$'-framing demux. The fix is to reset the counter per
// connection (channel ids are scoped to one TCP connection, so a reconnect must restart at 0); this
// predicate is the backstop that detects the exhaustion case within a single connection.
inline bool rtspInterleavedChannelsExhausted(unsigned nextChannelId, unsigned numChannelsNeeded) {
  return nextChannelId + numChannelsNeeded > RTSP_MAX_INTERLEAVED_CHANNEL + 1;
}

#endif

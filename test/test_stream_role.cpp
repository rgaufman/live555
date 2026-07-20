// Unit tests for StreamRole.hh -- deciding whether a torn-down TCP socket was
// the UPSTREAM camera link or a DOWNSTREAM consumer of our fan-out.
//
// WHY THIS EXISTS (measured, not hypothetical):
//
// On box 6397272975e8c006c6fc, teardown lines in one 60-minute window broke
// down by peer address as:
//
//     166  127.0.0.1        <- our own recorder/detector reconnecting
//      20  192.168.11.52    <- the worst ACTUAL camera
//      14  192.168.11.66
//      14  192.168.11.55
//      ...
//
// Loopback consumers outnumbered every camera by ~8x, yet every one of those
// lines was emitted as TX-FIN / TX-RST with the words "Far-end close PROVEN",
// i.e. our own recorder restarting was logged as a PROVEN camera fault.
//
// That is not a cosmetic wording issue. bin/check_flap tallies those exact
// codes, and on this box it concluded "REAL upstream stalls (camera/network/
// NIC)" and told the operator to "FIX the camera link, NOT the watchdog" --
// a verdict computed from a population dominated by our own clients. A tool
// that cannot see the difference between "the camera dropped us" and "our
// recorder restarted" will keep blaming the customer's network for our
// behaviour.
//
// Hence the rule these tests encode: a teardown may only blame the far end
// when we KNOW the far end is the camera. An unknown role must never quietly
// inherit the camera-blaming wording.

#include "StreamRole.hh"
#include "check.hh"

#include <stdio.h>
#include <string.h>

static void checkStr(char const* got, char const* want, char const* what) {
  if (got == NULL || want == NULL || strcmp(got, want) != 0) {
    fprintf(stderr, "FAIL: %s (got \"%s\", want \"%s\")\n",
            what, got ? got : "(null)", want ? want : "(null)");
    ++g_checkFailures;
  }
}

// --- role classification from the socket's owner ---------------------------

static void testRoleFromOwner() {
  // The proxy RECEIVES from the camera, so an RTP source is the upstream link.
  check(streamRoleForOwner(/*isSource*/ True, /*isSink*/ False) == STREAM_ROLE_UPSTREAM,
        "an RTP source (we receive) is the UPSTREAM camera link");

  // The proxy SENDS to consumers, so an RTP sink is a downstream client.
  check(streamRoleForOwner(/*isSource*/ False, /*isSink*/ True) == STREAM_ROLE_DOWNSTREAM,
        "an RTP sink (we send) is a DOWNSTREAM consumer");

  // Neither, or both, is not something we can reason about. It must be
  // explicit, NOT silently folded into "upstream" -- that default is exactly
  // what produced 166 mislabelled loopback teardowns.
  check(streamRoleForOwner(False, False) == STREAM_ROLE_UNKNOWN,
        "an owner that is neither source nor sink is UNKNOWN, not upstream");
  check(streamRoleForOwner(True, True) == STREAM_ROLE_UNKNOWN,
        "an owner that is both source and sink is UNKNOWN, not upstream");
}

// --- which fault code a teardown may carry ---------------------------------

static void testCodeForRole() {
  // Upstream keeps the existing camera-side vocabulary unchanged, so existing
  // greps and dashboards for real camera faults keep working.
  checkStr(teardownCodeForRole(STREAM_ROLE_UPSTREAM, TX_CODE_FIN), TX_CODE_FIN,
           "upstream FIN stays TX-FIN");
  checkStr(teardownCodeForRole(STREAM_ROLE_UPSTREAM, TX_CODE_RST), TX_CODE_RST,
           "upstream RST stays TX-RST");

  // A downstream consumer going away is NORMAL -- the recorder restarts, a
  // viewer closes a tab. It must not share a code with a camera fault, or it
  // will be counted as one.
  checkStr(teardownCodeForRole(STREAM_ROLE_DOWNSTREAM, TX_CODE_FIN), TX_CODE_CLIENT_GONE,
           "downstream FIN is a client leaving, not a camera fault");
  checkStr(teardownCodeForRole(STREAM_ROLE_DOWNSTREAM, TX_CODE_RST), TX_CODE_CLIENT_GONE,
           "downstream RST is a client leaving, not a camera fault");
  checkStr(teardownCodeForRole(STREAM_ROLE_DOWNSTREAM, TX_CODE_STALL), TX_CODE_CLIENT_GONE,
           "downstream stall is still a client-side event");

  // Unknown gets its own code so it is COUNTABLE and visible, rather than
  // inflating either population.
  checkStr(teardownCodeForRole(STREAM_ROLE_UNKNOWN, TX_CODE_FIN), TX_CODE_ROLE_UNKNOWN,
           "unknown role gets its own code, never the camera's");
}

// --- who may be blamed ------------------------------------------------------

static void testBlameRules() {
  check(teardownMayBlamePeer(STREAM_ROLE_UPSTREAM),
        "only an upstream teardown may say the far end closed");
  check(!teardownMayBlamePeer(STREAM_ROLE_DOWNSTREAM),
        "a downstream teardown must NOT say 'Far-end close PROVEN' -- the far "
        "end is our own recorder");
  check(!teardownMayBlamePeer(STREAM_ROLE_UNKNOWN),
        "an unknown role must not assign blame");
}

// --- reconnect semantics ----------------------------------------------------

static void testReconnectRules() {
  // Losing the camera means we must go get it back.
  check(teardownShouldReconnect(STREAM_ROLE_UPSTREAM),
        "an upstream teardown must trigger a reconnect");

  // A consumer leaving must NOT drag the upstream session down -- the other
  // consumers are still watching, and the camera never went anywhere.
  check(!teardownShouldReconnect(STREAM_ROLE_DOWNSTREAM),
        "a downstream teardown must not reconnect the upstream stream");
}

static void testRoleNames() {
  // Names are used in log prose; an unnamed enum value would print as a number
  // and be unsearchable.
  checkStr(streamRoleName(STREAM_ROLE_UPSTREAM), "upstream", "upstream name");
  checkStr(streamRoleName(STREAM_ROLE_DOWNSTREAM), "downstream", "downstream name");
  checkStr(streamRoleName(STREAM_ROLE_UNKNOWN), "unknown", "unknown name");
}

int main() {
  testRoleFromOwner();
  testCodeForRole();
  testBlameRules();
  testReconnectRules();
  testRoleNames();

  return checkSummary("test_stream_role");
}

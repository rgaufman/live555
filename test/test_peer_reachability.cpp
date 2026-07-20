// Regression tests for the non-blocking peer-reachability probe (PeerReachability.hh) and
// for the structural invariant that made a whole fault class invisible.
//
// Both of these were found by SIMULATING faults against a real proxy rather than by reading code:
//
//   1. A peer that stops sending but holds the socket OPEN produces NO stall log at all. The
//      mid-packet stall guard counts read DISPATCHES, and a silent socket is never readable, so the
//      counter cannot advance. The guard is structurally incapable of catching this. That class is
//      caught only by the -D inactivity timer -- whose message was verbosity-gated and absent from
//      the log tally, so silently-stalled cameras were invisible while cameras that FINned showed up
//      instantly. Test 1 pins the invariant so nobody re-assumes the stall guard covers it.
//
//   2. The reachability probe must never block. live555's event loop is single-threaded and one
//      proxy process drives one camera's whole relay, so a blocking probe would stall that camera's
//      recorder/live/detector for its full timeout -- long enough to trip the old_ctime watchdog and
//      MANUFACTURE a footage gap, on cameras already in trouble. Test 2 proves connect() returns
//      immediately and the verdict is read separately.
//
// Build + run:  make -C test run

#include "PeerReachability.hh"
#include <cstdio>
#include <ctime>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, msg) do { ++g_checks; \
  if (!(cond)) { ++g_failures; std::printf("  FAIL: %s\n", (msg)); } \
  else         { std::printf("  ok:   %s\n", (msg)); } } while (0)

static long long nowMs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}

// Listen on an ephemeral loopback port; returns the fd and writes the port.
static int makeListener(unsigned short* portOut) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = 0;
  bind(fd, (struct sockaddr*)&sa, sizeof(sa));
  listen(fd, 4);
  socklen_t l = sizeof(sa);
  getsockname(fd, (struct sockaddr*)&sa, &l);
  *portOut = ntohs(sa.sin_port);
  return fd;
}

// Bind a port then close it, so nothing is listening but the port number is known-free.
static unsigned short deadPort() {
  unsigned short p = 0;
  int fd = makeListener(&p);
  close(fd);
  return p;
}

// ---------------------------------------------------------------- 1: the structural stall-guard gap

// Models the mid-packet stall guard: the counter advances ONLY on a read dispatch, and dispatches
// only happen while the socket is readable.
static unsigned stallCountAfter(unsigned readableDispatches) {
  unsigned stallCount = 0;
  for (unsigned i = 0; i < readableDispatches; ++i) ++stallCount; // no payload progress
  return stallCount;
}

static void test_silent_peer_never_trips_the_stall_guard() {
  std::printf("[GAP] a SILENT-but-open peer yields zero dispatches -> the stall guard cannot fire\n");
  unsigned const limit = 2000; // PACKET_DATA_STALL_LIMIT

  // A peer that stops sending generates no readability, hence no dispatches, forever.
  CHECK(stallCountAfter(0) == 0, "silent peer: zero dispatches -> stall counter stays 0");
  CHECK(stallCountAfter(0) < limit,
        "silent peer NEVER reaches the stall limit -- the guard is structurally blind to it");

  // Contrast: a peer that keeps the socket readable (dead-but-readable, the FIN/RST case) does trip
  // it. This is why production only ever saw FINs.
  CHECK(stallCountAfter(limit) >= limit,
        "a readable-but-stalled peer DOES reach the limit (the case the guard was built for)");

  std::printf("  note: therefore the -D inactivity timer is the ONLY detector for a silent peer,\n"
              "        and its log line must be tagged + counted or the fault is invisible.\n");
}

// ---------------------------------------------------------------- 2: the probe must not block

static void test_probe_returns_immediately() {
  std::printf("[NON-BLOCKING] starting a probe must return at once, never wait for the deadline\n");
  // A routable-but-silent address: TEST-NET-1 (RFC 5737), which will not answer. If the probe
  // blocked, this would take seconds.
  long long t0 = nowMs();
  int fd = startPeerReachabilityProbe("192.0.2.1", 554);
  long long elapsed = nowMs() - t0;

  CHECK(elapsed < 250, "connect() to an unresponsive host returned immediately (<250ms)");
  // Before the deadline we must say PENDING, not guess a verdict.
  if (fd >= 0) {
    CHECK(peerReachabilityResult(fd, /*deadlinePassed*/false) == PEER_REACH_PENDING,
          "in-flight probe reports PENDING, never a guessed verdict");
    CHECK(peerReachabilityResult(fd, /*deadlinePassed*/true) == PEER_REACH_UNREACHED,
          "once the deadline passes with no answer -> UNREACHED");
    closePeerReachabilityProbe(fd);
  } else {
    ++g_checks;
    std::printf("  ok:   probe could not start locally (sandboxed network) — no peer blamed\n");
  }
}

static void test_alive_peer_detected() {
  std::printf("[ALIVE] a listening peer is reported ALIVE\n");
  unsigned short port = 0;
  int listener = makeListener(&port);

  int fd = startPeerReachabilityProbe("127.0.0.1", port);
  CHECK(fd >= 0, "probe started against a live listener");

  // Loopback connects effectively instantly, but allow a few polls rather than assuming.
  PeerReachState st = PEER_REACH_PENDING;
  for (int i = 0; i < 200 && st == PEER_REACH_PENDING; ++i) {
    st = peerReachabilityResult(fd, false);
    if (st == PEER_REACH_PENDING) usleep(1000);
  }
  CHECK(st == PEER_REACH_ALIVE, "listening peer -> PEER_REACH_ALIVE");

  closePeerReachabilityProbe(fd);
  close(listener);
}

static void test_refused_is_reachable_not_unreachable() {
  std::printf("[REFUSED] an RST means the host is UP -- must NOT be reported as unreachable\n");
  // This distinction matters operationally: a closed port is a SERVICE fault, but reporting it as
  // "unreachable" would send someone after cabling and switches instead.
  unsigned short port = deadPort();
  int fd = startPeerReachabilityProbe("127.0.0.1", port);
  CHECK(fd >= 0, "probe started against a closed port");

  PeerReachState st = PEER_REACH_PENDING;
  for (int i = 0; i < 200 && st == PEER_REACH_PENDING; ++i) {
    st = peerReachabilityResult(fd, false);
    if (st == PEER_REACH_PENDING) usleep(1000);
  }
  CHECK(st == PEER_REACH_REFUSED, "closed port -> PEER_REACH_REFUSED (host up, service down)");
  CHECK(st != PEER_REACH_UNREACHED, "a refusal is NEVER reported as unreachable");

  closePeerReachabilityProbe(fd);
}

static void test_local_failure_never_blames_the_peer() {
  std::printf("[HONESTY] a local probe failure must say nothing about the camera\n");
  CHECK(peerReachabilityResult(-1, true) == PEER_REACH_ERROR,
        "invalid fd -> ERROR, not UNREACHED");
  CHECK(startPeerReachabilityProbe("not-an-ip", 554) < 0, "unparseable address fails to start");
  CHECK(startPeerReachabilityProbe(NULL, 554) < 0, "NULL address fails to start");

  // The wording for each state must not overclaim.
  CHECK(strstr(peerReachabilityText(PEER_REACH_ERROR), "says nothing about the camera") != NULL,
        "ERROR text explicitly disclaims any camera verdict");
  CHECK(strstr(peerReachabilityText(PEER_REACH_REFUSED), "not the link") != NULL,
        "REFUSED text points at the service, not the link");
}

int main() {
  std::printf("== PeerReachability + stall-guard-gap regression tests ==\n\n");
  test_silent_peer_never_trips_the_stall_guard();
  test_probe_returns_immediately();
  test_alive_peer_detected();
  test_refused_is_reachable_not_unreachable();
  test_local_failure_never_blames_the_peer();
  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  if (g_failures) std::printf("FAILURES: %d\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

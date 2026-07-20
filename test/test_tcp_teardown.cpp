// Regression tests for the RTP-over-TCP teardown classifier (RTPTcpTeardown.hh).
//
// These prove, against REAL loopback TCP sockets (not mocks), that the proxy can state WHO tore a
// stream down: a camera FIN, a camera RST, or mid-path packet loss (still-open-but-silent). The bug
// they lock in: the old classifier ran getpeername() only at teardown time, by which point the
// socket had walked to TCP_CLOSE and every teardown logged "UNPROVEN". The fix captures a
// non-consuming MSG_PEEK at the FIRST stall; these tests assert that captured evidence is
// authoritative even when the live fd is already dead.
//
// Build + run:  make -C test            (or: make -C test run)
// No gtest dependency — a tiny self-contained assert harness keeps it buildable anywhere g++ is.

#include "RTPTcpTeardown.hh"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do {                                                     \
    ++g_checks;                                                                    \
    if (!(cond)) { ++g_failures; std::printf("  FAIL: %s\n", (msg)); }             \
    else         { std::printf("  ok:   %s\n", (msg)); }                           \
  } while (0)

// A connected loopback TCP pair: `client` is the read side we probe (stands in for the proxy's
// upstream read socket); `server` is the far end (stands in for the camera).
struct TcpPair { int client; int server; };

static TcpPair makePair() {
  int lsn = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a; memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  if (::bind(lsn, (sockaddr*)&a, sizeof(a)) != 0) { perror("bind"); exit(2); }
  if (::listen(lsn, 1) != 0) { perror("listen"); exit(2); }
  socklen_t al = sizeof(a);
  if (::getsockname(lsn, (sockaddr*)&a, &al) != 0) { perror("getsockname"); exit(2); }

  int client = ::socket(AF_INET, SOCK_STREAM, 0);
  if (::connect(client, (sockaddr*)&a, al) != 0) { perror("connect"); exit(2); }
  int server = ::accept(lsn, NULL, NULL);
  if (server < 0) { perror("accept"); exit(2); }
  ::close(lsn);
  return TcpPair{client, server};
}

// Wait until `fd` is readable (POLLIN fires on data AND on EOF/FIN; POLLERR/POLLHUP on RST), so the
// probe runs after the kernel has processed the far-end event — deterministic, no sleeps.
static void waitReadable(int fd, int ms) {
  pollfd p; p.fd = fd; p.events = POLLIN; p.revents = 0;
  ::poll(&p, 1, ms);
}

static void test_fin_is_proven_camera_close() {
  std::printf("[FIN] camera closes gracefully (no data, then close) -> PROVEN far-end close\n");
  TcpPair t = makePair();
  ::close(t.server);            // graceful close -> FIN
  waitReadable(t.client, 1000);

  int peek = probeTcpClose(t.client);
  CHECK(peek == TCP_PEEK_FIN, "probeTcpClose returns TCP_PEEK_FIN on a graceful far-end close");

  char who[768]; Boolean farEndClosed = False;
  classifyTcpTeardown(t.client, who, sizeof(who), farEndClosed, peek);
  CHECK(farEndClosed == True, "classify marks farEndClosed=True for a FIN");
  CHECK(strstr(who, "FIN") != NULL && strstr(who, "CONFIRMED") != NULL,
        "message states FIN + CONFIRMED");
  ::close(t.client);
}

static void test_rst_is_proven_camera_close() {
  std::printf("[RST] camera aborts (SO_LINGER 0 then close) -> PROVEN far-end close\n");
  TcpPair t = makePair();
  struct linger lg; lg.l_onoff = 1; lg.l_linger = 0; // close() now sends RST, not FIN
  ::setsockopt(t.server, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
  ::close(t.server);
  waitReadable(t.client, 1000);

  int peek = probeTcpClose(t.client);
  CHECK(peek == TCP_PEEK_RST, "probeTcpClose returns TCP_PEEK_RST on a far-end reset");

  char who[768]; Boolean farEndClosed = False;
  classifyTcpTeardown(t.client, who, sizeof(who), farEndClosed, peek);
  CHECK(farEndClosed == True, "classify marks farEndClosed=True for a RST");
  CHECK(strstr(who, "RST") != NULL && strstr(who, "CONFIRMED") != NULL,
        "message states RST + CONFIRMED");
  ::close(t.client);
}

static void test_silent_open_is_not_a_camera_close() {
  std::printf("[SILENT] socket open, camera sends nothing -> NOT a proven close (mid-path loss)\n");
  TcpPair t = makePair();  // both ends open, no data sent

  int peek = probeTcpClose(t.client);
  CHECK(peek == TCP_PEEK_WOULDBLOCK, "probeTcpClose returns TCP_PEEK_WOULDBLOCK on an idle-open socket");

  char who[768]; Boolean farEndClosed = True; // deliberately seed True to prove classify clears it
  classifyTcpTeardown(t.client, who, sizeof(who), farEndClosed, peek);
  CHECK(farEndClosed == False, "classify does NOT blame the camera for a still-open silent socket");
  CHECK(strstr(who, "NOT a") != NULL, "message says NOT a proven camera close");
  ::close(t.client); ::close(t.server);
}

static void test_data_pending_is_not_a_close() {
  std::printf("[DATA] camera has data pending -> not a close\n");
  TcpPair t = makePair();
  const char* payload = "X";
  if (::send(t.server, payload, 1, 0) != 1) { perror("send"); exit(2); }
  waitReadable(t.client, 1000);

  int peek = probeTcpClose(t.client);
  CHECK(peek == TCP_PEEK_DATA, "probeTcpClose returns TCP_PEEK_DATA when a byte is pending (non-consuming)");
  // MSG_PEEK must not consume: a real read still sees the byte.
  unsigned char b = 0; ssize_t r = ::recv(t.client, &b, 1, 0);
  CHECK(r == 1 && b == 'X', "MSG_PEEK left the byte in the queue (real recv still gets it)");
  ::close(t.client); ::close(t.server);
}

static void test_captured_peek_beats_dead_live_state() {
  std::printf("[AUTHORITATIVE] captured FIN/RST peek is PROVEN even when the live fd is dead (-1)\n");
  // This is the whole point of the fix: at teardown time the real fd is often already CLOSE/invalid,
  // so live getsockopt/getpeername give nothing. The peek captured earlier must still win.
  char who[768]; Boolean farEndClosed = False;

  classifyTcpTeardown(-1, who, sizeof(who), farEndClosed, TCP_PEEK_FIN);
  CHECK(farEndClosed == True && strstr(who, "CONFIRMED") != NULL,
        "dead fd + captured FIN peek -> still a CONFIRMED far-end close");

  farEndClosed = False;
  classifyTcpTeardown(-1, who, sizeof(who), farEndClosed, TCP_PEEK_RST);
  CHECK(farEndClosed == True && strstr(who, "CONFIRMED") != NULL,
        "dead fd + captured RST peek -> still a CONFIRMED far-end close");

  // And with NO captured evidence on a dead fd, it must honestly say UNPROVEN (never guess a camera fault).
  farEndClosed = True;
  classifyTcpTeardown(-1, who, sizeof(who), farEndClosed, TCP_PEEK_NONE);
  CHECK(farEndClosed == False && strstr(who, "UNPROVEN") != NULL,
        "dead fd + no peek -> UNPROVEN, camera NOT blamed");
}

// ---------------------------------------------------------------- never blame without certainty

// The blast radius of getting this wrong is real: a false "the camera closed it" sends an engineer
// (or a customer) after cabling, switches and vendor firmware for a fault that was ours. These tests
// exist so that can only ever be said when the kernel PROVED it.

static void test_farEndClosed_only_ever_true_for_fin_or_rst() {
  std::printf("[CERTAINTY] farEndClosed is True for FIN/RST and NOTHING else\n");
  // Exhaustive over every TcpClosePeek value, on a dead fd so no live state can rescue a verdict.
  int const peeks[] = {TCP_PEEK_NONE, TCP_PEEK_WOULDBLOCK, TCP_PEEK_FIN, TCP_PEEK_RST, TCP_PEEK_DATA};
  for (unsigned i = 0; i < sizeof(peeks)/sizeof(peeks[0]); ++i) {
    char who[768]; Boolean farEndClosed = False;
    classifyTcpTeardown(-1, who, sizeof(who), farEndClosed, peeks[i]);
    Boolean const shouldBlame = (peeks[i] == TCP_PEEK_FIN || peeks[i] == TCP_PEEK_RST) ? True : False;
    if (farEndClosed != shouldBlame) {
      std::printf("  FAIL: peek=%d -> farEndClosed=%d, expected %d\n",
                  peeks[i], (int)farEndClosed, (int)shouldBlame);
      ++g_failures;
    }
    ++g_checks;
  }
  std::printf("  ok:   all 5 peek states blame the far end ONLY on proven FIN/RST\n");
}

static void test_never_says_camera() {
  std::printf("[CERTAINTY] the message must never hardcode \"camera\" -- this path also serves\n"
              "            DOWNSTREAM consumer sockets, where blaming the camera is a lie\n");
  // A SocketDescriptor is used for the upstream camera connection AND for local consumers (recorder,
  // live, detector). Nothing in the socket state identifies the role, so the wording must stay
  // role-neutral ("far end"/"peer") and let the printed peer ADDRESS do the attribution.
  int const peeks[] = {TCP_PEEK_NONE, TCP_PEEK_WOULDBLOCK, TCP_PEEK_FIN, TCP_PEEK_RST, TCP_PEEK_DATA};
  for (unsigned i = 0; i < sizeof(peeks)/sizeof(peeks[0]); ++i) {
    char who[768]; Boolean farEndClosed = False;
    classifyTcpTeardown(-1, who, sizeof(who), farEndClosed, peeks[i]);
    // The rule is NOT "never say the word camera" — it is "never ASSERT the camera did it". Naming
    // the camera is fine, and in fact necessary, when the sentence is explicitly saying that its
    // involvement is NOT confirmed. What must never happen is a bare claim of camera fault.
    Boolean const mentionsCamera = (strstr(who, "camera") != NULL || strstr(who, "Camera") != NULL)
                                   ? True : False;
    Boolean const disclaims = (strstr(who, "NOT CONFIRMED") != NULL) ? True : False;
    if (mentionsCamera && !disclaims) {
      std::printf("  FAIL: peek=%d asserts a camera fault with no NOT-CONFIRMED caveat: %s\n",
                  peeks[i], who);
      ++g_failures;
    }
    ++g_checks;
  }
  std::printf("  ok:   no peek state ASSERTS a camera fault (any mention is inside a NOT CONFIRMED clause)\n");
}

static void test_silent_socket_never_blamed_live() {
  std::printf("[CERTAINTY] a LIVE, open, silent socket is never a proven close\n");
  TcpPair t = makePair();
  // Server is alive and simply sending nothing -- the mid-path-loss case. Must NOT blame the peer,
  // no matter which side we inspect.
  char who[288]; Boolean farEndClosed = True;
  classifyTcpTeardown(t.client, who, sizeof(who), farEndClosed, TCP_PEEK_WOULDBLOCK);
  CHECK(farEndClosed == False, "live open+silent socket -> far end NOT blamed");
  CHECK(strstr(who, "NOT a proven") != NULL, "message explicitly says NOT proven");

  // Same socket, but with no captured evidence at all: still must not blame anyone.
  farEndClosed = True;
  classifyTcpTeardown(t.client, who, sizeof(who), farEndClosed, TCP_PEEK_NONE);
  CHECK(farEndClosed == False, "live socket with no captured peek -> far end NOT blamed");
  ::close(t.client); ::close(t.server);
}

static void test_proven_messages_name_the_peer() {
  std::printf("[CERTAINTY] a PROVEN close names the peer, so the reader can tell camera from consumer\n");
  TcpPair t = makePair();
  ::close(t.server); // graceful FIN from the far end
  waitReadable(t.client, 1000);
  int const peek = probeTcpClose(t.client);

  char who[288]; Boolean farEndClosed = False;
  classifyTcpTeardown(t.client, who, sizeof(who), farEndClosed, peek);
  CHECK(farEndClosed == True, "loopback FIN is a proven far-end close");
  // Loopback peer is 127.0.0.1 -- exactly the case that would be MISREAD as a camera fault if the
  // message said "camera". It must instead state the address.
  CHECK(strstr(who, "127.0.0.1") != NULL,
        "message states the actual peer address (127.0.0.1 here, not \"the camera\")");
  ::close(t.client);
}

int main() {
  std::printf("== RTPTcpTeardown regression tests (real loopback TCP) ==\n\n");
  test_fin_is_proven_camera_close();
  test_rst_is_proven_camera_close();
  test_silent_open_is_not_a_camera_close();
  test_data_pending_is_not_a_close();
  test_captured_peek_beats_dead_live_state();
  test_farEndClosed_only_ever_true_for_fin_or_rst();
  test_never_says_camera();
  test_silent_socket_never_blamed_live();
  test_proven_messages_name_the_peer();
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

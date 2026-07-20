// Non-blocking peer-reachability probe, used to answer "is the camera still there?" AT THE MOMENT a
// stream fault is classified.
//
// WHY AT THE MOMENT: reachability checked afterwards is a different measurement. By the time an
// external diagnostic or a periodic ping runs, a camera that rebooted is answering again, and one that
// closed one stream
// while staying perfectly reachable looks identical to one that vanished. The distinction we want —
// "dropped off the network" vs "still up, closed the stream on purpose" — only exists at the instant
// of the fault.
//
// WHY NON-BLOCKING, NON-NEGOTIABLE: live555's event loop is single-threaded, and one proxy process
// drives one camera's entire relay to every downstream consumer. A blocking probe stalls that relay
// for its whole timeout. At 2s that is comfortably enough to make a downstream recorder's HLS
// playlist go stale and trip a staleness watchdog into a restart — i.e. a blocking "diagnostic" would
// MANUFACTURE the footage gap it was added to explain, and only ever on cameras already in trouble.
// This is the same class of bug the fan-out decoupling removed (no blocking send, ever); do not
// reintroduce it here.
//
// So: connect() on a non-blocking socket returns EINPROGRESS immediately; the caller registers the
// fd for writability and arms a delayed task for the deadline. Writable + SO_ERROR==0 means the
// peer's TCP stack answered. The deadline firing first means it did not.
//
// WHY TCP RATHER THAN ICMP: ICMP needs a raw socket (or net.ipv4.ping_group_range), and plenty of
// networks filter ICMP while RTSP works perfectly — and the reverse, a camera whose kernel answers
// ping while its RTSP server is wedged. A connect() to the RTSP port tests the thing we actually
// care about. It is also strictly cheaper than a ping: one SYN, no privileges.
//
// Header-only and free of liveMedia dependencies so the socket mechanics can be unit-tested against
// real loopback listeners (see test/test_peer_reachability.cpp).

#ifndef _PEER_REACHABILITY_HH
#define _PEER_REACHABILITY_HH

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

// How long we are willing to wait for the peer's TCP stack to answer. Never WAITED on -- this is the
// deadline for a scheduled task, not a timeout passed to a blocking call.
unsigned const PEER_REACH_TIMEOUT_MS = 2000;

enum PeerReachState {
  PEER_REACH_PENDING   = 0, // connect() in flight; no verdict yet
  PEER_REACH_ALIVE     = 1, // completed -> the peer's TCP stack answered
  PEER_REACH_REFUSED   = 2, // RST -> host is UP but nothing is listening on that port
  PEER_REACH_UNREACHED = 3, // no answer / network error -> host did not respond
  PEER_REACH_ERROR     = 4  // we failed locally (socket/fd exhaustion); says nothing about the peer
};

// Start a non-blocking connect. Returns a socket fd the caller must register for WRITABILITY, or -1
// if we could not even start (a local failure -- never blame the peer for it).
//
// A connect() on a non-blocking socket returns -1/EINPROGRESS in the normal case; on loopback it can
// also succeed immediately, which is why the caller must handle an instant verdict too.
static inline int startPeerReachabilityProbe(char const* ipv4, unsigned short port) {
  if (ipv4 == NULL || ipv4[0] == '\0') return -1;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); return -1; }

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, ipv4, &sa.sin_addr) != 1) { close(fd); return -1; }

  if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0) return fd; // immediate (loopback)
  if (errno == EINPROGRESS) return fd;                                // the normal path

  close(fd);
  return -1;
}

// Read the verdict of a probe started above. Safe to call at any time: before the socket is writable
// it reports PENDING rather than guessing.
//
// `deadlinePassed` is supplied by the caller's scheduled task -- this header never consults a clock,
// so the decision stays testable and the caller keeps ownership of the timing.
static inline PeerReachState peerReachabilityResult(int fd, bool deadlinePassed) {
  if (fd < 0) return PEER_REACH_ERROR;

  int soErr = 0;
  socklen_t len = sizeof(soErr);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len) != 0) return PEER_REACH_ERROR;

  if (soErr == 0) {
    // SO_ERROR is also 0 while the connect is still in flight, so "no error yet" is only a verdict
    // once the peer has actually answered. getpeername() succeeding is that proof.
    struct sockaddr_storage pa;
    socklen_t pl = sizeof(pa);
    if (getpeername(fd, (struct sockaddr*)&pa, &pl) == 0) return PEER_REACH_ALIVE;
    return deadlinePassed ? PEER_REACH_UNREACHED : PEER_REACH_PENDING;
  }

  // ECONNREFUSED is a POSITIVE reachability result: the host answered with an RST, so it is on the
  // network and routable -- only that port is closed. Reporting it as "unreachable" would wrongly
  // implicate the network for what is a service-level fault.
  if (soErr == ECONNREFUSED) return PEER_REACH_REFUSED;

  if (soErr == EINPROGRESS || soErr == EALREADY) {
    return deadlinePassed ? PEER_REACH_UNREACHED : PEER_REACH_PENDING;
  }

  // ONLY these mean "the peer did not answer". Everything else is enumerated deliberately rather
  // than swept into UNREACHED: a catch-all would report a LOCAL failure (EINVAL, EAFNOSUPPORT, fd
  // exhaustion...) as "the camera dropped off the network", i.e. blame a camera for our own bug. The
  // whole point of this probe is attribution, so an errno we did not anticipate must be reported as
  // "we don't know", never as a verdict about the peer.
  if (soErr == EHOSTUNREACH || soErr == ENETUNREACH || soErr == ETIMEDOUT ||
      soErr == EHOSTDOWN    || soErr == ENETDOWN) {
    return PEER_REACH_UNREACHED;
  }

  return PEER_REACH_ERROR; // unanticipated: say nothing about the peer
}

// Short, stable phrase for the fault log. Deliberately states what was OBSERVED rather than
// concluding: "still reachable" is evidence the network is fine, not proof the camera is healthy.
static inline char const* peerReachabilityText(PeerReachState s) {
  switch (s) {
    case PEER_REACH_ALIVE:     return "camera still answering TCP at the moment of the fault "
                                      "(on the network -- this was not a link drop)";
    case PEER_REACH_REFUSED:   return "camera answered with RST at the moment of the fault "
                                      "(host UP and routable, RTSP port closed -- service fault, not the link)";
    case PEER_REACH_UNREACHED: return "camera did NOT answer within the probe deadline "
                                      "(consistent with it dropping off the network)";
    case PEER_REACH_PENDING:   return "reachability probe still in flight (no verdict)";
    case PEER_REACH_ERROR:
    default:                   return "reachability probe failed locally (says nothing about the camera)";
  }
}

static inline void closePeerReachabilityProbe(int fd) { if (fd >= 0) close(fd); }

#endif

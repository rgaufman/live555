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
// Pure, header-only helpers to prove WHO tore down an RTP-over-TCP socket
// (camera FIN / camera RST / mid-path loss), from VERIFIED kernel state only. Header-only and free
// of any live555 dependency except Boolean.hh so it can be unit-tested standalone against real
// loopback TCP sockets (see test/test_tcp_teardown.cpp) as well as compiled into RTPInterface.cpp.
//
// WHY THIS EXISTS: the mid-packet-stall teardown used to run getpeername() at the *2000th* stalled
// dispatch, by which point the socket had walked all the way to TCP_CLOSE and getpeername just failed
// ENOTCONN (errno 107) -- so every teardown logged "who closed it is UNPROVEN". The proof (FIN vs RST
// vs still-open) is only reliably available at the MOMENT the close first appears. probeTcpClose() is
// a non-consuming MSG_PEEK that reads that moment; the caller captures it on the FIRST no-progress
// dispatch and feeds it to classifyTcpTeardown() as the primary, authoritative evidence.

#ifndef _RTP_TCP_TEARDOWN_HH
#define _RTP_TCP_TEARDOWN_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif
#include "StreamFaultCodes.hh" // the stable TX_CODE_* tokens these messages are tagged with

#include <errno.h>
#include <stdio.h>
#include <string.h>      // strncmp() — Linux needs this explicitly; macOS pulls it in transitively
#include <stdlib.h>      // atoi() for capturePeerAddress()
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>  // IPPROTO_TCP (Linux needs this explicitly; macOS pulls it in via tcp.h)
#include <netdb.h>       // getnameinfo() + NI_NUMERIC* for describeTcpPeer()
#if defined(__linux__)
#include <netinet/tcp.h> // TCP_INFO / struct tcp_info (Linux only): the fallback state signal
#endif

// A send() failure with one of these errnos is a teardown race (the socket is already gone on one
// side), not a real drop worth logging. Kept in one place so all send sites agree on the set.
static inline Boolean isTcpTeardownErrno(int err) {
  return err == EBADF || err == EPIPE;
}

// Result of a non-consuming read-side probe (probeTcpClose). Ordered by strength of evidence.
enum TcpClosePeek {
  TCP_PEEK_NONE       = -2, // not probed / inconclusive errno
  TCP_PEEK_WOULDBLOCK = -1, // socket OPEN, no data pending (EAGAIN) -> NOT a close; consistent with mid-path loss
  TCP_PEEK_FIN        = 0,  // recv()==0: far end sent FIN (graceful close) -> PROVEN far-end close
  TCP_PEEK_RST        = 1,  // ECONNRESET: far end sent RST (abortive close) -> PROVEN far-end close
  TCP_PEEK_DATA       = 2   // data is actually pending (socket alive) -> NOT closed
};

// Non-consuming probe of a TCP socket's read side: what has the far end done, right now? MSG_PEEK
// leaves any byte in the receive queue, so calling this never disturbs the real read path. Must be
// called AT the moment the stall is first noticed -- captured evidence stays valid even after the
// socket later drops to TCP_CLOSE.
static inline int probeTcpClose(int fd) {
  if (fd < 0) return TCP_PEEK_NONE;
  unsigned char b;
  ssize_t r = recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
  if (r > 0) return TCP_PEEK_DATA;
  if (r == 0) return TCP_PEEK_FIN; // EOF == the far end half-closed (FIN)
  if (errno == ECONNRESET) return TCP_PEEK_RST;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return TCP_PEEK_WOULDBLOCK;
  return TCP_PEEK_NONE; // ENOTCONN/EBADF/etc -- socket already fully torn down; inconclusive
}

// Describe a socket's peer as "ip:port", or "peer unknown" if the socket is already detached.
//
// WHY THIS MATTERS: an RTPInterface SocketDescriptor is used for BOTH ends of a proxy -- the UPSTREAM
// connection to the camera AND the DOWNSTREAM connections to local consuming clients. These messages
// used to say "camera" unconditionally, which is actively misleading on a downstream socket: it
// blames the camera for a local consumer restarting. Nothing in the socket state says which role an
// fd plays, but the peer ADDRESS does, factually and with no plumbing: the camera is the LAN address
// you configured, a local consumer is typically 127.0.0.1. So we state the peer and let the reader
// attribute, rather than asserting a role we did not verify.
static inline void describeTcpPeer(int fd, char* out, unsigned long outLen) {
  struct sockaddr_storage pa; socklen_t pl = sizeof(pa);
  if (fd < 0 || getpeername(fd, (struct sockaddr*)&pa, &pl) != 0) {
    snprintf(out, outLen, "peer unknown");
    return;
  }
  char host[128] = "?", serv[32] = "?";
  if (getnameinfo((struct sockaddr*)&pa, pl, host, sizeof(host), serv, sizeof(serv),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    snprintf(out, outLen, "peer unprintable");
    return;
  }
  snprintf(out, outLen, "peer %s:%s", host, serv);
}

// Capture the peer address in MACHINE-USABLE form (numeric IP + port), for callers that need to act
// on it later — e.g. probing whether that peer is still reachable at the moment of a fault. Kept
// separate from describeTcpPeer() so the human-facing sentence stays free to be reworded without
// breaking anything that depends on the address. Writes an empty string if the socket has no peer.
static inline void capturePeerAddress(int fd, char* ipOut, unsigned long ipLen,
                                      unsigned short& portOut) {
  ipOut[0] = '\0';
  portOut = 0;

  struct sockaddr_storage pa; socklen_t pl = sizeof(pa);
  if (fd < 0 || getpeername(fd, (struct sockaddr*)&pa, &pl) != 0) return;

  char host[128], serv[32];
  if (getnameinfo((struct sockaddr*)&pa, pl, host, sizeof(host), serv, sizeof(serv),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) return;

  snprintf(ipOut, ipLen, "%s", host);
  portOut = (unsigned short)atoi(serv);
}

// Kernel-measured facts about the link, at the instant of the fault. Linux only; on other platforms
// it says so rather than inventing numbers.
//
// WHY: "the connection aborted" means very different things on a clean link versus a lossy one, and
// that difference decides whether anyone should be looking at the network at all. retransmits and
// RTT are recorded by the kernel for this exact connection, so they are measurements, not
// inferences — and they let the reader separate "aborted while the link was perfectly healthy" from
// "aborted after the link had been struggling", without guessing.
static inline void describeTcpLink(int fd, char* out, unsigned long outLen) {
#if defined(__linux__)
  struct tcp_info ti; socklen_t tl = sizeof(ti);
  if (fd >= 0 && getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &tl) == 0) {
    snprintf(out, outLen,
             "link state at that instant [MEASURED]: %u retransmits total, %u lost, rtt %ums, "
             "%ums since the last byte arrived",
             (unsigned)ti.tcpi_total_retrans, (unsigned)ti.tcpi_lost,
             (unsigned)(ti.tcpi_rtt/1000), (unsigned)(ti.tcpi_last_data_recv));
    return;
  }
#else
  (void)fd;
#endif
  snprintf(out, outLen, "link state NOT AVAILABLE on this platform/socket (not measured)");
}

// Classify WHO tore down / stalled a TCP socket, from VERIFIED kernel state only (never a guess).
// `stallPeek` is the TcpClosePeek captured at the FIRST no-progress dispatch (TCP_PEEK_NONE if the
// caller has none); it is the PRIMARY evidence because it was sampled while the distinction still
// existed. Live SO_ERROR / getpeername / (Linux) TCP_INFO are the fallback. Writes a human-readable
// description into `out`, and sets `farEndClosed` True ONLY when there is positive proof the far end
// closed -- a captured FIN/RST, a live RST (SO_ERROR==ECONNRESET), or a live FIN (TCP_INFO
// CLOSE_WAIT). Anything else stays False so the caller never blames the peer without proof.
//
// WHAT THE WORDING ASSERTS, AND WHAT IT DOES NOT: the CLOSE itself is attributed only to "the far
// end"/"the peer", identified by ADDRESS, because this same code path also serves downstream consumer
// sockets -- calling every peer "the camera" would be wrong on half of them (see describeTcpPeer()).
// "camera" appears only inside the NOT-CONFIRMED clause, where the open question is precisely whether
// the camera or an in-path device originated the reset; naming it there states the unknown rather
// than asserting a conclusion.
// `codeOut`, when non-NULL, receives the stable fault code for this classification
// (StreamFaultCodes.hh) so callers can log a token that greps reliably regardless of wording.
// `knownPeer` is a description captured EARLY (while the socket was still connected) and is used when
// the live getpeername() can no longer answer.
//
// WHY: by the time a teardown is classified the socket has usually walked to TCP_CLOSE, so
// getpeername() fails ENOTCONN and the address prints as "peer unknown" — which is precisely the
// case the address was added for (telling an upstream camera apart from a local consumer).
// This is the SAME mistake the MSG_PEEK capture exists to avoid: evidence must be sampled while it
// still exists, not at teardown. Caught against live traffic; the loopback simulation missed it
// because there the socket was still connected at classify time.
static inline void classifyTcpTeardown(int fd, char* out, unsigned long outLen, Boolean& farEndClosed,
                                       int stallPeek = TCP_PEEK_NONE, char const** codeOut = NULL,
                                       char const* knownPeer = NULL) {
  farEndClosed = False;
  if (codeOut != NULL) *codeOut = TX_CODE_STALL; // until proven otherwise, nobody is blamed

  char peer[160]; describeTcpPeer(fd, peer, sizeof(peer));
  if (knownPeer != NULL && knownPeer[0] != '\0' && strncmp(peer, "peer unknown", 12) == 0) {
    snprintf(peer, sizeof(peer), "%s", knownPeer); // fall back to what we captured while connected
  }

  // (1) PRIMARY: the peek captured when the close first appeared. Authoritative when present.
  // Kernel-measured link facts, gathered at the same instant. These are FACTS, not inferences: they
  // let a reader tell an abort on a clean link (0 retransmits) from one on a lossy link, without
  // anybody having to guess which it was.
  char link[128]; describeTcpLink(fd, link, sizeof(link));

  if (stallPeek == TCP_PEEK_RST) {
    farEndClosed = True;
    if (codeOut != NULL) *codeOut = TX_CODE_RST;
    // Deliberately separates CONFIRMED from NOT CONFIRMED. An RST arriving is proven; its ORIGIN is
    // not observable from a socket, because an in-path firewall/IPS can inject a reset that is
    // byte-identical at this layer. Saying "the camera reset it" here would be an assumption, and
    // this whole file exists to stop exactly that.
    snprintf(out, outLen,
             "CONFIRMED: an RST was received on this connection from %s [MSG_PEEK -> ECONNRESET]. "
             "%s. NOT CONFIRMED by this measurement: whether the camera itself sent the RST or a "
             "device in the network path injected it -- a socket cannot see that. To settle it, "
             "packet-capture this peer and compare the RST's IP TTL against its data packets",
             peer, link);
    return;
  }
  if (stallPeek == TCP_PEEK_FIN) {
    farEndClosed = True;
    if (codeOut != NULL) *codeOut = TX_CODE_FIN;
    snprintf(out, outLen,
             "CONFIRMED: a FIN was received on this connection from %s [MSG_PEEK -> EOF], i.e. the "
             "far end closed it gracefully rather than the link dropping. %s. NOT CONFIRMED: whether "
             "the camera or an in-path device originated the close", peer, link);
    return;
  }

  // (2) FALLBACK: live socket state (used when there was no captured peek, or it was inconclusive).
  int soErr = 0; socklen_t el = sizeof(soErr);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&soErr, &el);
  struct sockaddr_storage pa; socklen_t pl = sizeof(pa);
  int gpn = getpeername(fd, (struct sockaddr*)&pa, &pl);
  int gpnErr = errno;
  int tstate = -1; Boolean closeWait = False, established = False;
#if defined(__linux__)
  struct tcp_info ti; socklen_t tl = sizeof(ti);
  if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &tl) == 0) {
    tstate = ti.tcpi_state;
    closeWait = (tstate == TCP_CLOSE_WAIT);
    established = (tstate == TCP_ESTABLISHED);
  }
#endif
  // A peek that said "still open, no data" (WOULDBLOCK) is itself positive evidence of mid-path loss:
  // the socket was alive and silent at the stall, so the far end did NOT close it.
  const char* peekNote = (stallPeek == TCP_PEEK_WOULDBLOCK)
    ? " [read-side probe at the stall: still OPEN, no data -> NOT a far-end close]" : "";

  if (soErr == ECONNRESET) {
    farEndClosed = True;
    if (codeOut != NULL) *codeOut = TX_CODE_RST;
    snprintf(out, outLen, "far end sent RST (%s aborted the connection) [SO_ERROR=ECONNRESET, tcp_state=%d]",
             peer, tstate);
  } else if (closeWait) {
    farEndClosed = True;
    if (codeOut != NULL) *codeOut = TX_CODE_FIN;
    snprintf(out, outLen, "far end sent FIN (%s closed gracefully) [tcp_state=CLOSE_WAIT, SO_ERROR=%d]",
             peer, soErr);
  } else if (stallPeek == TCP_PEEK_WOULDBLOCK) {
    snprintf(out, outLen, "socket to %s still OPEN + no data at the stall -- consistent with mid-path packet "
                          "loss, NOT a proven far-end close%s", peer, peekNote);
  } else if (gpn != 0) {
    snprintf(out, outLen, "socket already disconnected (getpeername FAILED errno=%d) -- who closed it is "
                          "UNPROVEN [SO_ERROR=%d, tcp_state=%d]%s", gpnErr, soErr, tstate, peekNote);
  } else if (established) {
    snprintf(out, outLen, "socket to %s STILL connected+ESTABLISHED but silent, no FIN/RST -- consistent with "
                          "mid-path packet loss, NOT a proven far-end close [SO_ERROR=%d]", peer, soErr);
  } else {
    snprintf(out, outLen, "cause UNPROVEN [getpeername=%s errno=%d, SO_ERROR=%d, tcp_state=%d]%s",
             gpn == 0 ? "connected" : "FAILED", gpnErr, soErr, tstate, peekNote);
  }
}

#endif

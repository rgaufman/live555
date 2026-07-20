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
// "liveMedia"
// Copyright (c) 1996-2026 Live Networks, Inc.  All rights reserved.
// An abstraction of a network interface used for RTP (or RTCP).
// (This allows the RTP-over-TCP hack (RFC 2326, section 10.12) to
// be implemented transparently.)
// Implementation

#include "RTPInterface.hh"
#include "MonotonicTime.hh" // monotonicSeconds() -- ALL elapsed-time gates, never time(NULL)
#include "RateLimitedLog.hh"
#include "RTPTcpTeardown.hh" // probeTcpClose() + classifyTcpTeardown() + isTcpTeardownErrno() (header-only, unit-tested)
#include "PeerReachability.hh" // non-blocking "is the peer still there RIGHT NOW?" probe (header-only, unit-tested)
#include "StreamStallDetector.hh" // TIME-based stall policy, distinct from the busy-loop guard (header-only, unit-tested)
#include "RTPFanoutDecision.hh" // fanoutDecide(): pure per-consumer SEND/DROP/WARN/EVICT decision (header-only, unit-tested)
#include "RTPInterleavedFraming.hh" // frameStep(): RFC 2326 sec 10.12 '$'-framing transitions (header-only, unit-tested)
// The reader uses the model's OWN ReadState as its state type (fTCPReadingState below). It was a
// private mirror enum with the same six names, which forced every model consultation through a
// mapping ternary -- and a binary ternary silently collapses any future model state into whichever
// of two legacy states it is not. One enum, no translation layer.
using RTPInterleavedFraming::ReadState;
using RTPInterleavedFraming::AWAITING_DOLLAR;
using RTPInterleavedFraming::AWAITING_STREAM_CHANNEL_ID;
using RTPInterleavedFraming::AWAITING_SIZE1;
using RTPInterleavedFraming::AWAITING_SIZE2;
using RTPInterleavedFraming::AWAITING_PACKET_DATA;
using RTPInterleavedFraming::SKIPPING_UNHANDLED_PAYLOAD;
#include <GroupsockHelper.hh>
#include <stdio.h>
#include <errno.h>
#if defined(__linux__)
#include <sys/ioctl.h>   // TIOCOUTQ: bytes still queued in a socket's kernel send buffer
#endif

// Read a TCP socket's RAW kernel send-buffer state: total SO_SNDBUF and bytes still queued (unsent +
// unacked). Returns True on success. The caller feeds both straight to fanoutDecide(), which owns the
// single derivation of usable free space (usable = SO_SNDBUF/2 - queued, since Linux reports SO_SNDBUF
// DOUBLED). We deliberately DON'T pre-compute free space here — keeping the "doubled" knowledge in one
// place (the decision fn) rather than split between this helper and the call site.
// `errnoOut` receives the errno of a FAILED query, or 0 when the platform simply has no support.
//
// 🚨 The distinction matters and used to be lost: both returned a bare False, and False makes
// fanoutDecide return unconditional SEND -- silently disabling DROP/WARN/EVICT and the frame-atomic
// fit check for that consumer, and falling through to the 500ms blocking send that the whole
// decoupling invariant exists to avoid. A real failure (e.g. EBADF on a consumer fd closed earlier
// in the same event-loop pass) then looked byte-identical to "this is not Linux", so an operator
// reading the log could not tell a degraded proxy from a correctly configured one.
// SO_SNDBUF for a consumer socket, fetched ONCE at registration: the value is constant for the
// socket's lifetime (nothing here calls setsockopt after the consumer is handed over), and
// re-reading it per consumer per packet was one wasted syscall on the hottest path in the tree --
// the same shape as the per-consumer clock reads the fan-out loop already removed. Returns:
//   > 0  the buffer size
//   < 0  -errno from the failed getsockopt, preserved so the per-packet query can still REPORT the
//        real error instead of inventing one
//     0  no query available on this platform
static int tcpSendBufferSize(int sock) {
#if defined(__linux__)
  int sb = 0; socklen_t l = sizeof(sb);
  if (sock < 0) return -EBADF;
  if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sb, &l) < 0) return -errno;
  return sb;
#else
  (void)sock; return 0;
#endif
}

static Boolean tcpSendBufferState(int sock, int sndbufCached, int& sndbuf, int& queued, int& errnoOut) {
  sndbuf = -1; queued = -1; errnoOut = 0;
#if defined(__linux__)
  if (sock < 0) { errnoOut = EBADF; return False; }
  if (sndbufCached == 0) return False; // registration-time fetch reported "platform unsupported"
  if (sndbufCached < 0) { errnoOut = -sndbufCached; return False; } // its real, preserved errno
  int q = 0;
  if (ioctl(sock, TIOCOUTQ, &q) < 0) { errnoOut = errno; return False; }
  queued = q; sndbuf = sndbufCached;
  return True;
#else
  (void)sock; (void)sndbufCached; return False; // no query available: errnoOut stays 0
#endif
}

// classifyTcpTeardown() + probeTcpClose() moved to RTPTcpTeardown.hh (header-only) so they can be
// unit-tested standalone against real loopback TCP sockets (test/test_tcp_teardown.cpp). See that
// header for the full rationale (proof captured at the moment of the stall, not at teardown time).

////////// Helper Functions - Definition //////////

// Helper routines and data structures, used to implement
// sending/receiving RTP/RTCP over a TCP socket:

class tcpStreamRecord {
  public:
  tcpStreamRecord(int streamSocketNum, unsigned char streamChannelId,
		  TLSState* tlsState,
                  tcpStreamRecord* next);
  virtual ~tcpStreamRecord();

public:
  tcpStreamRecord* fNext;
  int fStreamSocketNum;
  unsigned char fStreamChannelId;
  TLSState* fTLSState;
  unsigned fFramesDropped; // whole interleaved frames dropped to this consumer because its send buffer was full (back-pressure); the source read is never blocked for it
  unsigned fEvictStrikes;  // consecutive sendPacket samples this consumer was over the evict threshold (see fanoutDecide): eviction needs a streak, not one sample
  long fBlockedSinceSec;   // MONOTONIC time this consumer FIRST refused a frame; 0 while it is draining. Eviction is measured from this, not from queued bytes (which SO_SNDBUF caps)
  int fSndBufSize;         // SO_SNDBUF, cached at registration (constant for the socket's life); see tcpSendBufferSize()
};

// Reading RTP-over-TCP is implemented using two levels of hash tables.
// The top-level hash table maps TCP socket numbers to a
// "SocketDescriptor" that contains a hash table for each of the
// sub-channels that are reading from this socket.

static HashTable* socketHashTable(UsageEnvironment& env, Boolean createIfNotPresent = True) {
  _Tables* ourTables = _Tables::getOurTables(env, createIfNotPresent);
  if (ourTables == NULL) return NULL;

  if (ourTables->socketTable == NULL) {
    // Create a new socket number -> SocketDescriptor mapping table:
    ourTables->socketTable = HashTable::create(ONE_WORD_HASH_KEYS);
  }
  return (HashTable*)(ourTables->socketTable);
}

// Tick interval for the stall check. One task per socket; cost is a comparison per second. The
// threshold itself lives in StreamStallDetector.hh with the policy it belongs to.
static unsigned const RTP_TCP_STALL_CHECK_INTERVAL_US = 1000000;

// Probe whether the peer is still reachable AT THE MOMENT a fault is classified, and log the answer
// alongside the fault code.
//
// WHY HERE AND NOT A PERIODIC JOB: the app already samples packet loss on a timer
// (DevicePacketLossJob), but that answers "was this camera reachable at some arbitrary time", which
// is a different question. A camera that drops a stream and is reachable again 20 seconds later
// looks identical, in a periodic sample, to one that never went away. The distinction we need —
// "did it vanish from the network, or was it sitting right there and chose to abort?" — exists only
// at the instant of failure.
//
// Self-owning and fully non-blocking: it starts a connect(), registers for writability, arms a
// deadline task, and deletes itself on whichever fires first. It deliberately does NOT hold a
// reference to the SocketDescriptor that spawned it, because that object is usually being torn down
// in the same breath.
class PeerProbe {
public:
  // Hard ceiling on probes in flight. Faults arrive in STORMS (a flapping fleet can produce many per
  // second), each probe holds an fd for up to PEER_REACH_TIMEOUT_MS, and this process also needs its
  // fds for actual video. Diagnostics must never be able to starve the thing they are diagnosing, so
  // past the cap we simply skip the probe: losing one line of context is strictly better than losing
  // a stream to EMFILE. Not a defensive nil-check — it is a bounded-resource budget.
  static unsigned const MAX_IN_FLIGHT = 8;

  static void start(UsageEnvironment& env, char const* ip, unsigned short port, char const* code) {
    if (ip == NULL || ip[0] == '\0') return;
    if (fInFlight >= MAX_IN_FLIGHT) return;

    int fd = startPeerReachabilityProbe(ip, port);
    if (fd < 0) return; // local failure: say nothing rather than blame the peer

    ++fInFlight;
    new PeerProbe(env, fd, ip, port, code); // owns itself from here
  }

private:
  PeerProbe(UsageEnvironment& env, int fd, char const* ip, unsigned short port, char const* code)
    : fEnv(env), fFd(fd), fPort(port), fDone(False) {
    snprintf(fIp, sizeof(fIp), "%s", ip);
    snprintf(fCode, sizeof(fCode), "%s", code == NULL ? "" : code);
    fEnv.taskScheduler().setBackgroundHandling(fFd, SOCKET_WRITABLE, onWritable, this);
    fDeadline = fEnv.taskScheduler().scheduleDelayedTask(PEER_REACH_TIMEOUT_MS*1000, onDeadline, this);
  }

  static void onWritable(void* cd, int /*mask*/) { ((PeerProbe*)cd)->finish(False); }
  static void onDeadline(void* cd)               { ((PeerProbe*)cd)->finish(True); }

  void finish(Boolean deadlineFired) {
    if (fDone) return; // both callbacks can race to us; only the first wins
    fDone = True;

    PeerReachState const st = peerReachabilityResult(fFd, deadlineFired ? true : false);
    // Carries its OWN greppable code as well as the triggering fault's, so a tally can count
    // "faults where the camera was still on the network" without parsing prose. REFUSED counts as UP:
    // an RST back means the host is present and routable, only the port is shut.
    char const* verdict = (st == PEER_REACH_ALIVE || st == PEER_REACH_REFUSED) ? TX_CODE_PEER_UP
                        : (st == PEER_REACH_UNREACHED)                         ? TX_CODE_PEER_GONE
                        : NULL; // PENDING/ERROR say nothing about the peer -> emit no verdict code
    fEnv << (verdict == NULL ? "" : verdict) << (verdict == NULL ? "" : " ")
	 << fCode << " peer-check: " << fIp << ":" << (int)fPort << " -- "
	 << peerReachabilityText(st) << "\n";

    fEnv.taskScheduler().disableBackgroundHandling(fFd);
    if (!deadlineFired) fEnv.taskScheduler().unscheduleDelayedTask(fDeadline);
    fDeadline = NULL;
    closePeerReachabilityProbe(fFd);
    fFd = -1;
    if (fInFlight > 0) --fInFlight;
    delete this;
  }

  UsageEnvironment& fEnv;
  int fFd;
  unsigned short fPort;
  Boolean fDone;
  TaskToken fDeadline;
  char fIp[64];
  char fCode[24];
  static unsigned fInFlight; // process-wide; the event loop is single-threaded so no lock is needed
};

unsigned PeerProbe::fInFlight = 0;

class SocketDescriptor {
public:
  SocketDescriptor(UsageEnvironment& env, int socketNum, TLSState* tlsState);
  virtual ~SocketDescriptor();

  void registerRTPInterface(unsigned char streamChannelId,
			    RTPInterface* rtpInterface);
  RTPInterface* lookupRTPInterface(unsigned char streamChannelId);
  void deregisterRTPInterface(unsigned char streamChannelId);

  void setServerRequestAlternativeByteHandler(ServerRequestAlternativeByteHandler* handler, void* clientData) {
    fServerRequestAlternativeByteHandler = handler;
    fServerRequestAlternativeByteHandlerClientData = clientData;
  }

  // Disconnect an evicted downstream consumer for real.
  //
  // Removing our tcpStreamRecords only stops us SENDING to it. The consumer's RTSP session stays
  // established, so it sits there receiving nothing, forever -- and the eviction log's promise that
  // "it will reconnect" was one the code could not keep. Worse: with no read error recorded, this
  // descriptor's destructor hands the server 0xFE ("take the socket back"), i.e. the exact opposite
  // of a teardown.
  //
  // The mechanism to do this properly already exists and is the one a genuine read error uses:
  // ~SocketDescriptor passes 0xFF to the server's alternative-byte handler, RTSPServer turns that
  // into handleRequestBytes(-1), and the connection is closed cleanly by its owner (no fd is stolen
  // or double-closed). All that was missing was recording the error and forcing the teardown even
  // when another subsession still holds a channel here -- an evicted consumer must lose the whole
  // connection, not just the video channel.
  void evictAndDisconnect() {
    fReadErrorOccurred = True;
    // Same reentrancy rule as deregisterRTPInterface(): deleting ourselves from inside the read
    // handler loop would free the object the loop is still walking.
    if (fAreInReadHandlerLoop) fDeleteMyselfNext = True;
    else delete this;
  }

private:
  static void tcpReadHandler(SocketDescriptor*, int mask);
  Boolean tcpReadHandler1(int mask);
  // Flag this TCP connection as broken and stop handling it: the caller should `return` this so the
  // read loop unwinds and the descriptor is torn down (its background read handler removed) instead
  // of spinning on a dead/desynced socket. Callers log their own specific reason first.
  Boolean failAndTeardown() { fReadErrorOccurred = True; fDeleteMyselfNext = True; return False; }

  // failAndTeardown() plus the EVIDENCE: classify the teardown from verified kernel state, log it
  // with the right fault code, probe whether the peer is still reachable, and — when the far end is
  // PROVEN to have closed — hand that proof to the session layer. Use this for every read-side
  // failure where the socket may have been closed by the peer; use bare failAndTeardown() only where
  // the socket is known not to be the story (fd already closed, framing desync on a live socket).
  Boolean failAndTeardownClassified(char const* what, char const* detail = NULL);
  // Record a proven far-end close on EVERY RTPInterface using this socket, not just one channel.
  void noteFarEndCloseProvenOnAllInterfaces();

private:
  UsageEnvironment& fEnv;
  int fOurSocketNum;
  TLSState* fTLSState;
  HashTable* fSubChannelHashTable;
  ServerRequestAlternativeByteHandler* fServerRequestAlternativeByteHandler;
  void* fServerRequestAlternativeByteHandlerClientData;
  u_int8_t fStreamChannelId, fSizeByte1;
  Boolean fReadErrorOccurred, fDeleteMyselfNext, fAreInReadHandlerLoop;
  ReadState fTCPReadingState; // RTPInterleavedFraming::ReadState -- the tested model's own type
  // A frame arrived on a channel id we never registered. Its header is still well-formed, so the
  // size field tells us exactly how many payload bytes to discard to reach the next frame boundary.
  //
  // Before this existed, an unregistered channel sent the reader back to AWAITING_DOLLAR with '$'
  // and the channel byte ALREADY consumed and the size never read, so it hunted for the next 0x24 --
  // which in H264 payload occurs roughly 1 byte in 256. Framing was then lost for the rest of the
  // connection and the desync guard tripped 2048 bytes later. Production DESYNC samples from a
  // Merit-Lilin 4K camera show exactly this: "04 5e 24 00 05 a2 80 63" is [len 1118]['$'][ch 0][len 1442]
  // [RTP v2 ...] -- a VALID header sitting two bytes ahead of where the reader was looking.
  // Modelled and unit-tested in liveMedia/include/RTPInterleavedFraming.hh + test_interleaved_framing.
  unsigned fSkipBytesRemaining;
  Boolean fFrameChannelRegistered;
  // interleaved-framing desync guard (see tcpReadHandler1). Counts header-state bytes read
  // WITHOUT parsing a valid '$'+channel+size frame; reset only when a valid channel is seen. A long
  // run = the RFC-2326 10.12 framing is lost and we byte-spin feeding binary to the RTSP parser (the
  // ~30% CPU busy-loop, ~30k 1-byte recvfrom/s). NB: must NOT reset on a bare '$' — binary payload
  // contains 0x24 ~1/256 bytes, which would falsely reset a "consecutive non-$" counter.
  unsigned fBytesSinceFrame;
  unsigned char fAltByteSample[8];
  // Guard the desync check against a FALSE POSITIVE on a pure RTSP-control socket. A downstream
  // consumer's connection carries the server's RTP '$' frames OUTBOUND (via sendPacket); the bytes
  // we READ from it are only client->server RTSP control (OPTIONS / GET_PARAMETER keepalives, etc.),
  // which are never '$'-framed. Without this flag fBytesSinceFrame would climb with every keepalive
  // and never reset, eventually tripping the desync teardown and killing a perfectly healthy
  // consumer (observed: "DESYNC ... First bytes: 47 45 54 5f 50 41 52 41" == ASCII "GET_PARA[METER]").
  // So only ever declare a desync on a socket that has ACTUALLY carried at least one '$' frame — a
  // real data channel whose framing was in sync and then broke. Control-only sockets never set this.
  Boolean fEverSawFrame;
  // Mid-packet stall guard (see tcpReadHandler1 AWAITING_PACKET_DATA). Counts consecutive dispatches
  // where the socket is select-readable but the current interleaved packet's payload makes NO
  // progress -- i.e. the peer sent a '$'+channel+size header then the connection died/stalled before
  // the payload arrived. A dead socket stays readable forever, so without this the read handler
  // re-dispatches ~65k/s and pins a CPU core. Reset to 0 on any progress or a new frame.
  unsigned fPacketStallCount;
  // Read-side close evidence (TcpClosePeek) captured at the FIRST no-progress dispatch, while the
  // FIN/RST distinction still exists -- by the teardown 2000 dispatches later the socket has usually
  // dropped to TCP_CLOSE and getpeername just fails ENOTCONN ("UNPROVEN"). This is what lets the
  // stall teardown log say "far end closed -- PROVEN" (FIN/RST) vs "mid-path loss, not the peer"
  // (still-open) reliably. TCP_PEEK_NONE until first captured; upgraded toward a definitive result.
  int fStallPeek;
  // When this socket started reading, so a teardown can report HOW LONG the stream survived. That
  // duration is the cheapest discriminator between a session cap and erratic firmware: a server
  // enforcing a session limit cuts every stream at ~the same age, whereas firmware closing on an
  // internal fault gives a scattered spread. Reading it per-socket in-process beats inferring it
  // from gaps between log lines (which conflates separate sockets and misses the first session).
  time_t fStreamStartSec;
  // Peer description captured AT CONSTRUCTION, while the socket is still connected. A teardown
  // classified later cannot ask the kernel who the peer was — getpeername() fails ENOTCONN once the
  // socket reaches TCP_CLOSE — and "peer unknown" is useless for the one job the address has: telling
  // an upstream camera apart from a local consumer. Same rule as the stall peek: capture evidence
  // while it exists.
  char fPeerDesc[160];
  // Peer address in machine-usable form, also captured at construction. Kept separately from
  // fPeerDesc rather than parsed back out of it: fPeerDesc is a human-facing sentence and must stay
  // free to change wording without breaking the reachability probe.
  char fPeerIp[64];
  unsigned short fPeerPort;
  // Which end of the proxy this socket serves, adopted from the RTPInterface(s)
  // registered on it (see noteStreamRole). Governs whether a teardown here may
  // be logged as a camera fault. Stays UNKNOWN until something tells us, and
  // returns to UNKNOWN if two interfaces disagree -- an unclassifiable socket
  // must report as unclassifiable rather than be assigned to either side.
  StreamRole fStreamRole;
  void noteStreamRole(StreamRole role);

  // ---- TIME-based stream-stall detector (distinct from the busy-loop guard above) ----
  //
  // fPacketStallCount is a CPU BUSY-LOOP guard: it counts read DISPATCHES, so it only works while the
  // socket keeps becoming readable (the dead-but-readable case). A peer that simply stops sending
  // while holding the socket open generates NO readability, hence no dispatches, so that counter is
  // structurally blind to it -- which is the most obvious kind of stall. This is the detector for
  // that case, and it asks the question an operator actually asks: how LONG has nothing arrived?
  //
  // It DETECTS and LOGS only; it does not tear down. failAndTeardown() merely sets fDeleteMyselfNext,
  // and the delete is performed by the read-handler loop -- which, for a silent socket, never runs.
  // Recovery therefore stays with the -D inactivity timer, which resets the whole session at the
  // ProxyRTSPClient layer (the correct place). Deleting a SocketDescriptor from a timer would race
  // the fAreInReadHandlerLoop / fDeleteMyselfNext machinery and risk a use-after-free.
  time_t fLastProgressSec;   // last time ANY payload progress was made
  Boolean fStallReported;    // latched so a persistent stall logs once, not once per tick
  TaskToken fStallCheckTask; // MUST be unscheduled in the destructor or it fires on freed memory
  static void stallCheck(void* clientData);
  void scheduleStallCheck();
  // monotonicSeconds(), never time(NULL): this feeds an ELAPSED-TIME stall check, and an NTP step
  // would otherwise hide a real stall (backward) or fabricate one on every stream at once (forward).
  void noteProgress() { fLastProgressSec = monotonicSeconds(); fStallReported = False; }
};

static SocketDescriptor*
lookupSocketDescriptor(UsageEnvironment& env, int sockNum, TLSState* tlsState = NULL,
		       Boolean createIfNotFound = True) {
  HashTable* table = socketHashTable(env, createIfNotFound);
  if (table == NULL) return NULL;

  char const* key = (char const*)(long)sockNum;
  SocketDescriptor* socketDescriptor = (SocketDescriptor*)(table->Lookup(key));
  if (socketDescriptor == NULL) {
    if (createIfNotFound) {
      socketDescriptor = new SocketDescriptor(env, sockNum, tlsState);
      table->Add((char const*)(long)(sockNum), socketDescriptor);
    } else if (table->IsEmpty()) {
      // We can also delete the table (to reclaim space):
      _Tables* ourTables = _Tables::getOurTables(env);
      delete table;
      ourTables->socketTable = NULL;
      ourTables->reclaimIfPossible();
    }
  }

  return socketDescriptor;
}

static void removeSocketDescription(UsageEnvironment& env, int sockNum) {
  char const* key = (char const*)(long)sockNum;
  HashTable* table = socketHashTable(env);
  table->Remove(key);

  if (table->IsEmpty()) {
    // We can also delete the table (to reclaim space):
    _Tables* ourTables = _Tables::getOurTables(env);
    delete table;
    ourTables->socketTable = NULL;
    ourTables->reclaimIfPossible();
  }
}


////////// RTPInterface - Implementation //////////

RTPInterface::RTPInterface(Medium* owner, Groupsock* gs)
  : fOwner(owner), fGS(gs),
    // Which END of the proxy this socket belongs to, derived from what the
    // owner IS rather than from the peer's address (see StreamRole.hh: a
    // downstream viewer can be remote, so loopback is not the discriminator).
    // A teardown may only blame the far end when this says UPSTREAM.
    fStreamRole(owner == NULL ? STREAM_ROLE_UNKNOWN
                              : streamRoleForOwner(owner->isSource(), owner->isSink())),
    fTCPStreams(NULL),
    fNextTCPReadSize(0), fNextTCPReadStreamSocketNum(-1),
    fNextTCPReadStreamChannelId(0xFF), fNextTCPReadTLSState(NULL), fReadHandlerProc(NULL),
    fStoredReadFd(-1),
    fAuxReadHandlerFunc(NULL), fAuxReadHandlerClientData(NULL),
    fFarEndCloseProven(False),
    fSendRateAccumBytes(0), fSendBytesPerSec(0.0) {
  fSendRateWindowStart.tv_sec = 0; fSendRateWindowStart.tv_usec = 0;
  // Make the socket non-blocking, even though it will be read from only asynchronously, when packets arrive.
  // The reason for this is that, in some OSs, reads on a blocking socket can (allegedly) sometimes block,
  // even if the socket was previously reported (e.g., by "select()") as having data available.
  // (This can supposedly happen if the UDP checksum fails, for example.)
  makeSocketNonBlocking(fGS->socketNum());
  increaseSendBufferTo(envir(), fGS->socketNum(), 50*1024);
}

RTPInterface::~RTPInterface() {
  stopNetworkReading();
  delete fTCPStreams;
}

void RTPInterface::setStreamSocket(int sockNum, unsigned char streamChannelId,
				   TLSState* tlsState) {
  // A NEW connection generation starts here, so the previous generation's proven-close verdict must
  // not carry over. A stale True would make the session layer reset the fresh session immediately,
  // and keep doing so — a reconnect loop manufactured by the very flag meant to shorten reconnects.
  fFarEndCloseProven = False;
  fGS->removeAllDestinations();
  envir().taskScheduler().disableBackgroundHandling(fGS->socketNum()); // turn off any reading on our datagram socket
  fGS->reset(); // and close our datagram socket, because we won't be using it anymore

  addStreamSocket(sockNum, streamChannelId, tlsState);
}

void RTPInterface::addStreamSocket(int sockNum, unsigned char streamChannelId,
				   TLSState* tlsState) {
  if (sockNum < 0) return;

  for (tcpStreamRecord* streams = fTCPStreams; streams != NULL;
       streams = streams->fNext) {
    if (streams->fStreamSocketNum == sockNum
	&& streams->fStreamChannelId == streamChannelId) {
      return; // we already have it
    }
  }

  fTCPStreams = new tcpStreamRecord(sockNum, streamChannelId, tlsState, fTCPStreams);

  // Also, make sure this new socket is set up for receiving RTP/RTCP-over-TCP:
  SocketDescriptor* socketDescriptor = lookupSocketDescriptor(envir(), sockNum, tlsState);
  socketDescriptor->registerRTPInterface(streamChannelId, this);

  unsigned destCount = 0;
  for (tcpStreamRecord* s = fTCPStreams; s != NULL; s = s->fNext) ++destCount;
  // NOTE the two numbers below count DIFFERENT things, and the wording must keep that clear.
  // `destCount` is per-INTERFACE (how many consumers this one interface fans out to), but these are
  // function-level statics, so the rate-limit tally is PROCESS-GLOBAL across every RTPInterface.
  // Previously the suffix just said "N attaches", directly after "this interface now fanning out to
  // M" — which read as though N belonged to this interface. It does not: one consumer attaching
  // produces 4 attaches here (2 tracks x RTP+RTCP), so two consumers show "8 attaches" beside
  // "2 TCP destinations" and look inconsistent while both numbers are correct.
  static time_t lastSec = 0; static unsigned long pending = 0;
  unsigned long n = rateLimitedLog(lastSec, pending, 5);
  if (n > 0) {
    envir() << "RTPInterface::addStreamSocket: added channel " << (int)streamChannelId
	    << "/socket " << sockNum << " (this interface now fanning out to "
	    << destCount << " TCP destination" << (destCount == 1 ? "" : "s") << ")";
    logRateLimitSuffix(envir(), n, "attaches across ALL interfaces in this proxy", 5);
  }
}

static void deregisterSocket(UsageEnvironment& env, int sockNum, unsigned char streamChannelId) {
  SocketDescriptor* socketDescriptor = lookupSocketDescriptor(env, sockNum, NULL, False);
  if (socketDescriptor != NULL) {
    socketDescriptor->deregisterRTPInterface(streamChannelId);
        // Note: This may delete "socketDescriptor",
        // if no more interfaces are using this socket
  }
}

void RTPInterface::removeStreamSocket(int sockNum,
				      unsigned char streamChannelId) {
  // Remove - from our list of 'TCP streams' - the record of the (sockNum,streamChannelId) pair.
  // (However "streamChannelId" == 0xFF is a special case, meaning remove all
  //  (sockNum,*) pairs.)
  
  while (1) {
    tcpStreamRecord** streamsPtr = &fTCPStreams;

    while (*streamsPtr != NULL) {
      if ((*streamsPtr)->fStreamSocketNum == sockNum
	  && (streamChannelId == 0xFF || streamChannelId == (*streamsPtr)->fStreamChannelId)) {
	// Delete the record pointed to by *streamsPtr :
	unsigned char streamChannelIdToRemove = (*streamsPtr)->fStreamChannelId;
	tcpStreamRecord* next = (*streamsPtr)->fNext;
	(*streamsPtr)->fNext = NULL;
	delete (*streamsPtr);
	*streamsPtr = next;

	// And 'deregister' this socket,channelId pair:
	deregisterSocket(envir(), sockNum, streamChannelIdToRemove);

	if (streamChannelId != 0xFF) return; // we're done
	break; // start again from the beginning of the list, in case the list has changed
      } else {
	streamsPtr = &((*streamsPtr)->fNext);
      }
    }
    if (*streamsPtr == NULL) break;
  }
}

void RTPInterface::setServerRequestAlternativeByteHandler(UsageEnvironment& env, int socketNum,
							  ServerRequestAlternativeByteHandler* handler, void* clientData) {
  SocketDescriptor* socketDescriptor = lookupSocketDescriptor(env, socketNum, NULL, False);

  if (socketDescriptor != NULL) socketDescriptor->setServerRequestAlternativeByteHandler(handler, clientData);
}

void RTPInterface::clearServerRequestAlternativeByteHandler(UsageEnvironment& env, int socketNum) {
  setServerRequestAlternativeByteHandler(env, socketNum, NULL, NULL);
}

Boolean RTPInterface::sendPacket(unsigned char* packet, unsigned packetSize) {
  Boolean success = True; // we'll return False instead if any of the sends fail

  // Normal case: Send as a UDP packet:
  if (!fGS->output(envir(), packet, packetSize)) success = False;

  // Maintain a coarse send-rate estimate (bytes/sec, ~1s window) so the per-consumer drift
  // warning below can express a consumer's backlog as TIME behind, not just bytes. Cheap:
  // one gettimeofday + an add per packet, recomputed once a second.
  {
    struct timeval now; gettimeofday(&now, NULL);
    if (fSendRateWindowStart.tv_sec == 0) fSendRateWindowStart = now;
    fSendRateAccumBytes += packetSize;
    double win = (now.tv_sec - fSendRateWindowStart.tv_sec)
               + (now.tv_usec - fSendRateWindowStart.tv_usec) / 1000000.0;
    if (win >= 1.0) {
      fSendBytesPerSec = fSendRateAccumBytes / win;
      fSendRateAccumBytes = 0; fSendRateWindowStart = now;
    }
  }

  // Also, send over each of our TCP sockets.
  //
  // DECOUPLING INVARIANT: fanning a packet out to a downstream consumer must have ZERO effect
  // on the UPSTREAM source read. This proxy is single-threaded, so a *blocking* send() to a slow
  // consumer would stall the whole event loop and starve the camera read (and, if the starve
  // outlasts the inter-packet-gap watchdog, trigger a needless reconnect that drops every
  // consumer). To make that impossible we take a FRAME-ATOMIC, NON-BLOCKING decision per
  // consumer: if the consumer's kernel send buffer can't hold this entire interleaved frame
  // right now, we DROP the frame for THAT consumer only. Dropping a whole frame keeps its
  // RTP-over-TCP framing perfectly in sync (we send nothing, so there is no half-written
  // header) — the consumer just loses a packet exactly as it would over UDP, and re-syncs at
  // the next keyframe. A chronically slow consumer therefore degrades only ITS OWN stream; the
  // source read and every other consumer are untouched. This replaces the old behaviour where a
  // full send buffer fell back to a blocking send.
  //
  // Two-tier drift policy per consumer: >100ms behind => WARN (rate-limited to ~1/s); >5000ms
  // behind => the backlog will never catch up, so EVICT the consumer (stop feeding it -> it
  // reconnects fresh) with an always-on ERROR. Eviction is DEFERRED to after the loop: never
  // mutate fTCPStreams (via removeStreamSocket) while iterating it.
  // Bounded eviction list. If more than this many consumers are hopelessly behind in the SAME frame
  // the surplus are simply re-evaluated (and evicted) on the next sendPacket — it self-heals, so this
  // is a per-frame batch cap, not a hard limit on how many can ever be evicted.
  static unsigned const MAX_EVICTIONS_PER_PACKET = 8;
  int evictSock[MAX_EVICTIONS_PER_PACKET]; unsigned nEvict = 0;
  // ONE clock read for the whole fan-out, not one per consumer. Every consumer below is being
  // judged at the same instant -- they are all inside this single sendPacket call -- so re-reading
  // the clock per consumer costs K syscalls per RTP packet to obtain K copies of the same value.
  // And ZERO reads when there is no fan-out: a UDP-only session takes this path per packet too, and
  // would otherwise pay for a value nothing reads.
  long const nowSec = (fTCPStreams != NULL) ? monotonicSeconds() : 0;
  tcpStreamRecord* nextStream;
  for (tcpStreamRecord* stream = fTCPStreams; stream != NULL; stream = nextStream) {
    nextStream = stream->fNext; // Set this now, in case the following deletes "stream":

    // Per-consumer non-blocking decision (TLS consumers buffer inside the TLS layer, so the
    // socket-level check doesn't apply — they take the normal send path). The policy lives in the
    // pure, unit-tested fanoutDecide() (RTPFanoutDecision.hh): SEND / DROP-this-frame / WARN / EVICT
    // from the consumer's send-buffer state, so one slow consumer can never block the source read.
    if (stream->fTLSState == NULL) {
      int sndbuf, queued, bufErrno;
      Boolean haveState = tcpSendBufferState(stream->fStreamSocketNum, stream->fSndBufSize,
                                             sndbuf, queued, bufErrno);
      if (!haveState && bufErrno != 0) {
        // A real query FAILURE (not "unsupported platform"). Fan-out protection is now OFF for this
        // consumer -- say so, rather than silently degrading to unconditional SEND.
        static std::map<int, RateLimitEntry> bufStateTracker;
        unsigned long n = rateLimitedLogPerKey(bufStateTracker, stream->fStreamSocketNum, 5);
        if (n > 0) {
          envir() << TX_CODE_UNEXPECTED << " RTPInterface: cannot read send-buffer state for the"
                     " consumer on socket " << stream->fStreamSocketNum << " (errno " << bufErrno
                  << "). Fan-out drop/drift/evict protection is DISABLED for this consumer, so a"
                     " stalled reader here can reach the blocking-send fallback and delay the event"
                     " loop. This is a local error, NOT a camera fault.";
          logRateLimitSuffix(envir(), n, "occurrences", 5);
        }
      }
      FanoutDecision fd = fanoutDecide(haveState, sndbuf, queued, packetSize,
                                       fSendBytesPerSec, stream->fEvictStrikes,
                                       nowSec, stream->fBlockedSinceSec);

      if (fd.driftWarn) {
        static std::map<int, RateLimitEntry> driftTracker;
        unsigned long n = rateLimitedLogPerKey(driftTracker, stream->fStreamSocketNum, 1/*~1/s*/);
        if (n > 0) {
          envir() << TX_CODE_DRIFT << " RTPInterface: downstream consumer on socket " << stream->fStreamSocketNum
                  << " (channel " << (int)stream->fStreamChannelId << ") is drifting ~"
                  << (unsigned)fd.behindMs << " ms behind (" << queued << " bytes queued at ~"
                  << (unsigned)(fSendBytesPerSec / 1024) << " KB/s) -- not draining fast enough "
                     "(slow disk/CPU on that consumer?). Source read UNAFFECTED.";
          logRateLimitSuffix(envir(), n, "drift warnings", 1);
        }
      }
      if (fd.evict) {
        // Record the eviction BEFORE logging it. The log used to come first and the cap check second,
        // so past MAX_EVICTIONS_PER_PACKET the line claimed an eviction that never happened — a log
        // that lies, in the one place an operator would be reading it to explain a customer's
        // dropped stream. If we cannot act on it this frame, say nothing; the next frame will
        // re-evaluate and evict it then.
        if (nEvict < MAX_EVICTIONS_PER_PACKET) {
          evictSock[nEvict++] = stream->fStreamSocketNum;

          static std::map<int, RateLimitEntry> evictTracker;
          unsigned long n = rateLimitedLogPerKey(evictTracker, stream->fStreamSocketNum, 5);
          if (n > 0) {
            envir() << TX_CODE_EVICT << " RTPInterface: downstream consumer on socket " << stream->fStreamSocketNum
                    << " (channel " << (int)stream->fStreamChannelId << ") was unable to accept a frame for "
                    << (int)(nowSec - stream->fBlockedSinceSec) << "s (" << stream->fEvictStrikes
                    << " consecutive samples) -- too far behind to recover; EVICTING it: its RTSP "
                       "connection is torn down so it sees a real disconnect and reconnects. "
                       "Source read + other consumers UNAFFECTED.";
            logRateLimitSuffix(envir(), n, "evictions", 5);
          }
        }
        success = False;
        continue; // don't send this frame to a consumer we're about to evict
      }
      if (fd.drop) {
        ++stream->fFramesDropped;
        success = False;
        static std::map<int, RateLimitEntry> dropTracker;
        unsigned long n = rateLimitedLogPerKey(dropTracker, stream->fStreamSocketNum, 5);
        if (n > 0) {
          // fanoutDecide separates "this consumer is behind" from "this frame cannot fit an EMPTY
          // buffer", and the distinction decides WHO is at fault. An oversized frame is OUR sizing
          // problem (SO_SNDBUF too small for the payload) and no consumer can ever accept it, so
          // reporting it as a slow consumer sends the reader after the wrong thing -- forever, once
          // per packet. The flag was computed and unit-tested but never read here.
          if (fd.oversizedFrame) {
            envir() << TX_CODE_DROP << " RTPInterface: frame is TOO LARGE for the send buffer on socket "
                    << stream->fStreamSocketNum << " (channel " << (int)stream->fStreamChannelId
                    << "): a " << (packetSize + RTP_TCP_FRAMING_HEADER_BYTES)
                    << "-byte frame cannot fit even an EMPTY buffer (usable " << fd.usableFree
                    << "). This is OUR socket sizing, NOT a slow consumer -- the consumer cannot fix"
                       " it by draining, and every frame this size will drop (total "
                    << stream->fFramesDropped << "). Raise SO_SNDBUF for downstream consumers.";
          } else {
            envir() << TX_CODE_DROP << " RTPInterface: downstream consumer on socket " << stream->fStreamSocketNum
                    << " (channel " << (int)stream->fStreamChannelId << ") send buffer full -- only "
                    << fd.usableFree << " usable bytes for a "
                    << (packetSize + RTP_TCP_FRAMING_HEADER_BYTES)
                    << "-byte frame; DROPPING this frame for that consumer (total dropped "
                    << stream->fFramesDropped << "). Source read + other consumers UNAFFECTED "
                       "(fan-out is decoupled; no blocking send is ever used).";
          }
          logRateLimitSuffix(envir(), n, "drops", 5);
        }
        continue; // skip this consumer for this frame; never block
      }
    }

    if (!sendRTPorRTCPPacketOverTCP(packet, packetSize,
				    stream->fStreamSocketNum, stream->fStreamChannelId,
				    stream->fTLSState)) {
      success = False;
    }
  }

  // Evict the consumers flagged above (hopelessly behind). Done HERE, after the loop, because both
  // calls below mutate fTCPStreams and must never run while we're iterating it.
  //
  // evictAndDisconnect() FIRST: it tears down the consumer's whole RTSP connection (see its comment
  // -- removeStreamSocket alone leaves the peer connected and silently starved). Its destructor
  // already calls back into removeStreamSocket for every channel it held, so the call that follows
  // is the idempotent tail for anything it did not cover (e.g. the descriptor was already gone).
  for (unsigned i = 0; i < nEvict; ++i) {
    SocketDescriptor* d = lookupSocketDescriptor(envir(), evictSock[i], NULL, False);
    if (d != NULL) d->evictAndDisconnect();
    removeStreamSocket(evictSock[i], 0xFF);
  }

  return success;
}

void RTPInterface
::startNetworkReading(TaskScheduler::BackgroundHandlerProc* handlerProc) {
  // Normal case: Arrange to read UDP packets:
  fStoredReadFd = fGS->socketNum(); // remember the fd so we can deregister by it if fGS is later reset to -1
  envir().taskScheduler().
    turnOnBackgroundReadHandling(fGS->socketNum(), handlerProc, fOwner);

  // Also, receive RTP over TCP, on each of our TCP connections:
  fReadHandlerProc = handlerProc;
  for (tcpStreamRecord* streams = fTCPStreams; streams != NULL;
       streams = streams->fNext) {
    // Get a socket descriptor for "streams->fStreamSocketNum":
    SocketDescriptor* socketDescriptor = lookupSocketDescriptor(envir(), streams->fStreamSocketNum);

    // Tell it about our subChannel:
    socketDescriptor->registerRTPInterface(streams->fStreamChannelId, this);
  }
}

Boolean RTPInterface::handleRead(unsigned char* buffer, unsigned bufferMaxSize,
				 unsigned& bytesRead, struct sockaddr_storage& fromAddress,
				 int& tcpSocketNum, unsigned char& tcpStreamChannelId,
				 Boolean& packetReadWasIncomplete) {
  packetReadWasIncomplete = False; // by default
  Boolean readSuccess;
  if (fNextTCPReadStreamSocketNum < 0) {
    // Normal case: read from the (datagram) 'groupsock':
    tcpSocketNum = -1;
    // dead-socket guard. If our datagram socket has been closed (reset to -1)
    // but our background-read handler is still registered under the fd we started on
    // (fStoredReadFd), SingleStep keeps dispatching us and every readSocket(-1) below
    // would return EBADF, pinning a CPU core (the ~30% recvfrom(-1) busy-loop). Turn the
    // stale handler off by the fd we actually registered on, and stop. Safe: our handler
    // can only still be the one firing if nothing else re-registered that fd (assignHandler
    // would have replaced ours), so turning it off cannot steal a live consumer's handler.
    if (fGS == NULL || fGS->socketNum() < 0) {
      if (fStoredReadFd >= 0) {
        envir().taskScheduler().turnOffBackgroundReadHandling(fStoredReadFd);
        envir() << TX_CODE_EBADF << " RTPInterface: datagram socket is closed (fd reset to -1) but read "
                   "handler was still registered on fd " << fStoredReadFd
                << " -- deregistering to stop an EBADF CPU busy-loop.\n";
        fStoredReadFd = -1;
      }
      bytesRead = 0;
      return False;
    }
    readSuccess = fGS->handleRead(buffer, bufferMaxSize, bytesRead, fromAddress);
  } else {
    // Read from the TCP connection:
    tcpSocketNum = fNextTCPReadStreamSocketNum;
    tcpStreamChannelId = fNextTCPReadStreamChannelId;

    bytesRead = 0;
    unsigned totBytesToRead = fNextTCPReadSize;
    if (totBytesToRead > bufferMaxSize) totBytesToRead = bufferMaxSize;
    unsigned curBytesToRead = totBytesToRead;
    int curBytesRead;
    // Because we're calling "readSocket()" on a stream socket, we don't expect "fromAddress"
    // to be filled in, so set it to a 'dummy' value instead:
    fromAddress.ss_family = AF_INET;
    ((sockaddr_in&)fromAddress).sin_addr.s_addr = 0;
    ((sockaddr_in&)fromAddress).sin_port = 0;
    
    while ((curBytesRead = (fNextTCPReadTLSState != NULL && fNextTCPReadTLSState->isNeeded)
	    ? fNextTCPReadTLSState->read(&buffer[bytesRead], curBytesToRead)
	    : readSocket(envir(), fNextTCPReadStreamSocketNum,
			 &buffer[bytesRead], curBytesToRead,
			 fromAddress)) > 0) {
      bytesRead += curBytesRead;
      if (bytesRead >= totBytesToRead) break;
      curBytesToRead -= curBytesRead;
    }
    fNextTCPReadSize -= bytesRead;
    if (fNextTCPReadSize == 0) {
      // We've read all of the data that we asked for
      readSuccess = True;
    } else if (curBytesRead < 0) {
      // There was an error reading the socket
      bytesRead = 0;
      readSuccess = False;
    } else {
      // We need to read more bytes, and there was not an error reading the socket
      packetReadWasIncomplete = True;
      return True;
    }
    fNextTCPReadStreamSocketNum = -1; // default, for next time
  }

  if (readSuccess && fAuxReadHandlerFunc != NULL) {
    // Also pass the newly-read packet data to our auxilliary handler:
    (*fAuxReadHandlerFunc)(fAuxReadHandlerClientData, buffer, bytesRead);
  }
  return readSuccess;
}

void RTPInterface::stopNetworkReading() {
  // Normal case
  if (fGS != NULL) envir().taskScheduler().turnOffBackgroundReadHandling(fGS->socketNum());
  // also deregister by the fd we originally registered on. If fGS->socketNum()
  // has since been reset to -1 (or moved), the line above turns off nothing and the stale
  // handler would leak under fStoredReadFd -> EBADF CPU busy-loop once that fd is reused.
  if (fStoredReadFd >= 0 && (fGS == NULL || fStoredReadFd != fGS->socketNum())) {
    // ROUTINE CLEANUP, not an anomaly: this fires 1-3 times every time a socket is torn down, so its
    // count tracks the RECONNECT RATE and nothing else. A baseline run read EBADF=316 as the largest
    // single count next to 313 reconnects — ~1 each — and it invited the reader to treat 316
    // independent problems as the headline. TX-CLEANUP, not TX-EBADF: sharing the busy-loop guard's
    // code buried that guard's (anomalous, rare) firings inside reconnect churn — one name, two
    // verdicts, distribution hidden.
    envir() << TX_CODE_CLEANUP << " RTPInterface: routine teardown cleanup -- deregistered a stale read "
               "handler on fd "
            << fStoredReadFd
            << " (current groupsock fd " << (fGS == NULL ? -1 : fGS->socketNum())
            << "). This is EXPECTED once or twice per socket teardown and prevents an EBADF CPU "
               "busy-loop when the fd is reused; its count tracks the reconnect rate, it is not an "
               "independent fault.\n";
    envir().taskScheduler().turnOffBackgroundReadHandling(fStoredReadFd);
  }
  fStoredReadFd = -1;

  // Also turn off read handling on each of our TCP connections:
  for (tcpStreamRecord* streams = fTCPStreams; streams != NULL; streams = streams->fNext) {
    deregisterSocket(envir(), streams->fStreamSocketNum, streams->fStreamChannelId);
  }
}


////////// Helper Functions - Implementation /////////

Boolean RTPInterface::sendRTPorRTCPPacketOverTCP(u_int8_t* packet, unsigned packetSize,
						 int socketNum, unsigned char streamChannelId,
						 TLSState* tlsState) {
#ifdef DEBUG_SEND
  fprintf(stderr, "sendRTPorRTCPPacketOverTCP: %d bytes over channel %d (socket %d)\n",
	  packetSize, streamChannelId, socketNum); fflush(stderr);
#endif
  // Send a RTP/RTCP packet over TCP, using the encoding defined in RFC 2326, section 10.12:
  //     $<streamChannelId><packetSize><packet>
  // (If the initial "send()" of '$<streamChannelId><packetSize>' succeeds, then we force
  // the subsequent "send()" for the <packet> data to succeed, even if we have to do so with
  // a blocking "send()".)
  Boolean framingOk = False;
  int err = 0; // errno of the failing send, captured INSIDE sendDataOverTCP (see below)
  do {
    u_int8_t framingHeader[RTP_TCP_FRAMING_HEADER_BYTES];
    framingHeader[0] = '$';
    framingHeader[1] = streamChannelId;
    framingHeader[2] = (u_int8_t) ((packetSize&0xFF00)>>8);
    framingHeader[3] = (u_int8_t) (packetSize&0xFF);
    if (!sendDataOverTCP(socketNum, tlsState, framingHeader, RTP_TCP_FRAMING_HEADER_BYTES, False, &err)) break;
    framingOk = True;

    if (!sendDataOverTCP(socketNum, tlsState, packet, packetSize, True, &err)) break;
#ifdef DEBUG_SEND
    fprintf(stderr, "sendRTPorRTCPPacketOverTCP: completed\n"); fflush(stderr);
#endif

    return True;
  } while (0);

  {
    // Failed before a full RTP/RTCP packet reached this destination. `framingOk`
    // tells us where: False => 4-byte framing header was skipped (clean drop,
    // socket still in sync); True => we wrote the framing header but the
    // body couldn't be completed and the socket was closed inside sendDataOverTCP.
    // Skip logging on EBADF/EPIPE — those are teardown races where the socket
    // is already gone on one side and the extra send attempt is just cleanup
    // noise, not a real drop we care about. `err` was captured INSIDE sendDataOverTCP
    // at the point of failure (an out-param), NOT re-read from the global errno here —
    // by this point sendDataOverTCP has already logged + torn the socket down, whose
    // syscalls would have clobbered errno, so a re-read would print a value unrelated
    // to the actual failure. Per-fd rate limit so one stalled destination doesn't mask
    // drops on a different one.
    if (!isTcpTeardownErrno(err)) {
      static std::map<int, RateLimitEntry> tracker;
      unsigned long n = rateLimitedLogPerKey(tracker, socketNum, 5);
      if (n > 0) {
	envir() << "RTPInterface::sendRTPorRTCPPacketOverTCP: dropping " << packetSize
		<< "-byte packet on channel " << (int)streamChannelId
		<< "/socket " << socketNum
		<< " (" << (framingOk ? "body incomplete, socket closed" : "framing header skipped")
		<< ", errno=" << err << ")";
	logRateLimitSuffix(envir(), n, "events on this socket", 5);
      }
    }
  }
  return False;
}

#ifndef RTPINTERFACE_BLOCKING_WRITE_TIMEOUT_MS
#define RTPINTERFACE_BLOCKING_WRITE_TIMEOUT_MS 500
#endif

Boolean RTPInterface::sendDataOverTCP(int socketNum, TLSState* tlsState,
				      u_int8_t const* data, unsigned dataSize,
				      Boolean forceSendToSucceed, int* outErrno) {
  int sendResult = (tlsState != NULL && tlsState->isNeeded)
    ? tlsState->write((char const*)data, dataSize)
    : send(socketNum, (char const*)data, dataSize, MSG_NOSIGNAL/*flags*/);
  if (sendResult < (int)dataSize) {
    // The TCP send() failed - at least partially. Capture errno NOW, before any logging/teardown
    // syscalls below can clobber it, and surface it to the caller via outErrno.
    int e = sendResult < 0 ? envir().getErrno() : 0;
    if (outErrno != NULL) *outErrno = e;

    unsigned numBytesSentSoFar = sendResult < 0 ? 0 : (unsigned)sendResult;
    if (numBytesSentSoFar > 0 || (forceSendToSucceed && e == EAGAIN)) {
      // The OS's TCP send buffer has filled up (because the stream's bitrate has exceeded
      // the capacity of the TCP connection!).
      // Force this data write to succeed, by blocking if necessary until it does:
      unsigned numBytesRemainingToSend = dataSize - numBytesSentSoFar;
      {
	// TCP send buffer full mid-write. NOTE: for RTP/RTCP fan-out this path is now
	// near-unreachable on Linux — RTPInterface::sendPacket pre-checks free send-buffer
	// space and DROPS a whole frame for a slow consumer rather than ever starting a send
	// it can't finish (see the DECOUPLING INVARIANT there). This blocking fallback remains
	// only for (a) platforms where the free-space check is unavailable (tcpSendBufferFree
	// returns -1) and (b) non-fan-out callers, so a committed partial frame is completed
	// rather than desyncing the stream. It still stalls the event loop up to
	// RTPINTERFACE_BLOCKING_WRITE_TIMEOUT_MS, so log it — if this ever fires on the fan-out
	// path, the pre-check margin needs widening. Per-fd so a stalled consumer doesn't mask
	// a different consumer also stalling.
	static std::map<int, RateLimitEntry> tracker;
	unsigned long n = rateLimitedLogPerKey(tracker, socketNum, 5);
	if (n > 0) {
	  envir() << "RTPInterface::sendDataOverTCP: TCP send buffer full on socket " << socketNum
		  << " (blocking " << numBytesRemainingToSend << "/" << dataSize << " bytes"
		  << ", force=" << (forceSendToSucceed ? "Y" : "N") << ")";
	  logRateLimitSuffix(envir(), n, "events on this socket", 5);
	}
      }
      makeSocketBlocking(socketNum, RTPINTERFACE_BLOCKING_WRITE_TIMEOUT_MS);
      sendResult = (tlsState != NULL && tlsState->isNeeded)
	? tlsState->write((char const*)(&data[numBytesSentSoFar]), numBytesRemainingToSend)
	: send(socketNum, (char const*)(&data[numBytesSentSoFar]), numBytesRemainingToSend, MSG_NOSIGNAL/*flags*/);
      int be = envir().getErrno(); // blocking send's errno, captured BEFORE makeSocketNonBlocking's ioctl
      makeSocketNonBlocking(socketNum);
      if ((unsigned)sendResult != numBytesRemainingToSend) {
	// The blocking "send()" failed, or timed out.  In either case, we assume that the
	// TCP connection has failed (or is 'hanging' indefinitely), and we stop using it
	// (for both RTP and RTP).
	// (If we kept using the socket here, the RTP or RTCP packet write would be in an
	//  incomplete, inconsistent state.)
	// Terminal — closes socket. Rate-limit globally (5s) to defend against
	// reconnect loops that rapidly cycle sockets. Skip EBADF/EPIPE teardown noise.
	if (outErrno != NULL) *outErrno = be; // blocking send's errno (captured before makeSocketNonBlocking)
	if (!isTcpTeardownErrno(be)) {
	  static time_t lastSec = 0; static unsigned long pending = 0;
	  unsigned long n = rateLimitedLog(lastSec, pending, 5);
	  if (n > 0) {
	    envir() << "RTPInterface::sendDataOverTCP: blocking send failed on socket " << socketNum
		    << " (delivered " << sendResult << "/" << numBytesRemainingToSend << " bytes"
		    << ", errno=" << be << "). Closing socket.";
	    logRateLimitSuffix(envir(), n, "similar events", 5);
	  }
	}
	removeStreamSocket(socketNum, 0xFF);
	return False;
      }

      return True;
    } else if (sendResult < 0 && e != EAGAIN) {
      // Because the "send()" call failed, assume that the socket is now unusable, so stop
      // using it (for both RTP and RTCP). Terminal — same reconnect-loop defense as above.
      // Skip EBADF/EPIPE teardown noise.
      if (!isTcpTeardownErrno(e)) {
	static time_t lastSec = 0; static unsigned long pending = 0;
	unsigned long n = rateLimitedLog(lastSec, pending, 5);
	if (n > 0) {
	  envir() << "RTPInterface::sendDataOverTCP: send error on socket " << socketNum
		  << " (errno=" << e << "). Closing socket.";
	  logRateLimitSuffix(envir(), n, "similar events", 5);
	}
      }
      removeStreamSocket(socketNum, 0xFF);
    }
    // EAGAIN with 0 bytes sent and !forceSendToSucceed falls through silently —
    // the caller (sendRTPorRTCPPacketOverTCP) logs the whole-packet drop with
    // more context (packet size, channel, framingOk), so no duplicate log here.

    return False;
  }

  return True;
}

SocketDescriptor::SocketDescriptor(UsageEnvironment& env, int socketNum, TLSState* tlsState)
  : fEnv(env), fOurSocketNum(socketNum), fTLSState(tlsState),
    fSubChannelHashTable(HashTable::create(ONE_WORD_HASH_KEYS)),
   fServerRequestAlternativeByteHandler(NULL), fServerRequestAlternativeByteHandlerClientData(NULL),
   fReadErrorOccurred(False), fDeleteMyselfNext(False), fAreInReadHandlerLoop(False), fTCPReadingState(AWAITING_DOLLAR),
   fSkipBytesRemaining(0), fFrameChannelRegistered(False),
   fBytesSinceFrame(0), fEverSawFrame(False), fPacketStallCount(0), fStallPeek(TCP_PEEK_NONE),
   fStreamStartSec(monotonicSeconds()), fStreamRole(STREAM_ROLE_UNKNOWN),
   fLastProgressSec(monotonicSeconds()), fStallReported(False), fStallCheckTask(NULL) {
  describeTcpPeer(socketNum, fPeerDesc, sizeof(fPeerDesc));
  capturePeerAddress(socketNum, fPeerIp, sizeof(fPeerIp), fPeerPort);
  scheduleStallCheck();
}

// ONE recurring task per socket, not one per frame: at 25fps across many streams, arming and
// cancelling a timer per frame would be constant scheduler churn for no benefit. A periodic "is
// anything overdue?" question costs a single comparison per tick.
void SocketDescriptor::scheduleStallCheck() {
  fStallCheckTask = fEnv.taskScheduler().scheduleDelayedTask(RTP_TCP_STALL_CHECK_INTERVAL_US,
							     stallCheck, this);
}

void SocketDescriptor::stallCheck(void* clientData) {
  SocketDescriptor* d = (SocketDescriptor*)clientData;
  d->fStallCheckTask = NULL; // this task has now fired; re-armed at the end

  // Policy lives in the pure, unit-tested streamStallCheck() (StreamStallDetector.hh) — the same
  // extract-the-decision pattern that let the SO_SNDBUF-doubling and single-sample-evict bugs be
  // caught by a test rather than by a customer.
  {
    StreamStallVerdict const v = streamStallCheck(d->fEverSawFrame == True, d->fStallReported == True,
						  (long)d->fLastProgressSec, monotonicSeconds());
    if (v.stalled) {
      long const idleSecs = v.idleSecs;
      d->fStallReported = True; // latch: report the onset once, not every tick
      // Nobody is blamed: silence is not proof of a close. If the peer had closed, the read side
      // would have gone readable and the teardown classifier would have proven FIN/RST instead.
      d->fEnv << TX_CODE_STALL << " RTPInterface: stream STALLED on socket " << d->fOurSocketNum
	      << " (" << d->fPeerDesc << ") -- no payload progress for " << (int)idleSecs
	      << "s while the socket stayed OPEN and silent. Not a proven close; consistent with the "
		 "peer having stopped sending (upstream fault or mid-path loss). Recovery is left to "
		 "the -D inactivity reset.\n";
      // A silent stall is exactly the case where reachability-at-the-instant is most informative:
      // still answering means it is on the network and has simply stopped sending.
      PeerProbe::start(d->fEnv, d->fPeerIp, d->fPeerPort, TX_CODE_STALL);
    }
  }

  d->scheduleStallCheck();
}

SocketDescriptor::~SocketDescriptor() {
  // MUST come first and MUST be unconditional: a recurring task still armed after this object is
  // freed fires stallCheck() on freed memory. This is the whole risk of a periodic task and the
  // reason the stall detector deliberately does not also perform teardown.
  fEnv.taskScheduler().unscheduleDelayedTask(fStallCheckTask);
  fStallCheckTask = NULL;

  fDeleteMyselfNext = False;
  fEnv.taskScheduler().turnOffBackgroundReadHandling(fOurSocketNum);
  removeSocketDescription(fEnv, fOurSocketNum);

  if (fSubChannelHashTable != NULL) {
    // Remove knowledge of this socket from any "RTPInterface"s that are using it:
    HashTable::Iterator* iter = HashTable::Iterator::create(*fSubChannelHashTable);
    RTPInterface* rtpInterface;
    char const* key;

    while ((rtpInterface = (RTPInterface*)(iter->next(key))) != NULL) {
      u_int64_t streamChannelIdLong = (u_int64_t)key;
      unsigned char streamChannelId = (unsigned char)streamChannelIdLong;

      rtpInterface->removeStreamSocket(fOurSocketNum, streamChannelId);
    }
    delete iter;

    // Then remove the hash table entries themselves, and then remove the hash table:
    while (fSubChannelHashTable->RemoveNext() != NULL) {}
    delete fSubChannelHashTable;
  }

  // Finally:
  if (fServerRequestAlternativeByteHandler != NULL) {
    // Hack: Pass a special character to our alternative byte handler, to tell it that either
    // - an error occurred when reading the TCP socket, or
    // - no error occurred, but it needs to take over control of the TCP socket once again.
    u_int8_t specialChar = fReadErrorOccurred ? 0xFF : 0xFE;
    (*fServerRequestAlternativeByteHandler)(fServerRequestAlternativeByteHandlerClientData, specialChar);
  }
}

// Adopt the role of whatever registers on this socket. All RTPInterfaces on one
// TCP connection necessarily face the same way, so a disagreement means an
// assumption here is wrong -- report it and fall back to UNKNOWN rather than
// keep whichever value happened to arrive first.
void SocketDescriptor::noteStreamRole(StreamRole role) {
  if (role == STREAM_ROLE_UNKNOWN) return; // nothing to learn
  if (fStreamRole == role) return;

  if (fStreamRole == STREAM_ROLE_UNKNOWN) {
    fStreamRole = role;
    return;
  }

  fEnv << TX_CODE_UNEXPECTED << " SocketDescriptor(socket " << fOurSocketNum
       << "): interfaces disagree on direction (" << streamRoleName(fStreamRole)
       << " vs " << streamRoleName(role)
       << "). Treating as unknown so no teardown here blames a peer.\n";
  fStreamRole = STREAM_ROLE_UNKNOWN;
}

void SocketDescriptor::registerRTPInterface(unsigned char streamChannelId,
					    RTPInterface* rtpInterface) {
  Boolean isFirstRegistration = fSubChannelHashTable->IsEmpty();
  if (rtpInterface != NULL) noteStreamRole(rtpInterface->fStreamRole);
#if defined(DEBUG_SEND)||defined(DEBUG_RECEIVE)
  fprintf(stderr, "SocketDescriptor(socket %d)::registerRTPInterface(channel %d): isFirstRegistration %d\n", fOurSocketNum, streamChannelId, isFirstRegistration);
#endif
  // BasicHashTable::Add REPLACES an existing entry and returns the old value. Discarding that return
  // meant a second RTPInterface registering the same (socket, channel) silently displaced the first:
  // from then on that subsession's '$'-framed packets were demuxed to the WRONG interface, with no
  // error anywhere. The channel space is one byte (RFC 2326 sec 10.12) and the client's allocator can
  // wrap, so this is reachable rather than theoretical — see the exhaustion guard in
  // RTSPClient::sendSetupCommand. An unanticipated collision must be reported, never absorbed.
  void* displaced = fSubChannelHashTable->Add((char const*)(long)streamChannelId, rtpInterface);
  if (displaced != NULL && displaced != (void*)rtpInterface) {
    fEnv << TX_CODE_UNEXPECTED << " SocketDescriptor: interleaved channel "
	 << (int)streamChannelId << " on socket " << fOurSocketNum
	 << " was ALREADY registered to a different RTPInterface; the new registration has displaced"
	    " it, so the previous subsession will stop receiving. This means two tracks were handed"
	    " the same channel id.\n";
  }

  if (isFirstRegistration) {
    // Arrange to handle reads on this TCP socket:
    TaskScheduler::BackgroundHandlerProc* handler
      = (TaskScheduler::BackgroundHandlerProc*)&tcpReadHandler;
    fEnv.taskScheduler().
      setBackgroundHandling(fOurSocketNum, SOCKET_READABLE|SOCKET_EXCEPTION, handler, this);
  }
}

RTPInterface* SocketDescriptor
::lookupRTPInterface(unsigned char streamChannelId) {
  char const* lookupArg = (char const*)(long)streamChannelId;
  return (RTPInterface*)(fSubChannelHashTable->Lookup(lookupArg));
}

void SocketDescriptor
::deregisterRTPInterface(unsigned char streamChannelId) {
#if defined(DEBUG_SEND)||defined(DEBUG_RECEIVE)
  fprintf(stderr, "SocketDescriptor(socket %d)::deregisterRTPInterface(channel %d)\n", fOurSocketNum, streamChannelId);
#endif
  fSubChannelHashTable->Remove((char const*)(long)streamChannelId);

  if (fSubChannelHashTable->IsEmpty()) {
    // No more interfaces are using us, so it's curtains for us now:
    if (fAreInReadHandlerLoop) {
      fDeleteMyselfNext = True; // we can't delete ourself yet, but we'll do so from "tcpReadHandler()" below
    } else {
      delete this;
    }
  }
}

void SocketDescriptor::tcpReadHandler(SocketDescriptor* socketDescriptor, int mask) {
  // if this descriptor is already marked for deletion, stop it from waking the scheduler
  // NOW. When fDeleteMyselfNext was set during a *reentrant* call (fAreInReadHandlerLoop stuck True),
  // the loop below skips tcpReadHandler1 (no read) AND the delete is deferred to the outermost call
  // that has already returned -- so this fd stays in select's read set, readable (a half-closed camera
  // TCP socket), and tcpReadHandler is re-invoked forever with ZERO I/O syscalls: a ~65k/s pselect
  // CPU busy-loop (seen after an RTP-over-TCP framing-desync teardown; strace = 100% pselect6, 0 recvfrom).
  // Deregister the socket's background handling immediately; the object is still deleted below once we
  // reach the outermost (non-recursive) call.
  if (socketDescriptor->fDeleteMyselfNext && socketDescriptor->fOurSocketNum >= 0) {
    socketDescriptor->fEnv.taskScheduler().turnOffBackgroundReadHandling(socketDescriptor->fOurSocketNum);
    socketDescriptor->fEnv << TX_CODE_REENTER << " SocketDescriptor: socket "
			   << socketDescriptor->fOurSocketNum
                           << " was marked for deletion but still registered in the event loop -- "
                              "deregistering it to stop a zero-syscall pselect CPU busy-loop.\n";
  }

  // Call the read handler until it returns false, with a limit to avoid starving other sockets
  unsigned count = 2000;
  Boolean areInRecursiveCall = socketDescriptor->fAreInReadHandlerLoop;

  socketDescriptor->fAreInReadHandlerLoop = True;
  while (!socketDescriptor->fDeleteMyselfNext && socketDescriptor->tcpReadHandler1(mask) && --count > 0) {}
  if (!areInRecursiveCall) {
    socketDescriptor->fAreInReadHandlerLoop = False;
    if (socketDescriptor->fDeleteMyselfNext) delete socketDescriptor;
  }
}

// max run of non-'$' bytes tolerated while awaiting an interleaved frame boundary before
// declaring the RTP-over-TCP framing desynced. Real RTSP keepalive responses mid-stream are a few
// hundred bytes; anything past this is binary payload being misread as RTSP text (the busy-loop).
static unsigned const RTP_TCP_DESYNC_LIMIT = 2048;


// max consecutive select-readable dispatches with NO progress on the current interleaved packet's
// payload before we declare the stream stalled mid-packet and tear it down. A healthy stream makes
// progress on every readable dispatch, so this only trips on a dead/stalled peer; the small bound
// keeps the CPU busy-loop to a few tens of ms before teardown.
static unsigned const PACKET_DATA_STALL_LIMIT = 2000;

void SocketDescriptor::noteFarEndCloseProvenOnAllInterfaces() {
  // Socket-wide, not per-channel: the close killed the CONNECTION, so every subsession interleaved
  // on it is equally dead. Doing this for only the channel that happened to notice left the other
  // subsessions' RTPInterfaces reporting "no proven close" about a connection we had just proven
  // closed -- and the session layer reads the interfaces, not this descriptor.
  if (fSubChannelHashTable == NULL) return;
  HashTable::Iterator* iter = HashTable::Iterator::create(*fSubChannelHashTable);
  RTPInterface* rtpInterface;
  char const* key;
  while ((rtpInterface = (RTPInterface*)(iter->next(key))) != NULL) {
    rtpInterface->noteFarEndCloseProven();
  }
  delete iter;
}

Boolean SocketDescriptor::failAndTeardownClassified(char const* what, char const* detail) {
  // Why this exists: the read-side close proof used to be captured on ONE path only -- the
  // mid-packet stall. But readSocket() returns -1 for a peer close in whatever state the reader
  // happens to be in, and the common case is the HEADER state (between frames), not mid-payload.
  // So the most frequent proven close produced no proof at all: the session layer saw only silence,
  // waited out the full -D inactivity interval before reconnecting, and then logged "peer went
  // silent; nobody is proven at fault" about a close the kernel could have confirmed immediately.
  //
  // Sized for the full CONFIRMED/NOT-CONFIRMED text plus the measured link facts. Too small and
  // snprintf would silently truncate the "NOT CONFIRMED" caveat off the end, which is the single
  // most important part of the sentence.
  char who[768]; Boolean farEndClosed; char const* code = TX_CODE_STALL;
  classifyTcpTeardown(fOurSocketNum, who, sizeof(who), farEndClosed, fStallPeek, &code, fPeerDesc);

  // The classification above describes the SOCKET. What it MEANS depends on which
  // end of the proxy the socket serves, and that distinction was missing: a
  // downstream consumer closing (our recorder restarting, a viewer leaving) was
  // emitted as TX-FIN/TX-RST "Far-end close PROVEN", i.e. as a proven camera
  // fault. Measured over one hour on 6397272975e8c006c6fc: 166 such teardowns
  // came from 127.0.0.1 against 20 from the worst real camera, and check_flap
  // duly tallied them into "REAL upstream stalls -- FIX the camera link".
  //
  // So the code is remapped by role, and only an upstream teardown may claim the
  // far end closed on us or drive a reconnect. See StreamRole.hh.
  char const* const roleCode = teardownCodeForRole(fStreamRole, code);
  Boolean const mayBlame = teardownMayBlamePeer(fStreamRole) && farEndClosed;

  // Stream AGE is printed first because it is the discriminator an operator needs: a server
  // enforcing a session cap kills every stream at ~the same age; erratic firmware gives a scattered
  // spread. Correlate across events before concluding either (one age proves nothing).
  long const ageSecs = monotonicSeconds() - fStreamStartSec;
  fEnv << roleCode << " RTPInterface: " << streamRoleName(fStreamRole) << " " << what
       << " on socket " << fOurSocketNum
       << " ch" << (int)fStreamChannelId << " after " << (int)ageSecs << "s of streaming";
  if (detail != NULL) fEnv << " (" << detail << ")";
  fEnv << ". Cause: " << who;
  if (fStreamRole == STREAM_ROLE_UPSTREAM) {
    fEnv << (farEndClosed ? ". Far-end close PROVEN" : ". Far end NOT proven at fault")
         << " -- reconnecting.\n";
  } else if (fStreamRole == STREAM_ROLE_DOWNSTREAM) {
    // Routine. Say so plainly, and do NOT imply the camera did anything.
    fEnv << ". This is a LOCAL CONSUMER of our fan-out disconnecting, not a camera"
            " fault; the upstream stream is untouched.\n";
  } else {
    fEnv << ". Socket direction UNKNOWN, so no peer is blamed and no reconnect is"
            " triggered from here.\n";
  }

  // Was the peer still on the network at THIS instant? A sampled ping cannot tell a camera that
  // aborted and recovered from one that never went away; asking now can. Only worth asking upstream:
  // probing our own loopback consumer answers a question nobody has.
  if (fStreamRole == STREAM_ROLE_UPSTREAM) PeerProbe::start(fEnv, fPeerIp, fPeerPort, roleCode);
  if (mayBlame) noteFarEndCloseProvenOnAllInterfaces();
  return failAndTeardown();
}

Boolean SocketDescriptor::tcpReadHandler1(int mask) {
  // We expect the following data over the TCP channel:
  //   optional RTSP command or response bytes (before the first '$' character)
  //   a '$' character
  //   a 1-byte channel id
  //   a 2-byte packet size (in network byte order)
  //   the packet data.
  // However, because the socket is being read asynchronously, this data might arrive in pieces.

  // a closed/invalid socket (fd < 0) must NEVER be polled — every read returns
  // recvfrom(-1) = EBADF and, because nothing removes the handler, the scheduler re-invokes us
  // immediately => a ~33k/s CPU busy-loop (seen after a mis-framing peer forces a teardown).
  // Tear down cleanly here so the background read handler is removed instead of spinning forever.
  if (fOurSocketNum < 0) {
    // TX-coded (it was bare prose): without a code this line routed to the discardable stdout
    // stream and no tally could count it -- a silent sibling of the tagged guard above.
    fEnv << TX_CODE_EBADF << " SocketDescriptor: read handler ran on a closed socket (fd < 0); tearing down to stop an EBADF busy-loop.\n";
    return failAndTeardown();
  }

  u_int8_t c;
  struct sockaddr_storage dummy; // not used
  if (fTCPReadingState != AWAITING_PACKET_DATA) {
    int result = (fTLSState != NULL && fTLSState->isNeeded)
      ? fTLSState->read(&c, 1)
      : readSocket(fEnv, fOurSocketNum, &c, 1, dummy);
    if (result == 0) { // There was no more data to read
      return False;
    } else if (result != 1) { // error reading TCP socket, so we will no longer handle it
#ifdef DEBUG_RECEIVE
      fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): readSocket(1 byte) returned %d (error)\n", fOurSocketNum, result);
#endif
      // readSocket() maps a stream EOF to -1, so this branch is where a peer close lands whenever it
      // happens BETWEEN frames -- which is most of the time. Classify it instead of tearing down
      // mutely, or the most common proven close is the one we never prove.
      return failAndTeardownClassified("read failed in the interleaved-framing header state");
    }

    // Interleaved-framing DESYNC guard. Count EVERY header-state byte; reset only when a
    // valid channel id is parsed (AWAITING_STREAM_CHANNEL_ID below). Healthy streaming keeps this
    // <=4 (a '$'+chan+2-byte-size header, then a bulk payload read). A long run with no valid frame
    // = the RFC 2326 sec 10.12 '$'-framing is lost: we read binary payload one byte per recvfrom
    // (~30k/s) and feed it to the RTSP parser (O(n^2) -> a ~30% CPU busy-loop) with no video
    // extracted. Sample the first bytes for diagnosis, then force a reconnect to re-sync instead of
    // spinning. Immune to spurious 0x24 in binary payload (a "consecutive non-$" counter is not:
    // payload has 0x24 ~1/256 bytes, which would falsely reset it). GATED on fEverSawFrame so a
    // pure RTSP-control socket (a downstream consumer sending only keepalives, never '$' frames)
    // can never trip it — that was a real false positive (see fEverSawFrame).
    // NOT counted while deliberately discarding an unhandled frame's payload: those bytes are being
    // skipped by a length the frame itself declared, so framing is known-good, not lost. Counting
    // them would let the guard fire on the one path that CANNOT be desynchronised.
    if (fTCPReadingState == SKIPPING_UNHANDLED_PAYLOAD) {
      // fall through to the state machine without touching the desync counter
    } else {
    if (fBytesSinceFrame < sizeof(fAltByteSample)) fAltByteSample[fBytesSinceFrame] = c;
    if (fEverSawFrame && ++fBytesSinceFrame >= RTP_TCP_DESYNC_LIMIT) {
      char hexbuf[3*sizeof(fAltByteSample) + 1];
      char* hp = hexbuf;
      for (unsigned i = 0; i < sizeof(fAltByteSample); ++i) { snprintf(hp, 4, " %02x", fAltByteSample[i]); hp += 3; }
      *hp = '\0';
      fEnv << TX_CODE_DESYNC << " RTPInterface: RTP-over-TCP interleaved framing DESYNC on socket " << fOurSocketNum
	   << " (" << (int)fBytesSinceFrame << " bytes read with no valid '$'+channel+size frame; "
	   << "RFC 2326 sec 10.12 framing lost). First bytes:" << hexbuf
	   << " -- auto-recovering by reconnect. NOTE: framing loss can follow upstream packet loss "
	      "as well as a camera framing bug, so this alone does NOT prove a camera fault; only treat "
	      "it as a vendor issue if it is frequent on one camera AND a packet capture shows the "
	      "camera itself emitting mis-framed bytes.\n";
      return failAndTeardown();
    }
    }
  }

  Boolean callAgain = True;
  switch (fTCPReadingState) {
    case AWAITING_DOLLAR: {
      if (c == '$') {
#ifdef DEBUG_RECEIVE
	fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): Saw '$'\n", fOurSocketNum);
#endif
	fTCPReadingState = AWAITING_STREAM_CHANNEL_ID;
      } else {
	// This character is part of a RTSP request or command, which is handled separately:
	if (fServerRequestAlternativeByteHandler != NULL && c != 0xFF && c != 0xFE) {
	  // Hack: 0xFF and 0xFE are used as special signaling characters, so don't send them
	  (*fServerRequestAlternativeByteHandler)(fServerRequestAlternativeByteHandlerClientData, c);
	}
      }
      break;
    }
    case AWAITING_STREAM_CHANNEL_ID: {
      // The byte that we read is the stream channel id.
      //
      // Read the size field EITHER WAY. A channel we do not handle is not a reason to abandon
      // framing: the frame is self-describing, so we take its length and skip exactly its payload
      // (see AWAITING_SIZE2 / SKIPPING_UNHANDLED_PAYLOAD) and resume precisely on the next '$'.
      // Dropping to AWAITING_DOLLAR here -- with '$' and this byte already consumed -- is what left
      // the reader two bytes out of step and caused the production DESYNCs.
      fStreamChannelId = c;
      fFrameChannelRegistered = (lookupRTPInterface(c) != NULL);
      if (fFrameChannelRegistered) {
	fBytesSinceFrame = 0; // valid frame header parsed -> framing is in sync (reset desync guard)
	fEverSawFrame = True; // this socket really is a '$'-framed data channel -> desync guard may now arm
	fPacketStallCount = 0; // new frame boundary -> reset the mid-packet stall guard
	noteProgress();        // a valid frame header IS progress -> the stream is alive
	fStallPeek = TCP_PEEK_NONE; // and the captured stall-close evidence
      }
#ifdef DEBUG_RECEIVE
      else fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): Saw unregistered stream channel id: 0x%02x -- will skip its payload by the declared size\n", fOurSocketNum, c);
#endif
      // THE defect this whole header exists for: the shipped code used to set AWAITING_DOLLAR here
      // for an unregistered channel, having already consumed '$' and this byte and never reading the
      // size -- so it hunted for the next 0x24, which in H264 payload occurs ~1 byte in 256. Take
      // the transition from the tested state machine so it cannot silently drift back.
      {
	RTPInterleavedFraming::FrameStep const step =
	  RTPInterleavedFraming::frameStep(c, AWAITING_STREAM_CHANNEL_ID,
					   fFrameChannelRegistered == True, /*sizeByte1*/ 0);
	fTCPReadingState = step.nextState;
      }
      break;
    }
    case AWAITING_SIZE1: {
      // The byte that we read is the first (high) byte of the 16-bit RTP or RTCP packet 'size'.
      fSizeByte1 = c;
      fTCPReadingState = AWAITING_SIZE2;
      break;
    }
    case AWAITING_SIZE2: {
      // The byte that we read is the second (low) byte of the 16-bit RTP or RTCP packet 'size'.
      unsigned short size = (fSizeByte1<<8)|c;

      // A complete, well-formed header for a channel we do not handle. Discard exactly this frame's
      // payload -- no scanning, no guessing, no lost alignment -- and resume at the next boundary.
      //
      // The decision (including the size==0 case) comes from the pure, unit-tested state machine in
      // RTPInterleavedFraming.hh rather than being restated here. That header used to model this
      // rule ALONGSIDE a hand-written copy in this file: its tests passed while proving only the
      // model, so changing the code here could not fail them. Same defect class the eviction and
      // reset guards had -- a green test wired to nothing.
      if (!fFrameChannelRegistered) {
	RTPInterleavedFraming::FrameStep const step =
	  RTPInterleavedFraming::frameStep(c, AWAITING_SIZE2,
					   /*channelRegistered*/ false, fSizeByte1);
	fSkipBytesRemaining = step.skipBytes;
	fTCPReadingState = step.nextState;
	break;
      }

      // Record the information about the packet data that will be read next:
      RTPInterface* rtpInterface = lookupRTPInterface(fStreamChannelId);
      if (rtpInterface != NULL) {
	rtpInterface->fNextTCPReadSize = size;
	rtpInterface->fNextTCPReadStreamSocketNum = fOurSocketNum;
	rtpInterface->fNextTCPReadStreamChannelId = fStreamChannelId;
	rtpInterface->fNextTCPReadTLSState = fTLSState;
      }
      fTCPReadingState = AWAITING_PACKET_DATA;
      break;
    }
    case SKIPPING_UNHANDLED_PAYLOAD: {
      // Discard the payload of a frame whose channel we do not handle, using the length its own
      // header declared. Read in CHUNKS: the pre-existing no-handler path drains one byte per
      // syscall, which for a 1442-byte frame is 1442 read()s on a single-threaded event loop serving
      // ~47 cameras. The byte `c` already consumed by the caller counts as the first skipped byte.
      callAgain = False;
      if (fSkipBytesRemaining > 0) --fSkipBytesRemaining;
      while (fSkipBytesRemaining > 0) {
	unsigned char scratch[512];
	unsigned const want = fSkipBytesRemaining < sizeof(scratch) ? fSkipBytesRemaining : (unsigned)sizeof(scratch);
	struct sockaddr_storage dummy2;
	int const result = (fTLSState != NULL && fTLSState->isNeeded)
	  ? fTLSState->read(scratch, want)
	  : readSocket(fEnv, fOurSocketNum, scratch, want, dummy2);
	if (result < 0) return failAndTeardown(); // a real socket error -- never silently ignored
	if (result == 0) { callAgain = True; break; } // nothing available yet; resume on the next read

	fSkipBytesRemaining -= (unsigned)result;
      }
      if (fSkipBytesRemaining == 0) {
	// Landed exactly on the next frame boundary. Framing was never lost, so the desync guard must
	// not be left holding bytes counted against a frame we deliberately discarded.
	fBytesSinceFrame = 0;
	fTCPReadingState = AWAITING_DOLLAR;
	callAgain = True;
      }
      break;
    }
    case AWAITING_PACKET_DATA: {
      callAgain = False;
      fTCPReadingState = AWAITING_DOLLAR; // the next state, unless we end up having to read more data in the current state
      // Call the appropriate read handler to get the packet data from the TCP stream:
      RTPInterface* rtpInterface = lookupRTPInterface(fStreamChannelId);
      if (rtpInterface != NULL) {
	if (rtpInterface->fNextTCPReadSize == 0) {
	  // We've already read all the data for this packet.
	  break;
	}
	if (rtpInterface->fReadHandlerProc != NULL) {
#ifdef DEBUG_RECEIVE
	  fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): reading %d bytes on channel %d\n", fOurSocketNum, rtpInterface->fNextTCPReadSize, rtpInterface->fNextTCPReadStreamChannelId);
#endif
	  fTCPReadingState = AWAITING_PACKET_DATA;
	  unsigned const sizeBefore = rtpInterface->fNextTCPReadSize;

	  // Peek BEFORE the consuming read. A socket's pending error is READ-AND-CLEAR: when a peer
	  // sends RST *after* sending data, our own recv() takes the ECONNRESET (and SO_ERROR resets to
	  // 0), so every probe afterwards sees plain EOF and the teardown is misreported as a graceful
	  // FIN. That is not hypothetical -- it was reproduced 5/5, and it means an unknown share of the
	  // FINs seen in production were really aborts. Peeking first is the only reliable order; the
	  // errno-after-read alternative is unsafe because readSocket() calls socketErr() on the error
	  // path, which may clobber errno before we could read it.
	  //
	  // COST, measured honestly: this state is entered once per RTP PACKET (~300/s on a 4 Mbps
	  // stream), not once per video frame, and it re-enters when a payload spans TCP segments. The
	  // guard below also does NOT short-circuit in steady state, because fStallPeek is reset on every
	  // frame header and every payload advance — so in healthy streaming this peek runs on essentially
	  // every packet. That is the price of correct RST-after-data attribution, and it is paid
	  // knowingly: one non-consuming recv against silently misreporting every abort as a graceful close.
	  int prePeek = TCP_PEEK_NONE;
	  if (fStallPeek != TCP_PEEK_FIN && fStallPeek != TCP_PEEK_RST) {
	    prePeek = probeTcpClose(fOurSocketNum);
	    if (prePeek == TCP_PEEK_RST || prePeek == TCP_PEEK_FIN) fStallPeek = prePeek;
	  }

	  rtpInterface->fReadHandlerProc(rtpInterface->fOwner, mask);
	  // Mid-packet stall guard. If the payload made NO progress, the peer sent a '$'+channel+size
	  // header then stopped/died before the payload arrived. A dead/stalled socket stays
	  // select-readable, so we'd be re-dispatched forever and pin a CPU core. Count consecutive
	  // no-progress dispatches; past the limit, treat the interleaved stream as broken and tear it
	  // down so it can reconnect. A healthy stream always makes progress while readable, so this
	  // never trips on a merely-slow packet.
	  if (rtpInterface->fNextTCPReadSize >= sizeBefore) {
	    // Reuse the pre-read peek rather than issuing a second one: this dispatch already probed a
	    // moment ago (just above), and on a stall that peek is the SAME answer. Re-probing here
	    // doubled the syscalls during exactly the spin the stall limit exists to bound. Only probe
	    // afresh if the pre-read peek was skipped because we already hold a definitive result.
	    if (fStallPeek != TCP_PEEK_FIN && fStallPeek != TCP_PEEK_RST) {
	      fStallPeek = (prePeek != TCP_PEEK_NONE) ? prePeek : probeTcpClose(fOurSocketNum);
	    }
	    // Short-circuit: a captured FIN/RST is PROOF the far end closed, so tear down at once rather
	    // than spinning to the full dispatch limit (that spin was a real, if brief, CPU burn). The
	    // limit still governs the still-open / WOULDBLOCK case (possible mid-path loss).
	    Boolean provenClose = (fStallPeek == TCP_PEEK_FIN || fStallPeek == TCP_PEEK_RST);
	    if (++fPacketStallCount >= PACKET_DATA_STALL_LIMIT || provenClose) {
	      // ALWAYS logged (not DEBUG-only): an abrupt mid-packet stop is a real fault to record.
	      // Use the teardown classifier so the log states WHO stopped the stream from VERIFIED
	      // kernel state (captured FIN/RST peek first, then live SO_ERROR / getpeername / TCP_INFO),
	      // never a guess — and so it only implicates the camera when there is PROOF (FIN or RST); a
	      // still-open-but-silent socket is reported as "mid-path packet loss", NOT a camera fault.
	      // The classify + log + peer-probe + hand-the-proof-to-the-session-layer sequence is the
	      // SAME one every other classified teardown runs, so it lives in one place; only the
	      // never-arrived payload count is specific to this path.
	      char detail[64];
	      snprintf(detail, sizeof(detail), "%d payload bytes never arrived",
		       (int)rtpInterface->fNextTCPReadSize);
	      return failAndTeardownClassified("stream stalled mid-packet", detail);
	    }
	  } else {
	    fPacketStallCount = 0;
	    noteProgress(); // payload advanced -> clear the stall latch so a later stall reports again
	    fStallPeek = TCP_PEEK_NONE; // payload made progress -> forget any prior stall-close evidence
	  }
	} else {
#ifdef DEBUG_RECEIVE
	  fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): No handler proc for \"rtpInterface\" for channel %d; need to skip %d remaining bytes\n", fOurSocketNum, fStreamChannelId, rtpInterface->fNextTCPReadSize);
#endif
	  int result = (fTLSState != NULL && fTLSState->isNeeded)
	    ? fTLSState->read(&c, 1)
	    : readSocket(fEnv, fOurSocketNum, &c, 1, dummy);
	  if (result < 0) { // error reading TCP socket, so we will no longer handle it
#ifdef DEBUG_RECEIVE
	    fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): readSocket(1 byte) returned %d (error)\n", fOurSocketNum, result);
#endif
	    return failAndTeardownClassified("read failed while discarding an unhandled channel's payload");
	  } else {
	    fTCPReadingState = AWAITING_PACKET_DATA;
	    if (result == 1) {
	      --rtpInterface->fNextTCPReadSize;
	      callAgain = True;
	    }
	  }
	}
      }
#ifdef DEBUG_RECEIVE
      else fprintf(stderr, "SocketDescriptor(socket %d)::tcpReadHandler(): No \"rtpInterface\" for channel %d\n", fOurSocketNum, fStreamChannelId);
#endif
    }
  }

  return callAgain;
}


////////// tcpStreamRecord implementation //////////

tcpStreamRecord
::tcpStreamRecord(int streamSocketNum, unsigned char streamChannelId,
		  TLSState* tlsState,
		  tcpStreamRecord* next)
  : fNext(next),
    fStreamSocketNum(streamSocketNum), fStreamChannelId(streamChannelId),
    fTLSState(tlsState), fFramesDropped(0), fEvictStrikes(0), fBlockedSinceSec(0),
    fSndBufSize(tcpSendBufferSize(streamSocketNum)) {
}

tcpStreamRecord::~tcpStreamRecord() {
  delete fNext;
}

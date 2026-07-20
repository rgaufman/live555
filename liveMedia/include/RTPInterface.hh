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
// C++ header

#ifndef _RTP_INTERFACE_HH
#define _RTP_INTERFACE_HH

#ifndef _MEDIA_HH
#include "Media.hh"
#endif
#ifndef _TLS_STATE_HH
#include "TLSState.hh"
#endif
#ifndef _GROUPSOCK_HH
#include <Groupsock.hh>
#endif
#ifndef _STREAM_ROLE_HH
#include "StreamRole.hh"
#endif

// Typedef for an optional auxilliary handler function, to be called
// when each new packet is read:
typedef void AuxHandlerFunc(void* clientData, unsigned char* packet,
			    unsigned& packetSize);

typedef void ServerRequestAlternativeByteHandler(void* instance, u_int8_t requestByte);
// A hack that allows a handler for RTP/RTCP packets received over TCP to process RTSP commands that may also appear within
// the same TCP connection.  A RTSP server implementation would supply a function like this - as a parameter to
// "ServerMediaSubsession::startStream()".

class RTPInterface {
public:
  RTPInterface(Medium* owner, Groupsock* gs);
  virtual ~RTPInterface();

  Groupsock* gs() const { return fGS; }

  void setStreamSocket(int sockNum, unsigned char streamChannelId, TLSState* tlsState);
  void addStreamSocket(int sockNum, unsigned char streamChannelId, TLSState* tlsState);
  void removeStreamSocket(int sockNum, unsigned char streamChannelId);
  static void setServerRequestAlternativeByteHandler(UsageEnvironment& env, int socketNum,
						     ServerRequestAlternativeByteHandler* handler, void* clientData);
  static void clearServerRequestAlternativeByteHandler(UsageEnvironment& env, int socketNum);

  Boolean sendPacket(unsigned char* packet, unsigned packetSize);
  void startNetworkReading(TaskScheduler::BackgroundHandlerProc*
                           handlerProc);
  Boolean handleRead(unsigned char* buffer, unsigned bufferMaxSize,
		     // out parameters:
		     unsigned& bytesRead, struct sockaddr_storage& fromAddress,
		     int& tcpSocketNum, unsigned char& tcpStreamChannelId,
		     Boolean& packetReadWasIncomplete);
  // Note: If "tcpSocketNum" < 0, then the packet was received over UDP, and "tcpStreamChannelId"
  //   is undefined (and irrelevant).


  // Otherwise (if "tcpSocketNum" >= 0), the packet was received (interleaved) over TCP, and
  //   "tcpStreamChannelId" will return the channel id.

  void stopNetworkReading();

  UsageEnvironment& envir() const { return fOwner->envir(); }

  void setAuxilliaryReadHandler(AuxHandlerFunc* handlerFunc,
				void* handlerClientData) {
    fAuxReadHandlerFunc = handlerFunc;
    fAuxReadHandlerClientData = handlerClientData;
  }

  // Did the teardown classifier PROVE the far end closed this stream (a captured FIN or RST)?
  //
  // A timer exists to INFER a fault that cannot be observed directly. Once the fault IS observed,
  // waiting for the timer adds only dead air, and reporting the timer's inference afterwards
  // contradicts the proof. This flag is how the proof reaches the session layer, which owns
  // re-establishment. See SessionRecoveryPolicy.hh for the decision, and test_session_recovery.cpp.
  //
  // Measured cost of not having it, across ~50 cameras over 30 min: 90 of 236 proven closes (38%)
  // produced a second, contradictory "nobody is proven at fault" line ~18s later, and recovery took
  // a median 26.8s (max 235.1s) when the far end was already provably gone.
  void noteFarEndCloseProven() { fFarEndCloseProven = True; }
  Boolean farEndCloseProven() const { return fFarEndCloseProven; }

  void forgetOurGroupsock() { fGS = NULL; fStoredReadFd = -1; }
    // This may be called - *only immediately prior* to deleting this - to prevent our destructor
    // from turning off background reading on the 'groupsock'.  (This is in case the 'groupsock'
    // is also being read from elsewhere.)
    // also clear fStoredReadFd so stopNetworkReading() won't turn off the shared fd's
    // handler (which belongs to the other reader) via our stored-fd fallback.

private:
  // Helper functions for sending a RTP or RTCP packet over a TCP connection:
  Boolean sendRTPorRTCPPacketOverTCP(unsigned char* packet, unsigned packetSize,
				     int socketNum, unsigned char streamChannelId,
				     TLSState* tlsState);
  Boolean sendDataOverTCP(int socketNum, TLSState* tlsState,
			  u_int8_t const* data, unsigned dataSize, Boolean forceSendToSucceed,
			  int* outErrno = NULL); // set to the failing send's errno (captured at the send, before teardown syscalls clobber it)

private:
  friend class SocketDescriptor;
  Medium* fOwner;
  Groupsock* fGS;
  // UPSTREAM (we receive from the camera) vs DOWNSTREAM (we send to a local
  // consumer). Fixed at construction from the owner's direction. Only an
  // upstream teardown may be logged as a camera fault or trigger a reconnect
  // -- see StreamRole.hh for the measurement that made this necessary.
  StreamRole fStreamRole;
  class tcpStreamRecord* fTCPStreams; // optional, for RTP-over-TCP streaming/receiving

  unsigned short fNextTCPReadSize;
    // how much data (if any) is available to be read from the TCP stream
  int fNextTCPReadStreamSocketNum;
  unsigned char fNextTCPReadStreamChannelId;
  TLSState* fNextTCPReadTLSState;
  TaskScheduler::BackgroundHandlerProc* fReadHandlerProc; // if any

  // the fd we registered our datagram (UDP) background-read handler on, in
  // startNetworkReading(). Kept so we can deregister the handler by *that* fd if the
  // groupsock socket is later reset to -1 without our handler being turned off. Without
  // this, SingleStep keeps dispatching the stale handler under the original fd while
  // fGS->socketNum() == -1, so every readSocket(-1) returns EBADF and pins a CPU core
  // (the ~30% recvfrom(-1) busy-loop observed on force_tcp proxies whose upstream died).
  int fStoredReadFd;

  AuxHandlerFunc* fAuxReadHandlerFunc;
  void* fAuxReadHandlerClientData;

  // Set once the teardown classifier PROVES a far-end close on this stream; cleared in
  // setStreamSocket() so a re-established connection never inherits the previous generation's
  // verdict (a stale True would reset the fresh session immediately, in a loop).
  Boolean fFarEndCloseProven;

  // Coarse outgoing send-rate estimate (bytes/sec) over a ~1s sliding window, maintained in
  // sendPacket(). Used only to convert a downstream consumer's queued bytes into a TIME-behind
  // figure for the always-on drift warning (e.g. a recorder whose disk can't keep up shows up
  // as ">100 ms behind"). A rough estimate is fine BECAUSE it drives only the advisory warning:
  // eviction is measured in wall-clock elapsed time and never from this figure.
  //
  // It used to feed eviction, which was a real defect — the byte-derived "time behind" is capped by
  // SO_SNDBUF, so it could never reach the 5s threshold whenever sndbuf/rate was under 5s, making
  // eviction silently unreachable. Keep this advisory. If a control decision ever needs a rate, it
  // needs a better one than a coarse 1s window.
  struct timeval fSendRateWindowStart;
  unsigned fSendRateAccumBytes;
  double fSendBytesPerSec;
};

#endif

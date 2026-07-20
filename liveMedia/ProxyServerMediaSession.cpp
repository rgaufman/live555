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
// A subclass of "ServerMediaSession" that can be used to create a (unicast) RTSP servers that acts as a 'proxy' for
// another (unicast or multicast) RTSP/RTP stream.
// Implementation

#include "liveMedia.hh"
#include "RTSPCommon.hh"
#include "GroupsockHelper.hh" // for "our_random()"
#include "RTSPKeepAlive.hh" // keep-alive timing/method policy (header-only, unit-tested)
#include "StreamFaultCodes.hh" // stable TX_CODE_* tokens the log tally greps
#include "SessionRecoveryPolicy.hh" // when/why to re-establish a back-end session (header-only, unit-tested)
#include "ResetReasons.hh" // THE fixed vocabulary of session-rebuild reasons
#include "MonotonicTime.hh" // durations must never be measured with a steppable wall clock

#ifndef MILLION
#define MILLION 1000000
#endif

// A "OnDemandServerMediaSubsession" subclass, used to implement a unicast RTSP server that's proxying another RTSP stream:

class ProxyServerMediaSubsession: public OnDemandServerMediaSubsession {
public:
  ProxyServerMediaSubsession(MediaSubsession& mediaSubsession,
			     portNumBits initialPortNum, Boolean multiplexRTCPWithRTP);
  virtual ~ProxyServerMediaSubsession();

  char const* codecName() const { return fCodecName; }
  char const* url() const { return ((ProxyServerMediaSession*)fParentSession)->url(); }

private: // redefined virtual functions
  virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
                                              unsigned& estBitrate);
  virtual void closeStreamSource(FramedSource *inputSource);
  virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                    unsigned char rtpPayloadTypeIfDynamic,
                                    FramedSource* inputSource);
  virtual Groupsock* createGroupsock(struct sockaddr_storage const& addr, Port port);
  virtual RTCPInstance* createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
				   unsigned char const* cname, RTPSink* sink);

private:
  static void subsessionByeHandler(void* clientData);
  void subsessionByeHandler();

  int verbosityLevel() const { return ((ProxyServerMediaSession*)fParentSession)->fVerbosityLevel; }

private:
  friend class ProxyRTSPClient;
  MediaSubsession& fClientMediaSubsession; // the 'client' media subsession object that corresponds to this 'server' media subsession
  char const* fCodecName;  // copied from "fClientMediaSubsession" once it's been set up
  ProxyServerMediaSubsession* fNext; // used when we're part of a queue
  Boolean fHaveSetupStream;
};


////////// ProxyServerMediaSession implementation //////////

UsageEnvironment& operator<<(UsageEnvironment& env, const ProxyServerMediaSession& psms) { // used for debugging
  return env << "ProxyServerMediaSession[" << psms.url() << "]";
}

ProxyRTSPClient*
defaultCreateNewProxyRTSPClientFunc(ProxyServerMediaSession& ourServerMediaSession,
				    char const* rtspURL,
				    char const* username, char const* password,
				    portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
				    int socketNumToServer, unsigned interPacketGapMaxTime, Boolean neverPause) {
  return new ProxyRTSPClient(ourServerMediaSession, rtspURL, username, password,
			     tunnelOverHTTPPortNum, verbosityLevel, socketNumToServer, interPacketGapMaxTime, neverPause);
}

ProxyServerMediaSession* ProxyServerMediaSession
::createNew(UsageEnvironment& env, GenericMediaServer* ourMediaServer,
	    char const* inputStreamURL, char const* streamName,
	    char const* username, char const* password,
	    portNumBits tunnelOverHTTPPortNum, int verbosityLevel, int socketNumToServer,
	    MediaTranscodingTable* transcodingTable, unsigned interPacketGapMaxTime, Boolean neverPause) {
  return new ProxyServerMediaSession(env, ourMediaServer, inputStreamURL, streamName, username, password,
				     tunnelOverHTTPPortNum, verbosityLevel, socketNumToServer,
				     transcodingTable, interPacketGapMaxTime, neverPause);
}


ProxyServerMediaSession
::ProxyServerMediaSession(UsageEnvironment& env, GenericMediaServer* ourMediaServer,
			  char const* inputStreamURL, char const* streamName,
			  char const* username, char const* password,
			  portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
			  int socketNumToServer,
			  MediaTranscodingTable* transcodingTable,
			  unsigned interPacketGapMaxTime, Boolean neverPause,
			  createNewProxyRTSPClientFunc* ourCreateNewProxyRTSPClientFunc,
			  portNumBits initialPortNum, Boolean multiplexRTCPWithRTP)
  : ServerMediaSession(env, streamName, NULL, NULL, False, NULL),
    describeCompletedFlag(0), fOurMediaServer(ourMediaServer), fClientMediaSession(NULL),
    fVerbosityLevel(verbosityLevel),
    fPresentationTimeSessionNormalizer(new PresentationTimeSessionNormalizer(envir())),
    fCreateNewProxyRTSPClientFunc(ourCreateNewProxyRTSPClientFunc),
    fTranscodingTable(transcodingTable),
    fInitialPortNum(initialPortNum), fMultiplexRTCPWithRTP(multiplexRTCPWithRTP) {
  // Open a RTSP connection to the input stream, and send a "DESCRIBE" command.
  // We'll use the SDP description in the response to set ourselves up.
  fProxyRTSPClient
    = (*fCreateNewProxyRTSPClientFunc)(*this, inputStreamURL, username, password,
				       tunnelOverHTTPPortNum,
				       verbosityLevel > 0 ? verbosityLevel-1 : verbosityLevel,
				       socketNumToServer, interPacketGapMaxTime, neverPause);
  fProxyRTSPClient->sendDESCRIBE();
}

ProxyServerMediaSession::~ProxyServerMediaSession() {
  if (fVerbosityLevel > 0) {
    envir() << *this << "::~ProxyServerMediaSession()\n";
  }

  // Begin by sending a "TEARDOWN" command (without checking for a response):
  if (fProxyRTSPClient != NULL && fClientMediaSession != NULL) {
    fProxyRTSPClient->sendTeardownCommand(*fClientMediaSession, NULL, fProxyRTSPClient->auth());
  }

  // Then delete our state:
  Medium::close(fClientMediaSession);
  Medium::close(fProxyRTSPClient); fProxyRTSPClient = NULL;
  Medium::close(fPresentationTimeSessionNormalizer);
}

char const* ProxyServerMediaSession::url() const {
  return fProxyRTSPClient == NULL ? "" : fProxyRTSPClient->url();
}

void ProxyServerMediaSession::getStreamStats(StreamStats& stats) const {
  // Cheap snapshot for periodic operational logging. We reach into the back-end
  // RTSPClient and its reception stats the same way the inter-packet-gap watchdog
  // does (see ProxyRTSPClient::checkInterPacketGaps_), so the packet count here is
  // the exact liveness signal the watchdog acts on.
  stats.describeCompleted = describeCompletedSuccessfully();
  stats.streamRTPOverTCP = fProxyRTSPClient != NULL && fProxyRTSPClient->fStreamRTPOverTCP;
  stats.totNumPacketsReceived = 0;
  stats.totNumKBytesReceived = 0.0;

  if (fClientMediaSession == NULL) return; // back-end DESCRIBE not (yet) completed

  MediaSubsessionIterator iter(*fClientMediaSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    RTPSource* src = subsession->rtpSource();
    if (src == NULL) continue;
    RTPReceptionStatsDB& db = src->receptionStatsDB();
    stats.totNumPacketsReceived += db.totNumPacketsReceived();
    // Byte totals live per-SSRC; sum across all SSRCs (usually just one). Include
    // inactive sources so bytes from an SSRC that has gone quiet are not dropped.
    RTPReceptionStatsDB::Iterator statsIter(db);
    RTPReceptionStats* perSSRC;
    while ((perSSRC = statsIter.next(True)) != NULL) {
      stats.totNumKBytesReceived += perSSRC->totNumKBytesReceived();
    }
  }
}

Groupsock* ProxyServerMediaSession
::createGroupsock(struct sockaddr_storage const& addr, Port port) {
  // Default implementation; may be redefined by subclasses:
  return new Groupsock(envir(), addr, port, 255);
}

RTCPInstance* ProxyServerMediaSession
::createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
	     unsigned char const* cname, RTPSink* sink) {
  // Default implementation; may be redefined by subclasses:
  return RTCPInstance::createNew(envir(), RTCPgs, totSessionBW, cname, sink, NULL/*we're a server*/);
}

Boolean ProxyServerMediaSession::allowProxyingForSubsession(MediaSubsession const& /*mss*/) {
  // Default implementation
  return True;
}

void ProxyServerMediaSession::continueAfterDESCRIBE(char const* sdpDescription) {
  describeCompletedFlag = 1;

  // Create a (client) "MediaSession" object from the stream's SDP description ("resultString"), then iterate through its
  // "MediaSubsession" objects, to set up corresponding "ServerMediaSubsession" objects that we'll use to serve the stream's tracks.
  do {
    fClientMediaSession = MediaSession::createNew(envir(), sdpDescription);
    if (fClientMediaSession == NULL) break;

    MediaSubsessionIterator iter(*fClientMediaSession);
    for (MediaSubsession* mss = iter.next(); mss != NULL; mss = iter.next()) {
      if (!allowProxyingForSubsession(*mss)) continue;

      ServerMediaSubsession* smss
	= new ProxyServerMediaSubsession(*mss, fInitialPortNum, fMultiplexRTCPWithRTP);
      addSubsession(smss);
      if (fVerbosityLevel > 0) {
	envir() << *this << " added new \"ProxyServerMediaSubsession\" for "
		<< mss->protocolName() << "/" << mss->mediumName() << "/" << mss->codecName() << " track\n";
      }
    }
  } while (0);
}

void ProxyServerMediaSession::resetDESCRIBEState() {
  // Delete all of our "ProxyServerMediaSubsession"s; they'll get set up again once we get a response to the new "DESCRIBE".
  if (fOurMediaServer != NULL) {
    // First, close any client connections that may have already been set up:
    fOurMediaServer->closeAllClientSessionsForServerMediaSession(this);
  }
  deleteAllSubsessions();

  // Finally, delete the client "MediaSession" object that we had set up after receiving the response to the previous "DESCRIBE":
  Medium::close(fClientMediaSession); fClientMediaSession = NULL;
}

///////// RTSP 'response handlers' //////////

static void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
  char const* res;

  if (resultCode == 0) {
    // The "DESCRIBE" command succeeded, so "resultString" should be the stream's SDP description.
    res = resultString;
  } else {
    // The "DESCRIBE" command failed. Reported HERE, where the resultCode still exists, before the
    // backoff ladder swallows it.
    ((ProxyRTSPClient*)rtspClient)->noteDescribeFailed(resultCode);
    res = NULL;
  }
  ((ProxyRTSPClient*)rtspClient)->continueAfterDESCRIBE(res);
  delete[] resultString;
}

static void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
  ((ProxyRTSPClient*)rtspClient)->continueAfterSETUP(resultCode);
  delete[] resultString;
}

static void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
  ((ProxyRTSPClient*)rtspClient)->continueAfterPLAY(resultCode);
  delete[] resultString;
}

static void continueAfterOPTIONS(RTSPClient* rtspClient, int resultCode, char* resultString) {
  Boolean serverSupportsGetParameter = False, serverSupportsSetParameter = False;
  if (resultCode == 0) {
    // Note which 'liveness'-capable commands the server told us it supports. Both matter: the
    // keep-alive follows GStreamer's order (SET > GET > OPTIONS), and plenty of cameras advertise
    // exactly one of the two (the Merit-Lilin under test advertises SET_PARAMETER and not GET_).
    serverSupportsGetParameter = RTSPOptionIsSupported("GET_PARAMETER", resultString);
    serverSupportsSetParameter = RTSPOptionIsSupported("SET_PARAMETER", resultString);
  }
  ((ProxyRTSPClient*)rtspClient)->continueAfterLivenessCommand(resultCode, serverSupportsGetParameter,
							      serverSupportsSetParameter);
  delete[] resultString;
}

// A *_PARAMETER liveness ping succeeded, so the capability we used is still valid. We deliberately do
// NOT re-derive the other capability here: only an OPTIONS response carries "Public:", so re-probing
// it would mean sending an extra command per keep-alive. Carry the known state forward instead.
static void continueAfterLivenessPARAMETER(RTSPClient* rtspClient, int resultCode, char* resultString) {
  ProxyRTSPClient* proxy = (ProxyRTSPClient*)rtspClient;
  proxy->continueAfterLivenessCommand(resultCode, proxy->serverSupportsGetParameter(),
				      proxy->serverSupportsSetParameter());
  delete[] resultString;
}


////////// "ProxyRTSPClient" implementation /////////

UsageEnvironment& operator<<(UsageEnvironment& env, const ProxyRTSPClient& proxyRTSPClient) { // used for debugging
  return env << "ProxyRTSPClient[" << proxyRTSPClient.url() << "]";
}

ProxyRTSPClient::ProxyRTSPClient(ProxyServerMediaSession& ourServerMediaSession, char const* rtspURL,
				 char const* username, char const* password,
				 portNumBits tunnelOverHTTPPortNum, int verbosityLevel, int socketNumToServer,
				 unsigned interPacketGapMaxTime, Boolean neverPause)
  : RTSPClient(ourServerMediaSession.envir(), rtspURL, verbosityLevel, "ProxyRTSPClient",
	       tunnelOverHTTPPortNum == (portNumBits)(~0) ? 0 : tunnelOverHTTPPortNum, socketNumToServer),
    fOurServerMediaSession(ourServerMediaSession), fOurURL(strDup(rtspURL)), fStreamRTPOverTCP(tunnelOverHTTPPortNum != 0),
    fUsingHTTPTunnel(tunnelOverHTTPPortNum != 0 && tunnelOverHTTPPortNum != (portNumBits)(~0)),
    fSetupQueueHead(NULL), fSetupQueueTail(NULL), fNumSetupsSent(0), fNextDESCRIBEDelay(1),
    fTotNumPacketsReceived(PACKET_COUNT_UNKNOWN), fInterPacketGapMaxTime(interPacketGapMaxTime), fNeverPause(neverPause),
    fServerSupportsGetParameter(False), fServerSupportsSetParameter(False),
    fLastCommandWasPLAY(False), fDoneDESCRIBE(False), fLastLivenessMethod(RTSP_LIVENESS_OPTIONS),
    fHavePlayedBefore(False), fLivenessReconnectAttempted(False), fLifecycleState(SL_INIT),
    fLivenessCommandTask(NULL), fDESCRIBECommandTask(NULL), fSubsessionTimerTask(NULL), fResetTask(NULL),
    fInterPacketGapsTask(NULL), fSetupRetryTask(NULL), fRequestDeadlineTask(NULL),
    fGuardedSetupCSeq(0), fLivenessDeadlineTask(NULL), fConsecutiveLivenessTimeouts(0) {
  if (username != NULL && password != NULL) {
    fOurAuthenticator = new Authenticator(username, password);
  } else {
    fOurAuthenticator = NULL;
  }
  fLastPacketProgressSec = 0; // set on first packet-count progress
}

void ProxyRTSPClient::noteLifecycle(SessionEvent event) {
  SessionTransition const t = sessionTransition(fLifecycleState, event);

  if (!t.legal) {
    // ALWAYS logged, at any verbosity, with a stable greppable code: an illegal transition means our
    // model of the session and the session's actual behaviour have diverged. That divergence is what
    // hid the double-reset and the discarded-DESCRIBE defects for weeks -- each was individually
    // plausible in a log read line by line, and only impossible when read against the state machine.
    // The state is deliberately NOT advanced: a rejected event must not corrupt the model as well.
    envir() << TX_CODE_UNEXPECTED << " ProxyRTSPClient: illegal session transition -- "
            << sessionEventName(event) << " while " << sessionStateName(fLifecycleState)
            << ": " << t.reason << " (" << fOurURL << ")\n";
    return;
  }

  if (fVerbosityLevel > 0 && t.nextState != fLifecycleState) {
    envir() << "ProxyRTSPClient: session " << sessionStateName(fLifecycleState) << " -> "
            << sessionStateName(t.nextState) << " on " << sessionEventName(event) << "\n";
  }
  fLifecycleState = t.nextState;
}

// The PAUSE we send carries no response handler, so this records the COMMITMENT -- we stopped
// expecting media -- not a server acknowledgement (hence SE_PAUSE_SENT, not an _OK event; RFC 2326
// A.2: PAUSE is Playing -> Ready). Disarming the gap watchdog is the other half of the same fact:
// silence is now the expected state, and a watchdog left armed would blame the camera for it.
// A method rather than three lines in ProxyServerMediaSubsession::closeStreamSource, so the
// lifecycle machine is driven only from inside this class.
void ProxyRTSPClient::notePauseSent() {
  noteLifecycle(SE_PAUSE_SENT);
  fLastCommandWasPLAY = False;
  envir().taskScheduler().unscheduleDelayedTask(fInterPacketGapsTask); fInterPacketGapsTask = NULL;
}

void ProxyRTSPClient::reset() {
  envir().taskScheduler().unscheduleDelayedTask(fLivenessCommandTask);
  envir().taskScheduler().unscheduleDelayedTask(fDESCRIBECommandTask);
  envir().taskScheduler().unscheduleDelayedTask(fSubsessionTimerTask);
  envir().taskScheduler().unscheduleDelayedTask(fResetTask);
  envir().taskScheduler().unscheduleDelayedTask(fInterPacketGapsTask); fInterPacketGapsTask = NULL;
  // A pending delayed SETUP re-send refers to fSetupQueueHead, which this function is about to NULL.
  // Leaving it scheduled would fire it against the torn-down queue of a session that no longer
  // exists -- a callback on freed state, the class of bug the lifecycle model exists to surface.
  envir().taskScheduler().unscheduleDelayedTask(fSetupRetryTask); fSetupRetryTask = NULL;
  // Likewise the response deadline: it refers to a request belonging to the session being torn down.
  envir().taskScheduler().unscheduleDelayedTask(fRequestDeadlineTask); fRequestDeadlineTask = NULL;
  fGuardedSetupCSeq = 0;
  // And the keep-alive no-reply deadline, for the same reason; its unanswered-run counter restarts
  // with the rebuilt session.
  envir().taskScheduler().unscheduleDelayedTask(fLivenessDeadlineTask); fLivenessDeadlineTask = NULL;
  fConsecutiveLivenessTimeouts = 0;

  fSetupQueueHead = fSetupQueueTail = NULL;
  fNumSetupsSent = 0;
  fNextDESCRIBEDelay = 1;
  fLastCommandWasPLAY = False;
  fLivenessReconnectAttempted = False; // the rebuilt session's first incident gets its own budget
  fTotNumPacketsReceived = PACKET_COUNT_UNKNOWN;
  fDoneDESCRIBE = False;
  // The rebuilt session gets a FRESH retry budget. Omitting this (it was omitted) means a session
  // that spent its budget is reborn already exhausted: the new session's very first SETUP failure
  // resets again immediately, with zero retries, and stays that way until a SETUP happens to
  // succeed. That is the always-reset behaviour this whole policy removes, returning permanently by
  // a slower route -- and it would look identical in the logs to the bug we started from.
  fRequestRetry.clear();

  RTSPClient::reset();
}

ProxyRTSPClient::~ProxyRTSPClient() {
  // Terminal: any callback that fires after this is running on freed memory, and the state machine
  // is the only thing positioned to say so rather than let it be misreported as something else.
  noteLifecycle(SE_CLOSED);
  reset();

  delete fOurAuthenticator;
  delete[] fOurURL;
}

int ProxyRTSPClient::connectToServer(int socketNum, portNumBits remotePortNum) {
  int res;
  res = RTSPClient::connectToServer(socketNum, remotePortNum);
  
  // Replacing the RTSP connection invalidates RTP-over-TCP subsessions interleaved on the OLD socket,
  // so those cases need a full re-DESCRIBE. But ONLY those: before the first SETUP nothing is bound
  // to the connection, and resetting there threw away a DESCRIBE that had just succeeded, making
  // every reconnect run two complete DESCRIBE/SETUP/PLAY cycles. Predicate + tests:
  // SessionRecoveryPolicy.hh / test_session_recovery.cpp; captured evidence:
  // spec/fixtures/diagnostics/live555_gapchk_reconnect_cycle.txt (tetherbox repo).
  // res: -1 = connect failed, 0 = PENDING (EINPROGRESS -- the normal case, sockets are non-blocking),
  // 1 = succeeded immediately. Both 0 and 1 mean a NEW connection is replacing the old one.
  if (reconnectRequiresFullReset(res >= 0 ? True : False, fDoneDESCRIBE, fStreamRTPOverTCP,
				 fNumSetupsSent)) {
    if (fVerbosityLevel > 0) {
      envir() << "ProxyRTSPClient::connectToServer calling scheduleReset()"
	      << " (setups_sent=" << fNumSetupsSent << " -- subsessions were bound to the old"
		 " connection, so the session must be rebuilt)\n";
    }
    // NOT "connect-failed". This fires when reconnectRequiresFullReset sees res >= 0 -- i.e. when the
    // connect SUCCEEDED (or is pending). The condition being detected is that a NEW RTSP connection
    // has replaced the old one while subsessions were interleaved on it, so those bindings are stale
    // and the session genuinely must be rebuilt. That is a legitimate rebuild, not a failure.
    //
    // The old name was measurably harmful: 183 of 217 resets in a 90-minute production window on
    // 6397272975e8c006c6fc carried reason=connect-failed, and were read as connection failures --
    // sending the investigation after a network fault that was not happening. A name that lies is
    // worse than no name, because the whole point of naming these call sites was to stop guessing
    // which path fired. (hard-rules.md: name a thing for what it MEASURES.)
    scheduleReset(RESET_REASON_RECONNECT_REPLACED_CONNECTION);
  }

  return res;
}

void ProxyRTSPClient::continueAfterDESCRIBE(char const* sdpDescription) {
  noteLifecycle(sdpDescription != NULL ? SE_DESCRIBE_OK : SE_DESCRIBE_FAILED);
  if (sdpDescription != NULL) {
    fOurServerMediaSession.continueAfterDESCRIBE(sdpDescription);

    // Unlike most RTSP streams, there might be a long delay between this "DESCRIBE" command (to the downstream server) and the
    // subsequent "SETUP"/"PLAY" - which doesn't occur until the first time that a client requests the stream.
    // To prevent the proxied connection (between us and the downstream server) from timing out, we send periodic 'liveness'
    // ("OPTIONS" or "GET_PARAMETER") commands.  (The usual RTCP liveness mechanism wouldn't work here, because RTCP packets
    // don't get sent until after the "PLAY" command.)
    scheduleLivenessCommand();
  } else {
    // The "DESCRIBE" command failed, most likely because the server or the stream is not yet running.
    // Reschedule another "DESCRIBE" command to take place later:
    scheduleDESCRIBECommand();
  }
  fDoneDESCRIBE = True;
}

void ProxyRTSPClient::continueAfterLivenessCommand(int resultCode, Boolean serverSupportsGetParameter,
						   Boolean serverSupportsSetParameter) {
  // A reply (or a definite failure) arrived, so the no-reply deadline has done its job. ANY
  // response also ends the unanswered run: the counter tracks CONSECUTIVE silence, and one answer
  // -- even an error -- proves the camera is still answering us.
  envir().taskScheduler().unscheduleDelayedTask(fLivenessDeadlineTask); fLivenessDeadlineTask = NULL;
  fConsecutiveLivenessTimeouts = 0;

  if (resultCode != 0) {
    // Same rule as continueAfterSETUP: trust a positive status, ask the client for anything else.
    RequestFailureCause const cause = failureCauseFor(resultCode);

    // A REJECTED keep-alive is not a dead connection.
    //
    // live555's own comment below states that resultCode > 0 means the server responded -- and then
    // the code ignored its own distinction and treated every non-zero result identically:
    // SE_CONNECTION_LOST plus a full session rebuild. But a status code is PROOF the connection is
    // alive: the server had to receive the request and write a reply to produce one. A camera that
    // answers 501 Not Implemented to GET_PARAMETER therefore had its session torn down on every
    // keepalive interval, forever, over a perfectly healthy TCP connection -- and every one of those
    // rebuilds dropped the fan-out clients and cost a downstream re-subscribe wait.
    //
    // The right response is GStreamer's: stop using the method the server just refused and fall back
    // to the next one it advertised. Only OPTIONS has no fallback left, and every RTSP server
    // implements OPTIONS, so a refusal there is genuinely anomalous.
    // A keep-alive that got NO REPLY is not evidence of anything. GStreamer's answer is to send
    // another keep-alive and carry on -- gstrtspsrc.c:6760-6765 (interleaved) and :6908-6913 (UDP),
    // `case GST_RTSP_ETIMEOUT: ... send keep-alive ... continue`. Nothing is torn down.
    //
    // We got this wrong in the obvious way: the "keep the session" branch below required
    // resultCode > 0, i.e. only an ANSWER could save the session. A timeout is resultCode < 0, so a
    // camera that merely replied late still cost a full rebuild -- DESCRIBE discarded, fan-out
    // clients dropped, multi-minute re-subscribe wait. RequestRetryPolicy.hh had defined
    // timeoutShouldKeepSessionAlive() for precisely this case and NOTHING CALLED IT: a written,
    // tested policy with no call site, which is the same shape as the retry-window bug.
    // NOTE: responses can no longer produce REQ_FAIL_TIMEOUT (a keep-alive that gets NO reply is
    // handled by the armed deadline -- livenessDeadlineExpired -- which never comes through here).
    // The branch is kept because a failure SITE can still record a timeout-shaped errno
    // (EAGAIN/EWOULDBLOCK on the send), and the answer is the same: not a disconnect, ping again.
    if (timeoutShouldKeepSessionAlive(cause)) {
      envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: keep-alive got no reply"
              << " (result " << resultCode << "). NOT a disconnect -- sending another keep-alive."
              << " Session and downstream clients KEPT.\n";
      scheduleLivenessCommand();
      return;
    }

    if (!requestFailureLostTheConnection(cause) && resultCode > 0) {
      // 454 Session Not Found: the connection is alive and the SESSION is already gone on the far
      // end (typical producer: our control-connection reconnect re-presented a session id the
      // camera expired while the connection was down). The connection-is-alive rule must not apply
      // -- keep-alives against a disowned id can never succeed, and "KEPT" here would be a zombie
      // whose logs look healthy forever. Only a rebuild can recover.
      if (rtspStatusEndsSession(resultCode)) {
        // TX-SESSION-GONE, not TX-UNEXPECTED: this is an anticipated, policy-handled condition, and
        // spending the always-investigate code on it would train operators to ignore it.
        envir() << TX_CODE_SESSION_GONE << " ProxyRTSPClient[" << url() << "]: keep-alive answered "
                << resultCode << " -- the connection is alive but the server no longer recognises"
                   " this session. Rebuilding.\n";
        scheduleReset(RESET_REASON_SESSION_NOT_FOUND, cause);
        return;
      }

      // OPTIONS refused with the connection up: nothing left to fall back to. Every RTSP server
      // implements OPTIONS, so a refusal here is genuinely anomalous -- rebuild, named for what it
      // is rather than as a lost connection. (Checked FIRST so the demotion switch below covers
      // only the two demotable methods, with no dead "demoted OPTIONS" arm.)
      if (fLastLivenessMethod == RTSP_LIVENESS_OPTIONS && methodRefusalDisablesMethod(resultCode)) {
        envir() << TX_CODE_UNEXPECTED << " ProxyRTSPClient[" << url() << "]: keep-alive OPTIONS refused"
                << " (status " << resultCode << ") on a live connection -- no fallback method remains.\n";
        fServerSupportsGetParameter = False;
        fServerSupportsSetParameter = False;
        scheduleReset(RESET_REASON_KEEPALIVE_OPTIONS_REFUSED);
        return;
      }

      // Only a "method refusal" status (405/406/501/551 -- gstrtspsrc.c:7917-7934) may demote the
      // method. Any OTHER answered status (a 500, say) is a server fault, not a missing verb:
      // demoting on it would silently stop us using a method the camera does support, and tearing
      // down would rebuild a session over a connection the answer just proved alive. GStreamer's
      // rule both times: keep the session, keep the method, and let the next keep-alive re-test it.
      if (!methodRefusalDisablesMethod(resultCode)) {
        envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: keep-alive answered status "
                << resultCode << " -- not a method refusal, and an answer proves the connection is"
                << " alive. Session, method and downstream clients KEPT.\n";
        scheduleLivenessCommand();
        return;
      }

      char const* demoted;
      if (fLastLivenessMethod == RTSP_LIVENESS_SET_PARAMETER) {
        fServerSupportsSetParameter = False; demoted = "SET_PARAMETER";
      } else {
        fServerSupportsGetParameter = False; demoted = "GET_PARAMETER";
      }
      envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: keep-alive " << demoted
              << " refused (status " << resultCode << "); the connection is ALIVE, so falling back"
              << " to the next advertised method. Session and downstream clients KEPT.\n";
      scheduleLivenessCommand();
      return;
    }

    // The CONTROL connection is gone -- but with UDP media that is not the same thing as the
    // SESSION being gone: the session outlives the connection (RFC 2326 sec 1.4), the media path is
    // separate and may still be flowing, and some cameras drop an idle control connection by
    // design. GStreamer reconnects and continues the same session (gstrtspsrc.c:6914-6926,
    // udp_reconnect). We do the lazy version: the next keep-alive re-opens the connection inside
    // sendRequest and re-presents the same session id. Bounded to ONE attempt per incident
    // (keepAliveLossReconnectsInPlace's `try == 0` shape) -- if that keep-alive also fails, fall
    // through here again with the flag set and rebuild for real. Never for TCP-interleaved media:
    // it rode the dead connection, and only a rebuild can rebind it.
    if (requestFailureLostTheConnection(cause) &&
        keepAliveLossReconnectsInPlace(fStreamRTPOverTCP == True, fLivenessReconnectAttempted == True)) {
      fLivenessReconnectAttempted = True;
      envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: control connection lost, but"
                 " media is UDP and the session outlives the connection (RFC 2326 sec 1.4)."
                 " Reconnecting the control connection with the SAME session -- one attempt."
                 " Session and downstream clients KEPT.\n";
      scheduleLivenessCommand();
      return;
    }

    // A LOCALLY-failed send (fd/buffer exhaustion -- most likely under the very fd pressure a reset
    // storm creates) says nothing about the session: rebuilding on it consumes MORE fds, amplifying
    // the shortage. Ping again instead, like a timeout; if the shortage outlives the upstream
    // session timeout, the camera answers 454 and the SESSION-GONE path rebuilds on evidence.
    if (cause == REQ_FAIL_LOCAL_RESOURCE) {
      envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: keep-alive send failed on a"
                 " LOCAL resource (not the camera, not the connection). Sending another keep-alive."
                 " Session and downstream clients KEPT.\n";
      scheduleLivenessCommand();
      return;
    }

    // From here the keep-alive failed without an answer: either the connection is provably gone, or
    // the send failed unclassifiably. Either way the session rebuilds -- but the two are
    // DIFFERENT facts and must not share a name.
    // We handle this by resetting our connection state with this server.  Any current clients will be closed, but
    // subsequent clients will cause new RTSP "SETUP"s and "PLAY"s to get done, restarting the stream.
    // Then continue by sending more "DESCRIBE" commands, to try to restore the stream.

    // Until we learn otherwise from a future "OPTIONS" response. Both capabilities are cleared: a
    // reconnect may land on different firmware, so a stale capability must never survive a reset.
    fServerSupportsGetParameter = False;
    fServerSupportsSetParameter = False;

    // ONLY a proven connection loss records SE_CONNECTION_LOST, and only it may carry the
    // connection-lost reason. This was unconditional, so a locally-failed send (fresh errno says
    // local-resource) was recorded as a lost connection in BOTH the state machine and the reason
    // tally -- production showed `reason=keepalive-connection-lost cause=local-resource`, a reason
    // asserting a condition its own cause field disclaimed.
    Boolean const provenLost = requestFailureLostTheConnection(cause);
    if (provenLost) noteLifecycle(SE_CONNECTION_LOST);

    if (resultCode < 0 && fVerbosityLevel > 0) {
      envir() << *this << ": keep-alive failed (cause=" << requestFailureCauseName(cause)
              << ", 'errno' " << -resultCode << " -- untrustworthy).  Scheduling reset...\n";
    }

    // Named by CAUSE, not by call site alone: "the socket reset" and "we could not classify errno
    // 4095" are different faults that a single "keepalive-failed" could not tell apart.
    scheduleReset(provenLost ? RESET_REASON_KEEPALIVE_CONNECTION_LOST : RESET_REASON_KEEPALIVE_FAILED,
                  cause);
    return;
  }

  fServerSupportsGetParameter = serverSupportsGetParameter;
  fServerSupportsSetParameter = serverSupportsSetParameter;
  // A successful keep-alive ends the incident: the next control-connection loss gets a fresh
  // one-shot reconnect budget rather than inheriting a spent one from an earlier incident.
  fLivenessReconnectAttempted = False;

  // Schedule the next 'liveness' command (i.e., to tell the back-end server that we're still alive):
  scheduleLivenessCommand();
}

#define SUBSESSION_TIMEOUT_SECONDS 5 // how many seconds to wait for the last track's "SETUP" to be done (note below)

void ProxyRTSPClient::continueAfterSETUP(int resultCode) {
  // A response arrived, so the deadline has done its job. Cancelling FIRST, before any branch, is
  // what stops a late-but-successful SETUP from being retried by a timer that was still armed.
  // (A response from a SETUP the deadline already gave up on cannot reach here: requestDeadlineExpired
  // disowns that request's handler before re-sending, so only the CURRENT SETUP's response arrives.)
  envir().taskScheduler().unscheduleDelayedTask(fRequestDeadlineTask); fRequestDeadlineTask = NULL;
  fGuardedSetupCSeq = 0;

  noteLifecycle(resultCode == 0 ? SE_SETUP_OK : SE_SETUP_FAILED);
  if (resultCode != 0) {
    // RETRY THE REQUEST, not the session -- GStreamer's rtspsrc behaviour, see RequestRetryPolicy.hh.
    //
    // This used to go straight to scheduleReset(), and that one line dominated every outage on this
    // site. A reset discards a COMPLETED DESCRIBE and, far worse, drops the fan-out clients; with
    // nobody attached, createNewStreamSource() is never called again until a consumer reconnects on
    // its own schedule. Measured on 192.168.11.19: three unanswered SETUPs cost three DESCRIBE
    // cycles plus downstream re-subscribe waits of 129.7s, 40s and 9.5s, inside a single 259s
    // recovery whose actual protocol work was ~40ms.
    //
    // Re-sending SETUP keeps the session and the clients, so a camera that missed one SETUP costs
    // milliseconds instead of minutes.
    // WHY it failed decides what to do. Flattening every failure into "it failed" is the same
    // mistake as always-resetting, one layer down: it would retry a rejected 401 twenty times (a
    // lockout risk that cannot succeed, since the credentials do not change between attempts) and
    // spend the same budget on a 404 that will never become anything else.
    // A POSITIVE resultCode is the real RTSP status, set deliberately at RTSPClient.cpp's response
    // path -- trustworthy, so classify it. A NEGATIVE one is `-errno` sampled at an arbitrary
    // moment and means nothing; for those we ask the client what its failure site actually
    // recorded. Decoding the negative number was how a response-PARSE failure came to be diagnosed
    // as EADDRINUSE.
    handleSetupFailure(resultCode, failureCauseFor(resultCode));
    return;
  }

  // A success clears the budget, so a long-lived proxy cannot creep into reset-only behaviour after
  // accumulating failures across its whole life.
  fRequestRetry.noteSuccess();
  continueAfterSETUPSucceeded();
}

// The ONE place a SETUP failure is acted on, whether it arrived as a response or as a deadline
// expiring with no response at all. Kept single deliberately: a deadline handler that made its own
// retry decision would be a second copy of this policy, free to drift from it -- and the drift would
// only show up as an outage.
void ProxyRTSPClient::handleSetupFailure(int resultCode, RequestFailureCause cause) {
    long const now = monotonicSeconds();
    // The transport reaches the decision: a connection loss under INTERLEAVED media must rebuild
    // directly (connectionLossRequiresRebuild) -- the replay's lazy reconnect would trigger the
    // new-connection reset anyway, and production measured every such retry paying a doubled
    // rebuild (2304 reconnect-replaced resets in 6h; see the fixture cited in RequestRetryPolicy.hh).
    RequestFailureAction const action =
      fRequestRetry.decide(REQ_SETUP, cause, now, fStreamRTPOverTCP == True);

    // The decision is TIME-bounded, not merely count-bounded. A count cannot bound outage time: 20
    // retries at the 20s TCP timeout is 400 seconds, worse than the 259s recovery this replaces.
    // fRequestRetry carries the clock alongside the counter so the two cannot be consulted apart.
    if (requestActionRetries(action) && fSetupQueueHead != NULL) {
      fRequestRetry.noteFailure(now, cause);
      unsigned const delayUs = requestRetryDelayUs(cause);
      envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: SETUP failed (result "
              << resultCode << ", cause=" << requestFailureCauseName(cause)
              // Three-way: an acquittal is only true for the provably-local causes. For
              // connection-closed/timeout the camera may be the party that closed or went silent
              // -- unproven is the strongest claim the evidence supports.
              << (requestFailureBlamesPeer(cause)     ? ", peer-attributed"
                  : requestFailureIsLocalFault(cause) ? ", NOT the camera's fault"
                                                      : ", fault unproven")
              << "), retrying the request -- attempt " << fRequestRetry.attempts
              << " of " << maxRetriesForCause(cause)
              << ", " << fRequestRetry.elapsedSince(now) << "s of "
              << maxRequestRetryWindowSecs() << "s window used"
              << ", delay " << delayUs / 1000 << "ms"
              << ". Session and downstream clients KEPT.\n";

      // A rejected TRANSPORT must be REPLACED, not re-sent: the server already said no, and nothing
      // about an identical request changes between attempts. GStreamer advances its protocol mask
      // before retrying (gstrtspsrc.c:8744-8750); with two transports, "next" is the other one
      // (nextTransportAfterRejection). The switch is durable for the session: the transport the
      // camera refused stays refused.
      // Never under an HTTP tunnel: sendSetupCommand forces TCP there regardless of the flag, so a
      // flip would re-send the refused transport (burning the budget-of-1 into a reset) while
      // leaving fStreamRTPOverTCP misdescribing the session to reconnectRequiresFullReset and
      // keepAliveLossReconnectsInPlace. A tunneled 461 is a plain refusal.
      if (requestFailureNeedsDifferentTransport(cause) && !fUsingHTTPTunnel) {
        Boolean const wasTCP = fStreamRTPOverTCP;
        RequestTransport const next =
          nextTransportAfterRejection(wasTCP ? REQ_TRANSPORT_TCP : REQ_TRANSPORT_UDP);
        fStreamRTPOverTCP = next == REQ_TRANSPORT_TCP ? True : False;
        // TX-RETRY first (it IS the counted in-place retry), TX-TCP-FALLBACK second so the durable
        // transport switch is still greppable as a token -- same two-token shape as the
        // TX-PEER-UP/TX-STALL peer-check lines; the tally counts the leftmost.
        envir() << TX_CODE_RETRY << " " << TX_CODE_TCP_FALLBACK
                << " ProxyRTSPClient[" << url() << "]: the camera refused "
                << (wasTCP ? "RTP-over-TCP" : "UDP") << " (461); retrying SETUP asking for "
                << (fStreamRTPOverTCP ? "RTP-over-TCP" : "UDP")
                << " instead -- never the transport it just rejected.\n";
      }

      // REQ_ACTION_RETRY_RECONNECT wants the RTSP connection re-established before the request is
      // replayed. live555 re-opens it lazily inside sendSetupCommand when the socket is gone, so the
      // same call covers both actions -- but they are NOT merged in the policy, because the day that
      // stops being true this call site must be able to tell them apart.
      //
      // REQ_ACTION_RETRY_DELAYED must actually WAIT. EADDRINUSE means a local RTP port is still held
      // by a socket that has not finished closing; re-sending immediately cannot succeed, and would
      // burn all 20 attempts inside a few milliseconds before rebuilding the session anyway -- a
      // retry loop that looks like it is trying and is in fact guaranteed to fail.
      if (delayUs > 0) {
        fSetupRetryTask = envir().taskScheduler().scheduleDelayedTask(
          delayUs, (TaskFunc*)resendSetup, this);
        return;
      }
      sendSetupWithDeadline(fSetupQueueHead->fClientMediaSubsession);
      return;
    }

    // Budget spent, rebuild required, or nothing queued to retry: the session needs rebuilding.
    // (Deferred rather than done inline, because a reset deletes the "ProxyServerMediaSubsession"
    // and we cannot do that during "ProxyServerMediaSubsession::createNewStreamSource()".)
    // The reason names the REBUILD condition, from the fixed vocabulary in ResetReasons.hh. The
    // failure cause travels separately in `cause=` -- putting it here made `reason=` hold two
    // different vocabularies depending on which branch produced it, which no tally can aggregate.
    // The interleaved-connection-loss case consults the SAME predicate the decision used, so the
    // label and the verdict cannot drift apart.
    scheduleReset(fSetupQueueHead == NULL ? RESET_REASON_SETUP_NOTHING_QUEUED
                  : connectionLossRequiresRebuild(cause, fStreamRTPOverTCP == True)
                      ? RESET_REASON_SETUP_INTERLEAVED_CONNECTION_LOST
                  : retryWindowExpired(fRequestRetry.elapsedSince(now))
                      ? RESET_REASON_SETUP_RETRY_WINDOW_EXPIRED
                      : RESET_REASON_SETUP_RETRIES_EXHAUSTED,
                  cause);
}

// The deadline expired with no response at all.
//
// live555 had NO per-request response deadline: a camera that simply never answered a SETUP left the
// request outstanding indefinitely, and only an unrelated watchdog eventually noticed. That is why
// requestTimeoutSecs() sat defined, documented and unit-tested with no production caller -- the
// policy described a deadline the binary did not have.
//
// GStreamer waits tcp_timeout (20s) for a response on both transports (gstrtspsrc.c:7578-7579,
// :7710) and treats expiry as a timeout, not a disconnect. So do we: the cause is REQ_FAIL_TIMEOUT,
// which carries the full retry budget and keeps the session.
void ProxyRTSPClient::requestDeadlineExpired(void* clientData) {
  ((ProxyRTSPClient*)clientData)->requestDeadlineExpired();
}

void ProxyRTSPClient::requestDeadlineExpired() {
  fRequestDeadlineTask = NULL;
  // Deliberately NO TX token: the decision has not been made yet. Stamping this line TX-RETRY
  // counted a retry that sometimes never happened (budget spent -> reset), and double-counted the
  // ones that did (handleSetupFailure logs its own TX-RETRY). The outcome line is the tally.
  envir() << "ProxyRTSPClient[" << url() << "]: SETUP got no response within "
          << requestTimeoutSecs()
          << "s. Treating as a TIMEOUT (the camera may simply be slow), NOT a disconnect.\n";
  // DISOWN the timed-out request before acting: its response may still arrive later, and with two
  // SETUPs then outstanding on one shared handler, the LATE response would cancel the deadline
  // armed for the RETRY (leaving it guardless -- the exact one-request-no-deadline defect this
  // mechanism exists to prevent) and, if both eventually succeeded, the second completion would run
  // against an already-emptied queue. NULLing the handler makes a late response inert.
  if (fGuardedSetupCSeq != 0) {
    changeResponseHandler(fGuardedSetupCSeq, NULL);
    fGuardedSetupCSeq = 0;
  }
  // resultCode 0 would mean success, and any negative number would be inventing an errno. The cause
  // argument is the real signal; the code is only carried for the log.
  handleSetupFailure(-ETIMEDOUT, REQ_FAIL_TIMEOUT);
}

// Send a SETUP and arm the response deadline. EVERY SETUP goes through here -- a call site that used
// sendSetupCommand directly would be one request with no deadline, which is indistinguishable from
// the bug this fixes.
void ProxyRTSPClient::sendSetupWithDeadline(MediaSubsession& subsession) {
  // A SETUP must never be the FIRST command on a connection. With the control connection gone,
  // sendSetupCommand would transparently REOPEN one and send the SETUP on it -- which at least
  // Tapo-class cameras silently ignore (no response, FIN at ~10s; measured 4x on 192.168.11.19,
  // tetherbox fixtures bare_setup_ignored_pcap_dialog_* / cam_keepalive_probe_*, 2026-07-21),
  // producing an unconvergeable rebuild loop. The same capture shows DESCRIBE+SETUP+PLAY on ONE
  // connection succeeding every time, so rebuild -- scheduleReset logs the reason= line itself.
  if (socketNum() < 0) {
    scheduleReset(RESET_REASON_SETUP_NO_CONNECTION, REQ_FAIL_CONNECTION_CLOSED);
    return;
  }
  // rescheduleDelayedTask is documented (UsageEnvironment.hh) as "cancel if pending, then
  // schedule": exactly the arm-a-single-deadline semantics needed here, in one call.
  envir().taskScheduler().rescheduleDelayedTask(fRequestDeadlineTask,
    requestTimeoutSecs() * MILLION,
    (TaskFunc*)requestDeadlineExpired, this);
  // Remember WHICH request this deadline guards, so an expiry can disown exactly that request (see
  // requestDeadlineExpired) rather than leaving it outstanding to answer late into the retry.
  //
  // Guarded assignment: a SYNCHRONOUS send failure runs the response handler INSIDE this call, and
  // that handler's retry may already have sent a replacement SETUP and stored ITS cseq. The outer
  // frame then resumes holding this call's return value 0 -- and an unconditional assignment would
  // clobber the replacement's guard, disarming the disown for precisely the request that needs it.
  unsigned const setupCSeq = sendSetupCommand(subsession, ::continueAfterSETUP, False,
                                              fStreamRTPOverTCP, False, fOurAuthenticator);
  if (setupCSeq != 0) fGuardedSetupCSeq = setupCSeq;
}

// Send the FIRST SETUP for a subsession: the send, the sent-counter and the subsession's
// have-setup flag move together, so no call site can send a new SETUP and forget the bookkeeping.
// resendSetup deliberately does NOT come through here: a re-send of the same request is not a new
// one, and counting it would inflate fNumSetupsSent -- which reconnectRequiresFullReset reads.
void ProxyRTSPClient::sendNewSetup(ProxyServerMediaSubsession& smss) {
  sendSetupWithDeadline(smss.fClientMediaSubsession);
  ++fNumSetupsSent;
  smss.fHaveSetupStream = True;
}

void ProxyRTSPClient::continueAfterSETUPSucceeded() {
  // The queue must never be empty here (every success dequeues the SETUP that produced it, and a
  // timed-out SETUP is disowned before its retry is sent). If it is, our bookkeeping has diverged --
  // report it rather than dereference NULL below. Checked FIRST: the verbosity trace just below
  // dereferences the head too.
  if (fSetupQueueHead == NULL) {
    envir() << TX_CODE_UNEXPECTED << " ProxyRTSPClient[" << url() << "]::continueAfterSETUPSucceeded:"
               " a SETUP response arrived with nothing on the SETUP queue -- a response this session"
               " was not expecting. Ignoring it.\n";
    return;
  }

  if (fVerbosityLevel > 0) {
    envir() << *this << "::continueAfterSETUP(): head codec: " << fSetupQueueHead->codecName()
	    << "; numSubsessions " << fSetupQueueHead->fParentSession->numSubsessions() << "\n\tqueue:";
    for (ProxyServerMediaSubsession* p = fSetupQueueHead; p != NULL; p = p->fNext) {
      envir() << "\t" << p->codecName();
    }
    envir() << "\n";
  }
  envir().taskScheduler().unscheduleDelayedTask(fSubsessionTimerTask); // in case it had been set

  // Dequeue the first "ProxyServerMediaSubsession" from our 'SETUP queue'.  It will be the one for which this "SETUP" was done:
  ProxyServerMediaSubsession* smss = fSetupQueueHead;
  fSetupQueueHead = fSetupQueueHead->fNext;
  if (fSetupQueueHead == NULL) fSetupQueueTail = NULL;

  if (fSetupQueueHead != NULL) {
    // There are still entries in the queue, for tracks for which we have still to do a "SETUP".
    // "SETUP" the first of these now:
    sendNewSetup(*fSetupQueueHead);
  } else {
    if (fNumSetupsSent >= smss->fParentSession->numSubsessions()) {
      // We've now finished setting up each of our subsessions (i.e., 'tracks').
      // Continue by sending a "PLAY" command (an 'aggregate' "PLAY" command, on the whole session):
      sendPlayCommand(smss->fClientMediaSubsession.parentSession(), ::continueAfterPLAY, -1.0f, -1.0f, 1.0f, fOurAuthenticator);
          // the "-1.0f" "start" parameter causes the "PLAY" to be sent without a "Range:" header, in case we'd already done
          // a "PLAY" before (as a result of a 'subsession timeout' (note below))
      fLastCommandWasPLAY = True;
    } else {
      // Some of this session's subsessions (i.e., 'tracks') remain to be "SETUP".  They might get "SETUP" very soon, but it's
      // also possible - if the remote client chose to play only some of the session's tracks - that they might not.
      // To allow for this possibility, we set a timer.  If the timer expires without the remaining subsessions getting "SETUP",
      // then we send a "PLAY" command anyway:
      fSubsessionTimerTask
	= envir().taskScheduler().scheduleDelayedTask(SUBSESSION_TIMEOUT_SECONDS*MILLION, (TaskFunc*)subsessionTimeout, this);
    }
  }
}

void ProxyRTSPClient::resendSetup(void* clientData) {
  ((ProxyRTSPClient*)clientData)->resendSetup();
}

void ProxyRTSPClient::resendSetup() {
  fSetupRetryTask = NULL;
  // The queue can empty while the delay runs -- a reset, a client going away, or the subsession
  // timeout firing. Re-sending against an empty queue would dereference NULL, so the emptiness is
  // checked HERE rather than assumed from the state at scheduling time. The delay is exactly the
  // window in which the world is allowed to change.
  if (fSetupQueueHead == NULL) {
    // TX-CLEANUP, not TX-RETRY: nothing is re-sent here. TX-RETRY tallies as "request re-sent,
    // session kept", and counting an abandoned no-op there inflates the retry rate.
    envir() << TX_CODE_CLEANUP << " ProxyRTSPClient[" << url()
            << "]: delayed SETUP retry abandoned -- the queue emptied while waiting.\n";
    return;
  }
  sendSetupWithDeadline(fSetupQueueHead->fClientMediaSubsession);
}

void ProxyRTSPClient::continueAfterPLAY(int resultCode) {
  // A PLAY response arriving AFTER a PAUSE superseded it must not resurrect the session: the last
  // client left while this PLAY was in flight, closeStreamSource already PAUSEd upstream and
  // disarmed the gap watchdog, and the camera (processing PLAY then PAUSE, in order) is now paused.
  // Treating this stale ok as "LIVE" re-entered PLAYING (legal per the table -- READY->PLAYING must
  // stay legal for real resumes, so the machine cannot catch this), re-armed the watchdog against a
  // stream WE paused, and ~2 gap intervals later reset it as "peer went silent" -- the
  // blame-the-camera-for-our-own-pause bug, reachable purely through response ordering.
  if (resultCode == 0 && !fLastCommandWasPLAY) {
    fRequestRetry.noteSuccess();
    if (fVerbosityLevel > 0) {
      envir() << *this << ": PLAY-ok arrived after a PAUSE superseded it -- upstream is paused,"
                 " so not marking LIVE and not arming the gap watchdog.\n";
    }
    return;
  }

  noteLifecycle(resultCode == 0 ? SE_PLAY_OK : SE_PLAY_FAILED);
  // NOTE: PLAY is deliberately NOT retried in place yet, even though RequestRetryPolicy.hh permits
  // it. The measured defect is SETUP -- the captured 259s recovery is three unanswered SETUPs, and
  // no PLAY failure appears in it. Retrying PLAY needs a MediaSession handle that is only obviously
  // available while the SETUP queue is non-empty, and inventing a fallback for the empty case would
  // be an untested path written to fix a fault nobody has observed. Add it when a trace shows a PLAY
  // failure actually costing a session; the policy header is already ready for it.
  if (resultCode == 0) fRequestRetry.noteSuccess();
  if (resultCode != 0) {
    // The "PLAY" command failed, so arrange to reset the state. (We don't do this now, because it deletes the
    // "ProxyServerMediaSubsession", and we can't do that during "ProxyServerMediaSubsession::createNewStreamSource()".)
    scheduleReset(RESET_REASON_PLAY_FAILED);
    return;
  }

  // PLAY succeeded => the upstream RTSP stream is now live. This is the positive ACK for the
  // (re)connect: the teardown paths (RTPInterface stall/desync/EBADF guards, checkInterPacketGaps
  // reset, liveness-command failure) all log the DOWN transition, but nothing logged the UP
  // transition, so the journal showed streams tearing down with no matching "back online" line.
  // ALWAYS logged (not verbosity-gated): on the first PLAY it marks the initial connect; on every
  // later PLAY it marks a successful reconnect after a reset, which is exactly the recovery event
  // an operator needs to see (and pairs with the mid-packet-stall / desync teardown logs above it).
  // Carries TX_CODE_LIVE so it is greppable as a token, and so the stdout/stderr log split routes
  // this one positive acknowledgement to the journal alongside the faults it pairs with.
  envir() << TX_CODE_LIVE << " ProxyRTSPClient: PLAY succeeded on \"" << url() << "\" -- upstream stream is LIVE ("
          << (fHavePlayedBefore ? "reconnected after a reset; fanout resumes"
                                : "initial connect") << ").\n";
  fHavePlayedBefore = True;

  // 🚨 Start the stall clock FROM NOW. A resume after a PAUSE (no downstream clients) leaves
  // fLastPacketProgressTime at its pre-pause value, and the gap check below runs immediately — so
  // the ENTIRE pause duration counted as upstream silence. Any pause longer than the -D limit made
  // the first viewer to reconnect trigger an instant reset, logged as "peer went silent without
  // closing" — blaming the camera for a silence WE caused by pausing it, and tearing down the
  // session milliseconds after restoring it. `-P`/neverPause hides this (nothing ever pauses), which
  // is why production has not seen it; the stock default does not, since neverPause defaults False.
  //
  // The sentinel makes the next check adopt the live count without judging a delta across the pause.
  fLastPacketProgressSec = monotonicSeconds();
  fTotNumPacketsReceived = PACKET_COUNT_UNKNOWN;

  if (fInterPacketGapsTask == NULL) checkInterPacketGaps_();
}

void ProxyRTSPClient::scheduleLivenessCommand() {
  // Delay a jittered time before sending another 'liveness' command. The deadline follows
  // GStreamer's rule (act a safety margin BEFORE the session timeout, not at it) and the jitter
  // keeps a box running many proxies from synchronising every keepalive into one burst.
  //
  // This replaces a broken randomisation: the old code computed
  //     us_1stPart + (us_2ndPart*our_random())%us_2ndPart
  // and (a*r) % a is identically 0 in exact arithmetic — it only varied at all through 32-bit
  // overflow, and then non-uniformly. Policy + arithmetic now live in RTSPKeepAlive.hh, covered by
  // test/test_rtsp_keepalive.cpp.
  // Phase matters: before the first SETUP response there is no session and the advertised session
  // timeout governs nothing -- the cadence must outrun the camera's CONNECTION idle timeout instead
  // (measured 10s FIN on 192.168.11.19; see RTSP_PRE_SESSION_IDLE_FIN_FLOOR_SECS). Without this the
  // DESCRIBE -> first-downstream-SETUP gap always outlived the connection, and every in-process
  // rebuild degenerated into the bare-SETUP-on-a-virgin-connection loop sendSetupWithDeadline guards.
  unsigned const uSecondsToDelay
    = rtspLivenessDelayUs(sessionTimeoutParameter(), (unsigned)our_random(),
                          haveRTSPSessionId() == True);
  // rescheduleDelayedTask, not schedule: several paths (a reply, a deadline expiry, a demotion) can
  // each ask for "the next keep-alive", and overwriting the token would leave the earlier task
  // queued -- two keep-alive chains where one was meant. Cancel-then-schedule collapses them.
  envir().taskScheduler().rescheduleDelayedTask(fLivenessCommandTask, uSecondsToDelay,
						sendLivenessCommand, this);
}

void ProxyRTSPClient::sendLivenessCommand(void* clientData) {
  ProxyRTSPClient* rtspClient = (ProxyRTSPClient*)clientData;
  rtspClient->fLivenessCommandTask = NULL;

  // Pick the 'liveness' command the way GStreamer's rtspsrc does: SET_PARAMETER, else GET_PARAMETER,
  // else OPTIONS -- whichever the back-end advertised (see RTSPKeepAlive.hh, and
  // test/test_rtsp_keepalive.cpp for the selection table).
  //
  // Why this is not simply "always OPTIONS" as upstream has it: a bare OPTIONS is not universally
  // treated as session activity, and upstream's GET_PARAMETER path is compiled out because
  // GET_PARAMETER crashes some cameras. That left OPTIONS as the only option in practice. Preferring
  // SET_PARAMETER matches the client the camera vendors actually test against, and we only ever send
  // a method the server itself advertised in "Public:".
  //
  // The SET_PARAMETER ping is deliberately BODYLESS (sendLivenessSetParameterCommand, not
  // sendSetParameterCommand): inventing a parameter name just to say "still here" is what some
  // cameras reject.
  MediaSession* sess = rtspClient->fOurServerMediaSession.fClientMediaSession;
  // haveRTSPSessionId, NOT fNumSetupsSent: a *_PARAMETER keep-alive cannot be SENT without a
  // session id (setRequestFields refuses), and the id only exists once the first SETUP's RESPONSE
  // arrives. Gating on SETUPs *sent* let a liveness tick inside the sent-but-unanswered window --
  // exactly the slow-camera window this fork exists for -- pick SET_PARAMETER, fail synchronously
  // with a STALE errno as its cause, and tear down a healthy mid-handshake session on the strength
  // of a number no syscall of ours produced.
  Boolean const canUseSession = (rtspClient->haveRTSPSessionId() && sess != NULL);

  RtspLivenessMethod const method = canUseSession
    ? chooseRtspLivenessMethod(rtspClient->fServerSupportsSetParameter,
			       rtspClient->fServerSupportsGetParameter)
    : RTSP_LIVENESS_OPTIONS; // no session yet -> OPTIONS is the only thing that makes sense
  rtspClient->fLastLivenessMethod = method;

  // Arm the no-reply deadline BEFORE the send (the pattern sendSetupWithDeadline uses). The send
  // can fail SYNCHRONOUSLY -- the response handler then runs inside the call -- and arming
  // afterwards left a deadline guarding a request that no longer existed: +20s later it logged a
  // false "got no reply" (the send failed; there was no silence) and pushed the pending keep-alive
  // out, far enough to blow the upstream session timeout. Armed first, the synchronous handler's
  // own cancel disarms it correctly.
  rtspClient->envir().taskScheduler().rescheduleDelayedTask(rtspClient->fLivenessDeadlineTask,
    requestTimeoutSecs() * MILLION, (TaskFunc*)livenessDeadlineExpired, rtspClient);

  switch (method) {
    case RTSP_LIVENESS_SET_PARAMETER:
      rtspClient->sendLivenessSetParameterCommand(*sess, ::continueAfterLivenessPARAMETER,
						  rtspClient->auth());
      break;
    case RTSP_LIVENESS_GET_PARAMETER:
      rtspClient->sendGetParameterCommand(*sess, ::continueAfterLivenessPARAMETER, "",
					  rtspClient->auth());
      break;
    case RTSP_LIVENESS_OPTIONS:
    default:
      rtspClient->sendOptionsCommand(::continueAfterOPTIONS, rtspClient->auth());
      break;
  }

  // (The no-reply deadline was armed ABOVE, before the send. Without one at all, a keep-alive the
  // camera never answers left the chain dead -- nothing re-schedules until a response handler runs
  // -- so the session silently stopped being kept alive, and the timeoutShouldKeepSessionAlive()
  // branch was a call site with no reachable trigger: the mirror image of the tested-but-unwired
  // defect. Same GStreamer rule as the SETUP deadline.)
}

void ProxyRTSPClient::livenessDeadlineExpired(void* clientData) {
  ((ProxyRTSPClient*)clientData)->livenessDeadlineExpired();
}

// The keep-alive got NO REPLY within the response deadline.
//
// One silent keep-alive is answered with another keep-alive (GStreamer's ETIMEOUT rule: silence is
// not evidence of anything). TWO consecutive silences exhaust that grace: the camera has now
// ignored us for 2x the deadline, and with no downstream client attached nothing else would ever
// rebuild the stream, so we escalate to a reset -- named for what was measured (unanswered), never
// as a connection loss silence cannot prove.
//
// If the ORIGINAL reply arrives after the deadline fired, continueAfterLivenessCommand still runs
// normally: it cancels the (re-armed) deadline, zeroes the unanswered counter, and schedules the
// next keep-alive -- rescheduleDelayedTask collapses the two chains into one. A late reply
// therefore self-heals; it cannot double-ping or leave a ping unguarded.
void ProxyRTSPClient::livenessDeadlineExpired() {
  fLivenessDeadlineTask = NULL;
  if (livenessSilenceEscalates(++fConsecutiveLivenessTimeouts)) {
    scheduleReset(RESET_REASON_KEEPALIVE_UNANSWERED, REQ_FAIL_TIMEOUT);
    return;
  }
  envir() << TX_CODE_RETRY << " ProxyRTSPClient[" << url() << "]: keep-alive got no reply within "
          << requestTimeoutSecs() << "s. NOT a disconnect (silence proves nothing) -- sending"
             " another keep-alive; a second consecutive silence rebuilds. Session and downstream"
             " clients KEPT.\n";
  scheduleLivenessCommand();
}

void ProxyRTSPClient::checkInterPacketGaps_() {
  if (fInterPacketGapMaxTime == 0) return; // we're not checking

  // Silence is only a FAULT while the session is supposed to be delivering media. Outside PLAYING —
  // mid-reset, after a PAUSE, before the first PLAY — no packets is the correct and expected state,
  // and resetting on it would blame the camera for a silence WE caused.
  //
  // That is not hypothetical: continueAfterPLAY carries a long comment about exactly this bug, where
  // a pause longer than -D made the first viewer to reconnect trigger an instant reset logged as
  // "peer went silent without closing". That fix reset the stall clock; this is the structural
  // version of the same guard, asked of the one place that knows the session's state.
  if (!sessionExpectsMedia(fLifecycleState)) {
    if (fVerbosityLevel > 0) {
      envir() << "ProxyRTSPClient: gap check skipped -- session is "
	      << sessionStateName(fLifecycleState) << ", not PLAYING, so no media is due\n";
    }
    // Re-arm: the session may reach PLAYING before the next tick, and dropping the chain here would
    // silently disarm the watchdog for the rest of the session's life.
    rearmInterPacketGapCheck();
    return;
  }

  // NO CONSUMER, NO MEASUREMENT -- checked BEFORE touching the client session. The packet total
  // below advances only when a downstream consumer pulls frames, and during attach/detach churn
  // the subsession's rtpSource can be mid-replacement: reading it with no consumer attached is
  // both meaningless (measures consumption, not arrival) and unsafe (measured: SIGSEGV on freed
  // rtpSource at exactly this read, macOS repro; frozen garbage counts on Linux -> eternal
  // 15s resets on ea9f/a3e5, 894/30min). Hold the clock, skip the read, re-arm: a returning
  // client gets a fresh full window, and a genuinely dead stream resets one window after it
  // returns. This block previously ran AFTER the iteration, which left the poisoned read live.
  if (fOurServerMediaSession.referenceCount() == 0) {
    fLastPacketProgressSec = monotonicSeconds();
    rearmInterPacketGapCheck();
    return;
  }

  // Check each subsession, counting up how many packets have been received:
  unsigned newTotNumPacketsReceived = 0;
  // Did the teardown classifier PROVE the far end closed any of our streams? Proof beats the timer:
  // there is nothing left to wait for, and the timer's own "nobody is proven at fault" wording would
  // contradict the close we already reported. See SessionRecoveryPolicy.hh + test_session_recovery.
  Boolean provenCloseSeen = False;

  MediaSubsessionIterator iter(*fOurServerMediaSession.fClientMediaSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    RTPSource* src = subsession->rtpSource();
    if (src == NULL) continue;
    newTotNumPacketsReceived += src->receptionStatsDB().totNumPacketsReceived();
    if (src->farEndCloseProven()) provenCloseSeen = True;
  }

  // MONOTONIC, not gettimeofday: this is the longest-horizon DURATION gate in the tree, so it is
  // the one most exposed to an NTP step. A forward step larger than -D would satisfy the gap limit
  // on every proxied camera at once, producing a fleet-wide burst of TX-IDLE resets describing
  // nothing; a backward step would hide a real stall. See MonotonicTime.hh.
  long const nowSec = monotonicSeconds();

  // Progress FIRST, time-since-progress SECOND. This check runs once per gap interval and the
  // stamp is only ever updated here, so deriving secsSinceProgress BEFORE this check's restamp
  // reads the PREVIOUS check's stamp: exactly one full interval, >= the limit, every time -- the
  // policy then reset streams that had just delivered thousands of packets. Measured: every UDP
  // session on ea9f/a3e5 torn down at PLAY+15.000s (+-120us) regardless of data, 894 resets/30min,
  // a footage gap each. (-D 10 worked on pre-policy builds because they compared COUNTS.)
  unsigned const prevTotNumPacketsReceived = fTotNumPacketsReceived;
  // ALWAYS adopt the new total, even when it did not advance. Leaving a stale high-water mark would
  // make every later check compare against a number that can never be reached again.
  Boolean const progressed = packetProgressMade(fTotNumPacketsReceived, newTotNumPacketsReceived);
  fTotNumPacketsReceived = newTotNumPacketsReceived;
  if (progressed) {
    // Packets advanced since the last check -> the stream is alive, as of NOW.
    fLastPacketProgressSec = nowSec;
  }

  // ONE derivation of "seconds since payload advanced", shared by the GAPCHK line and the policy
  // input below; after the restamp above it is 0 whenever this check saw progress.
  bool const haveProgressStamp = fLastPacketProgressSec != 0;
  long const secsSinceProgress = haveProgressStamp ? (nowSec - fLastPacketProgressSec) : 0;

  // GAPCHK: the raw inputs to the progress decision -- every field an observation, not a verdict.
  // Verbosity-gated; the captured output is the fixture the progress rule is tested against.
  if (fVerbosityLevel > 0) {
    envir() << "GAPCHK prev=" << (int)prevTotNumPacketsReceived
	    << " new=" << (int)newTotNumPacketsReceived
	    << " delta=" << (int)(newTotNumPacketsReceived - prevTotNumPacketsReceived)
	    << " secs_since_progress=" << (int)(haveProgressStamp ? secsSinceProgress : -1)
	    << " proven_close=" << (provenCloseSeen ? 1 : 0) << "\n";
  }

  // The decision itself lives in SessionRecoveryPolicy.hh as a pure function, unit-tested by
  // test/test_session_recovery.cpp (21 checks). Keeping it out of here is what makes the
  // proof-beats-timer rule, and the "exactly one action reports idle" exclusivity, testable at all.
  //
  // 🚨 The WALL-CLOCK gate is load-bearing — do NOT go back to resetting on a bare
  // "count unchanged between two invocations" compare. This watchdog can be entered more than
  // once in quick succession (a 2nd chain armed by continueAfterPLAY during the window where
  // the periodic callback has NULLed fInterPacketGapsTask but not yet rescheduled it). With the
  // old count-only compare, two checks firing ~0ms apart both saw the same count and FALSELY
  // reset a fully-healthy stream (RTP still arriving, frames still fanning out) -> reconnect ->
  // footage gaps on force_tcp cameras. Gating on elapsed real time since the last count
  // increase makes the reset correct regardless of how often/how close together the check
  // runs. Proven against a ~50-camera RTP-over-TCP proxy fleet.
  SessionRecoveryInput in;
  in.provenCloseSeen = provenCloseSeen;
  // Measure DATA RECEIVED, not "a check has run". This previously read
  // `fLastPacketProgressSec != 0`, which is stamped ~20 lines above by the FIRST check after
  // any reset (the PACKET_COUNT_UNKNOWN sentinel makes packetProgressMade return True with zero
  // packets received). So it was structurally always True and the "still connecting" guard below it
  // could never execute — a guard named for what we hoped it caught rather than what it measured.
  in.everReceivedData = newTotNumPacketsReceived > 0 ? True : False;
  in.secsSinceProgress = secsSinceProgress;
  in.gapLimitSecs = fInterPacketGapMaxTime;
  in.mediaOverTCP = fStreamRTPOverTCP;
  // GStreamer's DEFAULT_TIMEOUT in its real role: how long a UDP session may sit at
  // zero-packets-ever before the media path is presumed blocked and the rebuild switches to TCP.
  in.udpNoDataLimitSecs = udpDataStallTimeoutSecs();
  // Always True on this path: the zero-consumer case returned before the (unsafe) input gather
  // above. The RULE lives in the policy (SessionRecoveryInput::consumerAttached); this early
  // return is only the wiring that also keeps the freed-rtpSource read unreachable.
  in.consumerAttached = True;
  SessionRecoveryAction const action = sessionRecoveryDecide(in);

  if (sessionRecoveryResets(action)) {
    if (sessionRecoveryReportsIdle(action)) {
      // ALWAYS logged, not verbosity-gated. This is the ONLY signal for a peer that stops sending but
      // holds the socket open: such a socket is never readable, so the mid-packet stall guard (which
      // counts read DISPATCHES) can never advance and emits nothing at all. Hiding this behind -v made
      // a whole class of fault invisible to the log tally, while cameras that FIN showed up
      // immediately — a skew that made the fleet look like it only ever suffered graceful closes.
      envir() << TX_CODE_IDLE << " " << *this << "::checkInterPacketGaps last packet received: >"
	      << fInterPacketGapMaxTime << " seconds ago. Resetting session (peer went silent without "
		 "closing; nobody is proven at fault)\n";
    }
    if (sessionRecoverySwitchesToTCP(action)) {
      // ALWAYS logged: a transport switch is a durable, session-scoped decision, and this line is
      // the only record of WHY the session that comes back is suddenly interleaved. Explicitly NOT
      // a camera fault: a blocked UDP path is network topology (firewall/NAT), and the camera
      // answered every RTSP request throughout.
      // TX-TCP-FALLBACK, not TX-RETRY: this line announces a session TEARDOWN plus a durable
      // transport switch, and TX-RETRY is tallied as the cheap-path acknowledgement ("rising is the
      // fix working") -- a fleet of firewalled-UDP cameras must not read as good news.
      envir() << TX_CODE_TCP_FALLBACK << " " << *this << "::checkInterPacketGaps: UDP session delivered"
                 " ZERO packets in " << (int)in.secsSinceProgress << "s since PLAY (limit "
              << udpDataStallTimeoutSecs() << "s). The RTSP handshake works, so UDP media is likely"
                 " blocked (firewall/NAT) -- rebuilding this session with RTP-over-TCP."
                 " NOT a camera fault.\n";
      fStreamRTPOverTCP = True;
    }
    // A PROVEN close deliberately logs no IDLE line here: the teardown classifier already reported
    // it, with better evidence (a captured FIN/RST plus measured link state). A second line would
    // be both redundant and contradictory — it would say "nobody is proven at fault" about a close
    // we just proved. Measured before this fix: 38% of proven closes produced exactly that.
    //
    // The action→reason mapping comes FROM the policy (sessionRecoveryResetReason): a call-site
    // ternary briefly mapped everything non-TCP-switching to inter-packet-gap, so a PROVEN-close
    // reset was tallied as an idle timeout — re-merging in the reason field exactly the two
    // populations the policy separates.
    //
    // ALWAYS scheduleReset, never a direct doReset(): scheduleReset is the one place the
    // reason/cause/state line is logged, and the direct branch (used by the periodic callback, i.e.
    // virtually every reset this function ever takes) silently skipped it — the reason vocabulary
    // was being bypassed on precisely the reset class it was built to explain. Deferring by one
    // event-loop turn is already the semantics every other reset site has.
    scheduleReset(sessionRecoveryResetReason(action));
    return; // the reset path re-arms the watchdog after the reconnect; do not reschedule here
  }

  rearmInterPacketGapCheck();
}

// Re-arm a SINGLE chain: rescheduleDelayedTask cancels any pending gap-check task before
// scheduling the next one, so a stray second chain collapses into one instead of accumulating and
// firing back-to-back. The one place the watchdog is armed, shared by the not-PLAYING skip and the
// normal tail -- two copies of this invariant would have to stay byte-identical forever.
void ProxyRTSPClient::rearmInterPacketGapCheck() {
  envir().taskScheduler().rescheduleDelayedTask(fInterPacketGapsTask, fInterPacketGapMaxTime*MILLION,
						checkInterPacketGaps, this);
}

void ProxyRTSPClient::checkInterPacketGaps(void* clientData) {
  ProxyRTSPClient* rtspClient = (ProxyRTSPClient*)clientData;
  rtspClient->fInterPacketGapsTask = NULL;
  rtspClient->checkInterPacketGaps_();
}

// A reset is the single most expensive thing this class does -- it discards a completed DESCRIBE and
// restarts the whole handshake -- yet the log never said WHICH of the five-plus call sites asked for
// one. That gap cost a whole investigation: a captured 259s recovery on 192.168.11.19 showed SETUP
// sent, then a reset 10s later from state DESCRIBED, with NO SETUP-failed and NO connection-lost
// lifecycle line. Three such cycles back to back. The caller could not be identified from the trace,
// so the reason is now recorded at the point of decision rather than inferred afterwards from which
// lines happen to be absent.
//
// ALWAYS logged, not verbosity-gated: a reset is never routine, and this is the line that says why.
void ProxyRTSPClient::scheduleReset(char const* reason, RequestFailureCause cause) {
  // FOUR fields, four separate vocabularies, never merged:
  //   reason=    WHY the session is being rebuilt   (ResetReasons.hh -- a fixed, greppable set)
  //   cause=     WHAT failed, when a request failed (RequestRetryPolicy.hh)
  //   state=     WHERE the session was              (SessionLifecycle.hh)
  //   transport= HOW the media was carried          (requestTransportName)
  // `reason` briefly carried cause names on some branches, which made it two vocabularies in one
  // field and any tally built from it meaningless. Most resets are not request failures at all
  // (an inter-packet gap, an upstream BYE), so cause= is "n/a" there rather than a fabricated value.
  // transport= exists because without it no tally could split faults TCP vs UDP -- the question a
  // 47-camera manufacturer report needed the journal to answer and it could not.
  envir() << TX_CODE_RESET << " ProxyRTSPClient[" << url() << "]::scheduleReset reason="
          << (reason == NULL ? "unspecified" : reason)
          // ALWAYS requestFailureCauseName(), including for REQ_FAIL_NONE ("none"). This briefly
          // printed a hand-written "n/a" for that case, which invented a SECOND spelling for a value
          // the vocabulary already names -- and it did not even survive the parser, whose
          // [a-z0-9-]+ field pattern read "n/a" as "n". One value, one name, one source.
          << " cause=" << requestFailureCauseName(cause)
          << " state=" << sessionStateName(fLifecycleState)
          << " transport="
          << requestTransportName(fStreamRTPOverTCP ? REQ_TRANSPORT_TCP : REQ_TRANSPORT_UDP) << "\n";
  envir().taskScheduler().rescheduleDelayedTask(fResetTask, 0, doReset, this);
}

void ProxyRTSPClient::doReset() {
  // CANCEL, do not merely forget: clearing fResetTask leaves the task queued, so a direct reset
  // racing a scheduled one let the orphan fire and tear down the session we had just rebuilt.
  // The task-fired path NULLs the handle first, so the cancel is a no-op there.
  // unscheduleDelayedTask is documented (UsageEnvironment.hh) as a no-op on NULL, and NULLs the
  // token itself -- so this single call IS "cancel if pending, then forget".
  envir().taskScheduler().unscheduleDelayedTask(fResetTask);
  if (fVerbosityLevel > 0) {
    envir() << *this << "::doReset\n";
  }

  // Bracket the teardown so any callback from the ABANDONED session that lands in between is
  // reported rather than applied to the session we are rebuilding. That interleaving is exactly
  // what made every reconnect run two full DESCRIBE/SETUP/PLAY cycles; it was invisible because
  // each individual callback looked perfectly ordinary on its own.
  noteLifecycle(SE_RESET_STARTED);
  reset();
  fOurServerMediaSession.resetDESCRIBEState();
  noteLifecycle(SE_RESET_COMPLETED);

  setBaseURL(fOurURL); // because we'll be sending an initial "DESCRIBE" all over again
  sendDESCRIBE();
}

void ProxyRTSPClient::doReset(void* clientData) {
  ProxyRTSPClient* rtspClient = (ProxyRTSPClient*)clientData;
  // Task already fired — drop the handle so doReset()'s cancel is a no-op here. Never unschedule an
  // already-fired token: UsageEnvironment.hh states that is not allowed, and it would touch freed
  // scheduler state.
  rtspClient->fResetTask = NULL;
  rtspClient->doReset();
}

// ALWAYS logged, never verbosity-gated: this path previously emitted NOTHING journal-worthy, so a
// camera that is off, unreachable, or rejecting credentials walked the 1s->256-511s DESCRIBE
// backoff in total silence -- "camera down for six hours" was indistinguishable in the journal from
// "proxy idle". The cause is derived the standard way (positive status classified, otherwise the
// failure site's own record); the ladder's next delay stays on the verbosity-gated line in
// scheduleDESCRIBECommand, which follows immediately.
void ProxyRTSPClient::noteDescribeFailed(int resultCode) {
  RequestFailureCause const cause = failureCauseFor(resultCode);
  envir() << TX_CODE_DESCRIBE_FAIL << " ProxyRTSPClient[" << url() << "]: DESCRIBE failed (result "
          << resultCode << ", cause=" << requestFailureCauseName(cause)
          << (requestFailureBlamesPeer(cause) ? ", peer-attributed" : ", not proven the camera's fault")
          << "). The stream cannot start; the DESCRIBE backoff ladder continues.\n";
}

void ProxyRTSPClient::scheduleDESCRIBECommand() {
  // Delay 1s, 2s, 4s, 8s ... 256s until sending the next "DESCRIBE".  Then, keep delaying a random time from [256..511] seconds:
  unsigned secondsToDelay;
  if (fNextDESCRIBEDelay <= 256) {
    secondsToDelay = fNextDESCRIBEDelay;
    fNextDESCRIBEDelay *= 2;
  } else {
    secondsToDelay = 256 + (our_random()&0xFF); // [256..511] seconds
  }

  if (fVerbosityLevel > 0) {
    envir() << *this << ": RTSP \"DESCRIBE\" command failed; trying again in " << secondsToDelay << " seconds\n";
  }
  fDESCRIBECommandTask = envir().taskScheduler().scheduleDelayedTask(secondsToDelay*MILLION, sendDESCRIBE, this);
}

void ProxyRTSPClient::sendDESCRIBE(void* clientData) {
  ProxyRTSPClient* rtspClient = (ProxyRTSPClient*)clientData;
  if (rtspClient != NULL) {
    rtspClient->fDESCRIBECommandTask = NULL;
    rtspClient->sendDESCRIBE();
  }
}

void ProxyRTSPClient::sendDESCRIBE() {
  noteLifecycle(SE_DESCRIBE_SENT);
  sendDescribeCommand(::continueAfterDESCRIBE, auth());
}

void ProxyRTSPClient::subsessionTimeout(void* clientData) {
  ((ProxyRTSPClient*)clientData)->handleSubsessionTimeout();
}

void ProxyRTSPClient::handleSubsessionTimeout() {
  fSubsessionTimerTask = NULL;
  // We still have one or more subsessions ('tracks') left to "SETUP".  But we can't wait any longer for them.  Send a "PLAY" now:
  MediaSession* sess = fOurServerMediaSession.fClientMediaSession;
  if (sess == NULL) {
    // ALWAYS logged: the subsession timer fired with no client session to PLAY. Nothing here can
    // proceed, and silently returning would leave the stream dark with no record of why.
    envir() << TX_CODE_UNEXPECTED << " ProxyRTSPClient[" << fOurURL << "]::handleSubsessionTimeout:"
	       " the SETUP timer fired but there is no client media session, so no PLAY was sent."
	       " The stream cannot start from here; re-establishing.\n";
    scheduleReset(RESET_REASON_SUBSESSION_TIMEOUT);
    return;
  }

  sendPlayCommand(*sess, ::continueAfterPLAY, -1.0f, -1.0f, 1.0f, fOurAuthenticator);
  // Set ONLY when a PLAY was actually sent. This used to be unconditional, i.e. named for the last
  // command but measuring "we reached this line": with no session it would claim a PLAY that never
  // happened, so closeStreamSource would later PAUSE a stream that never played and
  // createNewStreamSource would skip the resume PLAY, leaving that client permanently dark.
  fLastCommandWasPLAY = True;
}


//////// "ProxyServerMediaSubsession" implementation //////////

ProxyServerMediaSubsession
::ProxyServerMediaSubsession(MediaSubsession& mediaSubsession,
			     portNumBits initialPortNum, Boolean multiplexRTCPWithRTP)
  : OnDemandServerMediaSubsession(mediaSubsession.parentSession().envir(), True/*reuseFirstSource*/,
				  initialPortNum, multiplexRTCPWithRTP),
    fClientMediaSubsession(mediaSubsession), fCodecName(strDup(mediaSubsession.codecName())),
    fNext(NULL), fHaveSetupStream(False) {
}

UsageEnvironment& operator<<(UsageEnvironment& env, const ProxyServerMediaSubsession& psmss) { // used for debugging
  return env << "ProxyServerMediaSubsession[" << psmss.url() << "," << psmss.codecName() << "]";
}

ProxyServerMediaSubsession::~ProxyServerMediaSubsession() {
  if (verbosityLevel() > 0) {
    envir() << *this << "::~ProxyServerMediaSubsession()\n";
  }

  delete[] (char*)fCodecName;
}

FramedSource* ProxyServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  ProxyServerMediaSession* const sms = (ProxyServerMediaSession*)fParentSession;

  if (verbosityLevel() > 0) {
    envir() << *this << "::createNewStreamSource(session id " << clientSessionId << ")\n";
  }

  // If we haven't yet created a data source from our 'media subsession' object, initiate() it to do so:
  if (fClientMediaSubsession.readSource() == NULL) {
    if (sms->fTranscodingTable == NULL || !sms->fTranscodingTable->weWillTranscode("audio", "MPA-ROBUST")) fClientMediaSubsession.receiveRawMP3ADUs(); // hack for proxying MPA-ROBUST streams
    if (sms->fTranscodingTable == NULL || !sms->fTranscodingTable->weWillTranscode("video", "JPEG")) fClientMediaSubsession.receiveRawJPEGFrames(); // hack for proxying JPEG/RTP streams.
    fClientMediaSubsession.initiate();
    if (verbosityLevel() > 0) {
      envir() << "\tInitiated: " << *this << "\n";
    }

    if (fClientMediaSubsession.readSource() != NULL) {
      // First, check whether we have defined a 'transcoder' filter to be used with this codec:
      if (sms->fTranscodingTable != NULL) {
	char* outputCodecName;
	FramedFilter* transcoder
	  = sms->fTranscodingTable->lookupTranscoder(fClientMediaSubsession, outputCodecName);
	if (transcoder != NULL) {
	  fClientMediaSubsession.addFilter(transcoder);
	  delete[] (char*)fCodecName; fCodecName = outputCodecName;
	}
      }

      // Then, add to the front of all data sources a filter that will 'normalize' their frames'
      // presentation times, before the frames get re-transmitted by our server:
      FramedFilter* normalizerFilter = sms->fPresentationTimeSessionNormalizer
	->createNewPresentationTimeSubsessionNormalizer(fClientMediaSubsession.readSource(),
							fClientMediaSubsession.rtpSource(),
							fCodecName);
      fClientMediaSubsession.addFilter(normalizerFilter);

      // Some data sources require a 'framer' object to be added, before they can be fed into
      // a "RTPSink".  Adjust for this now:
      if (strcmp(fCodecName, "H264") == 0) {
	fClientMediaSubsession.addFilter(H264VideoStreamDiscreteFramer
					 ::createNew(envir(), fClientMediaSubsession.readSource()));
      } else if (strcmp(fCodecName, "H265") == 0) {
	fClientMediaSubsession.addFilter(H265VideoStreamDiscreteFramer
					 ::createNew(envir(), fClientMediaSubsession.readSource()));
      } else if (strcmp(fCodecName, "MP4V-ES") == 0) {
	fClientMediaSubsession.addFilter(MPEG4VideoStreamDiscreteFramer
					 ::createNew(envir(), fClientMediaSubsession.readSource(),
						     True/* leave PTs unmodified*/));
      } else if (strcmp(fCodecName, "MPV") == 0) {
	fClientMediaSubsession.addFilter(MPEG1or2VideoStreamDiscreteFramer
					 ::createNew(envir(), fClientMediaSubsession.readSource(),
						     False, 5.0, True/* leave PTs unmodified*/));
      } else if (strcmp(fCodecName, "DV") == 0) {
	fClientMediaSubsession.addFilter(DVVideoStreamFramer
					 ::createNew(envir(), fClientMediaSubsession.readSource(),
						     False, True/* leave PTs unmodified*/));
      }
    }

    if (fClientMediaSubsession.rtcpInstance() != NULL) {
      fClientMediaSubsession.rtcpInstance()->setByeHandler(subsessionByeHandler, this);
    }
  }

  ProxyRTSPClient* const proxyRTSPClient = sms->fProxyRTSPClient;
  if (clientSessionId != 0) {
    // We're being called as a result of implementing a RTSP "SETUP".
    if (!fHaveSetupStream) {
      // This is our first "SETUP".  Send RTSP "SETUP" and later "PLAY" commands to the proxied server, to start streaming:
      // (Before sending "SETUP", enqueue ourselves on the "RTSPClient"s 'SETUP queue', so we'll be able to get the correct
      //  "ProxyServerMediaSubsession" to handle the response.  (Note that responses come back in the same order as requests.))
      Boolean queueWasEmpty = proxyRTSPClient->fSetupQueueHead == NULL;
      if (queueWasEmpty) {
	proxyRTSPClient->fSetupQueueHead = this;
	proxyRTSPClient->fSetupQueueTail = this;
      } else {
	// Add ourself to the "RTSPClient"s 'SETUP queue' (if we're not already on it):
	ProxyServerMediaSubsession* psms;
	for (psms = proxyRTSPClient->fSetupQueueHead; psms != NULL; psms = psms->fNext) {
	  if (psms == this) break;
	}
	if (psms == NULL) {
	    proxyRTSPClient->fSetupQueueTail->fNext = this;
	    proxyRTSPClient->fSetupQueueTail = this;
	}
      }

      // Hack: If there's already a pending "SETUP" request, don't send this track's "SETUP" right away, because
      // the server might not properly handle 'pipelined' requests.  Instead, wait until after previous "SETUP" responses come back.
      if (queueWasEmpty) {
	proxyRTSPClient->sendNewSetup(*this);
      }
    } else {
      // This is a "SETUP" from a new client.  We know that there are no other currently active clients (otherwise we wouldn't
      // have been called here), so we know that the substream was previously "PAUSE"d.  Send "PLAY" downstream once again,
      // to resume the stream:
      if (!proxyRTSPClient->fLastCommandWasPLAY) { // so that we send only one "PLAY"; not one for each subsession
	proxyRTSPClient->sendPlayCommand(fClientMediaSubsession.parentSession(), ::continueAfterPLAY, -1.0f/*resume from previous point*/,
					 -1.0f, 1.0f, proxyRTSPClient->auth());
	proxyRTSPClient->fLastCommandWasPLAY = True;
      }
    }
  }

  estBitrate = fClientMediaSubsession.bandwidth();
  if (estBitrate == 0) estBitrate = 50; // kbps, estimate
  return fClientMediaSubsession.readSource();
}

void ProxyServerMediaSubsession::closeStreamSource(FramedSource* /*inputSource*/) {
  if (verbosityLevel() > 0) {
    envir() << *this << "::closeStreamSource()\n";
  }
  // Because there's only one input source for this 'subsession' (regardless of how many downstream clients are proxying it),
  // we don't close the input source here.  (Instead, we wait until *this* object gets deleted.)
  // However, because (as evidenced by this function having been called) we no longer have any clients accessing the stream,
  // then we "PAUSE" the downstream proxied stream, until a new client arrives:
  if (fHaveSetupStream) {
    ProxyServerMediaSession* const sms = (ProxyServerMediaSession*)fParentSession;
    ProxyRTSPClient* const proxyRTSPClient = sms->fProxyRTSPClient;
    if (proxyRTSPClient->fLastCommandWasPLAY) { // so that we send only one "PAUSE"; not one for each subsession
      if (fParentSession->referenceCount() > 1) {
	// There are other client(s) still streaming other subsessions of this stream.
	// Therefore, we don't send a "PAUSE" for the whole stream.
	// In principle, we would send a "PAUSE" only for the sub-stream here, but some
	// back-end servers might mis-handle that by pausing the entire stream.
	// So instead, we do nothing here.
	//proxyRTSPClient->sendPauseCommand(fClientMediaSubsession, NULL, proxyRTSPClient->auth());
      } else if (!proxyRTSPClient->fNeverPause) {
	// Normal case: There are no other clients still streaming (parts of) this stream.
	// Send a "PAUSE" for the whole stream.
	//
	// -P/neverPause: when set, we SKIP this entire block. We never PAUSE the
	// upstream, so it stays PLAYing forever and the inter-packet-gap watchdog (-D) is never
	// unscheduled. This closes the wedge where a buggy camera drops the resume handshake:
	// with pause disabled there is no resume to lose, and the -D watchdog keeps recovering
	// any real data stall via doReset() -> re-DESCRIBE. Cost is one idle upstream RTSP
	// session per proxy (cheap; recording cameras hold one permanently anyway).
	proxyRTSPClient->sendPauseCommand(fClientMediaSubsession.parentSession(), NULL, proxyRTSPClient->auth());
	proxyRTSPClient->notePauseSent();
      }
    }
  }
}

RTPSink* ProxyServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) {
  if (verbosityLevel() > 0) {
    envir() << *this << "::createNewRTPSink()\n";
  }

  // Create (and return) the appropriate "RTPSink" object for our codec:
  // (Note: The configuration string might not be correct if a transcoder is used. FIX!) #####
  RTPSink* newSink;
  if (strcmp(fCodecName, "AC3") == 0 || strcmp(fCodecName, "EAC3") == 0) {
    newSink = AC3AudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					 fClientMediaSubsession.rtpTimestampFrequency()); 
#if 0 // This code does not work; do *not* enable it:
  } else if (strcmp(fCodecName, "AMR") == 0 || strcmp(fCodecName, "AMR-WB") == 0) {
    Boolean isWideband = strcmp(fCodecName, "AMR-WB") == 0;
    newSink = AMRAudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					 isWideband, fClientMediaSubsession.numChannels());
#endif
  } else if (strcmp(fCodecName, "DV") == 0) {
    newSink = DVVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
  } else if (strcmp(fCodecName, "GSM") == 0) {
    newSink = GSMAudioRTPSink::createNew(envir(), rtpGroupsock);
  } else if (strcmp(fCodecName, "H263-1998") == 0 || strcmp(fCodecName, "H263-2000") == 0) {
    newSink = H263plusVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					      fClientMediaSubsession.rtpTimestampFrequency()); 
  } else if (strcmp(fCodecName, "H264") == 0) {
    newSink = H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					  fClientMediaSubsession.fmtp_spropparametersets());
  } else if (strcmp(fCodecName, "H265") == 0) {
    newSink = H265VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					  fClientMediaSubsession.fmtp_spropvps(),
					  fClientMediaSubsession.fmtp_spropsps(),
					  fClientMediaSubsession.fmtp_sproppps());
  } else if (strcmp(fCodecName, "JPEG") == 0) {
    newSink = SimpleRTPSink::createNew(envir(), rtpGroupsock, 26, 90000, "video", "JPEG",
				       1/*numChannels*/, False/*allowMultipleFramesPerPacket*/, False/*doNormalMBitRule*/);
  } else if (strcmp(fCodecName, "MP4A-LATM") == 0) {
    newSink = MPEG4LATMAudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					       fClientMediaSubsession.rtpTimestampFrequency(),
					       fClientMediaSubsession.fmtp_config(),
					       fClientMediaSubsession.numChannels());
  } else if (strcmp(fCodecName, "MP4V-ES") == 0) {
    newSink = MPEG4ESVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					     fClientMediaSubsession.rtpTimestampFrequency(),
					     fClientMediaSubsession.attrVal_unsigned("profile-level-id"),
					     fClientMediaSubsession.fmtp_config()); 
  } else if (strcmp(fCodecName, "MPA") == 0) {
    newSink = MPEG1or2AudioRTPSink::createNew(envir(), rtpGroupsock);
  } else if (strcmp(fCodecName, "MPA-ROBUST") == 0) {
    newSink = MP3ADURTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
  } else if (strcmp(fCodecName, "MPEG4-GENERIC") == 0) {
    newSink = MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
					     rtpPayloadTypeIfDynamic, fClientMediaSubsession.rtpTimestampFrequency(),
					     fClientMediaSubsession.mediumName(),
					     fClientMediaSubsession.attrVal_str("mode"),
					     fClientMediaSubsession.fmtp_config(), fClientMediaSubsession.numChannels());
  } else if (strcmp(fCodecName, "MPV") == 0) {
    newSink = MPEG1or2VideoRTPSink::createNew(envir(), rtpGroupsock);
  } else if (strcmp(fCodecName, "OPUS") == 0) {
    newSink = SimpleRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
				       48000, "audio", "OPUS", 2, False/*only 1 Opus 'packet' in each RTP packet*/);
  } else if (strcmp(fCodecName, "T140") == 0) {
    newSink = T140TextRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
  } else if (strcmp(fCodecName, "THEORA") == 0) {
    newSink = TheoraVideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					    fClientMediaSubsession.fmtp_config()); 
  } else if (strcmp(fCodecName, "VORBIS") == 0) {
    newSink = VorbisAudioRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
					    fClientMediaSubsession.rtpTimestampFrequency(), fClientMediaSubsession.numChannels(),
					    fClientMediaSubsession.fmtp_config()); 
  } else if (strcmp(fCodecName, "VP8") == 0) {
    newSink = VP8VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
  } else if (strcmp(fCodecName, "VP9") == 0) {
    newSink = VP9VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
  } else if (strcmp(fCodecName, "AMR") == 0 || strcmp(fCodecName, "AMR-WB") == 0) {
    // Proxying of these codecs is currently *not* supported, because the data received by the "RTPSource" object is not in a
    // form that can be fed directly into a corresponding "RTPSink" object.
    if (verbosityLevel() > 0) {
      envir() << "\treturns NULL (because we currently don't support the proxying of \""
	      << fClientMediaSubsession.mediumName() << "/" << fCodecName << "\" streams)\n";
    }
    return NULL;
  } else if (strcmp(fCodecName, "QCELP") == 0 ||
	     strcmp(fCodecName, "H261") == 0 ||
	     strcmp(fCodecName, "H263-1998") == 0 || strcmp(fCodecName, "H263-2000") == 0 ||
	     strcmp(fCodecName, "X-QT") == 0 || strcmp(fCodecName, "X-QUICKTIME") == 0) {
    // This codec requires a specialized RTP payload format; however, we don't yet have an appropriate "RTPSink" subclass for it:
    if (verbosityLevel() > 0) {
      envir() << "\treturns NULL (because we don't have a \"RTPSink\" subclass for this RTP payload format)\n";
    }
    return NULL;
  } else {
    // This codec is assumed to have a simple RTP payload format that can be implemented just with a "SimpleRTPSink":
    Boolean allowMultipleFramesPerPacket = True; // by default
    Boolean doNormalMBitRule = True; // by default
    // Some codecs change the above default parameters:
    if (strcmp(fCodecName, "MP2T") == 0) {
      doNormalMBitRule = False; // no RTP 'M' bit
    }
    newSink = SimpleRTPSink::createNew(envir(), rtpGroupsock,
				       rtpPayloadTypeIfDynamic, fClientMediaSubsession.rtpTimestampFrequency(),
				       fClientMediaSubsession.mediumName(), fCodecName,
				       fClientMediaSubsession.numChannels(), allowMultipleFramesPerPacket, doNormalMBitRule);
  }

  // Because our relayed frames' presentation times are inaccurate until the input frames have been RTCP-synchronized,
  // we temporarily disable RTCP "SR" reports for this "RTPSink" object:
  newSink->enableRTCPReports() = False;

  // Also tell our "PresentationTimeSubsessionNormalizer" object about the "RTPSink", so it can enable RTCP "SR" reports later:
  PresentationTimeSubsessionNormalizer* ssNormalizer;
  if (strcmp(fCodecName, "H264") == 0 ||
      strcmp(fCodecName, "H265") == 0 ||
      strcmp(fCodecName, "MP4V-ES") == 0 ||
      strcmp(fCodecName, "MPV") == 0 ||
      strcmp(fCodecName, "DV") == 0) {
    // There was a separate 'framer' object in front of the "PresentationTimeSubsessionNormalizer", so go back one object to get it:
    ssNormalizer = (PresentationTimeSubsessionNormalizer*)(((FramedFilter*)inputSource)->inputSource());
  } else {
    ssNormalizer = (PresentationTimeSubsessionNormalizer*)inputSource;
  }
  ssNormalizer->setRTPSink(newSink);

  return newSink;
}

Groupsock* ProxyServerMediaSubsession
::createGroupsock(struct sockaddr_storage const& addr, Port port) {
  ProxyServerMediaSession* parentSession = (ProxyServerMediaSession*)fParentSession;
  return parentSession->createGroupsock(addr, port);
}

RTCPInstance* ProxyServerMediaSubsession
::createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
	     unsigned char const* cname, RTPSink* sink) {
  ProxyServerMediaSession* parentSession = (ProxyServerMediaSession*)fParentSession;
  return parentSession->createRTCP(RTCPgs, totSessionBW, cname, sink);
}

void ProxyServerMediaSubsession::subsessionByeHandler(void* clientData) {
  ((ProxyServerMediaSubsession*)clientData)->subsessionByeHandler();
}

void ProxyServerMediaSubsession::subsessionByeHandler() {
  if (verbosityLevel() > 0) {
    envir() << *this << ": received RTCP \"BYE\".  (The back-end stream has ended.)\n";
  }

  // This "BYE" signals that our input source has (effectively) closed, so pass this onto the front-end clients:
  fHaveSetupStream = False; // hack to stop "PAUSE" getting sent by:
  if (fClientMediaSubsession.readSource() != NULL) {
    fClientMediaSubsession.readSource()->handleClosure();
  }

  // And then treat this as if we had lost connection to the back-end server,
  // and can reestablish streaming from it only by sending another "DESCRIBE":
  ProxyServerMediaSession* const sms = (ProxyServerMediaSession*)fParentSession;
  ProxyRTSPClient* const proxyRTSPClient = sms->fProxyRTSPClient;
  proxyRTSPClient->scheduleReset(RESET_REASON_STREAM_SOURCE_CLOSED);
}


////////// PresentationTimeSessionNormalizer and PresentationTimeSubsessionNormalizer implementations //////////

// PresentationTimeSessionNormalizer:

PresentationTimeSessionNormalizer::PresentationTimeSessionNormalizer(UsageEnvironment& env)
  : Medium(env),
    fSubsessionNormalizers(NULL), fMasterSSNormalizer(NULL) {
}

PresentationTimeSessionNormalizer::~PresentationTimeSessionNormalizer() {
  while (fSubsessionNormalizers != NULL) {
    Medium::close(fSubsessionNormalizers);
  }
}

PresentationTimeSubsessionNormalizer* PresentationTimeSessionNormalizer
::createNewPresentationTimeSubsessionNormalizer(FramedSource* inputSource, RTPSource* rtpSource,
						char const* codecName) {
  fSubsessionNormalizers
    = new PresentationTimeSubsessionNormalizer(*this, inputSource, rtpSource, codecName, fSubsessionNormalizers);
  return fSubsessionNormalizers;
}

void PresentationTimeSessionNormalizer
::normalizePresentationTime(PresentationTimeSubsessionNormalizer* ssNormalizer,
			    struct timeval& toPT, struct timeval const& fromPT) {
  Boolean const hasBeenSynced = ssNormalizer->fRTPSource->hasBeenSynchronizedUsingRTCP();

  if (!hasBeenSynced) {
    // If "fromPT" has not yet been RTCP-synchronized, then it was generated by our own receiving code, and thus
    // is already aligned with 'wall-clock' time.  Just copy it 'as is' to "toPT":
    toPT = fromPT;
  } else {
    if (fMasterSSNormalizer == NULL) {
      // Make "ssNormalizer" the 'master' subsession - meaning that its presentation time is adjusted to align with 'wall clock'
      // time, and the presentation times of other subsessions (if any) are adjusted to retain their relative separation with
      // those of the master:
      fMasterSSNormalizer = ssNormalizer;

      struct timeval timeNow;
      gettimeofday(&timeNow, NULL);

      // Compute: fPTAdjustment = timeNow - fromPT
      fPTAdjustment.tv_sec = timeNow.tv_sec - fromPT.tv_sec;
      fPTAdjustment.tv_usec = timeNow.tv_usec - fromPT.tv_usec;
      // Note: It's OK if one or both of these fields underflows; the result still works out OK later.
    }

    // Compute a normalized presentation time: toPT = fromPT + fPTAdjustment
    toPT.tv_sec = fromPT.tv_sec + fPTAdjustment.tv_sec - 1;
    toPT.tv_usec = fromPT.tv_usec + fPTAdjustment.tv_usec + MILLION;
    while (toPT.tv_usec > MILLION) { ++toPT.tv_sec; toPT.tv_usec -= MILLION; }

    // Because "ssNormalizer"s relayed presentation times are accurate from now on, enable RTCP "SR" reports for its "RTPSink":
    RTPSink* const rtpSink = ssNormalizer->fRTPSink;
    if (rtpSink != NULL) { // sanity check; should always be true
      rtpSink->enableRTCPReports() = True;
    }
  }
}

void PresentationTimeSessionNormalizer
::removePresentationTimeSubsessionNormalizer(PresentationTimeSubsessionNormalizer* ssNormalizer) {
  // Clear the master pointer if it referenced the normalizer being removed. Without this, after an
  // upstream reconnect (which frees all subsession normalizers via the client session) fMasterSSNormalizer
  // dangles at freed memory; normalizePresentationTime only tests `== NULL`, so it would NOT re-elect a
  // new master and would keep applying the PREVIOUS session's wall-clock fPTAdjustment -> a presentation-
  // time discontinuity in the relayed stream/recordings on every reconnect. (This fork reconnects
  // aggressively via the -D inter-packet-gap watchdog, so this fired constantly, not rarely.)
  if (fMasterSSNormalizer == ssNormalizer) fMasterSSNormalizer = NULL;

  // Unlink "ssNormalizer" from the linked list (starting with "fSubsessionNormalizers"):
  if (fSubsessionNormalizers == ssNormalizer) {
    fSubsessionNormalizers = fSubsessionNormalizers->fNext;
  } else {
    PresentationTimeSubsessionNormalizer** ssPtrPtr = &(fSubsessionNormalizers->fNext);
    while (*ssPtrPtr != ssNormalizer) ssPtrPtr = &((*ssPtrPtr)->fNext);
    *ssPtrPtr = (*ssPtrPtr)->fNext;
  }
}

// PresentationTimeSubsessionNormalizer:

PresentationTimeSubsessionNormalizer
::PresentationTimeSubsessionNormalizer(PresentationTimeSessionNormalizer& parent, FramedSource* inputSource, RTPSource* rtpSource,
				       char const* codecName, PresentationTimeSubsessionNormalizer* next)
  : FramedFilter(parent.envir(), inputSource),
    fParent(parent), fRTPSource(rtpSource), fRTPSink(NULL), fCodecName(codecName), fNext(next) {
}

PresentationTimeSubsessionNormalizer::~PresentationTimeSubsessionNormalizer() {
  fParent.removePresentationTimeSubsessionNormalizer(this);
}

void PresentationTimeSubsessionNormalizer::afterGettingFrame(void* clientData, unsigned frameSize,
							     unsigned numTruncatedBytes,
							     struct timeval presentationTime,
							     unsigned durationInMicroseconds) {
  ((PresentationTimeSubsessionNormalizer*)clientData)
    ->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void PresentationTimeSubsessionNormalizer::afterGettingFrame(unsigned frameSize,
							     unsigned numTruncatedBytes,
							     struct timeval presentationTime,
							     unsigned durationInMicroseconds) {
  // This filter is implemented by passing all frames through unchanged, except that "fPresentationTime" is changed:
  fFrameSize = frameSize;
  fNumTruncatedBytes = numTruncatedBytes;
  fDurationInMicroseconds = durationInMicroseconds;

  fParent.normalizePresentationTime(this, fPresentationTime, presentationTime);

  // Hack for JPEG/RTP proxying.  Because we're proxying JPEG by just copying the raw JPEG/RTP payloads, without interpreting them,
  // we need to also 'copy' the RTP 'M' (marker) bit from the "RTPSource" to the "RTPSink":
  if (fRTPSource->curPacketMarkerBit() && strcmp(fCodecName, "JPEG") == 0) ((SimpleRTPSink*)fRTPSink)->setMBitOnNextPacket();

  // Complete delivery:
  FramedSource::afterGetting(this);
}

void PresentationTimeSubsessionNormalizer::doGetNextFrame() {
  fInputSource->getNextFrame(fTo, fMaxSize, afterGettingFrame, this, FramedSource::handleClosure, this);
}

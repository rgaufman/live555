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
// C++ header

#ifndef _PROXY_SERVER_MEDIA_SESSION_HH
#define _PROXY_SERVER_MEDIA_SESSION_HH

#ifndef _SERVER_MEDIA_SESSION_HH
#include "ServerMediaSession.hh"
#endif
#ifndef _MEDIA_SESSION_HH
#include "MediaSession.hh"
#endif
#ifndef _RTSP_CLIENT_HH
#include "RTSPClient.hh"
#endif
#ifndef _MEDIA_TRANSCODING_TABLE_HH
#include "MediaTranscodingTable.hh"
#endif
// ONE guard per include. These three were briefly wrapped in a single `#ifndef _SESSION_LIFECYCLE_HH`
// block, which meant any translation unit that had already reached SessionLifecycle.hh skipped the
// other two entirely -- taking RequestRetryState and RtspLivenessMethod with them, and only in some
// build orders. Same family as the StreamFaultCodes.hh break that compiled on macOS and killed the
// box build. test_proxy_wiring.cpp now fails if a guard block ever contains more than one #include.
#ifndef _SESSION_LIFECYCLE_HH
#include "SessionLifecycle.hh"
#endif
#ifndef _REQUEST_RETRY_POLICY_HH
#include "RequestRetryPolicy.hh" // retry the failed REQUEST rather than rebuilding the session
#endif
#ifndef _RTSP_KEEP_ALIVE_HH
#include "RTSPKeepAlive.hh" // RtspLivenessMethod: which keep-alive we last sent
#endif

// A subclass of "RTSPClient", used to refer to the particular "ProxyServerMediaSession" object being used.
// It is used only within the implementation of "ProxyServerMediaSession", but is defined here, in case developers wish to
// subclass it.

class ProxyRTSPClient: public RTSPClient {
public:
  ProxyRTSPClient(class ProxyServerMediaSession& ourServerMediaSession, char const* rtspURL,
                  char const* username, char const* password,
                  portNumBits tunnelOverHTTPPortNum, int verbosityLevel, int socketNumToServer,
                  unsigned interPacketGapMaxTime = 0, Boolean neverPause = False);
  virtual ~ProxyRTSPClient();

  void continueAfterDESCRIBE(char const* sdpDescription);
  // Report a failed DESCRIBE (TX-DESCRIBE-FAIL, always-on) while the resultCode still exists --
  // continueAfterDESCRIBE only receives the collapsed NULL. Called by the response wrapper before
  // the backoff ladder is scheduled.
  void noteDescribeFailed(int resultCode);
  void continueAfterLivenessCommand(int resultCode, Boolean serverSupportsGetParameter,
				    Boolean serverSupportsSetParameter);
  Boolean serverSupportsGetParameter() const { return fServerSupportsGetParameter; }
  Boolean serverSupportsSetParameter() const { return fServerSupportsSetParameter; }
  void continueAfterSETUP(int resultCode);
  void continueAfterPLAY(int resultCode);
  // `reason` names the CALL SITE that asked for the reset, and is logged. A reset discards a
  // completed DESCRIBE and restarts the whole handshake, so "why" is the first question every time --
  // and a captured 259s recovery could not answer it, because every caller produced an identical
  // line. Defaulted so no caller can silently omit it, but prefer an explicit string.
  // `reason` MUST be a RESET_REASON_* constant from ResetReasons.hh -- never a string literal. That
  // file is the single vocabulary the log tally (bin/lib/flap_diag.rb) parses, and a literal here is
  // how a near-duplicate spelling gets introduced that nothing catches.
  //
  // `cause` is the REQUEST failure, when the rebuild followed one. It is a SEPARATE field on
  // purpose: cause names were briefly passed as `reason`, so that one field held two vocabularies
  // depending on the branch and could not be aggregated. Most resets are not request failures, and
  // those correctly report REQ_FAIL_NONE rather than a plausible-looking invention.
  void scheduleReset(char const* reason, RequestFailureCause cause = REQ_FAIL_NONE);

private:
  void reset();
  int connectToServer(int socketNum, portNumBits remotePortNum);

  Authenticator* auth() { return fOurAuthenticator; }

  void scheduleLivenessCommand();
  static void sendLivenessCommand(void* clientData);
  // No delay-vs-direct parameter: every reset it decides goes through scheduleReset, because that
  // is the one place the reason/cause/state line is logged -- the direct-doReset variant silently
  // skipped it on the periodic path, bypassing the reason vocabulary on most gap resets.
  void checkInterPacketGaps_();
  static void checkInterPacketGaps(void* clientData);
  // Re-arm the gap watchdog as a SINGLE chain (cancel any pending check, then schedule the next).
  // One helper because this is an invariant, not a convenience: two call sites each carrying their
  // own unschedule+schedule pair must stay byte-identical forever, and the day they drift the
  // watchdog is silently disarmed or double-armed.
  void rearmInterPacketGapCheck();
  // The PAUSE we send is fire-and-forget (no response handler), so this records what the client has
  // COMMITTED to -- it stopped expecting media (SE_PAUSE_SENT) -- and disarms the gap watchdog that
  // would otherwise blame the camera for the silence we just requested. Lives here so the lifecycle
  // machine is driven only from inside this class, not by ProxyServerMediaSubsession reaching in.
  void notePauseSent();
  void doReset();
  static void doReset(void* clientData);

  void scheduleDESCRIBECommand();
  static void sendDESCRIBE(void* clientData);
  void sendDESCRIBE();

  // Re-send the queued SETUP after a deliberate wait. Only REQ_ACTION_RETRY_DELAYED uses this: a
  // local port conflict (EADDRINUSE) clears when the previous socket finishes closing, so the retry
  // has to happen LATER, not harder.
  static void resendSetup(void* clientData);
  void resendSetup();

  // Send a SETUP and arm the response deadline. The ONLY way a SETUP should leave this class: a
  // direct sendSetupCommand would be a request with no deadline, which is the defect being fixed.
  void sendSetupWithDeadline(MediaSubsession& subsession);
  // The keep-alive's no-reply deadline (same GStreamer rule as the SETUP deadline). Without it a
  // keep-alive the camera never answers left the chain dead: nothing re-scheduled until a response
  // handler ran, so the session silently stopped being kept alive.
  static void livenessDeadlineExpired(void* clientData);
  void livenessDeadlineExpired();
  // A NEW SETUP (first for its subsession): send + count + mark, kept together so no call site can
  // send one and forget the bookkeeping. Re-sends (resendSetup) bypass this on purpose -- they must
  // not inflate fNumSetupsSent, which reconnectRequiresFullReset reads.
  void sendNewSetup(class ProxyServerMediaSubsession& smss);
  // The deadline expired with no response. GStreamer treats this as a timeout, not a disconnect.
  static void requestDeadlineExpired(void* clientData);
  void requestDeadlineExpired();
  // The one place a SETUP failure is acted on, shared by the response path and the deadline path so
  // the retry policy cannot exist in two copies that drift apart.
  void handleSetupFailure(int resultCode, RequestFailureCause cause);
  // The success half of continueAfterSETUP, split out so the failure half could be shared.
  void continueAfterSETUPSucceeded();

  static void subsessionTimeout(void* clientData);
  void handleSubsessionTimeout();

  // Drive the session state machine (SessionLifecycle.hh) and report anything that does not fit it.
  // Every RTSP milestone routes through here, so there is ONE place that knows what state the
  // session is in -- previously that knowledge was scattered across four independent flags and no
  // caller could tell a legal sequence from an impossible one. Legal transitions log only at
  // verbosity>0 (debug progress); an ILLEGAL one always logs, with TX_CODE_UNEXPECTED, because it
  // means our bookkeeping has diverged from what the session is actually doing.
  void noteLifecycle(SessionEvent event);

private:
  friend class ProxyServerMediaSession;
  friend class ProxyServerMediaSubsession;
  ProxyServerMediaSession& fOurServerMediaSession;
  char* fOurURL;
  Authenticator* fOurAuthenticator;
  Boolean fStreamRTPOverTCP;
  // Genuinely tunnelling RTSP over HTTP (not the 0xFFFF plain-TCP hack). Kept because a 461
  // transport switch is IMPOSSIBLE under a tunnel: sendSetupCommand forces streamUsingTCP there, so
  // flipping fStreamRTPOverTCP would re-send the refused transport AND leave the flag misdescribing
  // the session to everything else that reads it (reconnectRequiresFullReset,
  // keepAliveLossReconnectsInPlace).
  Boolean fUsingHTTPTunnel;
  class ProxyServerMediaSubsession *fSetupQueueHead, *fSetupQueueTail;
  // SETUPs SENT, not completed — incremented immediately after sendSetupCommand(), before any
  // response. The name used to say "Done", which was a lie the callers then read as truth.
  //
  // "Sent" is deliberately the right moment for its main consumer, reconnectRequiresFullReset():
  // that asks "were subsessions bound to the OLD TCP connection?", and RTP-over-TCP interleaved
  // channel ids are claimed inside sendSetupCommand (RTSPClient.cpp) as the request goes out — not
  // when the response arrives. So the binding exists from the moment of sending, and a counter of
  // completions would under-report it. The value is correct; only the name was wrong.
  unsigned fNumSetupsSent;
  unsigned fNextDESCRIBEDelay; // in seconds
  unsigned fTotNumPacketsReceived;
  long fLastPacketProgressSec; // MONOTONIC seconds at the last fTotNumPacketsReceived increase; the inter-packet-gap reset gates on real elapsed time since this, not a bare count compare (see checkInterPacketGaps_). Monotonic, not wall-clock, so an NTP step cannot fabricate or hide the gap (MonotonicTime.hh)
  unsigned fInterPacketGapMaxTime; // in seconds
  Boolean fNeverPause; // if True, never PAUSE the upstream on last client leaving -> stream stays PLAYing + gap-watchdog stays armed
  // What the back-end advertised in its "Public:"/"Allow:" header. Both are tracked so the keep-alive
  // can follow GStreamer's preference order (SET_PARAMETER > GET_PARAMETER > OPTIONS) instead of
  // always falling back to OPTIONS -- see RTSPKeepAlive.hh.
  Boolean fServerSupportsGetParameter, fServerSupportsSetParameter, fLastCommandWasPLAY, fDoneDESCRIBE;
  // Which keep-alive we ACTUALLY sent. Recorded rather than recomputed in the continuation: the
  // choice also depends on fNumSetupsSent and the client session, either of which can change while
  // the request is in flight, so a recomputed value could name a method we never sent -- and this
  // value decides which capability to demote when the server rejects one.
  RtspLivenessMethod fLastLivenessMethod;
  Boolean fHavePlayedBefore; // False until the first successful PLAY; lets continueAfterPLAY log a first CONNECT vs a reconnect RESUME (positive ACK that the upstream stream is live again)
  // Has the CURRENT incident already spent its one in-place control-reconnect? A lost control
  // connection with UDP media reconnects in place once (keepAliveLossReconnectsInPlace,
  // RTSPKeepAlive.hh); this is the `try == 0` state that bounds it. Cleared by any successful
  // keep-alive and by reset(), so each incident gets exactly one.
  Boolean fLivenessReconnectAttempted;
  SessionState fLifecycleState; // single source of truth for where this session is; see noteLifecycle()
  // Consecutive failures of the CURRENT request AND when this run of retries started. Bounds the
  // retry loop that replaced always-resetting; see RequestRetryPolicy.hh for the 259s recovery that
  // motivated it.
  //
  // Deliberately ONE object rather than a bare counter. It was a bare `unsigned fRequestAttempts`,
  // and with no clock beside it every call site consulted the count-only decision -- so the policy's
  // 60-second window was unreachable in production and the true worst case was 20 retries x the 20s
  // TCP timeout = 400 seconds, WORSE than the outage being fixed, while the unit tests passed.
  // Bundling the clock with the counter is what makes that combination unwritable.
  RequestRetryState fRequestRetry;
  TaskToken fLivenessCommandTask, fDESCRIBECommandTask, fSubsessionTimerTask, fResetTask, fInterPacketGapsTask;
  TaskToken fSetupRetryTask; // pending delayed SETUP re-send; MUST be unscheduled by reset()
  TaskToken fRequestDeadlineTask; // response deadline for the in-flight SETUP; cancelled on any reply
  // CSeq of the SETUP the deadline guards (0 = none). On expiry that request is DISOWNED
  // (changeResponseHandler(cseq, NULL)) before the retry is sent: a late response to the abandoned
  // request would otherwise cancel the deadline armed for the retry and complete against a queue
  // the retry already consumed.
  unsigned fGuardedSetupCSeq;
  TaskToken fLivenessDeadlineTask; // no-reply deadline for the in-flight keep-alive
  // Consecutive keep-alives that got NO reply. One is answered with another keep-alive (GStreamer's
  // ETIMEOUT rule); two rebuild, with reason=keepalive-unanswered -- named for the measurement
  // (silence), never as a connection loss silence cannot prove. Cleared by ANY response.
  unsigned fConsecutiveLivenessTimeouts;
};


typedef ProxyRTSPClient*
createNewProxyRTSPClientFunc(ProxyServerMediaSession& ourServerMediaSession,
			     char const* rtspURL,
			     char const* username, char const* password,
			     portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
			     int socketNumToServer, unsigned interPacketGapMaxTime, Boolean neverPause);
ProxyRTSPClient*
defaultCreateNewProxyRTSPClientFunc(ProxyServerMediaSession& ourServerMediaSession,
				    char const* rtspURL,
				    char const* username, char const* password,
				    portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
				    int socketNumToServer, unsigned interPacketGapMaxTime, Boolean neverPause);

class ProxyServerMediaSession: public ServerMediaSession {
public:
  static ProxyServerMediaSession* createNew(UsageEnvironment& env,
					    GenericMediaServer* ourMediaServer, // Note: We can be used by just one server
					    char const* inputStreamURL, // the "rtsp://" URL of the stream we'll be proxying
					    char const* streamName = NULL,
					    char const* username = NULL, char const* password = NULL,
					    portNumBits tunnelOverHTTPPortNum = 0,
					        // for streaming the *proxied* (i.e., back-end) stream
					    int verbosityLevel = 0,
					    int socketNumToServer = -1,
					    MediaTranscodingTable* transcodingTable = NULL,
					    unsigned interPacketGapMaxTime = 0, Boolean neverPause = False);
      // Hack: "tunnelOverHTTPPortNum" == 0xFFFF (i.e., all-ones) means: Stream RTP/RTCP-over-TCP, but *not* using HTTP
      // "verbosityLevel" == 1 means display basic proxy setup info; "verbosityLevel" == 2 means display RTSP client protocol also.
      // If "socketNumToServer" is >= 0, then it is the socket number of an already-existing TCP connection to the server.
      //      (In this case, "inputStreamURL" must point to the socket's endpoint, so that it can be accessed via the socket.)

  virtual ~ProxyServerMediaSession();

  char const* url() const;

  char describeCompletedFlag;
    // initialized to 0; set to 1 when the back-end "DESCRIBE" completes.
    // (This can be used as a 'watch variable' in "doEventLoop()".)
  Boolean describeCompletedSuccessfully() const { return fClientMediaSession != NULL; }
    // This can be used - along with "describeCompletedFlag" - to check whether the back-end "DESCRIBE" completed *successfully*.

  // ---- Lightweight per-stream stats snapshot (for periodic operational logging) ----
  // A cheap read of the counters this proxy already maintains, so a caller can log
  // one concise line per proxied stream every N seconds. All counters are cumulative
  // since the back-end stream was (re)established; a caller diffs successive snapshots
  // to get per-interval deltas. No I/O and no allocation on the read path, so it is
  // safe to call from a periodic scheduled task. Safe before the back-end DESCRIBE has
  // completed: reports describeCompleted == False with zeroed counters.
  struct StreamStats {
    Boolean describeCompleted;      // back-end DESCRIBE finished successfully (stream established)
    Boolean streamRTPOverTCP;       // back-end transport: True = RTP-over-TCP, False = UDP/multicast
    unsigned totNumPacketsReceived; // cumulative RTP packets received from the back-end (all subsessions)
    double totNumKBytesReceived;    // cumulative RTP payload KBytes received from the back-end (all subsessions)
  };
  void getStreamStats(StreamStats& stats) const;

protected:
  ProxyServerMediaSession(UsageEnvironment& env, GenericMediaServer* ourMediaServer,
			  char const* inputStreamURL, char const* streamName,
			  char const* username, char const* password,
			  portNumBits tunnelOverHTTPPortNum, int verbosityLevel,
			  int socketNumToServer,
			  MediaTranscodingTable* transcodingTable,
			  unsigned interPacketGapMaxTime = 0, Boolean neverPause = False,
			  createNewProxyRTSPClientFunc* ourCreateNewProxyRTSPClientFunc
			  = defaultCreateNewProxyRTSPClientFunc,
			  portNumBits initialPortNum = 6970,
			  Boolean multiplexRTCPWithRTP = False);

  // If you subclass "ProxyRTSPClient", then you will also need to define your own function
  // - with signature "createNewProxyRTSPClientFunc" (see above) - that creates a new object
  // of this subclass.  You should also subclass "ProxyServerMediaSession" and, in your
  // subclass's constructor, initialize the parent class (i.e., "ProxyServerMediaSession")
  // constructor by passing your new function as the "ourCreateNewProxyRTSPClientFunc"
  // parameter.

  // Subclasses may redefine the following functions, if they want "ProxyServerSubsession"s
  // to create subclassed "Groupsock" and/or "RTCPInstance" objects:
  virtual Groupsock* createGroupsock(struct sockaddr_storage const& addr, Port port);
  virtual RTCPInstance* createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
				   unsigned char const* cname, RTPSink* sink);

  virtual Boolean allowProxyingForSubsession(MediaSubsession const& mss);
  // By default, this function always returns True.  However, a subclass may redefine this
  // if it wishes to restrict which subsessions of a stream get proxied - e.g., if it wishes
  // to proxy only video tracks, but not audio (or other) tracks.

protected:
  GenericMediaServer* fOurMediaServer;
  ProxyRTSPClient* fProxyRTSPClient;
  MediaSession* fClientMediaSession;

private:
  friend class ProxyRTSPClient;
  friend class ProxyServerMediaSubsession;
  void continueAfterDESCRIBE(char const* sdpDescription);
  void resetDESCRIBEState(); // undoes what was done by "contineAfterDESCRIBE()"

private:
  int fVerbosityLevel;
  class PresentationTimeSessionNormalizer* fPresentationTimeSessionNormalizer;
  createNewProxyRTSPClientFunc* fCreateNewProxyRTSPClientFunc;
  MediaTranscodingTable* fTranscodingTable;
  portNumBits fInitialPortNum;
  Boolean fMultiplexRTCPWithRTP;
};


////////// PresentationTimeSessionNormalizer and PresentationTimeSubsessionNormalizer definitions //////////

// The following two classes are used by proxies to convert incoming streams' presentation times into wall-clock-aligned
// presentation times that are suitable for our "RTPSink"s (for the corresponding outgoing streams).
// (For multi-subsession (i.e., audio+video) sessions, the outgoing streams' presentation times retain the same relative
//  separation as those of the incoming streams.)

class PresentationTimeSubsessionNormalizer: public FramedFilter {
public:
  void setRTPSink(RTPSink* rtpSink) { fRTPSink = rtpSink; }

private:
  friend class PresentationTimeSessionNormalizer;
  PresentationTimeSubsessionNormalizer(PresentationTimeSessionNormalizer& parent, FramedSource* inputSource, RTPSource* rtpSource,
				       char const* codecName, PresentationTimeSubsessionNormalizer* next);
      // called only from within "PresentationTimeSessionNormalizer"
  virtual ~PresentationTimeSubsessionNormalizer();

  static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame(unsigned frameSize,
			 unsigned numTruncatedBytes,
			 struct timeval presentationTime,
			 unsigned durationInMicroseconds);

private: // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  PresentationTimeSessionNormalizer& fParent;
  RTPSource* fRTPSource;
  RTPSink* fRTPSink;
  char const* fCodecName;
  PresentationTimeSubsessionNormalizer* fNext;
};

class PresentationTimeSessionNormalizer: public Medium {
public:
  PresentationTimeSessionNormalizer(UsageEnvironment& env);
  virtual ~PresentationTimeSessionNormalizer();

  PresentationTimeSubsessionNormalizer*
  createNewPresentationTimeSubsessionNormalizer(FramedSource* inputSource, RTPSource* rtpSource, char const* codecName);

private: // called only from within "~PresentationTimeSubsessionNormalizer":
  friend class PresentationTimeSubsessionNormalizer;
  void normalizePresentationTime(PresentationTimeSubsessionNormalizer* ssNormalizer,
				 struct timeval& toPT, struct timeval const& fromPT);
  void removePresentationTimeSubsessionNormalizer(PresentationTimeSubsessionNormalizer* ssNormalizer);

private:
  PresentationTimeSubsessionNormalizer* fSubsessionNormalizers;
  PresentationTimeSubsessionNormalizer* fMasterSSNormalizer; // used for subsessions that have been RTCP-synced

  struct timeval fPTAdjustment; // Added to (RTCP-synced) subsession presentation times to 'normalize' them with wall-clock time.
};

#endif

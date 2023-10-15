#pragma once

/**
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
**/

// Copyright (c) 1996-2023, Live Networks, Inc.  All rights reserved
// A demo application, showing how to create and run a RTSP client (that can
// potentially receive multiple streams concurrently).
//

#include "WiFi.h"
#include "Print.h" // Arduino Print
// include live555
#include "BasicUsageEnvironment.hh"
#include "RTSPClient.hh"
#include "SimpleTaskScheduler.hh"

// By default, we request that the server stream its data using RTP/UDP.
// If, instead, you want to request that the server stream via RTP-over-TCP,
// change the following to True:
#define REQUEST_STREAMING_OVER_TCP false

// Even though we're not going to be doing anything with the incoming data, we
// still need to receive it. Define the size of the buffer that we'll use:
#define RTSP_SINK_BUFFER_SIZE 2048


// Counts how many streams (i.e., "RTSPClient"s) are currently in use.
static unsigned rtspClientCount = 0;
static char rtspEventLoopWatchVariable = 0;
static Print* rtspOutput = nullptr;
static uint32_t rtspSinkReceiveBufferSize = 0;
static bool rtspUseTCP = REQUEST_STREAMING_OVER_TCP;

// If you're streaming just a single stream (i.e., just from a single URL,
// once), then you can define and use just a single "StreamClientState"
// structure, as a global variable in your application.  However, because - in
// this demo application - we're showing how to play multiple streams,
// concurrently, we can't do that.  Instead, we have to have a separate
// "StreamClientState" structure for each "RTSPClient".  To do this, we subclass
// "RTSPClient", and add a "StreamClientState" field to the subclass:

class StreamClientState {
 public:
  StreamClientState();
  virtual ~StreamClientState();

 public:
  MediaSubsessionIterator* iter;
  MediaSession* session;
  MediaSubsession* subsession;
  TaskToken streamTimerTask;
  double duration;
};


class OurRTSPClient : public RTSPClient {
 public:
  static OurRTSPClient* createNew(UsageEnvironment& env, char const* progName, char const* rtspURL);


 protected:
  OurRTSPClient(UsageEnvironment& env, char const* rtspURL, int verbosityLevel,
                char const* applicationName, portNumBits tunnelOverHTTPPortNum);
  // called only by createNew();
  virtual ~OurRTSPClient();

  static OurRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
                                  int verbosityLevel = 0,
                                  char const* applicationName = NULL,
                                  portNumBits tunnelOverHTTPPortNum = 0);

 public:
  StreamClientState scs;
};

/**
 * @brief Configuration for RTSPSimpleClient
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
*/
struct RTSPSimpleClientConfig {
    const char* ssid=nullptr;
    const char* password = nullptr;
    bool is_blocking = false;
    bool is_tcp = REQUEST_STREAMING_OVER_TCP;
    uint32_t buffer_size = RTSP_SINK_BUFFER_SIZE;
    const char* url = nullptr;
    const char* application_name = "RTSPSimpleClient";
    Print *output = nullptr;

};

/**
 * @brief A simple RTSPClient using https://github.com/pschatzmann/live555  
 * @author Phil Schatzmann
 * @copyright GPLv3
*/
class RTSPSimpleClient {
  public:
    RTSPSimpleClient() = default;

    RTSPSimpleClientConfig defaultConfig() {
        RTSPSimpleClientConfig default_config;
        return default_config;
    }

    /// Starts the processing
    bool begin(RTSPSimpleClientConfig cfg) {
      this->cfg = cfg;
      rtspOutput = cfg.output;
      rtspEventLoopWatchVariable = 0;
      rtspUseTCP = cfg.is_tcp;
      rtspSinkReceiveBufferSize = cfg.buffer_size;

      if (cfg.url==nullptr) {
        Serial.println("no url");
        return false;
      }

      if (cfg.output==nullptr){
        Serial.println("no output");
        return false;
      }

      if (!login()){
        Serial.println("wifi down");
        return false;
      }

      // Begin by setting up our usage environment:
      scheduler = SimpleTaskScheduler::createNew();
      env = BasicUsageEnvironment::createNew(*scheduler);

      // There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start
      // streaming each one:
      rtsp_client = OurRTSPClient::createNew(*env, cfg.application_name, (char const*) cfg.url);

      scheduler = (SimpleTaskScheduler*) & env->taskScheduler(); 

      // All subsequent activity takes place within the event loop:
      if (cfg.is_blocking) scheduler->doEventLoop(&rtspEventLoopWatchVariable);
      // This function call does not return, unless, at some point in time,
      // "rtspEventLoopWatchVariable" gets set to something non-zero.

      return true;
    }

    /// to be called in Arduino loop when blocking = false
    void singleStep() {
      if (rtspEventLoopWatchVariable==0) scheduler->SingleStep();  
    }

    void end() {
      rtspEventLoopWatchVariable = 1; 
      env->reclaim();
      env = nullptr;
      delete scheduler;
      scheduler = nullptr;
      bool is_blocking = false;
    }

    OurRTSPClient *client() {
      return rtsp_client;
    }

  protected:
    OurRTSPClient* rtsp_client;
    UsageEnvironment* env=nullptr;
    SimpleTaskScheduler* scheduler=nullptr;
    RTSPSimpleClientConfig cfg;

    /// login to wifi: optional convinience method. You can also just start Wifi the normal way
    bool login(){
      if(WiFi.status() != WL_CONNECTED && cfg.ssid!=nullptr && cfg.password!=nullptr){
        WiFi.mode(WIFI_STA); 
        WiFi.begin(cfg.ssid, cfg.password);
        while(WiFi.status() != WL_CONNECTED){
            Serial.print(".");
            delay(100);
        }
        Serial.println();
        Serial.print("Local Address: ");
        Serial.println(WiFi.localIP());
      }
      return WiFi.status() == WL_CONNECTED;
    }
};

// Define a class to hold per-stream state that we maintain throughout each
// stream's lifetime:

// Forward function definitions:

// RTSP 'response handlers':
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode,
                           char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode,
                        char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode,
                       char* resultString);

// Other event handler functions:
void subsessionAfterPlaying(
    void* clientData);  // called when a stream's subsession (e.g., audio or
                        // video substream) ends
void subsessionByeHandler(void* clientData, char const* reason);
// called when a RTCP "BYE" is received for a subsession
void streamTimerHandler(void* clientData);
// called at the end of a stream's expected duration (if the stream has not
// already signaled its end using a RTCP "BYE")

// Used to iterate through each stream's 'subsessions', setting up each one:
void setupNextSubsession(RTSPClient* rtspClient);

// Used to shut down and close a stream (including its "RTSPClient" object):
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

// A function that outputs a string that identifies each stream (for debugging
// output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env,
                             const RTSPClient& rtspClient) {
  return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

// A function that outputs a string that identifies each subsession (for
// debugging output).  Modify this if you wish:
UsageEnvironment& operator<<(UsageEnvironment& env,
                             const MediaSubsession& subsession) {
  return env << subsession.mediumName() << "/" << subsession.codecName();
}


// Define a data sink (a subclass of "MediaSink") to receive the data for each
// subsession (i.e., each audio or video 'substream'). In practice, this might
// be a class (or a chain of classes) that decodes and then renders the incoming
// audio or video. Or it might be a "FileSink", for outputting the received data
// into a file (as is done by the "openRTSP" application). In this example code,
// however, we define a simple 'dummy' sink that receives incoming data, but
// does nothing with it.

class OurSink : public MediaSink {
 public:
  static OurSink* createNew(
      UsageEnvironment& env,
      MediaSubsession&
          subsession,  // identifies the kind of data that's being received
      char const* streamId = NULL);  // identifies the stream itself (optional)

 private:
  OurSink(UsageEnvironment& env, MediaSubsession& subsession,
            char const* streamId);
  // called only by "createNew()"
  virtual ~OurSink();

  static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                         struct timeval presentationTime,
                         unsigned durationInMicroseconds);

 private:
  // redefined virtual functions:
  virtual Boolean continuePlaying();

 private:
  u_int8_t* fReceiveBuffer;
  MediaSubsession& fSubsession;
  char* fStreamId;
};


// Implementation of the RTSP 'response handlers':

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode,
                           char* resultString) {
  do {
    UsageEnvironment& env = rtspClient->envir();                 // alias
    StreamClientState& scs = ((OurRTSPClient*)rtspClient)->scs;  // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to get a SDP description: " << resultString
          << "\n";
      delete[] resultString;
      break;
    }

    char* const sdpDescription = resultString;
    env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

    // Create a media session object from this SDP description:
    scs.session = MediaSession::createNew(env, sdpDescription);
    delete[] sdpDescription;  // because we don't need it anymore
    if (scs.session == NULL) {
      env << *rtspClient
          << "Failed to create a MediaSession object from the SDP description: "
          << env.getResultMsg() << "\n";
      break;
    } else if (!scs.session->hasSubsessions()) {
      env << *rtspClient
          << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
      break;
    }

    // Then, create and set up our data source objects for the session.  We do
    // this by iterating over the session's 'subsessions', calling
    // "MediaSubsession::initiate()", and then sending a RTSP "SETUP" command,
    // on each one. (Each 'subsession' will have its own data source.)
    scs.iter = new MediaSubsessionIterator(*scs.session);
    setupNextSubsession(rtspClient);
    return;
  } while (0);

  // An unrecoverable error occurred with this stream.
  shutdownStream(rtspClient);
}

void setupNextSubsession(RTSPClient* rtspClient) {
  UsageEnvironment& env = rtspClient->envir();                 // alias
  StreamClientState& scs = ((OurRTSPClient*)rtspClient)->scs;  // alias

  scs.subsession = scs.iter->next();
  if (scs.subsession != NULL) {
    if (!scs.subsession->initiate()) {
      env << *rtspClient << "Failed to initiate the \"" << *scs.subsession
          << "\" subsession: " << env.getResultMsg() << "\n";
      setupNextSubsession(
          rtspClient);  // give up on this subsession; go to the next one
    } else {
      env << *rtspClient << "Initiated the \"" << *scs.subsession
          << "\" subsession (";
      if (scs.subsession->rtcpIsMuxed()) {
        env << "client port " << scs.subsession->clientPortNum();
      } else {
        env << "client ports " << scs.subsession->clientPortNum() << "-"
            << scs.subsession->clientPortNum() + 1;
      }
      env << ")\n";

      // Continue setting up this subsession, by sending a RTSP "SETUP" command:
      rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False,
                                   rtspUseTCP);
    }
    return;
  }

  // We've finished setting up all of the subsessions.  Now, send a RTSP "PLAY"
  // command to start the streaming:
  if (scs.session->absStartTime() != NULL) {
    // Special case: The stream is indexed by 'absolute' time, so send an
    // appropriate "PLAY" command:
    rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY,
                                scs.session->absStartTime(),
                                scs.session->absEndTime());
  } else {
    scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
    rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
  }
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode,
                        char* resultString) {
  do {
    UsageEnvironment& env = rtspClient->envir();                 // alias
    StreamClientState& scs = ((OurRTSPClient*)rtspClient)->scs;  // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to set up the \"" << *scs.subsession
          << "\" subsession: " << resultString << "\n";
      break;
    }

    env << *rtspClient << "Set up the \"" << *scs.subsession
        << "\" subsession (";
    if (scs.subsession->rtcpIsMuxed()) {
      env << "client port " << scs.subsession->clientPortNum();
    } else {
      env << "client ports " << scs.subsession->clientPortNum() << "-"
          << scs.subsession->clientPortNum() + 1;
    }
    env << ")\n";

    // Having successfully setup the subsession, create a data sink for it, and
    // call "startPlaying()" on it. (This will prepare the data sink to receive
    // data; the actual flow of data from the client won't start happening until
    // later, after we've sent a RTSP "PLAY" command.)

    scs.subsession->sink =
        OurSink::createNew(env, *scs.subsession, rtspClient->url());
    // perhaps use your own custom "MediaSink" subclass instead
    if (scs.subsession->sink == NULL) {
      env << *rtspClient << "Failed to create a data sink for the \""
          << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
      break;
    }

    env << *rtspClient << "Created a data sink for the \"" << *scs.subsession
        << "\" subsession\n";
    scs.subsession->miscPtr =
        rtspClient;  // a hack to let subsession handler functions get the
                     // "RTSPClient" from the subsession
    scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
                                       subsessionAfterPlaying, scs.subsession);
    // Also set a handler to be called if a RTCP "BYE" arrives for this
    // subsession:
    if (scs.subsession->rtcpInstance() != NULL) {
      scs.subsession->rtcpInstance()->setByeWithReasonHandler(
          subsessionByeHandler, scs.subsession);
    }
  } while (0);
  delete[] resultString;

  // Set up the next subsession, if any:
  setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode,
                       char* resultString) {
  Boolean success = False;

  do {
    UsageEnvironment& env = rtspClient->envir();                 // alias
    StreamClientState& scs = ((OurRTSPClient*)rtspClient)->scs;  // alias

    if (resultCode != 0) {
      env << *rtspClient << "Failed to start playing session: " << resultString
          << "\n";
      break;
    }

    // Set a timer to be handled at the end of the stream's expected duration
    // (if the stream does not already signal its end using a RTCP "BYE").  This
    // is optional.  If, instead, you want to keep the stream active - e.g., so
    // you can later 'seek' back within it and do another RTSP "PLAY" - then you
    // can omit this code. (Alternatively, if you don't want to receive the
    // entire stream, you could set this timer for some shorter value.)
    if (scs.duration > 0) {
      unsigned const delaySlop =
          2;  // number of seconds extra to delay, after the stream's expected
              // duration.  (This is optional.)
      scs.duration += delaySlop;
      unsigned uSecsToDelay = (unsigned)(scs.duration * 1000000);
      scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(
          uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
    }

    env << *rtspClient << "Started playing session";
    if (scs.duration > 0) {
      env << " (for up to " << scs.duration << " seconds)";
    }
    env << "...\n";

    success = True;
  } while (0);
  delete[] resultString;

  if (!success) {
    // An unrecoverable error occurred with this stream.
    shutdownStream(rtspClient);
  }
}

// Implementation of the other event handlers:

void subsessionAfterPlaying(void* clientData) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

  // Begin by closing this subsession's stream:
  Medium::close(subsession->sink);
  subsession->sink = NULL;

  // Next, check whether *all* subsessions' streams have now been closed:
  MediaSession& session = subsession->parentSession();
  MediaSubsessionIterator iter(session);
  while ((subsession = iter.next()) != NULL) {
    if (subsession->sink != NULL) return;  // this subsession is still active
  }

  // All subsessions' streams have now been closed, so shutdown the client:
  shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData, char const* reason) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
  UsageEnvironment& env = rtspClient->envir();  // alias

  env << *rtspClient << "Received RTCP \"BYE\"";
  if (reason != NULL) {
    env << " (reason:\"" << reason << "\")";
    delete[] (char*)reason;
  }
  env << " on \"" << *subsession << "\" subsession\n";

  // Now act as if the subsession had closed:
  subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void* clientData) {
  OurRTSPClient* rtspClient = (OurRTSPClient*)clientData;
  StreamClientState& scs = rtspClient->scs;  // alias

  scs.streamTimerTask = NULL;

  // Shut down the stream:
  shutdownStream(rtspClient);
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {
  UsageEnvironment& env = rtspClient->envir();                 // alias
  StreamClientState& scs = ((OurRTSPClient*)rtspClient)->scs;  // alias

  // First, check whether any subsessions have still to be closed:
  if (scs.session != NULL) {
    Boolean someSubsessionsWereActive = False;
    MediaSubsessionIterator iter(*scs.session);
    MediaSubsession* subsession;

    while ((subsession = iter.next()) != NULL) {
      if (subsession->sink != NULL) {
        Medium::close(subsession->sink);
        subsession->sink = NULL;

        if (subsession->rtcpInstance() != NULL) {
          subsession->rtcpInstance()->setByeHandler(
              NULL, NULL);  // in case the server sends a RTCP "BYE" while
                            // handling "TEARDOWN"
        }

        someSubsessionsWereActive = True;
      }
    }

    if (someSubsessionsWereActive) {
      // Send a RTSP "TEARDOWN" command, to tell the server to shutdown the
      // stream. Don't bother handling the response to the "TEARDOWN".
      rtspClient->sendTeardownCommand(*scs.session, NULL);
    }
  }

  env << *rtspClient << "Closing the stream.\n";
  Medium::close(rtspClient);
  // Note that this will also cause this stream's "StreamClientState" structure
  // to get reclaimed.

  if (--rtspClientCount == 0) {
    // The final stream has ended, so exit the application now.
    // (Of course, if you're embedding this code into your own application, you
    // might want to comment this out, and replace it with
    // "rtspEventLoopWatchVariable = 1;", so that we leave the LIVE555 event loop,
    // and continue running "main()".)
    // exit(exitCode);
    rtspEventLoopWatchVariable = 1;
    return;
  }
}

// Implementation of "OurRTSPClient":
OurRTSPClient* OurRTSPClient::createNew(UsageEnvironment& env, char const* progName, char const* rtspURL) {
  // Begin by creating a "RTSPClient" object.  Note that there is a separate
  // "RTSPClient" object for each stream that we wish to receive (even if more
  // than stream uses the same "rtsp://" URL).
  OurRTSPClient* rtspClient = OurRTSPClient::createNew(
      env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
  if (rtspClient == NULL) {
    env << "Failed to create a RTSP client for URL \"" << rtspURL
        << "\": " << env.getResultMsg() << "\n";
    return nullptr;
  }

  ++rtspClientCount;

  // Next, send a RTSP "DESCRIBE" command, to get a SDP description for the
  // stream. Note that this command - like all RTSP commands - is sent
  // asynchronously; we do not block, waiting for a response. Instead, the
  // following function call returns immediately, and we handle the RTSP
  // response later, from within the event loop:
  rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
  return rtspClient;
}

OurRTSPClient* OurRTSPClient::createNew(UsageEnvironment& env,
                                        char const* rtspURL, int verbosityLevel,
                                        char const* applicationName,
                                        portNumBits tunnelOverHTTPPortNum) {
  return new OurRTSPClient(env, rtspURL, verbosityLevel, applicationName,
                           tunnelOverHTTPPortNum);
}

OurRTSPClient::OurRTSPClient(UsageEnvironment& env, char const* rtspURL,
                             int verbosityLevel, char const* applicationName,
                             portNumBits tunnelOverHTTPPortNum)
    : RTSPClient(env, rtspURL, verbosityLevel, applicationName,
                 tunnelOverHTTPPortNum, -1) {}

OurRTSPClient::~OurRTSPClient() {}

// Implementation of "StreamClientState":

StreamClientState::StreamClientState()
    : iter(NULL),
      session(NULL),
      subsession(NULL),
      streamTimerTask(NULL),
      duration(0.0) {}

StreamClientState::~StreamClientState() {
  delete iter;
  if (session != NULL) {
    // We also need to delete "session", and unschedule "streamTimerTask" (if
    // set)
    UsageEnvironment& env = session->envir();  // alias

    env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
    Medium::close(session);
  }
}

// Implementation of "OurSink":

OurSink* OurSink::createNew(UsageEnvironment& env,
                                MediaSubsession& subsession,
                                char const* streamId) {
  return new OurSink(env, subsession, streamId);
}

OurSink::OurSink(UsageEnvironment& env, MediaSubsession& subsession,
                     char const* streamId)
    : MediaSink(env), fSubsession(subsession) {
  fStreamId = strDup(streamId);
  fReceiveBuffer = new u_int8_t[rtspSinkReceiveBufferSize];
}

OurSink::~OurSink() {
  delete[] fReceiveBuffer;
  delete[] fStreamId;
}

void OurSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds) {
  OurSink* sink = (OurSink*)clientData;
  sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime,
                          durationInMicroseconds);
}

void OurSink::afterGettingFrame(unsigned frameSize,
                                  unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
  // We've just received a frame of data.  (Optionally) print out information
  // about it:
#ifdef DEBUG_PRINT_EACH_RECEIVED_FRAME
  if (fStreamId != NULL) envir() << "Stream \"" << fStreamId << "\"; ";
  envir() << fSubsession.mediumName() << "/" << fSubsession.codecName()
          << ":\tReceived " << frameSize << " bytes";
  if (numTruncatedBytes > 0)
    envir() << " (with " << numTruncatedBytes << " bytes truncated)";
  char uSecsStr[6 + 1];  // used to output the 'microseconds' part of the
                         // presentation time
  sprintf(uSecsStr, "%06u", (unsigned)presentationTime.tv_usec);
  envir() << ".\tPresentation time: " << (int)presentationTime.tv_sec << "."
          << uSecsStr;
  if (fSubsession.rtpSource() != NULL &&
      !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
    envir() << "!";  // mark the debugging output to indicate that this
                     // presentation time is not RTCP-synchronized
  }
#ifdef DEBUG_PRINT_NPT
  envir() << "\tNPT: " << fSubsession.getNormalPlayTime(presentationTime);
#endif
  envir() << "\n";
#endif

  // Decode the data
  if (rtspOutput != nullptr) {
    size_t writtenSize = rtspOutput->write(fReceiveBuffer, frameSize);
    envir() << "Output: written: " <<  writtenSize << " of " << frameSize << "\n";;
  }

  // Then continue, to request the next frame of data:
  continuePlaying();
}

Boolean OurSink::continuePlaying() {
  if (fSource == NULL) return False;  // sanity check (should not happen)

  // Request the next frame of data from our input source. "afterGettingFrame()"
  // will get called later, when it arrives:
  fSource->getNextFrame(fReceiveBuffer, rtspSinkReceiveBufferSize,
                        afterGettingFrame, this, onSourceClosure, this);
  return True;
}


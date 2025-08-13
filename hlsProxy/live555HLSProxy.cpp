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
// Copyright (c) 1996-2025, Live Networks, Inc.  All rights reserved
// A program that acts as a proxy for a RTSP stream, converting it into a sequence of
// HLS (HTTP Live Streaming) segments, plus a ".m3u8" file that can be accessed via a web browser.
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#define RTSP_CLIENT_VERBOSITY_LEVEL 0 // set to 1 for more verbose output from the "RTSPClient"
#define OUR_HLS_SEGMENTATION_DURATION 6 /*seconds*/
#define OUR_HLS_REWIND_DURATION 60 /*seconds: How far back in time a browser can seek*/

UsageEnvironment* env;
char const* programName;
char* username = NULL;
char* password = NULL;
Authenticator* ourAuthenticator = NULL;
Boolean streamUsingTCP = False;
portNumBits tunnelOverHTTPPortNum = 0;
char const* hlsPrefix;
MediaSession* session;
MPEG2TransportStreamFromESSource* transportStream;
MediaSubsessionIterator* iter;
MediaSubsession* subsession;
unsigned numUsableSubsessions = 0;
double duration = 0.0;
Boolean createHandlerServerForREGISTERCommand = False;
portNumBits handlerServerForREGISTERCommandPortNum = 0;
HandlerServerForREGISTERCommand* handlerServerForREGISTERCommand;
char* usernameForREGISTER = NULL;
char* passwordForREGISTER = NULL;
UserAuthenticationDatabase* authDBForREGISTER = NULL;

void usage() {
  *env << "usage:\t" << programName << " [-u <username> <password>] [-t|-T <http-port>] <input-RTSP-url> <HLS-prefix>\n";
  *env << "   or:\t" << programName << " -R [<port-num>] [-U <username-for-REGISTER> <password-for-REGISTER>] <HLS-prefix>\n";
  exit(1);
}

// Forward function definitions:
void continueAfterClientCreation0(RTSPClient* rtspClient, Boolean requestStreamingOverTCP);
void continueAfterClientCreation1(RTSPClient* rtspClient);
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Output information about the program (and LIVE555 version):
  *env << "LIVE555 HLS Proxy, documented at http://www.live555.com/hlsProxy/\n";
  *env << "\t(LIVE555 Streaming Media version " << LIVEMEDIA_LIBRARY_VERSION_STRING << ")\n";

  // Parse the command line:
  programName = argv[0];
  while (argc > 1) {
    char* const opt = argv[1];
    if (opt[0] != '-') {
      if (argc <= 3) break; // only the URL + prefix is left
      usage();
    }

    switch (opt[1]) {
      case 'u': { // specify a username and password
	if (argc < 4) usage(); // there's no argv[3] (for the "password")
	username = argv[2];
	password = argv[3];
	argv+=2; argc-=2;
	ourAuthenticator = new Authenticator(username, password);
	break;
      }

      case 't': { // stream RTP and RTCP over the TCP 'control' connection
	streamUsingTCP = True;
	break;
      }

      case 'T': {
	if (argc > 3 && argv[2][0] != '-') {
	  // The next argument is the HTTP server port number:
	  if (sscanf(argv[2], "%hu", &tunnelOverHTTPPortNum) == 1
	      && tunnelOverHTTPPortNum > 0) {
	    ++argv; --argc;
	    break;
	  }
	}

	// If we get here, the option was specified incorrectly:
	usage();
	break;
      }

      case 'R': {
	// set up a handler server for incoming "REGISTER" commands
	createHandlerServerForREGISTERCommand = True;
	if (argc > 2 && argv[2][0] != '-') {
	  // The next argument is the REGISTER handler server port number:
	  if (sscanf(argv[2], "%hu", &handlerServerForREGISTERCommandPortNum) == 1 && handlerServerForREGISTERCommandPortNum > 0) {
	    ++argv; --argc;
	    break;
	  }
	}
	break;
      }

      case 'U': { // specify a username and password to be used to authentication an incoming "REGISTER" command (for use with -R)
	if (argc < 4) usage(); // there's no argv[3] (for the "password")
	usernameForREGISTER = argv[2];	
	passwordForREGISTER = argv[3];
	argv+=2; argc-=2;

	if (authDBForREGISTER == NULL) authDBForREGISTER = new UserAuthenticationDatabase;
	authDBForREGISTER->addUserRecord(usernameForREGISTER, passwordForREGISTER);
	break;
      }

      default: {
	*env << "Invalid option: " << opt << "\n";
	usage();
	break;
      }
    }

    ++argv; --argc;
  }
	  
  // Create (or arrange to create) our RTSP client object:
  if (createHandlerServerForREGISTERCommand) {
    if (argc != 2) usage();
    hlsPrefix = argv[1];

    handlerServerForREGISTERCommand
      = HandlerServerForREGISTERCommand::createNew(*env, continueAfterClientCreation0,
						   handlerServerForREGISTERCommandPortNum, authDBForREGISTER,
						   RTSP_CLIENT_VERBOSITY_LEVEL, programName);
    if (handlerServerForREGISTERCommand == NULL) {
      *env << "Failed to create a server for handling incoming \"REGISTER\" commands: " << env->getResultMsg() << "\n";
      exit(1);
    } else {
      *env << "Awaiting an incoming \"REGISTER\" command on port " << handlerServerForREGISTERCommand->serverPortNum() << "\n";
    }
  } else { // Normal case
    if (argc != 3) usage();
    if (usernameForREGISTER != NULL) {
      *env << "The '-U <username-for-REGISTER> <password-for-REGISTER>' option can be used only with -R\n";
      usage();
    }
    char const* rtspURL = argv[1];
    hlsPrefix = argv[2];

    RTSPClient* rtspClient
      = RTSPClient::createNew(*env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, programName, tunnelOverHTTPPortNum);
    if (rtspClient == NULL) {
      *env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env->getResultMsg() << "\n";
      exit(1);
    }

    continueAfterClientCreation1(rtspClient);
  }

  // All further processing will be done from within the event loop:
  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void continueAfterClientCreation0(RTSPClient* newRTSPClient, Boolean requestStreamingOverTCP) {
  if (newRTSPClient == NULL) return;

  streamUsingTCP = requestStreamingOverTCP;

  // Having handled one "REGISTER" command (giving us a "rtsp://" URL to stream from), we don't handle any more:
  Medium::close(handlerServerForREGISTERCommand); handlerServerForREGISTERCommand = NULL;

  continueAfterClientCreation1(newRTSPClient);
}

void continueAfterClientCreation1(RTSPClient* rtspClient) {
  // Having created a "RTSPClient" object, send a RTSP "DESCRIBE" command for the URL:
  rtspClient->sendDescribeCommand(continueAfterDESCRIBE, ourAuthenticator);
}

// A function that outputs a string that identifies each stream (for debugging output).
UsageEnvironment& operator<<(UsageEnvironment& env, const RTSPClient& rtspClient) {
  return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

// A function that outputs a string that identifies each subsession (for debugging output).
UsageEnvironment& operator<<(UsageEnvironment& env, const MediaSubsession& subsession) {
  return env << subsession.mediumName() << "/" << subsession.codecName();
}

void setupNextSubsession(RTSPClient* rtspClient); // forward

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    if (resultCode != 0) {
      *env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
      delete[] resultString;
      break;
    }

    // Create a Transport Stream to multiplex the Elementary Stream data from each subsession:
    transportStream = MPEG2TransportStreamFromESSource::createNew(*env);

    // Create a media session object from the SDP description.
    // Then iterate over it, to look for subsession(s) that we can handle:
    session = MediaSession::createNew(*env, resultString);
    delete[] resultString;
    if (session == NULL) {
      *env << *rtspClient << "Failed to create a MediaSession object from the SDP description: " << env->getResultMsg() << "\n";
      break;
    } else if (!session->hasSubsessions()) {
      *env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
      break;
    }
    
    iter = new MediaSubsessionIterator(*session);
    setupNextSubsession(rtspClient);
    return;
  } while (0);

  // An error occurred:
  exit(1);
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString); // forward
void startPlayingSession(RTSPClient* rtspClient); // forward

void setupNextSubsession(RTSPClient* rtspClient) {
  subsession = iter->next();
  if (subsession != NULL) {
    // Check whether this subsession is a codec that we support.
    // We support H.264 or H.265 video, and AAC audio.
    if ((strcmp(subsession->mediumName(), "video") == 0 &&
	 (strcmp(subsession->codecName(), "H264") == 0 ||
	  strcmp(subsession->codecName(), "H265") == 0)) ||
	(strcmp(subsession->mediumName(), "audio") == 0 &&
	 strcmp(subsession->codecName(), "MPEG4-GENERIC"/*aka. AAC*/) == 0)) {
      // Use this subsession.
      ++numUsableSubsessions;
      if (!subsession->initiate()) {
	*env << *rtspClient << "Failed to initiate the \"" << *subsession << "\" subsession: " << env->getResultMsg() << "\n";
      } else {
	*env << *rtspClient << "Initiated the \"" << *subsession << "\" subsession\n";
    
	// Continue setting up this subsession, by sending a RTSP "SETUP" command:
	rtspClient->sendSetupCommand(*subsession, continueAfterSETUP, False, streamUsingTCP,
				     False, ourAuthenticator);
	return;
      }
    }      
    setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
    return;
  }

  // We've gone through all of the subsessions.
  if (numUsableSubsessions == 0) {
    *env << *rtspClient << "This stream has no usable subsessions\n";
    exit(0);
  }

  startPlayingSession(rtspClient);
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    if (resultCode != 0) {
      *env << *rtspClient << "Failed to set up the \"" << *subsession << "\" subsession: " << resultString << "\n";
      break;
    }
    delete[] resultString;

    *env << *rtspClient << "Set up the \"" << *subsession << "\" subsession\n";

    // Feed this subsession's input source into the Transport Stream:
    if (strcmp(subsession->mediumName(), "video") == 0) {
      // Create a 'framer' filter for the input source, to put the stream of NAL units into a
      // form that's usable to output to the Transport Stream.
      // (Note that we use a *DiscreteFramer*, because the input source is a stream of discrete
      //  NAL units - i.e., one at a time.)
      H264or5VideoStreamDiscreteFramer* framer;
      int mpegVersion;

      if (strcmp(subsession->codecName(), "H264") == 0) {
	mpegVersion = 5; // for H.264
	framer = H264VideoStreamDiscreteFramer::createNew(*env, subsession->readSource(),
							  True/*includeStartCodeInOutput*/,
							  True/*insertAccessUnitDelimiters*/);

	// Add any known SPS and PPS NAL units to the framer, so they'll get output ASAP:
	u_int8_t* sps = NULL; unsigned spsSize = 0;
	u_int8_t* pps = NULL; unsigned ppsSize = 0;
	unsigned numSPropRecords;
	SPropRecord* sPropRecords
	  = parseSPropParameterSets(subsession->fmtp_spropparametersets(), numSPropRecords);
	if (numSPropRecords > 0) {
	  sps = sPropRecords[0].sPropBytes;
	  spsSize = sPropRecords[0].sPropLength;
	}
	if (numSPropRecords > 1) {
	  pps = sPropRecords[1].sPropBytes;
	  ppsSize = sPropRecords[1].sPropLength;
	}
	framer->setVPSandSPSandPPS(NULL, 0, sps, spsSize, pps, ppsSize);
	delete[] sPropRecords;
      } else { // H.265
	mpegVersion = 6; // for H.265
	framer = H265VideoStreamDiscreteFramer::createNew(*env, subsession->readSource(),
							  True/*includeStartCodeInOutput*/,
							  True/*insertAccessUnitDelimiters*/);
	
	// Add any known VPS, SPS and PPS NAL units to the framer, so they'll get output ASAP:
	u_int8_t* vps = NULL; unsigned vpsSize = 0;
	u_int8_t* sps = NULL; unsigned spsSize = 0;
	u_int8_t* pps = NULL; unsigned ppsSize = 0;
	unsigned numSPropRecords;
	SPropRecord *sPropRecordsVPS, *sPropRecordsSPS, *sPropRecordsPPS;
	
	sPropRecordsVPS = parseSPropParameterSets(subsession->fmtp_spropvps(), numSPropRecords);
	if (numSPropRecords > 0) {
	  vps = sPropRecordsVPS[0].sPropBytes;
	  vpsSize = sPropRecordsVPS[0].sPropLength;
	}
	
	sPropRecordsSPS = parseSPropParameterSets(subsession->fmtp_spropsps(), numSPropRecords);
	if (numSPropRecords > 0) {
	  sps = sPropRecordsSPS[0].sPropBytes;
	  spsSize = sPropRecordsSPS[0].sPropLength;
	}
	
	sPropRecordsPPS = parseSPropParameterSets(subsession->fmtp_sproppps(), numSPropRecords);
	if (numSPropRecords > 0) {
	  pps = sPropRecordsPPS[0].sPropBytes;
	  ppsSize = sPropRecordsPPS[0].sPropLength;
	}
	
	framer->setVPSandSPSandPPS(vps, vpsSize, sps, spsSize, pps, ppsSize);
	delete[] sPropRecordsVPS; delete[] sPropRecordsSPS; delete[] sPropRecordsPPS;
      }
      
      transportStream->addNewVideoSource(framer, mpegVersion);
    } else { // audio (AAC)
      // Create a 'framer' filter for the input source, to add a ADTS header to each AAC frame,
      // to make the audio playable.
      ADTSAudioStreamDiscreteFramer* framer
	= ADTSAudioStreamDiscreteFramer::createNew(*env, subsession->readSource(),
						   subsession->fmtp_config());
      transportStream->addNewAudioSource(framer, 4/*mpegVersion: AAC*/);
    }

    // Also set up BYE handler//#####@@@@@
  } while (0);

  // Set up the next subsession, if any:
  setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString); // forward
void segmentationCallback(void* clientData, char const* segmentFileName, double segmentDuration); // forward
void afterPlaying(void* clientData); // forward

void startPlayingSession(RTSPClient* rtspClient) {
  // Having successfully setup the session, create a data sink for it, and call "startPlaying()" on it.
  // (This will prepare the data sink to receive data; the actual flow of data from the client won't start happening until later,
  // after we've sent a RTSP "PLAY" command.)

  MediaSink* sink
    = HLSSegmenter::createNew(*env, OUR_HLS_SEGMENTATION_DURATION, hlsPrefix, segmentationCallback);

  // Start playing the sink object:
  *env << "Beginning to read...\n";
  sink->startPlaying(*transportStream, afterPlaying, NULL);

  // Now, send a RTSP "PLAY" command to start the streaming:
  if (session->absStartTime() != NULL) {
    // Special case: The stream is indexed by 'absolute' time, so send an appropriate "PLAY" command:
    rtspClient->sendPlayCommand(*session, continueAfterPLAY, session->absStartTime(), session->absEndTime(), 1.0f, ourAuthenticator);
  } else {
    duration = session->playEndTime() - session->playStartTime();
    rtspClient->sendPlayCommand(*session, continueAfterPLAY, 0.0f, -1.0f, 1.0f, ourAuthenticator);
  }
}

// A record that defines a segment that has been written.  These records are kept in a list:
class SegmentRecord {
public:
  SegmentRecord(char const* segmentFileName, double segmentDuration)
    : fNext(NULL), fSegmentFileName(strDup(segmentFileName)), fSegmentDuration(segmentDuration) {
  }
  virtual ~SegmentRecord() {
    delete[] fSegmentFileName;
    delete fNext;
  }

  SegmentRecord*& next() { return fNext; }
  char const* fileName() const { return fSegmentFileName; }
  double duration() const { return fSegmentDuration; }

private:
  SegmentRecord* fNext;
  char* fSegmentFileName;
  double fSegmentDuration;
};

SegmentRecord* head = NULL;
SegmentRecord* tail = NULL;
double totalDuration = 0.0;
char* ourM3U8FileName = NULL;

void segmentationCallback(void* /*clientData*/,
			  char const* segmentFileName, double segmentDuration) {
  // Begin by updating our list of segments:
  SegmentRecord* newSegment = new SegmentRecord(segmentFileName, segmentDuration);
  if (tail != NULL) {
    tail->next() = newSegment;
  } else {
    head = newSegment;
  }
  tail = newSegment;
  totalDuration += segmentDuration;

  fprintf(stderr, "Wrote segment \"%s\" (duration: %f seconds) -> %f seconds of data stored\n",
	  segmentFileName, segmentDuration, totalDuration);
  
  static unsigned firstSegmentCounter = 1;
  while (totalDuration > OUR_HLS_REWIND_DURATION) {
    // Remove segments from the head of the list:
    SegmentRecord* segmentToRemove = head;
    if (segmentToRemove == NULL) exit(1); // should not happen

    head = segmentToRemove->next();
    if (tail == segmentToRemove) { // should not happen
      tail = NULL;
    }
    segmentToRemove->next() = NULL;

    totalDuration -= segmentToRemove->duration();
    fprintf(stderr, "\tDeleting segment \"%s\" (duration: %f seconds) -> %f seconds of data stored\n",
	    segmentToRemove->fileName(), segmentToRemove->duration(), totalDuration);
    if (unlink(segmentToRemove->fileName()) != 0) {
      *env << "\t\tunlink(\"" << segmentToRemove->fileName() << "\") failed: " << env->getResultMsg() << "\n";
    }
    delete segmentToRemove;
    ++firstSegmentCounter;
  }

  // Then, rewrite our ".m3u8" file with the new list of segments:
  if (ourM3U8FileName == NULL) {
    ourM3U8FileName = new char[strlen(hlsPrefix) + 5/*strlen(".m3u8")*/ + 1];
    if (ourM3U8FileName == NULL) exit(1);
    sprintf(ourM3U8FileName, "%s.m3u8", hlsPrefix);
  }

  // Open our ".m3u8" file for output, and write the prefix:
  FILE* ourM3U8Fid = fopen(ourM3U8FileName, "wb");
  if (ourM3U8Fid == NULL) {
    *env << "Failed to open file \"" << ourM3U8FileName << "\": " << env->getResultMsg();
    exit(1);
  }

  fprintf(ourM3U8Fid,
	  "#EXTM3U\n"
	  "#EXT-X-VERSION:3\n"
	  "#EXT-X-INDEPENDENT-SEGMENTS\n"
	  "#EXT-X-TARGETDURATION:%u\n"
	  "#EXT-X-MEDIA-SEQUENCE:%u\n",
	  OUR_HLS_SEGMENTATION_DURATION,
	  firstSegmentCounter);

  // Write the list of segments:
  for (SegmentRecord* segment = head; segment != NULL; segment = segment->next()) {
    fprintf(ourM3U8Fid,
	    "#EXTINF:%f,\n"
	    "%s\n",
	    segment->duration(),
	    segment->fileName());
  }

  // Close our ".m3u8" file:
  fclose(ourM3U8Fid);

  static Boolean isFirstTime = True;
  if (isFirstTime) {
    fprintf(stderr, "Wrote index file \"%s\"; the stream can now be played from a URL pointing to this file.\007\n", ourM3U8FileName);
    isFirstTime = False;
  }
}

void afterPlaying(void* /*clientData*/) {
  *env << "...Done reading\n";
  exit(0);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    if (resultCode != 0) {
      *env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
      break;
    }
    delete[] resultString;

    // Set timer based on duration #####@@@@@

    *env << *rtspClient << "Started playing session";
    if (duration > 0) {
      *env << " (for up to " << duration << " seconds)";
    }
    *env << "...\n";

    return;
  } while (0);

  // An error occurred:
  exit(1);
}

/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2013, Live Networks, Inc.  All rights reserved
// A test program that reads a MPEG-4 Video Elementary Stream file,
// and streams both using RTP, through a remote Darwin Streaming Server.
// main program

////////// NOTE //////////
// This demo software is provided only as a courtesy to those developers who - for whatever reason - wish
// to send outgoing streams through a separate Darwin Streaming Server.  However, it is not necessary to use
// a Darwin Streaming Server in order to serve streams using RTP/RTSP.  Instead, the "LIVE555 Streaming Media"
// software includes its own RTSP/RTP server implementation, which you should use instead.  For tips on using
// our RTSP/RTP server implementation, see the "testOnDemandRTSPServer" demo application, and/or the
// "live555MediaServer" application (in the "mediaServer") directory.
//////////////////////////

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

UsageEnvironment* env;
char const* inputFileName = "test.m4e";
char const* remoteStreamName = "test.sdp"; // the stream name, as served by the DSS
MPEG4VideoStreamFramer* videoSource;
RTPSink* videoSink;

char const* programName;

void usage() {
  *env << "usage: " << programName
       << " <Darwin Streaming Server name or IP address>\n";
  exit(1);
}

Boolean awaitConfigInfo(RTPSink* sink); // forward
void play(); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Parse command-line arguments:
  programName = argv[0];
  if (argc != 2) usage();
  char const* dssNameOrAddress = argv[1];

  // Create a 'Darwin injector' object:
  DarwinInjector* injector = DarwinInjector::createNew(*env, programName);

  // Create 'groupsocks' for RTP and RTCP.
  // (Note: Because we will actually be streaming through a remote Darwin server,
  // via TCP, we just use dummy destination addresses, port numbers, and TTLs here.)
  struct in_addr dummyDestAddress;
  dummyDestAddress.s_addr = 0;
  Groupsock rtpGroupsockVideo(*env, dummyDestAddress, 0, 0);
  Groupsock rtcpGroupsockVideo(*env, dummyDestAddress, 0, 0);

  // Create a 'MPEG-4 Video RTP' sink from the RTP 'groupsock':
  videoSink = MPEG4ESVideoRTPSink::createNew(*env, &rtpGroupsockVideo, 96);

  // HACK, specifically for MPEG-4 video:
  // Before we can use this RTP sink, we need its MPEG-4 'config' information (for
  // use in the SDP description).  Unfortunately, this config information depends
  // on the the properties of the MPEG-4 input data.  Therefore, we need to start
  // 'playing' this RTP sink from the input source now, and wait until we get
  // the needed config information, before continuing:
  // that we need:
  *env << "Beginning streaming...\n";
  play();

  if (!awaitConfigInfo(videoSink)) {
    *env << "Failed to get MPEG-4 'config' information from input file: "
	 << env->getResultMsg() << "\n";
    exit(1);
  }

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned estimatedSessionBandwidthVideo = 500; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  RTCPInstance* videoRTCP =
    RTCPInstance::createNew(*env, &rtcpGroupsockVideo,
			    estimatedSessionBandwidthVideo, CNAME,
			    videoSink, NULL /* we're a server */);
  // Note: This starts RTCP running automatically

  // Add these to our 'Darwin injector':
  injector->addStream(videoSink, videoRTCP);

  // Next, specify the destination Darwin Streaming Server:
  if (!injector->setDestination(dssNameOrAddress, remoteStreamName,
				programName, "LIVE555 Streaming Media")) {
    *env << "injector->setDestination() failed: "
	 << env->getResultMsg() << "\n";
    exit(1);
  }

  *env << "Play this stream (from the Darwin Streaming Server) using the URL:\n"
       << "\trtsp://" << dssNameOrAddress << "/" << remoteStreamName << "\n";

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* clientData) {
  *env << "...done reading from file\n";

  Medium::close(videoSource);
  // Note: This also closes the input file that this source read from.

  // Start playing once again:
  play();
}

void play() {
  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* fileSource
    = ByteStreamFileSource::createNew(*env, inputFileName);
  if (fileSource == NULL) {
    *env << "Unable to open file \"" << inputFileName
	 << "\" as a byte-stream file source\n";
    exit(1);
  }

  FramedSource* videoES = fileSource;

  // Create a framer for the Video Elementary Stream:
  FramedSource* videoSource = MPEG4VideoStreamFramer::createNew(*env, videoES);

  // Finally, start playing:
  *env << "Beginning to read from file...\n";
  videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
}

static char doneFlag = 0;

static void checkForAuxSDPLine(void* clientData) {
  RTPSink* sink = (RTPSink*)clientData;
  if (sink->auxSDPLine() != NULL) {
    // Signal the event loop that we're done:
    doneFlag = ~0;
  } else {
    // No luck yet.  Try again, after a brief delay:
    int uSecsToDelay = 100000; // 100 ms
    env->taskScheduler().scheduleDelayedTask(uSecsToDelay,
					     (TaskFunc*)checkForAuxSDPLine, sink);
  }
}

Boolean awaitConfigInfo(RTPSink* sink) {
  // Check whether the sink's 'auxSDPLine()' is ready:
  checkForAuxSDPLine(sink);

  env->taskScheduler().doEventLoop(&doneFlag);

  char const* auxSDPLine = sink->auxSDPLine();
  return auxSDPLine != NULL;
}

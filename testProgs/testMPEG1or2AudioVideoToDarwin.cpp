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
// A test program that reads a MPEG-1 or 2 Program Stream file,
// splits it into Audio and Video Elementary Streams,
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
char const* inputFileName = "test.mpg";
char const* remoteStreamName = "test.sdp"; // the stream name, as served by the DSS
MPEG1or2Demux* mpegDemux;
FramedSource* audioSource;
FramedSource* videoSource;
RTPSink* audioSink;
RTPSink* videoSink;

char const* programName;

// To stream *only* MPEG "I" frames (e.g., to reduce network bandwidth),
// change the following "False" to "True":
Boolean iFramesOnly = False;

void usage() {
  *env << "usage: " << programName
       << " <Darwin Streaming Server name or IP address>\n";
  exit(1);
}

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

  ////////// AUDIO //////////
  // Create 'groupsocks' for RTP and RTCP.
  // (Note: Because we will actually be streaming through a remote Darwin server,
  // via TCP, we just use dummy destination addresses, port numbers, and TTLs here.)
  struct in_addr dummyDestAddress;
  dummyDestAddress.s_addr = 0;
  Groupsock rtpGroupsockAudio(*env, dummyDestAddress, 0, 0);
  Groupsock rtcpGroupsockAudio(*env, dummyDestAddress, 0, 0);

  // Create a 'MPEG Audio RTP' sink from the RTP 'groupsock':
  audioSink = MPEG1or2AudioRTPSink::createNew(*env, &rtpGroupsockAudio);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned estimatedSessionBandwidthAudio = 160; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  RTCPInstance* audioRTCP =
    RTCPInstance::createNew(*env, &rtcpGroupsockAudio,
			    estimatedSessionBandwidthAudio, CNAME,
			    audioSink, NULL /* we're a server */);
  // Note: This starts RTCP running automatically

  // Add these to our 'Darwin injector':
  injector->addStream(audioSink, audioRTCP);
  ////////// END AUDIO //////////

  ////////// VIDEO //////////
  // Create 'groupsocks' for RTP and RTCP.
  // (Note: Because we will actually be streaming through a remote Darwin server,
  // via TCP, we just use dummy destination addresses, port numbers, and TTLs here.)
  Groupsock rtpGroupsockVideo(*env, dummyDestAddress, 0, 0);
  Groupsock rtcpGroupsockVideo(*env, dummyDestAddress, 0, 0);

  // Create a 'MPEG Video RTP' sink from the RTP 'groupsock':
  videoSink = MPEG1or2VideoRTPSink::createNew(*env, &rtpGroupsockVideo);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned estimatedSessionBandwidthVideo = 4500; // in kbps; for RTCP b/w share
  RTCPInstance* videoRTCP =
    RTCPInstance::createNew(*env, &rtcpGroupsockVideo,
			      estimatedSessionBandwidthVideo, CNAME,
			      videoSink, NULL /* we're a server */);
  // Note: This starts RTCP running automatically

  // Add these to our 'Darwin injector':
  injector->addStream(videoSink, videoRTCP);
  ////////// END VIDEO //////////

  // Next, specify the destination Darwin Streaming Server:
  if (!injector->setDestination(dssNameOrAddress, remoteStreamName,
				programName, "LIVE555 Streaming Media")) {
    *env << "injector->setDestination() failed: "
	 << env->getResultMsg() << "\n";
    exit(1);
  }

  *env << "Play this stream (from the Darwin Streaming Server) using the URL:\n"
       << "\trtsp://" << dssNameOrAddress << "/" << remoteStreamName << "\n";

  // Finally, start the streaming:
  *env << "Beginning streaming...\n";
  play();

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* clientData) {
  // One of the sinks has ended playing.
  // Check whether any of the sources have a pending read.  If so,
  // wait until its sink ends playing also:
  if (audioSource->isCurrentlyAwaitingData()
      || videoSource->isCurrentlyAwaitingData()) return;

  // Now that both sinks have ended, close both input sources,
  // and start playing again:
  *env << "...done reading from file\n";

  audioSink->stopPlaying();
  videoSink->stopPlaying();
      // ensures that both are shut down
  Medium::close(audioSource);
  Medium::close(videoSource);
  Medium::close(mpegDemux);
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

  // We must demultiplex Audio and Video Elementary Streams
  // from the input source:
  mpegDemux = MPEG1or2Demux::createNew(*env, fileSource);
  FramedSource* audioES = mpegDemux->newAudioStream();
  FramedSource* videoES = mpegDemux->newVideoStream();

  // Create a framer for each Elementary Stream:
  audioSource
    = MPEG1or2AudioStreamFramer::createNew(*env, audioES);
  videoSource
    = MPEG1or2VideoStreamFramer::createNew(*env, videoES, iFramesOnly);

  // Finally, start playing each sink.
  *env << "Beginning to read from file...\n";
  videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
  audioSink->startPlaying(*audioSource, afterPlaying, audioSink);
}

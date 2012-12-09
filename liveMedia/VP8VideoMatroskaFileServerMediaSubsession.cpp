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
// "liveMedia"
// Copyright (c) 1996-2013 Live Networks, Inc.  All rights reserved.
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand, from a VP8 Video track within a Matroska file.
// Implementation

#include "VP8VideoMatroskaFileServerMediaSubsession.hh"
#include "VP8VideoRTPSink.hh"
#include "MatroskaDemuxedTrack.hh"

VP8VideoMatroskaFileServerMediaSubsession* VP8VideoMatroskaFileServerMediaSubsession
::createNew(MatroskaFileServerDemux& demux, unsigned trackNumber) {
  return new VP8VideoMatroskaFileServerMediaSubsession(demux, trackNumber);
}

VP8VideoMatroskaFileServerMediaSubsession
::VP8VideoMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber)
  : FileServerMediaSubsession(demux.envir(), demux.fileName(), False),
    fOurDemux(demux), fTrackNumber(trackNumber) {
}

VP8VideoMatroskaFileServerMediaSubsession
::~VP8VideoMatroskaFileServerMediaSubsession() {
}

float VP8VideoMatroskaFileServerMediaSubsession::duration() const { return fOurDemux.fileDuration(); }

void VP8VideoMatroskaFileServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double /*streamDuration*/, u_int64_t& /*numBytes*/) {
  ((MatroskaDemuxedTrack*)inputSource)->seekToTime(seekNPT);
}

FramedSource* VP8VideoMatroskaFileServerMediaSubsession
::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  estBitrate = 500; // kbps, estimate

  return fOurDemux.newDemuxedTrack(clientSessionId, fTrackNumber);
}

RTPSink* VP8VideoMatroskaFileServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* /*inputSource*/) {
  return VP8VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
}

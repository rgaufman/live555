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
// on demand, from an text (subtitle) track within a Matroska file.
// Implementation

#include "T140TextMatroskaFileServerMediaSubsession.hh"
#include "T140TextRTPSink.hh"
#include "MatroskaDemuxedTrack.hh"

T140TextMatroskaFileServerMediaSubsession* T140TextMatroskaFileServerMediaSubsession
::createNew(MatroskaFileServerDemux& demux, unsigned trackNumber) {
  return new T140TextMatroskaFileServerMediaSubsession(demux, trackNumber);
}

T140TextMatroskaFileServerMediaSubsession
::T140TextMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber)
  : FileServerMediaSubsession(demux.envir(), demux.fileName(), False),
    fOurDemux(demux), fTrackNumber(trackNumber) {
}

T140TextMatroskaFileServerMediaSubsession
::~T140TextMatroskaFileServerMediaSubsession() {
}

float T140TextMatroskaFileServerMediaSubsession::duration() const { return fOurDemux.fileDuration(); }

void T140TextMatroskaFileServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double /*streamDuration*/, u_int64_t& /*numBytes*/) {
  ((MatroskaDemuxedTrack*)inputSource)->seekToTime(seekNPT);
}

FramedSource* T140TextMatroskaFileServerMediaSubsession
::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  estBitrate = 48; // kbps, estimate

  return fOurDemux.newDemuxedTrack(clientSessionId, fTrackNumber);
}

RTPSink* T140TextMatroskaFileServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* /*inputSource*/) {
  return T140TextRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
}

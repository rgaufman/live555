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
// on demand, from an AAC audio track within a Matroska file.
// Implementation

#include "AACAudioMatroskaFileServerMediaSubsession.hh"
#include "MPEG4GenericRTPSink.hh"
#include "MatroskaDemuxedTrack.hh"

AACAudioMatroskaFileServerMediaSubsession* AACAudioMatroskaFileServerMediaSubsession
::createNew(MatroskaFileServerDemux& demux, unsigned trackNumber) {
  return new AACAudioMatroskaFileServerMediaSubsession(demux, trackNumber);
}

AACAudioMatroskaFileServerMediaSubsession
::AACAudioMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber)
  : FileServerMediaSubsession(demux.envir(), demux.fileName(), False),
    fOurDemux(demux), fTrackNumber(trackNumber) {
  // The Matroska file's 'Codec Private' data is assumed to be the AAC configuration information.
  // Use this to generate a 'config string':
  MatroskaTrack* track = fOurDemux.lookup(fTrackNumber);
  fConfigStr = new char[2*track->codecPrivateSize + 1]; // 2 hex digits per byte, plus the trailing '\0'
  for (unsigned i = 0; i < track->codecPrivateSize; ++i) sprintf(&fConfigStr[2*i], "%02X", track->codecPrivate[i]);
}

AACAudioMatroskaFileServerMediaSubsession
::~AACAudioMatroskaFileServerMediaSubsession() {
  delete[] fConfigStr;
}

float AACAudioMatroskaFileServerMediaSubsession::duration() const { return fOurDemux.fileDuration(); }

void AACAudioMatroskaFileServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double /*streamDuration*/, u_int64_t& /*numBytes*/) {
  ((MatroskaDemuxedTrack*)inputSource)->seekToTime(seekNPT);
}

FramedSource* AACAudioMatroskaFileServerMediaSubsession
::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  estBitrate = 96; // kbps, estimate

  return fOurDemux.newDemuxedTrack(clientSessionId, fTrackNumber);
}

RTPSink* AACAudioMatroskaFileServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* /*inputSource*/) {
  MatroskaTrack* track = fOurDemux.lookup(fTrackNumber);
  return MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
                                        rtpPayloadTypeIfDynamic,
                                        track->samplingFrequency,
                                        "audio", "AAC-hbr", fConfigStr,
                                        track->numChannels);
}

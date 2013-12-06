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
// on demand, from an H264 video track within a Matroska file.
// Implementation

#include "H264VideoMatroskaFileServerMediaSubsession.hh"
#include "H264VideoStreamDiscreteFramer.hh"
#include "MatroskaDemuxedTrack.hh"

H264VideoMatroskaFileServerMediaSubsession* H264VideoMatroskaFileServerMediaSubsession
::createNew(MatroskaFileServerDemux& demux, unsigned trackNumber) {
  return new H264VideoMatroskaFileServerMediaSubsession(demux, trackNumber);
}

#define checkPtr if (ptr >= limit) return;
#define numBytesRemaining (unsigned)(limit - ptr)

H264VideoMatroskaFileServerMediaSubsession
::H264VideoMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber)
  : H264VideoFileServerMediaSubsession(demux.envir(), demux.fileName(), False),
    fOurDemux(demux), fTrackNumber(trackNumber),
    fSPSSize(0), fSPS(NULL), fPPSSize(0), fPPS(NULL) {
  // Use our track's 'Codec Private' data: Bytes 5 and beyond contain SPS and PPSs:
  unsigned numSPSandPPSBytes;
  u_int8_t* SPSandPPSBytes;
  MatroskaTrack* track = fOurDemux.lookup(fTrackNumber);

  if (track->codecPrivateSize >= 6) {
    numSPSandPPSBytes = track->codecPrivateSize - 5;
    track->codecPrivate[5] &=~ 0xE0; // mask out the top 3 (reserved) bits from the 'numSPSs' byte
    SPSandPPSBytes = &track->codecPrivate[5];
  } else {
    numSPSandPPSBytes = 0;
    SPSandPPSBytes = NULL;
  }

  // Extract, from "SPSandPPSBytes", one SPS NAL unit, and one PPS NAL unit.  (I hope one is all we need of each.)
  do {
    if (numSPSandPPSBytes == 0 || SPSandPPSBytes == NULL) break; // sanity check
    u_int8_t* ptr = SPSandPPSBytes;
    u_int8_t* limit = &SPSandPPSBytes[numSPSandPPSBytes];

    unsigned numSPSs = *ptr++; checkPtr;
    unsigned i;
    for (i = 0; i < numSPSs; ++i) {
      unsigned spsSize = (*ptr++)<<8; checkPtr;
      spsSize |= *ptr++; checkPtr;

      if (spsSize > numBytesRemaining) return;
      if (i == 0) { // save the first one
	fSPSSize = spsSize;
	fSPS = new u_int8_t[spsSize];
	memmove(fSPS, ptr, spsSize);
      }
      ptr += spsSize;
    }

    unsigned numPPSs = *ptr++; checkPtr;
    for (i = 0; i < numPPSs; ++i) {
      unsigned ppsSize = (*ptr++)<<8; checkPtr;
      ppsSize |= *ptr++; checkPtr;

      if (ppsSize > numBytesRemaining) return;
      if (i == 0) { // save the first one
	fPPSSize = ppsSize;
	fPPS = new u_int8_t[ppsSize];
	memmove(fPPS, ptr, ppsSize);
      }
      ptr += ppsSize;
    }
  } while (0);
}

H264VideoMatroskaFileServerMediaSubsession
::~H264VideoMatroskaFileServerMediaSubsession() {
  delete[] fSPS;
  delete[] fPPS;
}

float H264VideoMatroskaFileServerMediaSubsession::duration() const { return fOurDemux.fileDuration(); }

void H264VideoMatroskaFileServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double /*streamDuration*/, u_int64_t& /*numBytes*/) {
  // "inputSource" is a framer. *Its* source is the demuxed track that we seek on:
  H264VideoStreamFramer* framer = (H264VideoStreamFramer*)inputSource;

  MatroskaDemuxedTrack* demuxedTrack = (MatroskaDemuxedTrack*)(framer->inputSource());
  demuxedTrack->seekToTime(seekNPT);
}

FramedSource* H264VideoMatroskaFileServerMediaSubsession
::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  // Allow for the possibility of very large NAL units being fed to our "RTPSink" objects:
  OutPacketBuffer::maxSize = 300000; // bytes
  estBitrate = 500; // kbps, estimate

  // Create the video source:
  FramedSource* baseH264VideoSource = fOurDemux.newDemuxedTrack(clientSessionId, fTrackNumber);
  if (baseH264VideoSource == NULL) return NULL;
  
  // Create a framer for the Video stream:
  H264VideoStreamFramer* framer = H264VideoStreamDiscreteFramer::createNew(envir(), baseH264VideoSource);
  framer->setSPSandPPS(fSPS, fSPSSize, fPPS, fPPSSize);

  return framer;
}

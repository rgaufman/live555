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
// on demand, from a Vorbis audio track within a Matroska file.
// Implementation

#include "VorbisAudioMatroskaFileServerMediaSubsession.hh"
#include "VorbisAudioRTPSink.hh"
#include "MatroskaDemuxedTrack.hh"

VorbisAudioMatroskaFileServerMediaSubsession* VorbisAudioMatroskaFileServerMediaSubsession
::createNew(MatroskaFileServerDemux& demux, unsigned trackNumber) {
  return new VorbisAudioMatroskaFileServerMediaSubsession(demux, trackNumber);
}

#define getPrivByte(b) if (n == 0) break; else do {b = *p++; --n;} while (0)

VorbisAudioMatroskaFileServerMediaSubsession
::VorbisAudioMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber)
  : FileServerMediaSubsession(demux.envir(), demux.fileName(), False),
    fOurDemux(demux), fTrackNumber(trackNumber),
    fIdentificationHeader(NULL), fIdentificationHeaderSize(0),
    fCommentHeader(NULL), fCommentHeaderSize(0),
    fSetupHeader(NULL), fSetupHeaderSize(0),
    fEstBitrate(96/* kbps, default guess */) {
  MatroskaTrack* track = fOurDemux.lookup(fTrackNumber);

  // The Matroska file's 'Codec Private' data is assumed to be the Vorbis configuration information,
  // containing the "Identification", "Comment", and "Setup" headers.  Extract these headers now:
  do {
    u_int8_t* p = track->codecPrivate;
    unsigned n = track->codecPrivateSize;
    if (n == 0 || p == NULL) break; // we have no 'Codec Private' data

    u_int8_t numHeaders;
    getPrivByte(numHeaders);
    unsigned headerSize[3]; // we don't handle any more than 2+1 headers

    // Extract the sizes of each of these headers:
    unsigned sizesSum = 0;
    Boolean success = True;
    unsigned i;
    for (i = 0; i < numHeaders && i < 3; ++i) {
      unsigned len = 0;
      u_int8_t c;

      do {
	success = False;
	getPrivByte(c);
	success = True;

	len += c;
      } while (c == 255);
      if (!success || len == 0) break;

      headerSize[i] = len;
      sizesSum += len;
    }
    if (!success) break;

    // Compute the implicit size of the final header:
    if (numHeaders < 3) {
      int finalHeaderSize = n - sizesSum;
      if (finalHeaderSize <= 0) break; // error in data; give up

      headerSize[numHeaders] = (unsigned)finalHeaderSize;
      ++numHeaders; // include the final header now
    } else {
      numHeaders = 3; // The maximum number of headers that we handle
    }

    // Then, extract and classify each header:
    for (i = 0; i < numHeaders; ++i) {
      success = False;
      unsigned newHeaderSize = headerSize[i];
      u_int8_t* newHeader = new u_int8_t[newHeaderSize];
      if (newHeader == NULL) break;
      
      u_int8_t* hdr = newHeader;
      while (newHeaderSize-- > 0) {
	success = False;
	getPrivByte(*hdr++);
	success = True;
      }
      if (!success) {
	delete[] newHeader;
	break;
      }

      u_int8_t headerType = newHeader[0];
      if (headerType == 1) {
	delete[] fIdentificationHeader; fIdentificationHeader = newHeader;
	fIdentificationHeaderSize = headerSize[i];

	if (fIdentificationHeaderSize >= 28) {
	  // Get the 'bitrate' values from this header, and use them to set "fEstBitrate":
	  u_int32_t val;
	  u_int8_t* p;

	  p = &fIdentificationHeader[16];
	  val = ((p[3]*256 + p[2])*256 + p[1])*256 + p[0]; // i.e., little-endian
	  int bitrate_maximum = (int)val;
	  if (bitrate_maximum < 0) bitrate_maximum = 0;

	  p = &fIdentificationHeader[20];
	  val = ((p[3]*256 + p[2])*256 + p[1])*256 + p[0]; // i.e., little-endian
	  int bitrate_nominal = (int)val;
	  if (bitrate_nominal < 0) bitrate_nominal = 0;

	  p = &fIdentificationHeader[24];
	  val = ((p[3]*256 + p[2])*256 + p[1])*256 + p[0]; // i.e., little-endian
	  int bitrate_minimum = (int)val;
	  if (bitrate_minimum < 0) bitrate_minimum = 0;

	  int bitrate
	    = bitrate_nominal>0 ? bitrate_nominal : bitrate_maximum>0 ? bitrate_maximum : bitrate_minimum>0 ? bitrate_minimum : 0;
	  if (bitrate > 0) fEstBitrate = ((unsigned)bitrate)/1000;
	}
      } else if (headerType == 3) {
	delete[] fCommentHeader; fCommentHeader = newHeader;
	fCommentHeaderSize = headerSize[i];
      } else if (headerType == 5) {
	delete[] fSetupHeader; fSetupHeader = newHeader;
	fSetupHeaderSize = headerSize[i];
      } else {
	delete[] newHeader; // because it was a header type that we don't understand
      }
    }
    if (!success) break;
  } while (0);
}

VorbisAudioMatroskaFileServerMediaSubsession
::~VorbisAudioMatroskaFileServerMediaSubsession() {
  delete[] fIdentificationHeader;
  delete[] fCommentHeader;
  delete[] fSetupHeader;
}

float VorbisAudioMatroskaFileServerMediaSubsession::duration() const { return fOurDemux.fileDuration(); }

void VorbisAudioMatroskaFileServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double /*streamDuration*/, u_int64_t& /*numBytes*/) {
  ((MatroskaDemuxedTrack*)inputSource)->seekToTime(seekNPT);
}

FramedSource* VorbisAudioMatroskaFileServerMediaSubsession
::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
  estBitrate = fEstBitrate; // kbps, estimate

  return fOurDemux.newDemuxedTrack(clientSessionId, fTrackNumber);
}

RTPSink* VorbisAudioMatroskaFileServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* /*inputSource*/) {
  MatroskaTrack* track = fOurDemux.lookup(fTrackNumber);
  return VorbisAudioRTPSink::createNew(envir(), rtpGroupsock,
				       rtpPayloadTypeIfDynamic, track->samplingFrequency, track->numChannels,
				       fIdentificationHeader, fIdentificationHeaderSize,
				       fCommentHeader, fCommentHeaderSize,
				       fSetupHeader, fSetupHeaderSize);
}

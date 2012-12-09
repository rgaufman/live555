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
// Vorbis Audio RTP Sources
// Implementation

#include "VorbisAudioRTPSource.hh"

////////// VorbisBufferedPacket and VorbisBufferedPacketFactory //////////

class VorbisBufferedPacket: public BufferedPacket {
public:
  VorbisBufferedPacket();
  virtual ~VorbisBufferedPacket();

private: // redefined virtual functions
  virtual unsigned nextEnclosedFrameSize(unsigned char*& framePtr,
					 unsigned dataSize);
};

class VorbisBufferedPacketFactory: public BufferedPacketFactory {
private: // redefined virtual functions
  virtual BufferedPacket* createNewPacket(MultiFramedRTPSource* ourSource);
};


///////// MPEG4VorbisAudioRTPSource implementation ////////

VorbisAudioRTPSource*
VorbisAudioRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
				unsigned char rtpPayloadFormat,
				unsigned rtpTimestampFrequency) {
  return new VorbisAudioRTPSource(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency);
}

VorbisAudioRTPSource
::VorbisAudioRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		     unsigned char rtpPayloadFormat,
		     unsigned rtpTimestampFrequency)
  : MultiFramedRTPSource(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency,
			 new VorbisBufferedPacketFactory),
    fCurPacketIdent(0) {
}

VorbisAudioRTPSource::~VorbisAudioRTPSource() {
}

Boolean VorbisAudioRTPSource
::processSpecialHeader(BufferedPacket* packet,
                       unsigned& resultSpecialHeaderSize) {
  unsigned char* headerStart = packet->data();
  unsigned packetSize = packet->dataSize();

  resultSpecialHeaderSize = 4;
  if (packetSize < resultSpecialHeaderSize) return False; // packet was too small

  // The first 3 bytes of the header are the "Ident" field:
  fCurPacketIdent = (headerStart[0]<<16) | (headerStart[1]<<8) | headerStart[2];

  // The 4th byte is F|VDT|numPkts.
  // Reject any packet with VDT == 3:
  if ((headerStart[3]&0x30) == 0x30) return False;

  u_int8_t F = headerStart[3]>>6;
  fCurrentPacketBeginsFrame = F <= 1; // "Not Fragmented" or "Start Fragment"
  fCurrentPacketCompletesFrame = F == 0 || F == 3; // "Not Fragmented" or "End Fragment"

  return True;
}

char const* VorbisAudioRTPSource::MIMEtype() const {
  return "audio/VORBIS";
}


////////// VorbisBufferedPacket and VorbisBufferedPacketFactory implementation //////////

VorbisBufferedPacket::VorbisBufferedPacket() {
}

VorbisBufferedPacket::~VorbisBufferedPacket() {
}

unsigned VorbisBufferedPacket
::nextEnclosedFrameSize(unsigned char*& framePtr, unsigned dataSize) {
  if (dataSize < 2) {
    // There's not enough space for a 2-byte header.  TARFU!  Just return the data that's left:
    return dataSize;
  }

  unsigned frameSize = (framePtr[0]<<8) | framePtr[1];
  framePtr += 2;
  if (frameSize > dataSize - 2) return dataSize - 2; // inconsistent frame size => just return all the data that's left

  return frameSize;
}

BufferedPacket* VorbisBufferedPacketFactory
::createNewPacket(MultiFramedRTPSource* /*ourSource*/) {
  return new VorbisBufferedPacket();
}

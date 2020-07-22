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
// "liveMedia"
// Copyright (c) 1996-2020 Live Networks, Inc.  All rights reserved.
// A filter that reads (discrete) AAC audio frames, and outputs each frame with
// a preceding ADTS header.
// Implementation

#include "ADTSAudioStreamDiscreteFramer.hh"

static u_int8_t hexToBinary(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');

  return 0; // default if 'c' is not hex
}

ADTSAudioStreamDiscreteFramer* ADTSAudioStreamDiscreteFramer
::createNew(UsageEnvironment& env, FramedSource* inputSource, char const* configStr) {
  u_int16_t configValue = 0;

  if (configStr != NULL && strlen(configStr) >= 4) {
    configValue =
      (hexToBinary(configStr[0])<<12)|
      (hexToBinary(configStr[1])<<8)|
      (hexToBinary(configStr[2])<<4)|
      hexToBinary(configStr[3]);
  }

  // Unpack the 2-byte 'config' value to get "profile", "samplingFrequencyIndex",
  // and "channelConfiguration":
  u_int8_t audioObjectType = configValue>>11;
  u_int8_t profile = audioObjectType == 0 ? 0 : audioObjectType-1;
  u_int8_t samplingFrequencyIndex = (configValue&0x0780)>>7;
  u_int8_t channelConfiguration = (configValue&0x0078)>>3;

  return new ADTSAudioStreamDiscreteFramer(env, inputSource,
					   profile, samplingFrequencyIndex, channelConfiguration);
}

ADTSAudioStreamDiscreteFramer
::ADTSAudioStreamDiscreteFramer(UsageEnvironment& env, FramedSource* inputSource,
				u_int8_t profile, u_int8_t samplingFrequencyIndex, u_int8_t channelConfiguration)
  : FramedFilter(env, inputSource) {
  // Set up the ADTS header that we'll be prepending to each audio frame.
  // This will be fixed, except for the frame size.
  profile &= 0x03; // use 2 bits only
  samplingFrequencyIndex &= 0x0F; // use 4 bits only
  channelConfiguration &= 0x07; // use 3 bits only

  fADTSHeader[0] = 0xFF; // first 8 bits of syncword
  fADTSHeader[1] = 0xF1; // last 4 bits of syncword; version 0; layer 0; protection absent
  fADTSHeader[2] = (profile<<6)|(samplingFrequencyIndex<<2)|(channelConfiguration>>2);
  fADTSHeader[3] = channelConfiguration<<6;
  fADTSHeader[4] = 0;
  fADTSHeader[5] = 0x1F; // set 'buffer fullness' to all-1s
  fADTSHeader[6] = 0xFC; // set 'buffer fullness' to all-1s
}

ADTSAudioStreamDiscreteFramer::~ADTSAudioStreamDiscreteFramer() {
}

void ADTSAudioStreamDiscreteFramer::doGetNextFrame() {
  // Arrange to read data (an AAC audio frame) from our data source, directly into the
  // downstream object's buffer, allowing for the ATDS header in front.
  // Make sure there's enoughn space:
  if (fMaxSize <= ADTS_HEADER_SIZE) {
    fNumTruncatedBytes = ADTS_HEADER_SIZE - fMaxSize;
    handleClosure();
    return;
  }

  fInputSource->getNextFrame(fTo + ADTS_HEADER_SIZE, fMaxSize - ADTS_HEADER_SIZE,
			     afterGettingFrame, this,
			     FramedSource::handleClosure, this);
}

void ADTSAudioStreamDiscreteFramer
::afterGettingFrame(void* clientData, unsigned frameSize,
		    unsigned numTruncatedBytes,
		    struct timeval presentationTime,
		    unsigned durationInMicroseconds) {
  ADTSAudioStreamDiscreteFramer* source = (ADTSAudioStreamDiscreteFramer*)clientData;
  source->afterGettingFrame1(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void ADTSAudioStreamDiscreteFramer
::afterGettingFrame1(unsigned frameSize, unsigned numTruncatedBytes,
		     struct timeval presentationTime,
		     unsigned durationInMicroseconds) {
  frameSize += ADTS_HEADER_SIZE;

  // Fill in the ADTS header with the (updated) frame size:
  fFrameSize = frameSize;
  frameSize &= 0x1FFF; // use only 13 bits in the ADTS header
  fADTSHeader[3] = (fADTSHeader[3]&0xFC)|(frameSize>>11);
  fADTSHeader[4] = frameSize>>3;
  fADTSHeader[5] = (fADTSHeader[5]&0x1F)|(frameSize<<5);
  
  // And copy the ADTS header to the destination:
  fTo[0] = fADTSHeader[0];
  fTo[1] = fADTSHeader[1];
  fTo[2] = fADTSHeader[2];
  fTo[3] = fADTSHeader[3];
  fTo[4] = fADTSHeader[4];
  fTo[5] = fADTSHeader[5];
  fTo[6] = fADTSHeader[6];

  // Complete delivery to the downstream object:
  fNumTruncatedBytes = numTruncatedBytes;
  fPresentationTime = presentationTime;
  fDurationInMicroseconds = durationInMicroseconds;
  afterGetting(this);
}

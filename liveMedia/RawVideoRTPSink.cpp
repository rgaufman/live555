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
// Copyright (c) 1996-2023 Live Networks, Inc.  All rights reserved.
// RTP sink for Raw video
// Implementation

#include "RawVideoRTPSink.hh"

RawVideoRTPSink* RawVideoRTPSink
::createNew(UsageEnvironment& env, Groupsock* RTPgs, u_int8_t rtpPayloadFormat,
	    unsigned width, unsigned height, unsigned depth,
	    char const* sampling, char const* colorimetry) {
  if (sampling == NULL || colorimetry == NULL) return NULL;
  
  return new RawVideoRTPSink(env, RTPgs,
                             rtpPayloadFormat,
                             width, height, depth,
                             sampling, colorimetry);
}

RawVideoRTPSink
::RawVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs, u_int8_t rtpPayloadFormat,
                  unsigned width, unsigned height, unsigned depth,
                  char const* sampling, char const* colorimetry)
  : VideoRTPSink(env, RTPgs, rtpPayloadFormat, 90000, "RAW"),
    fLineIndex(0), fP(width, height, depth, sampling)  {
    
  // Construct our "a=fmtp:" SDP line:
  // ASSERT: sampling != NULL && colorimetry != NULL
  unsigned const fmtpSDPLineMaxSize
    = 200 + strlen(sampling) + strlen(colorimetry); // more than enough space
  fFmtpSDPLine = new char[fmtpSDPLineMaxSize];
  sprintf(fFmtpSDPLine, "a=fmtp:%d sampling=%s;width=%u;height=%u;depth=%u;colorimetry=%s\r\n",
	  rtpPayloadType(), sampling, width, height, depth, colorimetry);
}

RawVideoRTPSink::~RawVideoRTPSink() {
  delete[] fFmtpSDPLine;
}

char const* RawVideoRTPSink::auxSDPLine() {
  return fFmtpSDPLine;
}

void RawVideoRTPSink
::doSpecialFrameHandling(unsigned fragmentationOffset,
			 unsigned char* frameStart,
			 unsigned numBytesInFrame,
			 struct timeval framePresentationTime,
			 unsigned numRemainingBytes) {
  u_int16_t* lengths;
  u_int16_t* offsets;
  unsigned numLines = getNumLinesInPacket(fragmentationOffset, lengths, offsets);
  unsigned specialHeaderSize = 2 + (6 * numLines);
  u_int8_t* specialHeader = new u_int8_t[specialHeaderSize];

  // Extended Sequence Number (not used)
  specialHeader[0] = specialHeader[1] = 0;

  unsigned index = 2;
  for (unsigned i = 0; i < numLines; ++i) {
    // Increment line number if necessary:
    if ((offsets[i] == 0) && fragmentationOffset != 0) {
      fLineIndex += fP.scanLineIterationStep;
    }

    // Set length:
    specialHeader[index++] = lengths[i]>>8;
    specialHeader[index++] = (u_int8_t)lengths[i];

    // Set field+line index:
    u_int8_t const fieldIdent = 0; // we assume non-interlaced video
    specialHeader[index++] = ((fLineIndex>>8) & 0x7F) | (fieldIdent<<7);
    specialHeader[index++] = (u_int8_t)fLineIndex;

    // Set continuation+offset:
    u_int8_t const continuationBit = i < numLines - 1;
    specialHeader[index++] = ((offsets[i]>>8) & 0x7F) | (continuationBit<<7);
    specialHeader[index++] = (u_int8_t)offsets[i];
  }

  setSpecialHeaderBytes(specialHeader, specialHeaderSize);
  
  if (numRemainingBytes == 0) {
    // This packet contains the last (or only) fragment of the frame.
    // Set the RTP 'M' ('marker') bit:
    setMarkerBit();
    // Reset line index
    fLineIndex = 0;
  }

  // Also set the RTP timestamp:
  setTimestamp(framePresentationTime);

  delete[] specialHeader;
  delete[] lengths;
  delete[] offsets;
}

Boolean RawVideoRTPSink::frameCanAppearAfterPacketStart(unsigned char const* /*frameStart*/,
                               unsigned /*numBytesInFrame*/) const {
  // Only one frame per packet:
  return False;
}

unsigned RawVideoRTPSink::specialHeaderSize() const {
  u_int16_t* lengths;
  u_int16_t* offsets;
  unsigned numLines = getNumLinesInPacket(curFragmentationOffset(), lengths, offsets);
  delete[] lengths;
  delete[] offsets;
  return 2 + (6 * numLines);
}

unsigned RawVideoRTPSink
::getNumLinesInPacket(unsigned fragOffset, u_int16_t*& lengths, u_int16_t*& offsets) const {
  lengths = offsets = NULL; // initially
  
  unsigned const rtpHeaderSize = 12;
  unsigned specialHeaderSize = 2; // Extended Sequence Number
  unsigned const packetMaxSize = ourMaxPacketSize();
  unsigned numLines = 0;
  unsigned remainingSizeInPacket;

  if (fragOffset >= fP.frameSize) {
    envir() << "RawVideoRTPSink::getNumLinesInPacket(): bad fragOffset " << fragOffset << "\n";
    return 0;
  }

  #define MAX_LINES_IN_PACKET 100
  u_int16_t lengthArray[MAX_LINES_IN_PACKET] = {0};
  u_int16_t offsetArray[MAX_LINES_IN_PACKET] = {0};
  unsigned curDataTotalLength = 0;
  unsigned offsetWithinLine = fragOffset % fP.scanLineSize;
  unsigned remainingLineSize = fP.scanLineSize - offsetWithinLine;

  while (1) {
    if (packetMaxSize - specialHeaderSize - rtpHeaderSize - 6 <= curDataTotalLength) {
      break; // packet sanity check
    }

    // add one line
    if (++numLines > MAX_LINES_IN_PACKET) return 0;
    specialHeaderSize += 6;

    remainingSizeInPacket = packetMaxSize - specialHeaderSize - rtpHeaderSize - curDataTotalLength;
    remainingSizeInPacket -= remainingSizeInPacket % fP.pgroupSize; // use only multiple of pgroup
    lengthArray[numLines-1] = remainingLineSize < remainingSizeInPacket ? remainingLineSize : remainingSizeInPacket;
    offsetArray[numLines-1] = (offsetWithinLine * fP.numPixelsInPgroup) / fP.pgroupSize;
        // Note that the offsets are in specified to be in pixels (not octets, nor pgroups)
    if (remainingLineSize >= remainingSizeInPacket) {
      break; // packet is full
    }

    // All subsequent lines in the packet will have offset 0
    curDataTotalLength += lengthArray[numLines-1];
    offsetWithinLine = 0;
    remainingLineSize = fP.scanLineSize;

    if (fragOffset + curDataTotalLength >= fP.frameSize) {
      break; // end of the frame.
    }
  }

  lengths = new u_int16_t[numLines];
  offsets = new u_int16_t[numLines];
  for (unsigned i = 0; i < numLines; i++) {
    lengths[i] = lengthArray[i];
    offsets[i] = offsetArray[i];
  }

  return numLines;
}

unsigned RawVideoRTPSink::computeOverflowForNewFrame(unsigned newFrameSize) const {
  unsigned initialOverflow = MultiFramedRTPSink::computeOverflowForNewFrame(newFrameSize);

  // Adjust (increase) this overflow to be a multiple of the pgroup value:
  unsigned numFrameBytesUsed = newFrameSize - initialOverflow;
  initialOverflow += numFrameBytesUsed % fP.pgroupSize;

  return initialOverflow;
}


///////// RawVideoFrameParameters implementation /////////

RawVideoFrameParameters
::RawVideoFrameParameters(unsigned width, unsigned height, unsigned depth, char const* sampling) {
  scanLineIterationStep = 1; // by default; different for YCbCr-4:2:0
  numPixelsInPgroup = 1; // by default
  pgroupSize = 2; // use this for unknown (sampling, depth)s 

  if (strcmp(sampling, "RGB") == 0 || strcmp(sampling, "BGR") == 0) {
    switch (depth) {
      case 8:
        pgroupSize = 3;
        break;
      case 10:
        pgroupSize = 15;
        numPixelsInPgroup = 4;
        break;
      case 12:
        pgroupSize = 9;
        numPixelsInPgroup = 2;
        break;
      case 16:
        pgroupSize = 6;
        break;
    }
  } else if (strcmp(sampling, "RGBA") == 0 || strcmp(sampling, "BGRA") == 0) {
    switch (depth) {
      case 8:
        pgroupSize = 4;
        break;
      case 10:
        pgroupSize = 5;
        break;
      case 12:
        pgroupSize = 6;
        break;
      case 16:
        pgroupSize = 8;
        break;
    }
  } else if (strcmp(sampling, "YCbCr-4:4:4") == 0) {
    switch (depth) {
      case 8:
        pgroupSize = 3;
        break;
      case 10:
        pgroupSize = 15;
        numPixelsInPgroup = 4;
        break;
      case 12:
        pgroupSize = 9;
        numPixelsInPgroup = 2;
        break;
      case 16:
        pgroupSize = 6;
        break;
    }
  } else if (strcmp(sampling, "YCbCr-4:2:2") == 0) {
    switch (depth) {
      case 8:
        pgroupSize = 4;
        break;
      case 10:
        pgroupSize = 5;
        break;
      case 12:
        pgroupSize = 6;
        break;
      case 16:
        pgroupSize = 8;
        break;
    }
    numPixelsInPgroup = 2;
  } else if (strcmp(sampling, "YCbCr-4:1:1") == 0) {
    switch (depth) {
      case 8:
        pgroupSize = 6;
        break;
      case 10:
        pgroupSize = 15;
        break;
      case 12:
        pgroupSize = 9;
        break;
      case 16:
        pgroupSize = 12;
        break;
    }
    numPixelsInPgroup = 4;
  } else if (strcmp(sampling, "YCbCr-4:2:0") == 0) {
    switch (depth) {
      case 8:
        pgroupSize = 6;
        break;
      case 10:
        pgroupSize = 15;
        break;
      case 12:
        pgroupSize = 9;
        break;
      case 16:
        pgroupSize = 12;
        break;
    }
    numPixelsInPgroup = 4;
    scanLineIterationStep = 2;
  }

  unsigned const numPgroupsPerScanLine
    = (width*scanLineIterationStep + (numPixelsInPgroup-1))/numPixelsInPgroup;
      // Note: This rounds up to the next 'pgroup'; see RFC 4175, section 4.3, paragraph 2

  scanLineSize = numPgroupsPerScanLine*pgroupSize;
      // Note: This is actually the size of 'scanLineIterationStep' scan lines

  frameSize = (height*scanLineSize)/scanLineIterationStep;
}

RawVideoFrameParameters::~RawVideoFrameParameters() {
}

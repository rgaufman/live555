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
// H.264 Video File sinks
// Implementation

#include "H264VideoFileSink.hh"
#include "OutputFile.hh"
#include "H264VideoRTPSource.hh"

////////// H264VideoFileSink //////////

H264VideoFileSink
::H264VideoFileSink(UsageEnvironment& env, FILE* fid,
		    char const* sPropParameterSetsStr,
		    unsigned bufferSize, char const* perFrameFileNamePrefix)
  : FileSink(env, fid, bufferSize, perFrameFileNamePrefix),
    fSPropParameterSetsStr(sPropParameterSetsStr), fHaveWrittenFirstFrame(False) {
}

H264VideoFileSink::~H264VideoFileSink() {
}

H264VideoFileSink*
H264VideoFileSink::createNew(UsageEnvironment& env, char const* fileName,
			     char const* sPropParameterSetsStr,
			     unsigned bufferSize, Boolean oneFilePerFrame) {
  do {
    FILE* fid;
    char const* perFrameFileNamePrefix;
    if (oneFilePerFrame) {
      // Create the fid for each frame
      fid = NULL;
      perFrameFileNamePrefix = fileName;
    } else {
      // Normal case: create the fid once
      fid = OpenOutputFile(env, fileName);
      if (fid == NULL) break;
      perFrameFileNamePrefix = NULL;
    }

    return new H264VideoFileSink(env, fid, sPropParameterSetsStr, bufferSize, perFrameFileNamePrefix);
  } while (0);

  return NULL;
}

void H264VideoFileSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) {
  unsigned char const start_code[4] = {0x00, 0x00, 0x00, 0x01};

  if (!fHaveWrittenFirstFrame) {
    // If we have PPS/SPS NAL units encoded in a "sprop parameter string", prepend these to the file:
    unsigned numSPropRecords;
    SPropRecord* sPropRecords = parseSPropParameterSets(fSPropParameterSetsStr, numSPropRecords);
    for (unsigned i = 0; i < numSPropRecords; ++i) {
      addData(start_code, 4, presentationTime);
      addData(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength, presentationTime);
    }
    delete[] sPropRecords;
    fHaveWrittenFirstFrame = True; // for next time
  }

  // Write the input data to the file, with the start code in front:
  addData(start_code, 4, presentationTime);

  // Call the parent class to complete the normal file write with the input data:
  FileSink::afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
}

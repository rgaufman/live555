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
// Copyright (c) 1996-2025 Live Networks, Inc.  All rights reserved.
// A filter that breaks up an MPEG-4 video elementary stream into
//   frames for:
// - Visual Object Sequence (VS) Header + Visual Object (VO) Header
//   + Video Object Layer (VOL) Header
// - Group of VOP (GOV) Header
// - VOP frame
// C++ header

#ifndef _MPEG4_VIDEO_STREAM_FRAMER_HH
#define _MPEG4_VIDEO_STREAM_FRAMER_HH

#ifndef _MPEG_VIDEO_STREAM_FRAMER_HH
#include "MPEGVideoStreamFramer.hh"
#endif

class MPEG4VideoStreamFramer: public MPEGVideoStreamFramer {
public:
  static MPEG4VideoStreamFramer*
  createNew(UsageEnvironment& env, FramedSource* inputSource);

  u_int8_t profile_and_level_indication() const {
    return fProfileAndLevelIndication;
  }

  unsigned char* getConfigBytes(unsigned& numBytes) const;

  void setConfigInfo(u_int8_t profileAndLevelIndication, char const* configStr);
    // Assigns the "profile_and_level_indication" number, and the 'config' bytes.
    // If this function is not called, then this data is only assigned later, when it appears in the input stream.

protected:
  MPEG4VideoStreamFramer(UsageEnvironment& env,
			 FramedSource* inputSource,
			 Boolean createParser = True);
      // called only by createNew(), or by subclass constructors
  virtual ~MPEG4VideoStreamFramer();

  void startNewConfig();
  void appendToNewConfig(unsigned char* newConfigBytes,
			 unsigned numNewBytes);
  void completeNewConfig();

private:
  // redefined virtual functions:
  virtual Boolean isMPEG4VideoStreamFramer() const;

protected:
  u_int8_t fProfileAndLevelIndication;
  unsigned char* fConfigBytes;
  unsigned fNumConfigBytes;

private:
  unsigned char* fNewConfigBytes;
  unsigned fNumNewConfigBytes;
  friend class MPEG4VideoStreamParser; // hack
};

#endif

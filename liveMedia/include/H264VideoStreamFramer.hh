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
// A filter that breaks up a H.264 Video Elementary Stream into NAL units.
// C++ header

#ifndef _H264_VIDEO_STREAM_FRAMER_HH
#define _H264_VIDEO_STREAM_FRAMER_HH

#ifndef _MPEG_VIDEO_STREAM_FRAMER_HH
#include "MPEGVideoStreamFramer.hh"
#endif

class H264VideoStreamFramer: public MPEGVideoStreamFramer {
public:
  static H264VideoStreamFramer* createNew(UsageEnvironment& env, FramedSource* inputSource,
					  Boolean includeStartCodeInOutput = False);

  void getSPSandPPS(u_int8_t*& sps, unsigned& spsSize, u_int8_t*& pps, unsigned& ppsSize) const{
    // Returns pointers to copies of the most recently seen SPS (sequence parameter set) and PPS (picture parameter set) NAL unit.
    // (NULL pointers are returned if the NAL units have not yet been seen.)
    sps = fLastSeenSPS; spsSize = fLastSeenSPSSize;
    pps = fLastSeenPPS; ppsSize = fLastSeenPPSSize;
  }

  void setSPSandPPS(u_int8_t* sps, unsigned spsSize, u_int8_t* pps, unsigned ppsSize) {
    // Assigns copies of the SPS and PPS NAL units.  If this function is not called, then these NAL units are assigned
    // only if/when they appear in the input stream.  
    saveCopyOfSPS(sps, spsSize);
    saveCopyOfPPS(pps, ppsSize);
  }
  void setSPSandPPS(char const* sPropParameterSetsStr);
    // As above, except that the SPS and PPS NAL units are decoded from the input string, which must be a Base-64 encoding of
    // these NAL units (in either order), separated by a comma.  (This string is typically found in a SDP description, and
    // accessed using "MediaSubsession::fmtp_spropparametersets()".

protected:
  H264VideoStreamFramer(UsageEnvironment& env, FramedSource* inputSource, Boolean createParser, Boolean includeStartCodeInOutput);
  virtual ~H264VideoStreamFramer();

  void saveCopyOfSPS(u_int8_t* from, unsigned size);
  void saveCopyOfPPS(u_int8_t* from, unsigned size);

  // redefined virtual functions:
  virtual Boolean isH264VideoStreamFramer() const;

private:
  void setPresentationTime() { fPresentationTime = fNextPresentationTime; }

private:
  u_int8_t* fLastSeenSPS;
  unsigned fLastSeenSPSSize;
  u_int8_t* fLastSeenPPS;
  unsigned fLastSeenPPSSize;
  struct timeval fNextPresentationTime; // the presentation time to be used for the next NAL unit to be parsed/delivered after this
  friend class H264VideoStreamParser; // hack
};

#endif

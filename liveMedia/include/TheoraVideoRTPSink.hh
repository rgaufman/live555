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
// RTP sink for Theora video
// C++ header

#ifndef _THEORA_VIDEO_RTP_SINK_HH
#define _THEORA_VIDEO_RTP_SINK_HH

#ifndef _VIDEO_RTP_SINK_HH
#include "VideoRTPSink.hh"
#endif

class TheoraVideoRTPSink: public VideoRTPSink {
public:
  static TheoraVideoRTPSink*
  createNew(UsageEnvironment& env, Groupsock* RTPgs, u_int8_t rtpPayloadFormat,
	    // The following headers provide the 'configuration' information, for the SDP description:
	    u_int8_t* identificationHeader, unsigned identificationHeaderSize,
	    u_int8_t* commentHeader, unsigned commentHeaderSize,
	    u_int8_t* setupHeader, unsigned setupHeaderSize,
	    u_int32_t identField = 0xFACADE);
  
  static TheoraVideoRTPSink*
  createNew(UsageEnvironment& env, Groupsock* RTPgs, u_int8_t rtpPayloadFormat,
            char const* configStr);
  // an optional variant of "createNew()" that takes a Base-64-encoded 'configuration' string,
  // rather than the raw configuration headers as parameter.

protected:
  TheoraVideoRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
		     u_int8_t rtpPayloadFormat,
		     u_int8_t* identificationHeader, unsigned identificationHeaderSize,
		     u_int8_t* commentHeader, unsigned commentHeaderSize,
		     u_int8_t* setupHeader, unsigned setupHeaderSize,
		     u_int32_t identField);
  // called only by createNew()
  
  virtual ~TheoraVideoRTPSink();
  
private: // redefined virtual functions:
  virtual char const* auxSDPLine(); // for the "a=fmtp:" SDP line
  
  virtual void doSpecialFrameHandling(unsigned fragmentationOffset,
				      unsigned char* frameStart,
				      unsigned numBytesInFrame,
				      struct timeval framePresentationTime,
				      unsigned numRemainingBytes);
  virtual Boolean frameCanAppearAfterPacketStart(unsigned char const* frameStart,
						 unsigned numBytesInFrame) const;
  virtual unsigned specialHeaderSize() const;
  
private:
  u_int32_t fIdent; // "Ident" field used by this stream.  (Only the low 24 bits of this are used.)
  char* fFmtpSDPLine;
};

#endif

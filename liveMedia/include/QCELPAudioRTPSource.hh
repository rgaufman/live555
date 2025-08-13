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
// Qualcomm "PureVoice" (aka. "QCELP") Audio RTP Sources
// C++ header

#ifndef _QCELP_AUDIO_RTP_SOURCE_HH
#define _QCELP_AUDIO_RTP_SOURCE_HH

#ifndef _RTP_SOURCE_HH
#include "RTPSource.hh"
#endif

class QCELPAudioRTPSource {
public:
  static FramedSource* createNew(UsageEnvironment& env,
				 Groupsock* RTPgs,
				 RTPSource*& resultRTPSource,
				 unsigned char rtpPayloadFormat = 12,
				 unsigned rtpTimestampFrequency = 8000);
      // This returns a source to read from, but "resultRTPSource" will
      // point to RTP-related state.
};

#endif

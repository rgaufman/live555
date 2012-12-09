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
// on demand, from an text (subtitle) track within a Matroska file.
// C++ header

#ifndef _T140_TEXT_MATROSKA_FILE_SERVER_MEDIA_SUBSESSION_HH
#define _T140_TEXT_MATROSKA_FILE_SERVER_MEDIA_SUBSESSION_HH

#ifndef _FILE_SERVER_MEDIA_SUBSESSION_HH
#include "FileServerMediaSubsession.hh"
#endif
#ifndef _MATROSKA_FILE_SERVER_DEMUX_HH
#include "MatroskaFileServerDemux.hh"
#endif

class T140TextMatroskaFileServerMediaSubsession: public FileServerMediaSubsession {
public:
  static T140TextMatroskaFileServerMediaSubsession*
  createNew(MatroskaFileServerDemux& demux, unsigned trackNumber);

private:
  T140TextMatroskaFileServerMediaSubsession(MatroskaFileServerDemux& demux, unsigned trackNumber);
      // called only by createNew();
  virtual ~T140TextMatroskaFileServerMediaSubsession();

private: // redefined virtual functions
  virtual float duration() const;
  virtual void seekStreamSource(FramedSource* inputSource, double& seekNPT, double streamDuration, u_int64_t& numBytes);
  virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
					      unsigned& estBitrate);
  virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource);

private:
  MatroskaFileServerDemux& fOurDemux;
  unsigned fTrackNumber;
};

#endif

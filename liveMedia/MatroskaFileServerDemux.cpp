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
// A server demultiplexor for a Matroska file
// Implementation

#include "MatroskaFileServerDemux.hh"
#include "MP3AudioMatroskaFileServerMediaSubsession.hh"
#include "AACAudioMatroskaFileServerMediaSubsession.hh"
#include "AC3AudioMatroskaFileServerMediaSubsession.hh"
#include "VorbisAudioMatroskaFileServerMediaSubsession.hh"
#include "H264VideoMatroskaFileServerMediaSubsession.hh"
#include "VP8VideoMatroskaFileServerMediaSubsession.hh"
#include "T140TextMatroskaFileServerMediaSubsession.hh"

void MatroskaFileServerDemux
::createNew(UsageEnvironment& env, char const* fileName,
	    onCreationFunc* onCreation, void* onCreationClientData,
	    char const* preferredLanguage) {
  (void)new MatroskaFileServerDemux(env, fileName,
				    onCreation, onCreationClientData,
				    preferredLanguage);
}

ServerMediaSubsession* MatroskaFileServerDemux::newServerMediaSubsession() {
  unsigned dummyResultTrackNumber;
  return newServerMediaSubsession(dummyResultTrackNumber);
}

ServerMediaSubsession* MatroskaFileServerDemux
::newServerMediaSubsession(unsigned& resultTrackNumber) {
  ServerMediaSubsession* result;
  resultTrackNumber = 0;

  for (result = NULL; result == NULL && fNextTrackTypeToCheck != MATROSKA_TRACK_TYPE_OTHER; fNextTrackTypeToCheck <<= 1) {
    if (fNextTrackTypeToCheck == MATROSKA_TRACK_TYPE_VIDEO) resultTrackNumber = fOurMatroskaFile->chosenVideoTrackNumber();
    else if (fNextTrackTypeToCheck == MATROSKA_TRACK_TYPE_AUDIO) resultTrackNumber = fOurMatroskaFile->chosenAudioTrackNumber();
    else if (fNextTrackTypeToCheck == MATROSKA_TRACK_TYPE_SUBTITLE) resultTrackNumber = fOurMatroskaFile->chosenSubtitleTrackNumber();

    result = newServerMediaSubsessionByTrackNumber(resultTrackNumber);
  }

  return result;
}

ServerMediaSubsession* MatroskaFileServerDemux
::newServerMediaSubsessionByTrackNumber(unsigned trackNumber) {
  MatroskaTrack* track = fOurMatroskaFile->lookup(trackNumber);
  if (track == NULL) return NULL;

  // Use the track's "codecID" string to figure out which "ServerMediaSubsession" subclass to use:
  ServerMediaSubsession* result = NULL;
  if (strncmp(track->codecID, "A_MPEG", 6) == 0) {
    track->mimeType = "audio/MPEG";
    result = MP3AudioMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber, False, NULL);
  } else if (strncmp(track->codecID, "A_AAC", 5) == 0) {
    track->mimeType = "audio/AAC";
    result = AACAudioMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber);
  } else if (strncmp(track->codecID, "A_AC3", 5) == 0) {
    track->mimeType = "audio/AC3";
    result = AC3AudioMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber);
  } else if (strncmp(track->codecID, "A_VORBIS", 8) == 0) {
    track->mimeType = "audio/VORBIS";
    result = VorbisAudioMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber);
  } else if (strcmp(track->codecID, "V_MPEG4/ISO/AVC") == 0) {
    track->mimeType = "video/H264";
    result = H264VideoMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber);
  } else if (strncmp(track->codecID, "V_VP8", 5) == 0) {
    track->mimeType = "video/VP8";
    result = VP8VideoMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber);
  } else if (strncmp(track->codecID, "S_TEXT", 6) == 0) {
    track->mimeType = "text/T140";
    result = T140TextMatroskaFileServerMediaSubsession::createNew(*this, track->trackNumber);
  }

  if (result != NULL) {
#ifdef DEBUG
    fprintf(stderr, "Created 'ServerMediaSubsession' object for track #%d: %s (%s)\n", track->trackNumber, track->codecID, track->mimeType);
#endif
  }

  return result;
}

FramedSource* MatroskaFileServerDemux::newDemuxedTrack(unsigned clientSessionId, unsigned trackNumber) {
  MatroskaDemux* demuxToUse = NULL;

  if (clientSessionId != 0 && clientSessionId == fLastClientSessionId) {
    demuxToUse = fLastCreatedDemux; // use the same demultiplexor as before
      // Note: This code relies upon the fact that the creation of streams for different
      // client sessions do not overlap - so all demuxed tracks are created for one "MatroskaDemux" at a time.
      // Also, the "clientSessionId != 0" test is a hack, because 'session 0' is special; its audio and video streams
      // are created and destroyed one-at-a-time, rather than both streams being
      // created, and then (later) both streams being destroyed (as is the case
      // for other ('real') session ids).  Because of this, a separate demultiplexor is used for each 'session 0' track.
  }

  if (demuxToUse == NULL) demuxToUse = fOurMatroskaFile->newDemux();

  fLastClientSessionId = clientSessionId;
  fLastCreatedDemux = demuxToUse;

  return demuxToUse->newDemuxedTrackByTrackNumber(trackNumber);
}

MatroskaFileServerDemux
::MatroskaFileServerDemux(UsageEnvironment& env, char const* fileName,
			  onCreationFunc* onCreation, void* onCreationClientData,
			  char const* preferredLanguage)
  : Medium(env),
    fFileName(fileName), fOnCreation(onCreation), fOnCreationClientData(onCreationClientData),
    fNextTrackTypeToCheck(0x1), fLastClientSessionId(0), fLastCreatedDemux(NULL) {
  MatroskaFile::createNew(env, fileName, onMatroskaFileCreation, this, preferredLanguage);
}

MatroskaFileServerDemux::~MatroskaFileServerDemux() {
  Medium::close(fOurMatroskaFile);
}

void MatroskaFileServerDemux::onMatroskaFileCreation(MatroskaFile* newFile, void* clientData) {
  ((MatroskaFileServerDemux*)clientData)->onMatroskaFileCreation(newFile);
}

void MatroskaFileServerDemux::onMatroskaFileCreation(MatroskaFile* newFile) {
  fOurMatroskaFile = newFile;

  // Now, call our own creation notification function:
  if (fOnCreation != NULL) (*fOnCreation)(this, fOnCreationClientData);
}

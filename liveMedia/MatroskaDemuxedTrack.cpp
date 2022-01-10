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
// Copyright (c) 1996-2022 Live Networks, Inc.  All rights reserved.
// A media track, demultiplexed from a Matroska file
// Implementation

#include "MatroskaDemuxedTrack.hh"
#include "MatroskaFile.hh"

void MatroskaDemuxedTrack::seekToTime(double& seekNPT) {
  fOurSourceDemux.seekToTime(seekNPT);
}

MatroskaDemuxedTrack::MatroskaDemuxedTrack(UsageEnvironment& env, unsigned trackNumber, MatroskaDemux& sourceDemux)
  : FramedSource(env),
    fOurTrackNumber(trackNumber), fOurSourceDemux(sourceDemux),
    fOpusFrameNumber(0) {
  reset();
}

MatroskaDemuxedTrack::~MatroskaDemuxedTrack() {
  fOurSourceDemux.removeTrack(fOurTrackNumber);
}

void MatroskaDemuxedTrack::doGetNextFrame() {
  fOurSourceDemux.continueReading();
}

void MatroskaDemuxedTrack::doStopGettingFrames() {
  fOurSourceDemux.pause();
}

char const* MatroskaDemuxedTrack::MIMEtype() const {
  MatroskaTrack* track = fOurSourceDemux.fOurFile.lookup(fOurTrackNumber);
  if (track == NULL) return "(unknown)"; // shouldn't happen
  return track->mimeType;
}

void MatroskaDemuxedTrack::reset() {
  fPrevPresentationTime.tv_sec = 0; fPrevPresentationTime.tv_usec = 0;
  fDurationImbalance = 0;
}

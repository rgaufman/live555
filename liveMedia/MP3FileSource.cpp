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
// MP3 File Sources
// Implementation

#include "MP3FileSource.hh"
#include "MP3StreamState.hh"
#include "InputFile.hh"

////////// MP3FileSource //////////

MP3FileSource* MP3FileSource::createNew(UsageEnvironment& env, char const* fileName) {
  MP3FileSource* newSource = NULL;

  do {
    FILE* fid;

    fid = OpenInputFile(env, fileName);
    if (fid == NULL) break;

    newSource = new MP3FileSource(env, fid);
    if (newSource == NULL) break;

    unsigned fileSize = (unsigned)GetFileSize(fileName, fid);
    newSource->assignStream(fid, fileSize);

    return newSource;
  } while (0);

  Medium::close(newSource);
  return NULL;
}

MP3FileSource::MP3FileSource(UsageEnvironment& env, FILE* fid)
  : FramedFileSource(env, fid),
    fStreamState(new MP3StreamState),
    fHaveStartedReading(False), fHaveBeenInitialized(False),
    fLimitNumBytesToStream(False), fNumBytesToStream(0) {
  //###### We can't make the socket non-blocking yet, because we still do synchronous reads
  //###### on it (to find MP3 headers).  Later, fix this.
  //makeSocketNonBlocking(fileno(fFid));

  // Test whether the file is seekable
  fFidIsSeekable = FileIsSeekable(fFid);
}

MP3FileSource::~MP3FileSource() {
  if (fFid != NULL) envir().taskScheduler().turnOffBackgroundReadHandling(fileno(fFid));

  delete fStreamState; // closes the input file
}

char const* MP3FileSource::MIMEtype() const {
  return "audio/MPEG";
}

float MP3FileSource::filePlayTime() const {
  return fStreamState->filePlayTime();
}

unsigned MP3FileSource::fileSize() const {
  return fStreamState->fileSize();
}

void MP3FileSource::setPresentationTimeScale(unsigned scale) {
  fStreamState->setPresentationTimeScale(scale);
}

void MP3FileSource::seekWithinFile(double seekNPT, double streamDuration) {
  float fileDuration = filePlayTime();

  // First, make sure that 0.0 <= seekNPT <= seekNPT + streamDuration <= fileDuration
  if (seekNPT < 0.0) {
    seekNPT = 0.0;
  } else if (seekNPT > fileDuration) {
    seekNPT = fileDuration;
  }
  if (streamDuration < 0.0) {
    streamDuration = 0.0;
  } else if (seekNPT + streamDuration > fileDuration) {
    streamDuration = fileDuration - seekNPT; 
  }

  float seekFraction = (float)seekNPT/fileDuration;
  unsigned seekByteNumber = fStreamState->getByteNumberFromPositionFraction(seekFraction);
  fStreamState->seekWithinFile(seekByteNumber);

  fLimitNumBytesToStream = False; // by default
  if (streamDuration > 0.0) {
    float endFraction = (float)(seekNPT + streamDuration)/fileDuration;
    unsigned endByteNumber = fStreamState->getByteNumberFromPositionFraction(endFraction);
    if (endByteNumber > seekByteNumber) { // sanity check
      fNumBytesToStream = endByteNumber - seekByteNumber;
      fLimitNumBytesToStream = True;
    }
  }
}

void MP3FileSource::getAttributes() const {
  char buffer[200];
  fStreamState->getAttributes(buffer, sizeof buffer);
  envir().setResultMsg(buffer);
}

void MP3FileSource::doGetNextFrame() {
  if (feof(fFid) || ferror(fFid) || (fLimitNumBytesToStream && fNumBytesToStream == 0)) {
    handleClosure();
    return;
  }

  if (!fHaveStartedReading) {
    // Await readable data from the file:
    envir().taskScheduler().turnOnBackgroundReadHandling(fileno(fFid),
		 (TaskScheduler::BackgroundHandlerProc*)&fileReadableHandler, this);
    fHaveStartedReading = True;
    return;
  }

  if (!fHaveBeenInitialized) {
    if (!initializeStream()) return;

    fPresentationTime = fFirstFramePresentationTime;
    fHaveBeenInitialized = True;
  } else {
    if (fStreamState->findNextHeader(fPresentationTime) == 0) return;
  }

  if (fLimitNumBytesToStream && fNumBytesToStream < (u_int64_t)fMaxSize) {
    fMaxSize = (unsigned)fNumBytesToStream;
  }
  if (!fStreamState->readFrame(fTo, fMaxSize, fFrameSize, fDurationInMicroseconds)) {
    char tmp[200];
    sprintf(tmp,
	    "Insufficient buffer size %d for reading MPEG audio frame (needed %d)\n",
	    fMaxSize, fFrameSize);
    envir().setResultMsg(tmp);
    handleClosure();
    return;
  }
  fNumBytesToStream -= fFrameSize;

  // Inform the reader that he has data:
  // Because the file read was done from the event loop, we can call the
  // 'after getting' function directly, without risk of infinite recursion:
  FramedSource::afterGetting(this);
}

void MP3FileSource::fileReadableHandler(MP3FileSource* source, int /*mask*/) {
  if (!source->isCurrentlyAwaitingData()) {
    source->doStopGettingFrames(); // we're not ready for the data yet
    return;
  }
  source->doGetNextFrame();
}

void MP3FileSource::assignStream(FILE* fid, unsigned fileSize) {
  fStreamState->assignStream(fid, fileSize);

  if (!fHaveBeenInitialized) {
    if (!initializeStream()) return;

    fPresentationTime = fFirstFramePresentationTime;
    fHaveBeenInitialized = True;
  }
}

Boolean MP3FileSource::initializeStream() {
  // Make sure the file has an appropriate header near the start:
  if (fStreamState->findNextHeader(fFirstFramePresentationTime) == 0) {
      envir().setResultMsg("not an MPEG audio file");
      return False;
  }

  fStreamState->checkForXingHeader(); // in case this is a VBR file

  // Hack: It's possible that our environment's 'result message' has been
  // reset within this function, so set it again to our name now:
  envir().setResultMsg(name());
  return True;
}

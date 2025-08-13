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
// A server demultiplexer for a MPEG 1 or 2 Program Stream
// Implementation

#include "MPEG1or2FileServerDemux.hh"
#include "MPEG1or2DemuxedServerMediaSubsession.hh"
#include "ByteStreamFileSource.hh"

MPEG1or2FileServerDemux*
MPEG1or2FileServerDemux::createNew(UsageEnvironment& env, char const* fileName,
				   Boolean reuseFirstSource) {
  return new MPEG1or2FileServerDemux(env, fileName, reuseFirstSource);
}

static float MPEG1or2ProgramStreamFileDuration(UsageEnvironment& env,
					       char const* fileName,
					       unsigned& fileSize); // forward
MPEG1or2FileServerDemux
::MPEG1or2FileServerDemux(UsageEnvironment& env, char const* fileName,
			  Boolean reuseFirstSource)
  : Medium(env),
    fReuseFirstSource(reuseFirstSource),
    fSession0Demux(NULL), fLastCreatedDemux(NULL), fLastClientSessionId(~0) {
  fFileName = strDup(fileName);
  fFileDuration = MPEG1or2ProgramStreamFileDuration(env, fileName, fFileSize);
}

MPEG1or2FileServerDemux::~MPEG1or2FileServerDemux() {
  Medium::close(fSession0Demux);
  delete[] (char*)fFileName;
}

ServerMediaSubsession*
MPEG1or2FileServerDemux::newAudioServerMediaSubsession() {
  return MPEG1or2DemuxedServerMediaSubsession::createNew(*this, 0xC0, fReuseFirstSource);
}

ServerMediaSubsession*
MPEG1or2FileServerDemux::newVideoServerMediaSubsession(Boolean iFramesOnly,
						       double vshPeriod) {
  return MPEG1or2DemuxedServerMediaSubsession::createNew(*this, 0xE0, fReuseFirstSource,
							 iFramesOnly, vshPeriod);
}

ServerMediaSubsession*
MPEG1or2FileServerDemux::newAC3AudioServerMediaSubsession() {
  return MPEG1or2DemuxedServerMediaSubsession::createNew(*this, 0xBD, fReuseFirstSource);
  // because, in a VOB file, the AC3 audio has stream id 0xBD
}

MPEG1or2DemuxedElementaryStream*
MPEG1or2FileServerDemux::newElementaryStream(unsigned clientSessionId,
					     u_int8_t streamIdTag) {
  MPEG1or2Demux* demuxToUse = NULL;

  if (clientSessionId == 0) {
    // 'Session 0' is treated especially, because its audio & video streams
    // are created and destroyed one-at-a-time, rather than both streams being
    // created, and then (later) both streams being destroyed (as is the case
    // for other ('real') session ids).  Because of this, a separate demux is
    // used for session 0, and its deletion is managed by us, rather than
    // happening automatically.
    if (fSession0Demux == NULL) {
      // Open our input file as a 'byte-stream file source':
      ByteStreamFileSource* fileSource
	= ByteStreamFileSource::createNew(envir(), fFileName);
      if (fileSource == NULL) return NULL;
      fSession0Demux = MPEG1or2Demux::createNew(envir(), fileSource, False/*note!*/);
    }
    demuxToUse = fSession0Demux;
  } else {
    // First, check whether this is a new client session.  If so, create a new
    // demux for it:
    if (clientSessionId == fLastClientSessionId) {
      demuxToUse = fLastCreatedDemux; // use the same demultiplexor as before
    }

    if (demuxToUse == NULL) {
      // Open our input file as a 'byte-stream file source':
      ByteStreamFileSource* fileSource
	= ByteStreamFileSource::createNew(envir(), fFileName);
      if (fileSource == NULL) return NULL;

      demuxToUse = MPEG1or2Demux::createNew(envir(), fileSource, True, onDemuxDeletion, this);
        // Note: We tell the demux to delete itself when its last
        // elementary stream is deleted.
    }

    fLastClientSessionId = clientSessionId;
    fLastCreatedDemux = demuxToUse;
  }

  return demuxToUse->newElementaryStream(streamIdTag);
}

void MPEG1or2FileServerDemux::onDemuxDeletion(void* clientData, MPEG1or2Demux* demuxBeingDeleted) {
  ((MPEG1or2FileServerDemux*)clientData)->onDemuxDeletion(demuxBeingDeleted);
}

void MPEG1or2FileServerDemux::onDemuxDeletion(MPEG1or2Demux* demuxBeingDeleted) {
  if (fLastCreatedDemux == demuxBeingDeleted) fLastCreatedDemux = NULL;
}


static Boolean getMPEG1or2TimeCode(FramedSource* dataSource,
				   MPEG1or2Demux& parentDemux,
				   Boolean returnFirstSeenCode,
				   float& timeCode); // forward

static float MPEG1or2ProgramStreamFileDuration(UsageEnvironment& env,
					       char const* fileName,
					       unsigned& fileSize) {
  FramedSource* dataSource = NULL;
  float duration = 0.0; // until we learn otherwise
  fileSize = 0; // ditto

  do {
    // Open the input file as a 'byte-stream file source':
    ByteStreamFileSource* fileSource = ByteStreamFileSource::createNew(env, fileName);
    if (fileSource == NULL) break;
    dataSource = fileSource;

    fileSize = (unsigned)(fileSource->fileSize());
    if (fileSize == 0) break;

    // Create a MPEG demultiplexor that reads from that source.
    MPEG1or2Demux* baseDemux = MPEG1or2Demux::createNew(env, dataSource, True);
    if (baseDemux == NULL) break;

    // Create, from this, a source that returns raw PES packets:
    dataSource = baseDemux->newRawPESStream();

    // Read the first time code from the file:
    float firstTimeCode;
    if (!getMPEG1or2TimeCode(dataSource, *baseDemux, True, firstTimeCode)) break;

    // Then, read the last time code from the file.
    // (Before doing this, flush the demux's input buffers,
    //  and seek towards the end of the file, for efficiency.)
    baseDemux->flushInput();
    unsigned const startByteFromEnd = 100000;
    unsigned newFilePosition
      = fileSize < startByteFromEnd ? 0 : fileSize - startByteFromEnd;
    if (newFilePosition > 0) fileSource->seekToByteAbsolute(newFilePosition);

    float lastTimeCode;
    if (!getMPEG1or2TimeCode(dataSource, *baseDemux, False, lastTimeCode)) break;

    // Take the difference between these time codes as being the file duration:
    float timeCodeDiff = lastTimeCode - firstTimeCode;
    if (timeCodeDiff < 0) break;
    duration = timeCodeDiff;
  } while (0);

  Medium::close(dataSource);
  return duration;
}

#define MFSD_DUMMY_SINK_BUFFER_SIZE (6+65535) /* large enough for a PES packet */

class MFSD_DummySink: public MediaSink {
public:
  MFSD_DummySink(MPEG1or2Demux& demux, Boolean returnFirstSeenCode);
  virtual ~MFSD_DummySink();

  EventLoopWatchVariable watchVariable;

private:
  // redefined virtual function:
  virtual Boolean continuePlaying();

private:
  static void afterGettingFrame(void* clientData, unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  void afterGettingFrame1();

private:
  MPEG1or2Demux& fOurDemux;
  Boolean fReturnFirstSeenCode;
  unsigned char fBuf[MFSD_DUMMY_SINK_BUFFER_SIZE];
};

static void afterPlayingMFSD_DummySink(MFSD_DummySink* sink); // forward
static float computeSCRTimeCode(MPEG1or2Demux::SCR const& scr); // forward

static Boolean getMPEG1or2TimeCode(FramedSource* dataSource,
				   MPEG1or2Demux& parentDemux,
				   Boolean returnFirstSeenCode,
				   float& timeCode) {
  // Start reading through "dataSource", until we see a SCR time code:
  parentDemux.lastSeenSCR().isValid = False;
  UsageEnvironment& env = dataSource->envir(); // alias
  MFSD_DummySink sink(parentDemux, returnFirstSeenCode);
  sink.startPlaying(*dataSource,
		    (MediaSink::afterPlayingFunc*)afterPlayingMFSD_DummySink, &sink);
  env.taskScheduler().doEventLoop(&sink.watchVariable);

  timeCode = computeSCRTimeCode(parentDemux.lastSeenSCR());
  return parentDemux.lastSeenSCR().isValid;
}


////////// MFSD_DummySink implementation //////////

MFSD_DummySink::MFSD_DummySink(MPEG1or2Demux& demux, Boolean returnFirstSeenCode)
  : MediaSink(demux.envir()),
    watchVariable(0), fOurDemux(demux), fReturnFirstSeenCode(returnFirstSeenCode) {
}

MFSD_DummySink::~MFSD_DummySink() {
}

Boolean MFSD_DummySink::continuePlaying() {
  if (fSource == NULL) return False; // sanity check

  fSource->getNextFrame(fBuf, sizeof fBuf,
			afterGettingFrame, this,
			onSourceClosure, this);
  return True;
}

void MFSD_DummySink::afterGettingFrame(void* clientData, unsigned /*frameSize*/,
				  unsigned /*numTruncatedBytes*/,
				  struct timeval /*presentationTime*/,
				  unsigned /*durationInMicroseconds*/) {
  MFSD_DummySink* sink = (MFSD_DummySink*)clientData;
  sink->afterGettingFrame1();
}

void MFSD_DummySink::afterGettingFrame1() {
  if (fReturnFirstSeenCode && fOurDemux.lastSeenSCR().isValid) {
    // We were asked to return the first SCR that we saw, and we've seen one,
    // so we're done.  (Handle this as if the input source had closed.)
    onSourceClosure();
    return;
  }

  continuePlaying();
}

static void afterPlayingMFSD_DummySink(MFSD_DummySink* sink) {
  // Return from the "doEventLoop()" call:
  sink->watchVariable = ~0;
}

static float computeSCRTimeCode(MPEG1or2Demux::SCR const& scr) {
  double result = scr.remainingBits/90000.0 + scr.extension/300.0;
  if (scr.highBit) {
    // Add (2^32)/90000 == (2^28)/5625
    double const highBitValue = (256*1024*1024)/5625.0;
    result += highBitValue;
  }

  return (float)result;
}

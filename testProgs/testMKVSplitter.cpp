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
51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
**********/
// Copyright (c) 1996-2022, Live Networks, Inc.  All rights reserved
// A test program that reads a ".mkv" (i.e., Matroska) file, demultiplexes each track
// (video, audio, subtitles), and outputs each track to a file.
// main program

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>

UsageEnvironment* env;
char const* programName;
char const* inputFileName;

// An array of structures representing the state of the video, audio, and subtitle tracks:
static struct {
  unsigned trackNumber;
  FramedSource* source;
  FileSink* sink;
} trackState[3];

void onMatroskaFileCreation(MatroskaFile* newFile, void* clientData); // forward

void usage() {
  *env << "usage: " << programName << " <input-Matroska-or-WebM-file-name>\n";
  exit(1);
}

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Parse the command line:
  programName = argv[0];
  if (argc != 2) usage();
  inputFileName = argv[1];

  // Arrange to create a "MatroskaFile" object for the specified file.
  // (Note that this object is not created immediately, but instead via a callback.)
  MatroskaFile::createNew(*env, inputFileName, onMatroskaFileCreation, NULL);

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void play(); // forward

void onMatroskaFileCreation(MatroskaFile* matroskaFile, void* /*clientData*/) {
  // Create a new demultiplexor for the file:
  MatroskaDemux* matroskaDemux = matroskaFile->newDemux();

  // Create source streams and file sinks for each preferred track;

  unsigned numActiveTracks = 0;
  for (unsigned i = 0; i < 3; ++i) {
    unsigned trackNumber;
    trackState[i].source = matroskaDemux->newDemuxedTrack(trackNumber);
    trackState[i].trackNumber = trackNumber;
    trackState[i].sink = NULL; // by default; may get changed below
    
    if (trackState[i].source == NULL) continue;
    
    char const* mimeType = matroskaFile->trackMIMEType(trackNumber);
    if (mimeType == NULL || mimeType[0] == '\0') continue;

    // Create the file name from "mimeType" by replacing "/" with "-", and adding the
    // track number at the end:
    char* fileName = new char[strlen(mimeType) + 100/*more than enough space*/];
    sprintf(fileName, "%s-%d", mimeType, trackNumber);
    for (unsigned j = 0; fileName[j] != '\0'; ++j) {
      if (fileName[j] == '/') {
	fileName[j] = '-';
	break;
      }
    }

    trackState[i].sink
	= matroskaFile->createFileSinkForTrackNumber(trackNumber, fileName);
    if (trackState[i].sink != NULL) {
      ++numActiveTracks;
      fprintf(stderr, "Created output file \"%s\" for track %d\n", fileName, trackNumber);
    }
  }

  if (numActiveTracks == 0) {
    *env << "Error: The Matroska file \"" << inputFileName << "\" has no streamable tracks\n";
    *env << "(Perhaps the file does not exist, or is not a 'Matroska' file.)\n";
    exit(1);
  }

  // Start the streaming:
  play();
}

void afterPlaying(void* /*clientData*/) {
  *env << "...done reading from file\n";

  // Stop playing all sinks, then close the source streams
  // (which will also close the demultiplexor itself):
  unsigned i;
  for (i = 0; i < 3; ++i) {
    if (trackState[i].sink != NULL) trackState[i].sink->stopPlaying();
    Medium::close(trackState[i].source); trackState[i].source = NULL;
  }

  // Finally, close the sinks:
  for (i = 0; i < 3; ++i) Medium::close(trackState[i].sink);

  exit(0);
}

void play() {
  *env << "Beginning to read from file...\n";

  // Start playing each track's RTP sink from its corresponding source:
  for (unsigned i = 0; i < 3; ++i) {
    if (trackState[i].sink != NULL && trackState[i].source != NULL) {
      trackState[i].sink->startPlaying(*trackState[i].source, afterPlaying, NULL);
    }
  }
}

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
// Copyright (c) 1996-2025, Live Networks, Inc.  All rights reserved
// A program that converts a H.264 (Elementary Stream) video file into a Transport Stream file.
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

char const* inputFileName = "in.264";
char const* outputFileName = "out.ts";

void afterPlaying(void* clientData); // forward

UsageEnvironment* env;

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Open the input file as a 'byte-stream file source':
  FramedSource* inputSource = ByteStreamFileSource::createNew(*env, inputFileName);
  if (inputSource == NULL) {
    *env << "Unable to open file \"" << inputFileName
	 << "\" as a byte-stream file source\n";
    exit(1);
  }

  // Create a 'framer' filter for this file source, to generate presentation times for each NAL unit:
  H264VideoStreamFramer* framer = H264VideoStreamFramer::createNew(*env, inputSource, True/*includeStartCodeInOutput*/);

  // Then create a filter that packs the H.264 video data into a Transport Stream:
  MPEG2TransportStreamFromESSource* tsFrames = MPEG2TransportStreamFromESSource::createNew(*env);
  tsFrames->addNewVideoSource(framer, 5/*mpegVersion: H.264*/);
  
  // Open the output file as a 'file sink':
  MediaSink* outputSink = FileSink::createNew(*env, outputFileName);
  if (outputSink == NULL) {
    *env << "Unable to open file \"" << outputFileName << "\" as a file sink\n";
    exit(1);
  }

  // Finally, start playing:
  *env << "Beginning to read...\n";
  outputSink->startPlaying(*tsFrames, afterPlaying, NULL);

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* /*clientData*/) {
  *env << "Done reading.\n";
  *env << "Wrote output file: \"" << outputFileName << "\"\n";
  exit(0);
}

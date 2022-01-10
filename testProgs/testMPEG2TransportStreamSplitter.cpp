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
// Copyright (c) 1996-2022, Live Networks, Inc.  All rights reserved
// A test program that splits a MPEG Transport Stream input (on 'stdin')
// into separate video and audio output files.
// main program

#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>

UsageEnvironment* env;
char const* programName;
char const* inputFileName = "stdin";
MPEG2TransportStreamDemux* baseDemultiplexor = NULL;

void usage() {
  *env << "usage: " << programName << " takes no arguments (it reads from \"stdin\")\n";
  exit(1);
}

void afterReading(void*);

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Parse the command line:
  programName = argv[0];
  if (argc != 1) usage();

  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* inputSource
    = ByteStreamFileSource::createNew(*env, inputFileName);
  if (inputSource == NULL) {
    *env << "Unable to open file \"" << inputFileName
	 << "\" as a byte-stream file source\n";
    exit(1);
  }

  // Create a demultiplexor that reads from that source, creating new 'demultiplexed tracks'
  // as they appear:
  baseDemultiplexor = MPEG2TransportStreamDemux::createNew(*env, inputSource, afterReading, NULL);

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterReading(void* /*clientData*/) {
  *env << "...done\n";
  exit(0);
}

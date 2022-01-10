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
// A program that converts a H.264 (Elementary Stream) video file into sequence of
// HLS (HTTP Live Streaming) segments, plus a ".m3u8" file that can be accessed via a web browser.
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#define OUR_HLS_SEGMENTATION_DURATION 6
#define OUR_HLS_FILENAME_PREFIX "hlsTest"
char const* inputFileName = "in.264";
FILE* ourM3U8Fid = NULL;

void segmentationCallback(void* clientData, char const* segmentFileName, double segmentDuration); // forward
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
  H264VideoStreamFramer* framer
    = H264VideoStreamFramer::createNew(*env, inputSource,
				       True/*includeStartCodeInOutput*/,
				       True/*insertAccessUnitDelimiters*/);

  // Then create a filter that packs the H.264 video data into a Transport Stream:
  MPEG2TransportStreamFromESSource* tsFrames = MPEG2TransportStreamFromESSource::createNew(*env);
  tsFrames->addNewVideoSource(framer, 5/*mpegVersion: H.264*/);
  
  // Create a 'HLS Segmenter' as the media sink:
  MediaSink* outputSink
    = HLSSegmenter::createNew(*env, OUR_HLS_SEGMENTATION_DURATION, OUR_HLS_FILENAME_PREFIX,
			      segmentationCallback);

  // Finally, start playing:
  *env << "Beginning to read...\n";
  outputSink->startPlaying(*tsFrames, afterPlaying, NULL);

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void segmentationCallback(void* /*clientData*/,
			  char const* segmentFileName, double segmentDuration) {
  if (ourM3U8Fid == NULL) {
    // Open our ".m3u8" file for output, and write the prefix:
    char* ourM3U8FileName = new char[strlen(OUR_HLS_FILENAME_PREFIX) + 5/*strlen(".m3u8")*/ + 1];
    sprintf(ourM3U8FileName, "%s.m3u8", OUR_HLS_FILENAME_PREFIX);
    ourM3U8Fid = fopen(ourM3U8FileName, "wb");

    fprintf(ourM3U8Fid,
	    "#EXTM3U\n"
	    "#EXT-X-VERSION:3\n"
	    "#EXT-X-INDEPENDENT-SEGMENTS\n"
	    "#EXT-X-TARGETDURATION:%u\n"
	    "#EXT-X-MEDIA-SEQUENCE:0\n",
	    OUR_HLS_SEGMENTATION_DURATION);
  }

  // Update our ".m3u8" file with information about this most recent segment:
  fprintf(ourM3U8Fid,
	  "#EXTINF:%f,\n"
	  "%s\n",
	  segmentDuration,
	  segmentFileName);
  
  fprintf(stderr, "Wrote segment \"%s\" (duration: %f seconds)\n", segmentFileName, segmentDuration);
}

void afterPlaying(void* /*clientData*/) {
  *env << "...Done reading\n";

  // Complete and close our ".m3u8" file:
  fprintf(ourM3U8Fid, "#EXT-X-ENDLIST\n");

  fprintf(stderr, "Wrote %s.m3u8\n", OUR_HLS_FILENAME_PREFIX);
  exit(0);
}

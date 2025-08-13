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
// A class encapsulating the state of a MP3 stream
// Implementation

#include "MP3StreamState.hh"
#include "InputFile.hh"
#include "GroupsockHelper.hh"

#if defined(__WIN32__) || defined(_WIN32)
#define snprintf _snprintf
#if _MSC_VER >= 1400 // 1400 == vs2005
#define fileno _fileno
#endif
#endif

#define MILLION 1000000

MP3StreamState::MP3StreamState()
  : fFid(NULL), fPresentationTimeScale(1) {
}

MP3StreamState::~MP3StreamState() {
  // Close our open file or socket:
  if (fFid != NULL && fFid != stdin) {
    if (fFidIsReallyASocket) {
      intptr_t fid_long = (intptr_t)fFid;
      closeSocket((int)fid_long);
    } else {
      CloseInputFile(fFid);
    }
  }
}

void MP3StreamState::assignStream(FILE* fid, unsigned fileSize) {
  fFid = fid;

  if (fileSize == (unsigned)(-1)) { /*HACK#####*/
    fFidIsReallyASocket = 1;
    fFileSize = 0;
  } else {
    fFidIsReallyASocket = 0;
    fFileSize = fileSize;
  }
  fNumFramesInFile = 0; // until we know otherwise
  fIsVBR = fHasXingTOC = False; // ditto

  // Set the first frame's 'presentation time' to the current wall time:
  gettimeofday(&fNextFramePresentationTime, NULL);
}

struct timeval MP3StreamState::currentFramePlayTime() const {
  unsigned const numSamples = 1152;
  unsigned const freq = fr().samplingFreq*(1 + fr().isMPEG2);

  // result is numSamples/freq
  unsigned const uSeconds
    = ((numSamples*2*MILLION)/freq + 1)/2; // rounds to nearest integer

  struct timeval result;
  result.tv_sec = uSeconds/MILLION;
  result.tv_usec = uSeconds%MILLION;
  return result;
}

float MP3StreamState::filePlayTime() const {
  unsigned numFramesInFile = fNumFramesInFile;
  if (numFramesInFile == 0) {
    // Estimate the number of frames from the file size, and the
    // size of the current frame:
    numFramesInFile = fFileSize/(4 + fCurrentFrame.frameSize);
  }

  struct timeval const pt = currentFramePlayTime();
  return numFramesInFile*(pt.tv_sec + pt.tv_usec/(float)MILLION);
}

unsigned MP3StreamState::getByteNumberFromPositionFraction(float fraction) {
  if (fHasXingTOC) {
    // The file is VBR, with a Xing TOC; use it to determine which byte to seek to:
    float percent = fraction*100.0f;
    unsigned a = (unsigned)percent;
    if (a > 99) a = 99;

    unsigned fa = fXingTOC[a];
    unsigned fb;
    if (a < 99) {
      fb = fXingTOC[a+1];
    } else {
      fb = 256;
    }
    fraction = (fa + (fb-fa)*(percent-a))/256.0f;
  }

  return (unsigned)(fraction*fFileSize);
}

void MP3StreamState::seekWithinFile(unsigned seekByteNumber) {
  if (fFidIsReallyASocket) return; // it's not seekable

  SeekFile64(fFid, seekByteNumber, SEEK_SET);
}

unsigned MP3StreamState::findNextHeader(struct timeval& presentationTime) {
  presentationTime = fNextFramePresentationTime;

  if (!findNextFrame()) return 0;

  // From this frame, figure out the *next* frame's presentation time:
  struct timeval framePlayTime = currentFramePlayTime();
  if (fPresentationTimeScale > 1) {
    // Scale this value
    unsigned secondsRem = framePlayTime.tv_sec % fPresentationTimeScale;
    framePlayTime.tv_sec -= secondsRem;
    framePlayTime.tv_usec += secondsRem*MILLION;
    framePlayTime.tv_sec /= fPresentationTimeScale;
    framePlayTime.tv_usec /= fPresentationTimeScale;
  }
  fNextFramePresentationTime.tv_usec += framePlayTime.tv_usec;
  fNextFramePresentationTime.tv_sec
    += framePlayTime.tv_sec + fNextFramePresentationTime.tv_usec/MILLION;
  fNextFramePresentationTime.tv_usec %= MILLION;

  return fr().hdr;
}

Boolean MP3StreamState::readFrame(unsigned char* outBuf, unsigned outBufSize,
				  unsigned& resultFrameSize,
				  unsigned& resultDurationInMicroseconds) {
  /* We assume that "findNextHeader()" has already been called */

  resultFrameSize = 4 + fr().frameSize;

  if (outBufSize < resultFrameSize) {
#ifdef DEBUG_ERRORS
    fprintf(stderr, "Insufficient buffer size for reading input frame (%d, need %d)\n",
	    outBufSize, resultFrameSize);
#endif
    if (outBufSize < 4) outBufSize = 0;
    resultFrameSize = outBufSize;

    return False;
  }

  if (resultFrameSize >= 4) {
    unsigned& hdr = fr().hdr;
    *outBuf++ = (unsigned char)(hdr>>24);
    *outBuf++ = (unsigned char)(hdr>>16);
    *outBuf++ = (unsigned char)(hdr>>8);
    *outBuf++ = (unsigned char)(hdr);

    memmove(outBuf, fr().frameBytes, resultFrameSize-4);
  }

  struct timeval const pt = currentFramePlayTime();
  resultDurationInMicroseconds = pt.tv_sec*MILLION + pt.tv_usec;

  return True;
}

void MP3StreamState::getAttributes(char* buffer, unsigned bufferSize) const {
  char const* formatStr
    = "bandwidth %d MPEGnumber %d MPEGlayer %d samplingFrequency %d isStereo %d playTime %d isVBR %d";
  unsigned fpt = (unsigned)(filePlayTime() + 0.5); // rounds to nearest integer
#if defined(IRIX) || defined(ALPHA) || defined(_QNX4) || defined(IMN_PIM) || defined(CRIS)
  /* snprintf() isn't defined, so just use sprintf() - ugh! */
  sprintf(buffer, formatStr,
	  fr().bitrate, fr().isMPEG2 ? 2 : 1, fr().layer, fr().samplingFreq, fr().isStereo,
	  fpt, fIsVBR);
#else
  snprintf(buffer, bufferSize, formatStr,
	  fr().bitrate, fr().isMPEG2 ? 2 : 1, fr().layer, fr().samplingFreq, fr().isStereo,
	  fpt, fIsVBR);
#endif
}

// This is crufty old code that needs to be cleaned up #####
#define HDRCMPMASK 0xfffffd00

Boolean MP3StreamState::findNextFrame() {
  unsigned char hbuf[8];
  unsigned l; int i;
#ifdef DEBUG_ERRORS
  int attempt = 0;
#endif

 read_again:
  if (readFromStream(hbuf, 4) != 4) return False;

  fr().hdr =  ((unsigned long) hbuf[0] << 24)
            | ((unsigned long) hbuf[1] << 16)
            | ((unsigned long) hbuf[2] << 8)
            | (unsigned long) hbuf[3];

#ifdef DEBUG_PARSE
  fprintf(stderr, "fr().hdr: 0x%08x\n", fr().hdr);
#endif
  if (fr().oldHdr != fr().hdr || !fr().oldHdr) {
    i = 0;
  init_resync:
#ifdef DEBUG_PARSE
    fprintf(stderr, "init_resync: fr().hdr: 0x%08x\n", fr().hdr);
#endif
    if (   (fr().hdr & 0xffe00000) != 0xffe00000
	|| (fr().hdr & 0x00060000) == 0 // undefined 'layer' field
	|| (fr().hdr & 0x0000F000) == 0 // 'free format' bitrate index
	|| (fr().hdr & 0x0000F000) == 0x0000F000 // undefined bitrate index
	|| (fr().hdr & 0x00000C00) == 0x00000C00 // undefined frequency index
	|| (fr().hdr & 0x00000003) != 0x00000000 // 'emphasis' field unexpectedly set
       ) {
      /* RSF: Do the following test even if we're not at the
	 start of the file, in case we have two or more
	 separate MP3 files cat'ed together:
      */
      /* Check for RIFF hdr */
      if (fr().hdr == ('R'<<24)+('I'<<16)+('F'<<8)+'F') {
	unsigned char buf[70 /*was: 40*/];
#ifdef DEBUG_ERRORS
	fprintf(stderr,"Skipped RIFF header\n");
#endif
	readFromStream(buf, 66); /* already read 4 */
	goto read_again;
      }
      /* Check for ID3 hdr */
      if ((fr().hdr&0xFFFFFF00) == ('I'<<24)+('D'<<16)+('3'<<8)) {
	unsigned tagSize, bytesToSkip;
	unsigned char buf[1000];
	readFromStream(buf, 6); /* already read 4 */
	tagSize = ((buf[2]&0x7F)<<21) + ((buf[3]&0x7F)<<14) + ((buf[4]&0x7F)<<7) + (buf[5]&0x7F);
	bytesToSkip = tagSize;
	while (bytesToSkip > 0) {
	  unsigned bytesToRead = sizeof buf;
	  if (bytesToRead > bytesToSkip) {
	    bytesToRead = bytesToSkip;
	  }
	  readFromStream(buf, bytesToRead);
	  bytesToSkip -= bytesToRead;
	}
#ifdef DEBUG_ERRORS
	fprintf(stderr,"Skipped %d-byte ID3 header\n", tagSize);
#endif
	goto read_again;
      }
      /* give up after 20,000 bytes */
      if (i++ < 20000/*4096*//*1024*/) {
	memmove (&hbuf[0], &hbuf[1], 3);
	if (readFromStream(hbuf+3,1) != 1) {
	  return False;
	}
	fr().hdr <<= 8;
	fr().hdr |= hbuf[3];
	fr().hdr &= 0xffffffff;
#ifdef DEBUG_PARSE
	fprintf(stderr, "calling init_resync %d\n", i);
#endif
	goto init_resync;
      }
#ifdef DEBUG_ERRORS
      fprintf(stderr,"Giving up searching valid MPEG header\n");
#endif
      return False;

#ifdef DEBUG_ERRORS
      fprintf(stderr,"Illegal Audio-MPEG-Header 0x%08lx at offset 0x%lx.\n",
	      fr().hdr,tell_stream(str)-4);
#endif
      /* Read more bytes until we find something that looks
	 reasonably like a valid header.  This is not a
	 perfect strategy, but it should get us back on the
	 track within a short time (and hopefully without
	 too much distortion in the audio output).  */
      do {
#ifdef DEBUG_ERRORS
	attempt++;
#endif
	memmove (&hbuf[0], &hbuf[1], 7);
	if (readFromStream(&hbuf[3],1) != 1) {
	  return False;
	}

	/* This is faster than combining fr().hdr from scratch */
	fr().hdr = ((fr().hdr << 8) | hbuf[3]) & 0xffffffff;

	if (!fr().oldHdr)
	  goto init_resync;       /* "considered harmful", eh? */

      } while ((fr().hdr & HDRCMPMASK) != (fr().oldHdr & HDRCMPMASK)
	       && (fr().hdr & HDRCMPMASK) != (fr().firstHdr & HDRCMPMASK));
#ifdef DEBUG_ERRORS
      fprintf (stderr, "Skipped %d bytes in input.\n", attempt);
#endif
    }
    if (!fr().firstHdr) {
      fr().firstHdr = fr().hdr;
    }

    fr().setParamsFromHeader();
    fr().setBytePointer(fr().frameBytes, fr().frameSize);

    fr().oldHdr = fr().hdr;

    if (fr().isFreeFormat) {
#ifdef DEBUG_ERRORS
      fprintf(stderr,"Free format not supported.\n");
#endif
      return False;
    }

#ifdef MP3_ONLY
    if (fr().layer != 3) {
#ifdef DEBUG_ERRORS
      fprintf(stderr, "MPEG layer %d is not supported!\n", fr().layer);
#endif
      return False;
    }
#endif
  }

  if ((l = readFromStream(fr().frameBytes, fr().frameSize))
      != fr().frameSize) {
    if (l == 0) return False;
    memset(fr().frameBytes+1, 0, fr().frameSize-1);
  }

  return True;
}

unsigned MP3StreamState::readFromStream(unsigned char* buf,
					unsigned numChars) {
  return fread(buf, 1, numChars, fFid);
}

#define XING_FRAMES_FLAG       0x0001
#define XING_BYTES_FLAG        0x0002
#define XING_TOC_FLAG          0x0004
#define XING_VBR_SCALE_FLAG    0x0008

void MP3StreamState::checkForXingHeader() {
  // Look for 'Xing' in the first 4 bytes after the 'side info':
  if (fr().frameSize < fr().sideInfoSize) return;
  unsigned bytesAvailable = fr().frameSize - fr().sideInfoSize;
  unsigned char* p = &(fr().frameBytes[fr().sideInfoSize]);

  if (bytesAvailable < 8) return;
  if (p[0] != 'X' || p[1] != 'i' || p[2] != 'n' || p[3] != 'g') return;

  // We found it.
  fIsVBR = True;

  u_int32_t flags = (p[4]<<24) | (p[5]<<16) | (p[6]<<8) | p[7];
  unsigned i = 8;
  bytesAvailable -= 8;

  if (flags&XING_FRAMES_FLAG) {
    // The next 4 bytes are the number of frames:
    if (bytesAvailable < 4) return;
    fNumFramesInFile = (p[i]<<24)|(p[i+1]<<16)|(p[i+2]<<8)|(p[i+3]);
    i += 4; bytesAvailable -= 4;
  }

  if (flags&XING_BYTES_FLAG) {
    // The next 4 bytes is the file size:
    if (bytesAvailable < 4) return;
    fFileSize = (p[i]<<24)|(p[i+1]<<16)|(p[i+2]<<8)|(p[i+3]);
    i += 4; bytesAvailable -= 4;
  }

  if (flags&XING_TOC_FLAG) {
    // Fill in the Xing 'table of contents':
    if (bytesAvailable < XING_TOC_LENGTH) return;
    fHasXingTOC = True;
    for (unsigned j = 0; j < XING_TOC_LENGTH; ++j) {
      fXingTOC[j] = p[i+j];
    }
    i += XING_TOC_FLAG; bytesAvailable -= XING_TOC_FLAG;
  }
}

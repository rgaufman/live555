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
// Copyright (c) 1996-2026 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation

#include "BasicUsageEnvironment.hh"
#include "StreamFaultCodes.hh" // isJournalWorthyLogLine() -- the stdout/stderr split below
#include <stdio.h>
#include <string.h>   // strchr
#include <time.h>     // strftime/localtime_r for the stdout trace timestamp
#include <sys/time.h> // gettimeofday (wall clock: these stamps are read against journald's)

////////// library version constants //////////

extern char const* const BasicUsageEnvironmentLibraryVersionStr = BASICUSAGEENVIRONMENT_LIBRARY_VERSION_STRING;
extern int const BasicUsageEnvironmentLibraryVersionInt = BASICUSAGEENVIRONMENT_LIBRARY_VERSION_INT;

////////// BasicUsageEnvironment //////////

#if defined(__WIN32__) || defined(_WIN32)
extern "C" int initializeWinsockIfNecessary();
#endif

BasicUsageEnvironment::BasicUsageEnvironment(TaskScheduler& taskScheduler)
: BasicUsageEnvironment0(taskScheduler) {
  // Verbose diagnostics now go to stdout (see the log split below), and stdout is FULLY buffered
  // when it is a pipe or a file — the default. That would hold ~4KB of trace before writing any of
  // it, so a log tail would appear frozen and the timestamps of what did appear would be wrong.
  // Line-buffer it so a line written is a line visible, matching stderr's behaviour.
  setvbuf(stdout, NULL, _IOLBF, 0);
#if defined(__WIN32__) || defined(_WIN32)
  if (!initializeWinsockIfNecessary()) {
    setResultErrMsg("Failed to initialize 'winsock': ");
    reportBackgroundError();
    internalError();
  }
#endif
}

BasicUsageEnvironment::~BasicUsageEnvironment() {
}

BasicUsageEnvironment*
BasicUsageEnvironment::createNew(TaskScheduler& taskScheduler) {
  return new BasicUsageEnvironment(taskScheduler);
}

int BasicUsageEnvironment::getErrno() const {
#if defined(__WIN32__) || defined(_WIN32) || defined(_WIN32_WCE)
  return WSAGetLastError();
#else
  return errno;
#endif
}

// Diagnostic output is split by DESTINATION, per line:
//   stderr — lines that begin with a TX- code (every classified fault, plus the TX-LIVE ack).
//            These are what a service journal must always keep.
//   stdout — everything else: verbose protocol tracing, progress, upstream's own chatter.
//            A caller can redirect this to a file when debugging, or discard it.
//
// Why in the library and not in the service command: TetherBox previously did the same filtering in
// the shell (`2>&1 | tee file | grep -E 'TX-|...'`). That put a grep between the proxy and its own
// journal, so anything the pattern did not anticipate — a crash message, an assertion, a libc error
// — was discarded, and the pipeline became a component that could itself fail. Splitting here means
// nothing is ever filtered away: both halves exist, the caller only chooses where each one goes.
//
// Routing is decided ONCE per line, on its first fragment, because operator<< is called per token.
// The decision is then held until a newline is emitted.
//
// The two flags are file-static: live555's event loop is single-threaded and a proxy process has
// exactly one UsageEnvironment, so there is nothing to race. Stated explicitly rather than left
// implicit, because it would be wrong under threads and the next reader deserves to know that.
static bool sLogLineAtStart = true;
static bool sLogLineToJournal = true;

// stdout lines carry their OWN timestamp; stderr lines do not.
//
// Because stderr goes to journald, systemd stamps every line there and adding our own would just
// duplicate it. stdout, though, is redirected straight to a file — and a file redirect has nothing
// to stamp it. The verbose trace was therefore timestamp-free, which made it useless for the one
// question it exists to answer: WHERE did the time go between a fault and the recovery. (Found the
// hard way: a capture taken to measure a 225s reconnect could not measure anything, because the
// journal had the timestamps and the trace had the detail, and neither had both.)
//
// Written as `YYYY-MM-DDTHH:MM:SS.mmm ` to match journald's -o short-iso, so a trace and a journal
// excerpt can be read against each other without converting formats.
static void writeStdoutTimestamp(FILE* out) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  // The seconds part only changes once a second, but this runs once per trace LINE -- hundreds/sec
  // per proxy under verbose capture -- and localtime_r is the expensive piece (glibc takes the tzset
  // lock and walks TZ state on every call). Cache the formatted prefix keyed on the second and
  // re-format only when it changes; the millisecond suffix is always fresh. Same single-threaded
  // justification as the routing flags above.
  static time_t sLastStampSec = 0;
  static char sStampBuf[40];
  static size_t sStampLen = 0;
  time_t const secs = tv.tv_sec;
  if (secs != sLastStampSec || sStampLen == 0) {
    struct tm tmv;
    localtime_r(&secs, &tmv);
    sStampLen = strftime(sStampBuf, sizeof sStampBuf, "%Y-%m-%dT%H:%M:%S", &tmv);
    sLastStampSec = secs;
  }
  if (sStampLen > 0) fprintf(out, "%s.%03d ", sStampBuf, (int)(tv.tv_usec / 1000));
}

static FILE* logStreamFor(char const* firstFragment) {
  bool const atStart = sLogLineAtStart;
  if (sLogLineAtStart) {
    sLogLineToJournal = isJournalWorthyLogLine(firstFragment);
    sLogLineAtStart = false;
  }
  FILE* const out = sLogLineToJournal ? stderr : stdout;
  if (atStart && out == stdout) writeStdoutTimestamp(out);
  return out;
}

// Non-string fragments (an int, a pointer) cannot begin a classifiable line. If one arrives at a
// line start we pass NULL, which isJournalWorthyLogLine() deliberately treats as journal-worthy:
// an unclassifiable line must not be quietly routed to a stream that may be /dev/null.
static FILE* logStreamForNonString() { return logStreamFor(NULL); }

static void noteLogFragmentWritten(char const* str) {
  if (str != NULL && strchr(str, '\n') != NULL) sLogLineAtStart = true;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(char const* str) {
  if (str == NULL) str = "(NULL)"; // sanity check
  fprintf(logStreamFor(str), "%s", str);
  noteLogFragmentWritten(str);
  return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(int i) {
  fprintf(logStreamForNonString(), "%d", i);
  return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(unsigned u) {
  fprintf(logStreamForNonString(), "%u", u);
  return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(double d) {
  fprintf(logStreamForNonString(), "%f", d);
  return *this;
}

UsageEnvironment& BasicUsageEnvironment::operator<<(void* p) {
  fprintf(logStreamForNonString(), "%p", p);
  return *this;
}

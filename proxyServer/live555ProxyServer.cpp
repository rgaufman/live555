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
// Copyright (c) 1996-2026, Live Networks, Inc.  All rights reserved
// LIVE555 Proxy Server
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include <signal.h>

#ifndef MILLION
#define MILLION 1000000
#endif

char const* progName;
UsageEnvironment* env;
UserAuthenticationDatabase* authDB = NULL;
UserAuthenticationDatabase* authDBForREGISTER = NULL;

// Default values of command-line parameters:
int verbosityLevel = 0;
Boolean streamRTPOverTCP = False;
portNumBits tunnelOverHTTPPortNum = 0;
portNumBits rtspServerPortNum = 554;
char* username = NULL;
char* password = NULL;
Boolean proxyREGISTERRequests = False;
char* usernameForREGISTER = NULL;
char* passwordForREGISTER = NULL;
unsigned interPacketGapMaxTime = 10;

// -P: never PAUSE the upstream stream, even when no downstream clients remain.
// Keeps the back-end RTSP session PLAYing forever so the inter-packet-gap watchdog
// (-D) is never disarmed — a buggy camera that ignores/drops a resume can no longer
// wedge us. Intended for recording proxies that must never drop the upstream.
Boolean neverPause = False;

// -S <interval-seconds>: every <interval> seconds, log ONE concise line per proxied
// stream (prefix "STREAM-STATS") so an operator watching the logs can see, at a glance,
// what each stream is doing without attaching a debugger. 0 (or absent) = off. Cheap:
// runs once per interval and only reads counters the proxy already maintains. See the
// statsTick() implementation below for the exact fields and how to read them.
unsigned statsIntervalSeconds = 0;

// Graceful-shutdown flag. On SIGTERM/SIGINT we break the event loop and delete our
// ProxyServerMediaSessions, whose destructors send an RTSP TEARDOWN to the back-end
// camera. Without this, a hard kill (systemd SIGKILL) leaves the camera's RTSP session
// and stream slot allocated until the camera's own session timeout — which starves the
// NEXT proxy's session and wedges the stream (SDP still served, no RTP). Cameras with a
// small cap on concurrent RTSP sessions hit this within one or two restarts.
// doEventLoop() takes an EventLoopWatchVariable* (std::atomic<char>*), not a plain char*.
static EventLoopWatchVariable eventLoopWatchVariable = 0;
static void proxyServerSignalHandler(int /*sig*/) { eventLoopWatchVariable = 1; }

// -e: custom stream-name prefix exposed to downstream clients. When serving a
// single rtsp:// URL the proxy publishes it as "rtsp://.../<prefix>"; for N
// URLs each gets "<prefix>-1" .. "<prefix>-N". Capped to keep the final
// stream name comfortably short and printable-URL-safe.
#define PROXY_STREAM_NAME_PREFIX_MAX 50
char const* streamNamePrefix = "proxyStream";

// -C: credentials required of downstream RTSP clients connecting to this
// proxy. Separate from -u, which are the credentials this proxy presents to
// the back-end (proxied) camera. Populated into authDB below.
char* clientAuthUsername = NULL;
char* clientAuthPassword = NULL;

static RTSPServer* createRTSPServer(Port port) {
  if (proxyREGISTERRequests) {
    return RTSPServerWithREGISTERProxying::createNew(*env, port, authDB, authDBForREGISTER, 65, streamRTPOverTCP, verbosityLevel, username, password);
  } else {
    return RTSPServer::createNew(*env, port, authDB);
  }
}

void usage() {
  *env << "Usage: " << progName
       << " [-v|-V]"
       << " [-t|-T <http-port>]"
       << " [-p <rtspServer-port>]"
       << " [-u <back-end-username> <back-end-password>]"
       << " [-R] [-U <username-for-REGISTER> <password-for-REGISTER>]"
       << " [-D <max-inter-packet-gap-time>]"
       << " [-P]"
       << " [-S <stats-interval-seconds>]"
       << " [-e <stream-name-prefix>]"
       << " [-C <client-username> <client-password>]"
       << " <rtsp-url-1> ... <rtsp-url-n>\n"
       << "  -S <stats-interval-seconds> Every N seconds, log one \"STREAM-STATS\" line\n"
       << "                             per proxied stream (upstream packet/byte deltas,\n"
       << "                             transport, downstream client count). 0/absent = off.\n"
       << "  -e <stream-name-prefix>   Publish streams as <prefix> (single URL) or\n"
       << "                             <prefix>-1..-n (multiple). Max "
       << PROXY_STREAM_NAME_PREFIX_MAX
       << " chars. Default: \"proxyStream\".\n"
       << "  -C <user> <pass>          Require downstream RTSP clients to authenticate\n"
       << "                             with these credentials (digest auth). Separate\n"
       << "                             from -u, which is for the back-end/proxied stream.\n";
  exit(1);
}

// Client data for the periodic per-stream stats task (-S). Holds the same
// proxySessions array main() builds, plus the previous cumulative counters per
// stream so each tick can report a per-interval delta. Heap-allocated in main()
// and never freed (it lives for the whole process, like the event loop itself).
struct StreamStatsCtx {
  ServerMediaSession** sessions; // the ProxyServerMediaSessions, as created in main()
  int numSessions;
  unsigned intervalSeconds;
  portNumBits serverPortNum;     // the RTSP server port; always the one requested, or we exited
  // Previous cumulative counters per stream, index-aligned with sessions[]. One struct per
  // stream (not parallel arrays) so the two values can never drift out of index-sync.
  struct PrevCounters { unsigned packets; double kBytes; };
  PrevCounters* prev;
};

// Log one concise line per proxied stream, then re-arm for the next interval (same
// self-rescheduling pattern as ProxyRTSPClient::checkInterPacketGaps). Runs on the
// event loop thread, once per interval only — no work is added to the hot path.
//
// Fields per line (all "since last tick" values are deltas of cumulative counters):
//   name=     the local stream name we publish downstream (e.g. "proxyStream")
//   url=      the back-end (proxied) rtsp:// URL
//   port=     our RTSP server port (where downstream clients connect)
//   transport=TCP|UDP  how we pull RTP from the back-end (camera) side
//   rx_pkts=  RTP packets received FROM the back-end this interval (0 => camera side stalled)
//   rx_kb=    RTP payload KBytes received from the back-end this interval
//   clients=  downstream RTSP client sessions currently connected (the consumers)
//   state=    LIVE once the back-end DESCRIBE has completed, else CONNECTING
static void statsTick(void* clientData) {
  StreamStatsCtx* ctx = (StreamStatsCtx*)clientData;

  for (int i = 0; i < ctx->numSessions; ++i) {
    ProxyServerMediaSession* psms = (ProxyServerMediaSession*)(ctx->sessions[i]);
    if (psms == NULL) continue;

    ProxyServerMediaSession::StreamStats s;
    psms->getStreamStats(s);

    // A back-end reset (inter-packet-gap watchdog reconnect) rebuilds the client
    // session, so its cumulative counters restart at 0. When current < previous the
    // session was re-established this interval; report the fresh count rather than
    // underflowing the unsigned subtraction.
    StreamStatsCtx::PrevCounters& prev = ctx->prev[i];
    unsigned pktDelta = s.totNumPacketsReceived >= prev.packets
      ? s.totNumPacketsReceived - prev.packets
      : s.totNumPacketsReceived;
    double kbDelta = s.totNumKBytesReceived >= prev.kBytes
      ? s.totNumKBytesReceived - prev.kBytes
      : s.totNumKBytesReceived;
    prev.packets = s.totNumPacketsReceived;
    prev.kBytes = s.totNumKBytesReceived;

    // referenceCount() is bumped once per downstream RTSP client session that has
    // SETUP this stream and decremented when it goes away, so it is exactly the
    // current downstream fan-out (recorder/detector/HLS/etc.). A DESCRIBE bumps it
    // only transiently within a single request handler, so it is accurate here.
    unsigned downstreamClients = psms->referenceCount();

    *env << "STREAM-STATS name=" << psms->streamName()
         << " url=" << psms->url()
         << " port=" << ctx->serverPortNum
         << " transport=" << (s.streamRTPOverTCP ? "TCP" : "UDP")
         << " rx_pkts=" << pktDelta
         << " rx_kb=" << kbDelta
         << " clients=" << downstreamClients
         << " state=" << (s.describeCompleted ? "LIVE" : "CONNECTING")
         << "\n";
  }

  // Re-arm for the next interval.
  env->taskScheduler().scheduleDelayedTask((int64_t)ctx->intervalSeconds*MILLION, statsTick, ctx);
}

int main(int argc, char** argv) {
  // Increase the maximum size of video frames that we can 'proxy' without truncation.
  // (Such frames are unreasonably large; the back-end servers should really not be sending frames this large!)
  OutPacketBuffer::maxSize = 2000000; // bytes

  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  *env << "LIVE555 Proxy Server\n"
       << "\t(LIVE555 Streaming Media library version "
       << LIVEMEDIA_LIBRARY_VERSION_STRING
       << "; licensed under the GNU LGPL)\n\n";

  // Check command-line arguments: optional parameters, then one or more rtsp:// URLs (of streams to be proxied):
  progName = argv[0];
  if (argc < 2) usage();
  while (argc > 1) {
    // Process initial command-line options (beginning with "-"):
    char* const opt = argv[1];
    if (opt[0] != '-') break; // the remaining parameters are assumed to be "rtsp://" URLs

    switch (opt[1]) {
    case 'v': { // verbose output
      verbosityLevel = 1;
      break;
    }

    case 'V': { // more verbose output
      verbosityLevel = 2;
      break;
    }

    case 't': {
      // Stream RTP and RTCP over the TCP 'control' connection.
      // (This is for the 'back end' (i.e., proxied) stream only.)
      streamRTPOverTCP = True;
      break;
    }

    case 'T': {
      // stream RTP and RTCP over a HTTP connection
      if (argc > 2 && argv[2][0] != '-') {
	// The next argument is the HTTP server port number:
	if (sscanf(argv[2], "%hu", &tunnelOverHTTPPortNum) == 1
	    && tunnelOverHTTPPortNum > 0) {
	  ++argv; --argc;
	  break;
	}
      }

      // If we get here, the option was specified incorrectly:
      usage();
      break;
    }

    case 'p': {
      // specify a rtsp server port number
      if (argc > 2 && argv[2][0] != '-') {
        // The next argument is the rtsp server port number:
        if (sscanf(argv[2], "%hu", &rtspServerPortNum) == 1
            && rtspServerPortNum > 0) {
          ++argv; --argc;
          break;
        }
      }

      // If we get here, the option was specified incorrectly:
      usage();
      break;
    }

    case 'u': { // specify a username and password (to be used if the 'back end' (i.e., proxied) stream requires authentication)
      if (argc < 4) usage(); // there's no argv[3] (for the "password")
      username = argv[2];
      password = argv[3];
      argv += 2; argc -= 2;
      break;
    }

    case 'U': { // specify a username and password to use to authenticate incoming "REGISTER" commands
      if (argc < 4) usage(); // there's no argv[3] (for the "password")
      usernameForREGISTER = argv[2];
      passwordForREGISTER = argv[3];

      if (authDBForREGISTER == NULL) authDBForREGISTER = new UserAuthenticationDatabase;
      authDBForREGISTER->addUserRecord(usernameForREGISTER, passwordForREGISTER);
      argv += 2; argc -= 2;
      break;
    }

    case 'R': { // Handle incoming "REGISTER" requests by proxying the specified stream:
      proxyREGISTERRequests = True;
      break;
    }

    case 'e': { // specify a custom stream-name prefix
      if (argc < 3 || argv[2][0] == '-' || argv[2][0] == '\0') usage();
      size_t len = strlen(argv[2]);
      if (len == 0 || len > PROXY_STREAM_NAME_PREFIX_MAX) {
        *env << "Invalid stream-name prefix (must be 1.."
             << PROXY_STREAM_NAME_PREFIX_MAX << " characters)\n";
        usage();
      }
      streamNamePrefix = argv[2];
      ++argv; --argc;
      break;
    }

    case 'C': { // credentials required of downstream clients connecting to this proxy
      if (argc < 4) usage(); // there's no argv[3] (for the "password")
      clientAuthUsername = argv[2];
      clientAuthPassword = argv[3];
      argv += 2; argc -= 2;
      break;
    }

    case 'D': { // specify maximum number of seconds to wait for packets:
      if (argc > 2 && argv[2][0] != '-') {
        if (sscanf(argv[2], "%u", &interPacketGapMaxTime) == 1) {
          ++argv; --argc;
          break;
        }
      }

      // If we get here, the option was specified incorrectly:
      usage();
      break;
    }

    case 'P': { // never PAUSE the upstream stream, even with no downstream clients
      neverPause = True;
      break;
    }

    case 'S': { // periodic per-stream stats logging interval, in seconds
      if (argc > 2 && argv[2][0] != '-') {
        if (sscanf(argv[2], "%u", &statsIntervalSeconds) == 1) {
          ++argv; --argc;
          break;
        }
      }

      // If we get here, the option was specified incorrectly:
      usage();
      break;
    }

    default: {
      usage();
      break;
    }
    }

    ++argv; --argc;
  }
  if (argc < 2 && !proxyREGISTERRequests) usage(); // there must be at least one URL at the end
  // Make sure that the remaining arguments appear to be "rtsp://" (or "rtsps://") URLs:
  int i;
  for (i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "rtsp://", 7) != 0 && strncmp(argv[i], "rtsps://", 8) != 0) usage();
  }
  // Do some additional checking for invalid command-line argument combinations:
  if (authDBForREGISTER != NULL && !proxyREGISTERRequests) {
    *env << "The '-U <username-for-REGISTER> <password-for-REGISTER>' option can be used only with -R\n";
    usage();
  }
  if (streamRTPOverTCP) {
    if (tunnelOverHTTPPortNum > 0) {
      *env << "The -t and -T options cannot both be used!\n";
      usage();
    } else {
      tunnelOverHTTPPortNum = (portNumBits)(~0); // hack to tell "ProxyServerMediaSession" to stream over TCP, but not using HTTP
    }
  }

#ifdef ACCESS_CONTROL
  // To implement client access control to the RTSP server, do the following:
  if (authDB == NULL) authDB = new UserAuthenticationDatabase;
  authDB->addUserRecord("username1", "password1"); // replace these with real strings
      // Repeat this line with each <username>, <password> that you wish to allow access to the server.
#endif

  // If "-C <user> <pass>" was given, require downstream RTSP clients to
  // authenticate with these credentials (digest auth, via live555's standard
  // UserAuthenticationDatabase — same mechanism RTSPServer already uses when
  // any authDB is non-null).
  if (clientAuthUsername != NULL) {
    if (authDB == NULL) authDB = new UserAuthenticationDatabase;
    authDB->addUserRecord(clientAuthUsername, clientAuthPassword);
  }

  // 🚨 ONE RULE, NO EXCEPTIONS: bind the port we were asked for, or die.
  //
  // Stock live555 tried the requested port, then 554, then 8554. There is no port for which that is
  // safe here, and the port number involved is irrelevant to why:
  //
  //   A proxy that binds a DIFFERENT port than the one it was asked for is not a degraded proxy,
  //   it is an INVISIBLE one. It is alive, streaming and healthy, so a supervisor never restarts
  //   it — while every consumer (recorder, HLS, live view, detector) connects to
  //   rtsp://127.0.0.1:<requested-port>/proxyStream, times out forever, and reports the CAMERA as
  //   dead. The real fault is a local bind conflict; the symptom points at the wrong component
  //   entirely, and nothing anywhere logs an error.
  //
  // That applies identically whether the requested port is 11503, 554, 8554 or anything else — a
  // duplicate 8554 is exactly as fatal as a duplicate 11503, and the fallback made 554/8554 the
  // MOST likely ports to collide, since every proxy that lost a race piled onto them. At ~99
  // proxies per box, one conflict produced a mixture of wrong-port and dead proxies with no single
  // symptom leading back to the cause.
  //
  // So there is no fallback and no special case for how the port was chosen. Exiting non-zero is
  // the correct outcome: the supervisor restarts us and we retry the CORRECT port once the
  // transient conflict clears. A real conflict must be resolved, never routed around.
  RTSPServer* rtspServer = createRTSPServer(rtspServerPortNum);
  if (rtspServer == NULL) {
    *env << "FATAL: could not bind the requested RTSP port " << rtspServerPortNum << ": "
         << env->getResultMsg() << "\n"
         << "Refusing to fall back to any other port: every consumer addresses this proxy by the"
         << " port it was asked for, so a different one would be silently unreachable.\n";
    exit(1);
  }

  // Create a proxy for each "rtsp://" URL specified on the command line.
  // Stream name is "<prefix>" for a single URL and "<prefix>-<i>" for many.
  // Buffer holds up to PROXY_STREAM_NAME_PREFIX_MAX + "-" + 10-digit index + NUL.
  ServerMediaSession** proxySessions = new ServerMediaSession*[argc];
  int numProxySessions = 0;
  for (i = 1; i < argc; ++i) {
    char const* proxiedStreamURL = argv[i];
    char streamName[PROXY_STREAM_NAME_PREFIX_MAX + 16];
    if (argc == 2) {
      snprintf(streamName, sizeof streamName, "%s", streamNamePrefix);
    } else {
      snprintf(streamName, sizeof streamName, "%s-%d", streamNamePrefix, i);
    }
    ServerMediaSession* sms
      = ProxyServerMediaSession::createNew(*env, rtspServer,
					   proxiedStreamURL, streamName,
					   username, password, tunnelOverHTTPPortNum, verbosityLevel, -1, NULL, interPacketGapMaxTime, neverPause);
    rtspServer->addServerMediaSession(sms);
    proxySessions[numProxySessions++] = sms;

    char* proxyStreamURL = rtspServer->rtspURL(sms);
    *env << "RTSP stream, proxying the stream \"" << proxiedStreamURL << "\"\n";
    *env << "\tPlay this stream using the URL: " << proxyStreamURL << "\n";
    delete[] proxyStreamURL;
  }

  if (proxyREGISTERRequests) {
    *env << "(We handle incoming \"REGISTER\" requests on port " << rtspServerPortNum << ")\n";
  }

  // Also, attempt to create a HTTP server for RTSP-over-HTTP tunneling.
  // Try first with the default HTTP port (80), and then with the alternative HTTP
  // port numbers (8000 and 8080).

  if (rtspServer->setUpTunnelingOverHTTP(80) || rtspServer->setUpTunnelingOverHTTP(8000) || rtspServer->setUpTunnelingOverHTTP(8080)) {
    *env << "\n(We use port " << rtspServer->httpServerPortNum() << " for optional RTSP-over-HTTP tunneling.)\n";
  } else {
    *env << "\n(RTSP-over-HTTP tunneling is not available.)\n";
  }

  // If "-S <interval>" was given, arm the periodic per-stream stats logger. We do this
  // after the port fallback has resolved so the logged port is the one we actually bound.
  if (statsIntervalSeconds > 0 && numProxySessions > 0) {
    StreamStatsCtx* statsCtx = new StreamStatsCtx;
    statsCtx->sessions = proxySessions;
    statsCtx->numSessions = numProxySessions;
    statsCtx->intervalSeconds = statsIntervalSeconds;
    statsCtx->serverPortNum = rtspServerPortNum;
    statsCtx->prev = new StreamStatsCtx::PrevCounters[numProxySessions](); // zero-initialised
    *env << "\n(Logging per-stream \"STREAM-STATS\" every " << statsIntervalSeconds << " seconds.)\n";
    env->taskScheduler().scheduleDelayedTask((int64_t)statsIntervalSeconds*MILLION, statsTick, statsCtx);
  }

  // Enter the event loop, breaking out cleanly on SIGTERM/SIGINT so we can TEARDOWN.
  // Check the result: if a handler can't be installed we'd silently lose graceful
  // shutdown (and leak the camera's RTSP session on kill), so surface it.
  if (signal(SIGTERM, proxyServerSignalHandler) == SIG_ERR ||
      signal(SIGINT, proxyServerSignalHandler) == SIG_ERR) {
    *env << "Warning: could not install SIGTERM/SIGINT handler; shutdown will not TEARDOWN cleanly.\n";
  }
  env->taskScheduler().doEventLoop(&eventLoopWatchVariable);

  // Graceful shutdown: delete each proxy session so ~ProxyServerMediaSession sends an
  // RTSP TEARDOWN to the back-end camera, freeing its session + stream slot immediately
  // instead of leaking it until the camera's own session timeout. sendTeardownCommand
  // writes the request synchronously, so it reaches the camera without the event loop.
  for (int k = 0; k < numProxySessions; ++k) {
    if (proxySessions[k] != NULL) Medium::close(proxySessions[k]);
  }
  delete[] proxySessions;

  return 0;
}

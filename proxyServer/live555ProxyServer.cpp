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
// LIVE555 Proxy Server
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

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
       << " [-u <username> <password>]"
       << " [-R] [-U <username-for-REGISTER> <password-for-REGISTER>]"
       << " [-D <max-inter-packet-gap-time>]"
       << " <rtsp-url-1> ... <rtsp-url-n>\n";
  exit(1);
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
  authDB = new UserAuthenticationDatabase;
  authDB->addUserRecord("username1", "password1"); // replace these with real strings
      // Repeat this line with each <username>, <password> that you wish to allow access to the server.
#endif

  // Create the RTSP server. Try first with the configured port number,
  // and then with the default port number (554) if different,
  // and then with the alternative port number (8554):
  RTSPServer* rtspServer;
  rtspServer = createRTSPServer(rtspServerPortNum);
  if (rtspServer == NULL) {
    if (rtspServerPortNum != 554) {
      *env << "Unable to create a RTSP server with port number " << rtspServerPortNum << ": " << env->getResultMsg() << "\n";
      *env << "Trying instead with the standard port numbers (554 and 8554)...\n";

      rtspServerPortNum = 554;
      rtspServer = createRTSPServer(rtspServerPortNum);
    }
  }
  if (rtspServer == NULL) {
    rtspServerPortNum = 8554;
    rtspServer = createRTSPServer(rtspServerPortNum);
  }
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }

  // Create a proxy for each "rtsp://" URL specified on the command line:
  for (i = 1; i < argc; ++i) {
    char const* proxiedStreamURL = argv[i];
    char streamName[30];
    if (argc == 2) {
      sprintf(streamName, "%s", "proxyStream"); // there's just one stream; give it this name
    } else {
      sprintf(streamName, "proxyStream-%d", i); // there's more than one stream; distinguish them by name
    }
    ServerMediaSession* sms
      = ProxyServerMediaSession::createNew(*env, rtspServer,
					   proxiedStreamURL, streamName,
					   username, password, tunnelOverHTTPPortNum, verbosityLevel, -1, NULL, interPacketGapMaxTime);
    rtspServer->addServerMediaSession(sms);

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

  // Now, enter the event loop:
  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

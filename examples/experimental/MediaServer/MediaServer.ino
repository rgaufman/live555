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
// Copyright (c) 1996-2019, Live Networks, Inc.  All rights reserved
// LIVE555 Media Server
// main program

#include "DynamicRTSPServer.hh"
#include "BasicUsageEnvironment.hh"
#include "SimpleTaskScheduler.hh"
#include "FileAccess.hh"
#include "WiFi.h"
#include "ESPmDNS.h"
#include "version.hh"
#include "SPI.h"
#include "SD.h"

#define PIN_SD_CARD_CS 13  
#define PIN_SD_CARD_MISO 2
#define PIN_SD_CARD_MOSI 15
#define PIN_SD_CARD_CLK  14

const char *ssid = "ssid";
const char *password = "password";
FileDriverSD fd(PIN_SD_CARD_CS, "/sd/test/"); // use files in /test subdirectory
SimpleTaskScheduler *scheduler = nullptr;
UsageEnvironment *env = nullptr;
UserAuthenticationDatabase *authDB = nullptr;

void setup() {
  Serial.begin(115200);
  // setup vfs for SD on the ESP32
  SPI.begin(PIN_SD_CARD_CLK, PIN_SD_CARD_MISO, PIN_SD_CARD_MOSI, PIN_SD_CARD_CS);
  set555FileDriver(fd);

  // Begin by setting up our usage environment:
  scheduler = SimpleTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // start WIFI
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    *env << "Connecting to WiFi..\n";
  }
  WiFi.setSleep(false);
  *env << "connected with IP: " << AddressString(ourIPv4Address(*env)).val() << "\n";

  // Create the RTSP server.  Try first with the default port number (554),
  // and then with the alternative port number (8554):
  RTSPServer *rtspServer;
  portNumBits rtspServerPortNum = 554;
  rtspServer = DynamicRTSPServer::createNew(*env, rtspServerPortNum, authDB);
  if (rtspServer == NULL) {
    rtspServerPortNum = 8554;
    rtspServer = DynamicRTSPServer::createNew(*env, rtspServerPortNum, authDB);
  }
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    //stop();
    while(true);
  }

  *env << "LIVE555 Media Server\n";
  *env << "\tversion " << MEDIA_SERVER_VERSION_STRING
       << " (LIVE555 Streaming Media library version "
       << LIVEMEDIA_LIBRARY_VERSION_STRING << ").\n";

  char *urlPrefix = rtspServer->rtspURLPrefix();
  *env << "Play streams from this server using the URL\n\t" << urlPrefix
       << "<filename>\nwhere <filename> is a file present in the current "
          "directory.\n";
  *env << "Each file's type is inferred from its name suffix:\n";
  *env << "\t\".264\" => a H.264 Video Elementary Stream file\n";
  *env << "\t\".265\" => a H.265 Video Elementary Stream file\n";
  *env << "\t\".aac\" => an AAC Audio (ADTS format) file\n";
  *env << "\t\".ac3\" => an AC-3 Audio file\n";
  *env << "\t\".amr\" => an AMR Audio file\n";
  *env << "\t\".dv\" => a DV Video file\n";
  *env << "\t\".m4e\" => a MPEG-4 Video Elementary Stream file\n";
  *env << "\t\".mkv\" => a Matroska audio+video+(optional)subtitles file\n";
  *env << "\t\".mp3\" => a MPEG-1 or 2 Audio file\n";
  *env << "\t\".mpg\" => a MPEG-1 or 2 Program Stream (audio+video) file\n";
  *env << "\t\".ogg\" or \".ogv\" or \".opus\" => an Ogg audio and/or video "
          "file\n";
  *env << "\t\".ts\" => a MPEG Transport Stream file\n";
  *env << "\t\t(a \".tsx\" index file - if present - provides server 'trick "
          "play' support)\n";
  *env << "\t\".vob\" => a VOB (MPEG-2 video with AC-3 audio) file\n";
  *env << "\t\".wav\" => a WAV Audio file\n";
  *env << "\t\".webm\" => a WebM audio(Vorbis)+video(VP8) file\n";
  *env << "See http://www.live555.com/mediaServer/ for additional "
          "documentation.\n";

  // Also, attempt to create a HTTP server for RTSP-over-HTTP tunneling.
  // Try first with the default HTTP port (80), and then with the alternative
  // HTTP port numbers (8000 and 8080).

  if (rtspServer->setUpTunnelingOverHTTP(80) ||
      rtspServer->setUpTunnelingOverHTTP(8000) ||
      rtspServer->setUpTunnelingOverHTTP(8080)) {
    *env << "(We use port " << rtspServer->httpServerPortNum()
         << " for optional RTSP-over-HTTP tunneling, or for HTTP live "
            "streaming (for indexed Transport Stream files only).)\n";

    // start multicast DNS (mDNS) 
    if (!MDNS.begin(WiFi.getHostname())) {
        *env << "Error setting up MDNS responder!" << "\n";
        while(1) {
            delay(1000);
        }
    }
    MDNS.addService("http", "tcp", rtspServer->httpServerPortNum());


  } else {
    *env << "(RTSP-over-HTTP tunneling is not available.)\n";
  }
}

void loop() {
  scheduler->SingleStep(); 
}

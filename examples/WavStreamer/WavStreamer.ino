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
// Copyright (c) 2021 Phil Schatzmann
// A simple demo sketch that streams a streamer file via RTP/RTCP

#include "FileAccessSdFat.hh"
#include "RTSPSimpleStreamer.hh"

SimpleWAVStreamer streamer(new FileAccessSdFat());

void setup() {
  Serial.begin(115200);

  auto cfg = streamer.defaultConfig();
  cfg.destinationAddress = "192.168.1.255";
  cfg.filePath = "/test/test.wav";
  cfg.ssid = "SSID";
  cfg.password = "password";
  cfg.isAutoRestart = true;

  if (streamer.begin(cfg)){
    streamer.play();
  }
}


void loop() {
  streamer.singleStep();
}

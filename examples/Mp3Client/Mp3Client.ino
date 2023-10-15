#include "AudioTools.h"  // https://github.com/pschatzmann/arduino-audio-tools
#include "AudioCodecs/CodecMP3Helix.h" // https://github.com/pschatzmann/arduino-libhelix
#include "SimpleClient.hh"

I2SStream i2s; // final output of decoded stream
EncodedAudioStream out_mp3(&i2s, new MP3DecoderHelix()); // Decoding stream
SimpleClient rtsp;

void setup(){
    auto cfg = rtsp.defaultConfig();
    cfg.url = "https://samples.mplayerhq.hu/A-codecs/MP3/01%20-%20Charity%20Case.mp3";
    cfg.output = &out_mp3;
    rtsp.begin(cfg);
}

void loop() {
    rtsp.singleStep();
}
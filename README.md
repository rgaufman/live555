## Arduino LIVE555 Streaming Media 

[LIVE555](http://www.live555.com/) is a set of open source (LGPL) C++ libraries developed by Live Networks, Inc. for multimedia streaming. The libraries support open standards such as RTP/RTCP and RTSP for streaming, and can also manage video RTP payload formats such as MPEG, AAC, AMR, AC-3 and Vorbis.

It is used internally by well-known software such as VLC and mplayer.

In order to make the library available for Arduino I have 

- Created the src directory with symlinks to all source files
- Deactivated exceptions (with the help of #defines)
- Used lwip as network interface
- Replaced printf(stderr, ...) with a separate LOG() method, so that we can handle them properly in Arduino
- Renamed inet.c to inet.cpp so that we can use c++ classes
- Applied plenty of additional corrections, so that the library runs on an Arduino-ESP32 
- Used a [file abstraction layer](https://pschatzmann.github.io/live555/html/class_abstract_file.html#details) to replace the stdio.h operations so that we can use different SD libraries

The biggest challenge was with the last point. Unfortunately Arduino does not provide a proper stdio.h implementation that can be used to access files. On the other hand most projects assume that stdio.h is available and therfore do not use any additional abstraction layer: live555 is here not an exception.

### Use Cases

- Stream Audio or Video to a Microcontroller
- Stream Audio or Video from a Microcontroller and play it e.g. with the help of the VLC Player


### Example Sketch

I have also created a simple mp3 streaming API. Here is an example that uses the [SdFat library](https://github.com/greiman/SdFat) from Bill Greiman to access the files on a SD card:
 

```
#include "FileAccessSdFat.hh"
#include "SimpleStreamer.hh"

SimpleMP3Streamer mp3(new FileAccessSdFat());

void setup() {
  Serial.begin(115200);

  auto cfg = mp3.defaultConfig();
  cfg.destinationAddress = "192.168.1.255";
  cfg.filePath = "/Music/Conquistadores.mp3";
  cfg.isAutoRestart = true;
  cfg.ssid = "SSID";
  cfg.password = "password";

  if (mp3.begin(cfg)){
    mp3.play();
  }
}


void loop() {
  mp3.singleStep();
}
```
In the constructor of the [SimpleMP3Streamer](https://pschatzmann.github.io/live555/html/class_simple_m_p3_streamer.html) we need to indicate the file access class. For starting the streaming we need to provide __a file__ and __the destination ip__ address: ```192.168.1.255``` is a broadcast address. This is done with the help of the [SimpleStreamerConfig](https://pschatzmann.github.io/live555/html/struct_simple_streamer_config.html). 
If we did not start the network yet we need to provide the ssid and password.  The ```cfg.isAutoRestart = true;``` is making shure that when the end of the file is reached that we restart again. I also provide the afterPlayingCallback method which can be used to select a new file and "restart playing"... 

### VLC Player

The VLC Player can be used to receive the streamed music. Just use -> File -> Open Network and enter the ```udp://@:6666``` address. 

### Class Documentation

Here is the [generated class documentation](https://pschatzmann.github.io/arduino-live555/html/classes.html)

### Project Status

I am really struggeling to make this work. The sound is breaking up and I did not find so far what is causing this.

### Installation

For Arduino you can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone pschatzmann/arduino-live555.git
```

I recommend to use the git method, so that you can simply get updates by executing the ```git pull``` command in the arduino-live555 library folder.

For building and running the code on your desktop, please consult the original [build instructions](https://github.com/pschatzmann/arduino-live555/blob/master/BUILD.md)
#pragma once
#include "inet.hh"
#include "WiFi.h"
#include "ArduinoHelper.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"
#include "inet.hh"
#include "liveMedia.hh"
#include "ESPmDNS.h"
#include "SimpleTaskScheduler.hh"


/**
 * @brief Configuration Parameters for the SimpleMP3Streamer class
 * @author Phil Schatzmann
 */
struct RTSPSimpleStreamerConfig {
  const char* filePath = nullptr;
  const char* destinationAddress = nullptr;
  unsigned short rtpPort = 8554;
  unsigned char ttl = 1;  // use a low value, in case routers don't admin scope
  const char* ssid = nullptr;
  const char* password = nullptr;
  void (*afterPlayingCallback)(void* ptr) = nullptr; 
  bool isAutoRestart = false;
  bool ismDNS = false;
  bool isSSM = false; // To stream using "source-specific multicast" (SSM)
  bool isServer = false; // To set up an internal RTSP server  (Note that this RTSP server works for multicast only)
  bool isADUS = false; // Create a 'MP3 RTP' sink from the RTP 'groupsock'
  bool isInterleaveADUS = false; //  Add another filter that interleaves the ADUs before packetizing them

  unsigned short rtcpPort() {
    return rtpPort+1;
  }

};


/**
 * @brief Abstract base class for a Simple API for a Streamer
 * @author Phil Schatzmann
 */
class RTSPSimpleStreamer {
 public:
  RTSPSimpleStreamer(AbstractFile& driver, int granularity=1000) {
    ::setFileDriver(&driver);
    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = SimpleTaskScheduler::createNew(granularity);
    env = BasicUsageEnvironment::createNew(*scheduler);
  }

  RTSPSimpleStreamer(AbstractFile* driver, int granularity=1000) {
    ::setFileDriver(driver);
    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = BasicTaskScheduler::createNew(granularity);
    env = BasicUsageEnvironment::createNew(*scheduler);
  }

  virtual RTSPSimpleStreamerConfig defaultConfig() {
    RTSPSimpleStreamerConfig cfg;
    return cfg;
  }

  virtual bool begin(RTSPSimpleStreamerConfig config) {
    LOG("begin\n");
    this->cfg = config;

    if (cfg.filePath == nullptr) {
      LOG("fileName must be defined\n");
      return false;
    }
    if (cfg.destinationAddress == nullptr) {
      LOG("destinationAddress must be defined\n");
      return false;
    }

    if (cfg.afterPlayingCallback == nullptr) {
      cfg.afterPlayingCallback = afterPlaying;
    }

    // startup WIFI
    startWifi();

    // start mDNS (multicast DNS)
    startmDNS();

    // diplay address
    *env << "Using rtp address:" << cfg.destinationAddress << ":" << cfg.rtpPort << "\n";

    struct sockaddr_storage destinationAddress;
    destinationAddress.ss_family = AF_INET;
    ((sockaddr_in&)destinationAddress).sin_addr.s_addr = our_inet_addr(cfg.destinationAddress);;

    const Port rtpPort(cfg.rtpPort);
    const unsigned short rtcpPortNum = cfg.rtcpPort();
    const Port rtcpPort(rtcpPortNum);

    rtpGroupsock = new Groupsock(*env, destinationAddress, rtpPort, cfg.ttl);
    rtcpGroupsock = new Groupsock(*env, destinationAddress, rtcpPort, cfg.ttl);

    if (cfg.isSSM) {
      rtpGroupsock->multicastSendOnly();
      rtcpGroupsock->multicastSendOnly();
    }

    if (!setupFileFormat()){
        LOG("setupFileFormat failed\n");
      return false;
    }

    if (cfg.isServer) {
      if (!setupServer()) {
        LOG("setupServer failed\n");
        return false;
      }
    }

    return true;
  }

  virtual bool play() = 0;

  /// calls the scheduler's event loop - does not return!
  virtual void doEventLoop() { env->taskScheduler().doEventLoop(); }

  /// execute singleStep for ARDUINO loop()
  virtual void singleStep(unsigned delay=0) { static_cast<SimpleTaskScheduler&>(env->taskScheduler()).SingleStep(delay);}

  virtual const char* hostName() {
    return (const char*)CNAME;
  }

 protected:
  RTSPSimpleStreamerConfig cfg;
  UsageEnvironment* env = nullptr;
  RTPSink* sink = nullptr;
  RTCPInstance* rtcpInstance = nullptr;
  Groupsock* rtpGroupsock = nullptr;
  Groupsock* rtcpGroupsock = nullptr;
  RTSPServer* rtspServer = nullptr;
  MediaSource* source = nullptr;
  unsigned char CNAME[MAX_CNAME_LEN + 1] = {0};

  /// assign source and sink (creats rtcpInstance)
  virtual bool setupFileFormat() = 0;

  // Note that this (attempts to) start a server on the default RTSP server
  virtual bool setupServer() {
    rtspServer = RTSPServer::createNew(*env);
    if (rtspServer == NULL) {
      *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
      return false;
    }
    ServerMediaSession* sms = ServerMediaSession::createNew(
        *env, "testStream", cfg.filePath,
        "Session streamed by \"testStreamer\"", cfg.isSSM);
    sms->addSubsession(
        PassiveServerMediaSubsession::createNew(*sink, rtcpInstance));
    rtspServer->addServerMediaSession(sms);

    char* url = rtspServer->rtspURL(sms);
    *env << "Play this stream using the URL \"" << url << "\"\n";
    delete[] url;
    return true;
  }

  /// Starts the Wifi if necessary and possible
  virtual void startWifi() {
    if (cfg.ssid != nullptr && cfg.password != nullptr &&
        WiFi.status() != WL_CONNECTED) {
        *env << "startWifi\n";
        // Connect to WIFI
        WiFi.begin(cfg.ssid, cfg.password);
        while (WiFi.status() != WL_CONNECTED) {
          delay(1000);
          *env << "Connecting to WiFi..\n";
        }
        //esp_wifi_set_ps(WIFI_PS_NONE);
        WiFi.setSleep(false);

        *env << "connected with IP: " << AddressString(WiFi.localIP()).val() << "\n";
    }
  }

  /// Stars the mDNS server
  virtual void startmDNS(){
    if (cfg.ismDNS){
        if (!MDNS.begin(WiFi.getHostname())) {
          MDNS.addService("rtp", "tcp", cfg.rtpPort);
          MDNS.addService("rtcp", "tcp", cfg.rtcpPort());
        } else {
            *env << "Error setting up MDNS responder!" << "\n";
        }
        *env <<  "mDNS responder started"<< "\n";
    }
  }

  /// static callback method
  static void afterPlaying(void* ptr) {
    if (ptr==nullptr){
      Serial.println("afterPlaying: ptr must not be null");
      return;
    }

    // determin caller object
    RTSPSimpleStreamer* self = (RTSPSimpleStreamer*)ptr;
    *(self->env) << "...done streaming\n";

    self->sink->stopPlaying();

    // End this loop by closing the current source:
    Medium::close(self->source);
    self->source = nullptr;

    // And start another loop:
    if (self->cfg.isAutoRestart) {
      *(self->env) << "Restarting...\n";
      self->play();
    }
  }
};

/**
 * @brief A Simple API for a MP3 Streamer
 * @author Phil Schatzmann
 */
class SimpleMP3Streamer : public RTSPSimpleStreamer {
 public:
  SimpleMP3Streamer(AbstractFile& driver) : RTSPSimpleStreamer(driver) {}

  SimpleMP3Streamer(AbstractFile* driver) : RTSPSimpleStreamer(driver) {}

  bool play() {
    // Open the file as a 'MP3 file source':
    source = MP3FileSource::createNew(*env, cfg.filePath);
    if (source == NULL) {
      *env << "Unable to open file \"" << cfg.filePath
           << "\" as a MP3 file source\n";
      return false;
    }

    if (cfg.isADUS) {
      // Add a filter that converts the source MP3s to ADUs:
      source = ADUFromMP3Source::createNew(*env, framedSource());
      if (source == NULL) {
        *env << "Unable to create a MP3->ADU filter for the source\n";
        return false;
      }

      if (cfg.isInterleaveADUS) {
        // Add another filter that interleaves the ADUs before packetizing them:
        unsigned char interleaveCycle[] = {0, 2, 1,
                                           3};  // or choose your own order...
        unsigned const interleaveCycleSize =
            (sizeof interleaveCycle) / (sizeof(unsigned char));
        Interleaving interleaving(interleaveCycleSize, interleaveCycle);
        source = MP3ADUinterleaver::createNew(*env, interleaving, framedSource());
        if (source == NULL) {
          *env  << "Unable to create an ADU interleaving filter for the source\n";
          return false;
        }
      }
    }

    // Finally, start the streaming:
    *env << "Beginning streaming...\n";
    sink->startPlaying(*framedSource(), cfg.afterPlayingCallback, this);
    return true;
  }

 protected:
  FramedSource* framedSource() {
    return (AudioInputDevice*)source;
  }

  bool setupFileFormat() {
    // Create a 'MP3 RTP' sink from the RTP 'groupsock':
    if (cfg.isADUS) {
      unsigned char rtpPayloadFormat = 96;  // A dynamic payload format code
      sink = MP3ADURTPSink::createNew(*env, rtpGroupsock, rtpPayloadFormat);
    } else {
      sink = MPEG1or2AudioRTPSink::createNew(*env, rtpGroupsock);
    }

    gethostname((char*)CNAME, MAX_CNAME_LEN);
    CNAME[MAX_CNAME_LEN] = '\0';  // just in case
    unsigned estimatedSessionBandwidth = 160;  // in kbps; for RTCP b/w share
    rtcpInstance = RTCPInstance::createNew(*env, rtcpGroupsock,
                                           estimatedSessionBandwidth, CNAME,
                                           sink, NULL, cfg.isSSM);   
    return true;                                 
  }
};

/**
 * @brief A simple API for a PCM Streamer
 * @author Phil Schatzmann
 */
class SimplePCMStreamer : public RTSPSimpleStreamer {
 public:
  SimplePCMStreamer(AbstractFile& driver) : RTSPSimpleStreamer(driver) {}

  SimplePCMStreamer(AbstractFile* driver) : RTSPSimpleStreamer(driver) {}

  bool play() {
    *env << "Beginning streaming...\n";

    if (!setupFile()){
      *env << "Error in file setup...\n";
      return false;
    }

    sink->startPlaying(*pcmSource(), cfg.afterPlayingCallback, this);
    return true;
  }
  void setChannels(unsigned int channels){
    this->numChannels = channels;
  }

  void setBitsPerSample(unsigned char bps){
    bitsPerSample = bps;
  }

  void setAudioFormat(unsigned char fmt){
    audioFormat = fmt;
  }

  void setAudioRate(int rate){
    samplingFrequency = rate; 
  }

 protected:
    unsigned char audioFormat = WA_PCM;
    unsigned char numChannels = 2;
    unsigned char bitsPerSample = 16;
    unsigned samplingFrequency = 44100;

  /// This is file specific - so we do nothing here
  bool setupFileFormat() {
    return true;
  }

  virtual AudioInputDevice *createSource() {
    return AudioInputDevice::createNew(*env, -1,
	    bitsPerSample, numChannels, samplingFrequency,  20U);
  }

  AudioInputDevice* pcmSource(){
    return (AudioInputDevice*) source;
  }

  /// Setup a single file
  virtual bool setupFile() {
    // Open the file as a 'WAV' file:
    if (source==nullptr){
      source = createSource();
    }

    // We handle only 4,8,16,20,24 bits-per-sample audio:
    if (bitsPerSample % 4 != 0 || bitsPerSample < 4 || bitsPerSample > 24 ||
        bitsPerSample == 12) {
      *env << "The input file contains " << bitsPerSample
           << " bit-per-sample audio, which we don't handle\n";
      return false;
    }

    unsigned bitsPerSecond = samplingFrequency * bitsPerSample * numChannels;

    *env << "Audio source parameters:\n\t" << samplingFrequency << " Hz, ";
    *env << bitsPerSample << " bits-per-sample, ";
    *env << numChannels << " channels => ";
    *env << bitsPerSecond << " bits-per-second\n";

    char const* mimeType;
    // by default, unless a static RTP payload type can be used
    unsigned char payloadFormatCode = 96;

    // Add in any filter necessary to transform the data prior to streaming.
    // (This is where any audio compression would get added.)
    switch (audioFormat) {
      case WA_PCM: {
        if (bitsPerSample == 16) {
          // Note that samples in the WAV audio file are in little-endian order.
          // Add a filter that converts from little-endian to network
          // (big-endian) order:
          source = EndianSwap16::createNew(*env, pcmSource());
          if (source == NULL) {
            *env << "Unable to create a little->bit-endian order filter from "
                    "the PCM audio source: "
                 << env->getResultMsg() << "\n";
            return false;
          }
          *env << "Converting to network byte order for streaming\n";
          mimeType = "L16";
          if (samplingFrequency == 44100 && numChannels == 2) {
            payloadFormatCode = 10;  // a static RTP payload type
          } else if (samplingFrequency == 44100 && numChannels == 1) {
            payloadFormatCode = 11;  // a static RTP payload type
          }
        } else if (bitsPerSample == 20 || bitsPerSample == 24) {
          // Add a filter that converts from little-endian to network
          // (big-endian) order:
          source = EndianSwap24::createNew(*env, pcmSource());
          if (source == NULL) {
            *env << "Unable to create a little->bit-endian order filter from "
                    "the PCM audio source: "
                 << env->getResultMsg() << "\n";
            return false;
          }
          *env << "Converting to network byte order for streaming\n";
          mimeType = bitsPerSample == 20 ? "L20" : "L24";
        } else {
          // bitsPerSample == 8 (we assume that bitsPerSample == 4 is
          // only for WA_IMA_ADPCM)
          // Don't do any transformation; send the 8-bit PCM data 'as is':
          mimeType = "L8";
        }
        break;

        case WA_PCMU: {
          mimeType = "PCMU";
          if (samplingFrequency == 8000 && numChannels == 1) {
            payloadFormatCode = 0;  // a static RTP payload type
          }
        } break;

        case WA_PCMA: {
          if (samplingFrequency == 8000 && numChannels == 1) {
            payloadFormatCode = 8;  // a static RTP payload type
          }
        } break;

        case WA_IMA_ADPCM: {
          mimeType = "DVI4";
          // Use a static payload type, if one is defined:
          if (numChannels == 1) {
            if (samplingFrequency == 8000) {
              payloadFormatCode = 5;  // a static RTP payload type
            } else if (samplingFrequency == 16000) {
              payloadFormatCode = 6;  // a static RTP payload type
            } else if (samplingFrequency == 11025) {
              payloadFormatCode = 16;  // a static RTP payload type
            } else if (samplingFrequency == 22050) {
              payloadFormatCode = 17;  // a static RTP payload type
            }
          }
        } break;

        default:
          *env << "Unknown audio format code \"" << mimeType << "\" in WAV file header\n";
          return false;
      }
    }

    *env << "Using mime: \"" << audioFormat << "\" \n";
    sink = SimpleRTPSink::createNew(*env, rtpGroupsock, payloadFormatCode, samplingFrequency,
			       "audio", mimeType, numChannels);

    unsigned estimatedSessionBandwidth = (bitsPerSecond + 500)/1000; 
    rtcpInstance = RTCPInstance::createNew(*env, rtcpGroupsock,
			      estimatedSessionBandwidth, CNAME,
			      sink, NULL /* we're a server */,
			      cfg.isSSM /* we're a SSM source*/);

    return true;
  }
};

/**
 * @brief A simple API for a WAV Streamer
 * @author Phil Schatzmann
 */
class SimpleWAVStreamer : public SimplePCMStreamer {
 public:
  SimpleWAVStreamer(AbstractFile& driver) : SimplePCMStreamer(driver) {}

  SimpleWAVStreamer(AbstractFile* driver) : SimplePCMStreamer(driver) {}

 protected:
  virtual AudioInputDevice *createSource() {
    return WAVAudioFileSource::createNew(*env, cfg.filePath);
  }

  WAVAudioFileSource* wavSource(){
    return (WAVAudioFileSource*)source;
  }

  /// Setup a single file
  bool setupFile() {
    // Open the file as a 'WAV' file:
    source = createSource();

    if (source == NULL) {
      *env << "Unable to open file \"" << cfg.filePath
           << "\" as a WAV audio file source: " << env->getResultMsg() << "\n";
      return false;
    }

    // Get attributes of the audio source:
    audioFormat = wavSource()->getAudioFormat();
    bitsPerSample = wavSource()->bitsPerSample();
    samplingFrequency = wavSource()->samplingFrequency();
    numChannels = wavSource()->numChannels();

    return SimplePCMStreamer::setupFile();
  }
};
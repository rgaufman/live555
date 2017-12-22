cmake_minimum_required ( VERSION 3.0 )

set ( PROJECT_NAME live555 )

project ( ${PROJECT_NAME} )

set ( ${PROJECT_NAME}_VERSION_MAJOR "2017" )
set ( ${PROJECT_NAME}_VERSION_MINOR "10" )
set ( ${PROJECT_NAME}_VERSION_PATCH "28" )
set ( ${PROJECT_NAME}_VERSION "${${PROJECT_NAME}_VERSION_MAJOR}.${${PROJECT_NAME}_VERSION_MINOR}.${${PROJECT_NAME}_VERSION_PATCH}" )

if ( CMAKE_COMPILER_IS_GNUCXX )
   set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive" )
endif ()

if ( NOT DEFINED BUILD_SHARED_LIBS )
   set ( BUILD_SHARED_LIBS TRUE )
endif ()

set ( ${PROJECT_NAME}_HOME ${CMAKE_CURRENT_SOURCE_DIR} )
include_directories ( ${${PROJECT_NAME}_HOME}/UsageEnvironment/include )
include_directories ( ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include )
include_directories ( ${${PROJECT_NAME}_HOME}/groupsock/include )
include_directories ( ${${PROJECT_NAME}_HOME}/liveMedia/include )

if ( WIN32 )
   list ( APPEND ${PROJECT_NAME}_DEFINITIONS
      -DWINNT
   )
else ()
   list ( APPEND ${PROJECT_NAME}_DEFINITIONS
      -DSOCKLEN_T=socklen_t
   )
   if ( APPLE )
      list ( APPEND ${PROJECT_NAME}_DEFINITIONS
         -DHAVE_SOCKADDR_LEN=1
         -DTIME_BASE=int
      )
   endif ()
endif ()

if ( BUILD_SHARED_LIBS )
   list ( APPEND ${PROJECT_NAME}_DEFINITIONS
      -DLIVEMEDIA_EXPORTS
   )
else ()
   list ( APPEND ${PROJECT_NAME}_DEFINITIONS
      -DLIVEMEDIA_STATIC
   )
endif ()
add_definitions ( ${${PROJECT_NAME}_DEFINITIONS} )


list ( APPEND ${PROJECT_NAME}_HEADERS_USAGEENVIRONMENT
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/include/Boolean.hh
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/include/HashTable.hh
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/include/strDup.hh
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/include/UsageEnvironment.hh
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/include/UsageEnvironment_version.hh
)
# Group (Filter) source files
source_group ( "Header Files\\UsageEnvironment" FILES ${${PROJECT_NAME}_HEADERS_USAGEENVIRONMENT} )

list ( APPEND ${PROJECT_NAME}_HEADERS_BASICUSAGEENVIRONMENT
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include/BasicHashTable.hh
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include/BasicUsageEnvironment.hh
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include/BasicUsageEnvironment_version.hh
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include/BasicUsageEnvironment0.hh
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include/DelayQueue.hh
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/include/HandlerSet.hh
)
# Group (Filter) source files
source_group ( "Header Files\\BasicUsageEnvironment" FILES ${${PROJECT_NAME}_HEADERS_BASICUSAGEENVIRONMENT} )

list ( APPEND ${PROJECT_NAME}_HEADERS_GROUPSOCK
   ${${PROJECT_NAME}_HOME}/groupsock/include/GroupEId.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/Groupsock.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/groupsock_version.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/GroupsockHelper.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/IOHandlers.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/NetAddress.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/NetCommon.h
   ${${PROJECT_NAME}_HOME}/groupsock/include/NetInterface.hh
   ${${PROJECT_NAME}_HOME}/groupsock/include/TunnelEncaps.hh
)
# Group (Filter) source files
source_group ( "Header Files\\groupsock" FILES ${${PROJECT_NAME}_HEADERS_GROUPSOCK} )

list ( APPEND ${PROJECT_NAME}_HEADERS_LIVEMEDIA
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AC3AudioFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AC3AudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AC3AudioRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AC3AudioStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ADTSAudioFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ADTSAudioFileSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AMRAudioFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AMRAudioFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AMRAudioFileSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AMRAudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AMRAudioRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AMRAudioSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AudioInputDevice.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/AVIFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/Base64.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/BasicUDPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/BasicUDPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/BitVector.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ByteStreamFileSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ByteStreamMemoryBufferSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ByteStreamMultiFileSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/DeviceSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/DigestAuthentication.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/DVVideoFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/DVVideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/DVVideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/DVVideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/export.h
   ${${PROJECT_NAME}_HOME}/liveMedia/include/FileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/FileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/FramedFileSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/FramedFilter.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/FramedSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/GenericMediaServer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/GSMAudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H261VideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H263plusVideoFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H263plusVideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H263plusVideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H263plusVideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264or5VideoFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264or5VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264or5VideoStreamDiscreteFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264or5VideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264VideoFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264VideoFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264VideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264VideoStreamDiscreteFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H264VideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H265VideoFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H265VideoFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H265VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H265VideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H265VideoStreamDiscreteFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/H265VideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/InputFile.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/JPEGVideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/JPEGVideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/JPEGVideoSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/liveMedia.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/liveMedia_version.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/Locale.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MatroskaFile.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MatroskaFileServerDemux.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/Media.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MediaSession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MediaSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MediaSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MediaTranscodingTable.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3ADU.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3ADUinterleaving.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3ADURTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3ADURTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3ADUTranscoder.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3AudioFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3FileSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MP3Transcoder.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2AudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2AudioRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2AudioStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2Demux.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2DemuxedElementaryStream.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2DemuxedServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2FileServerDemux.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2VideoFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2VideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2VideoStreamDiscreteFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG1or2VideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2IndexFromTransportStream.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamAccumulator.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamFromESSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamFromPESSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamIndexFile.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamMultiplexor.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportStreamTrickModeFilter.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG2TransportUDPServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4ESVideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4ESVideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4GenericRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4GenericRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4LATMAudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4LATMAudioRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4VideoFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4VideoStreamDiscreteFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEG4VideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MPEGVideoStreamFramer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MultiFramedRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/MultiFramedRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/OggFile.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/OggFileServerDemux.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/OggFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/OnDemandServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ourMD5.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/OutputFile.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/PassiveServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ProxyServerMediaSession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/QCELPAudioRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/QuickTimeFileSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/QuickTimeGenericRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTCP.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTPInterface.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTSPClient.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTSPCommon.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTSPRegisterSender.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTSPServer.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/RTSPServerSupportingHTTPStreaming.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/ServerMediaSession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/SimpleRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/SimpleRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/SIPClient.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/StreamReplicator.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/T140TextRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/TCPStreamSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/TextRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/TheoraVideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/TheoraVideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/uLawAudioFilter.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VorbisAudioRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VorbisAudioRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VP8VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VP8VideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VP9VideoRTPSink.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/VP9VideoRTPSource.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/WAVAudioFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/include/WAVAudioFileSource.hh
)
# Group (Filter) source files
source_group ( "Header Files\\liveMedia" FILES ${${PROJECT_NAME}_HEADERS_LIVEMEDIA} )

list ( APPEND ${PROJECT_NAME}_HEADERS
   ${${PROJECT_NAME}_HEADERS_USAGEENVIRONMENT}
   ${${PROJECT_NAME}_HEADERS_BASICUSAGEENVIRONMENT}
   ${${PROJECT_NAME}_HEADERS_GROUPSOCK}
   ${${PROJECT_NAME}_HEADERS_LIVEMEDIA}
)


# C/C++-Sources
list ( APPEND ${PROJECT_NAME}_SOURCES_USAGEENVIRONMENT
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/HashTable.cpp
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/strDup.cpp
   ${${PROJECT_NAME}_HOME}/UsageEnvironment/UsageEnvironment.cpp
)
# Group (Filter) source files
source_group ( "Source Files\\UsageEnvironment" FILES ${${PROJECT_NAME}_SOURCES_USAGEENVIRONMENT} )

# C/C++-Sources
list ( APPEND ${PROJECT_NAME}_SOURCES_BASICUSAGEENVIRONMENT
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/BasicHashTable.cpp
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/BasicTaskScheduler.cpp
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/BasicTaskScheduler0.cpp
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/BasicUsageEnvironment.cpp
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/BasicUsageEnvironment0.cpp
   ${${PROJECT_NAME}_HOME}/BasicUsageEnvironment/DelayQueue.cpp
)
# Group (Filter) source files
source_group ( "Source Files\\BasicUsageEnvironment" FILES ${${PROJECT_NAME}_SOURCES_BASICUSAGEENVIRONMENT} )

# C/C++-Sources
list ( APPEND ${PROJECT_NAME}_SOURCES_GROUPSOCK
   ${${PROJECT_NAME}_HOME}/groupsock/GroupEId.cpp
   ${${PROJECT_NAME}_HOME}/groupsock/Groupsock.cpp
   ${${PROJECT_NAME}_HOME}/groupsock/GroupsockHelper.cpp
   ${${PROJECT_NAME}_HOME}/groupsock/inet.c
   ${${PROJECT_NAME}_HOME}/groupsock/IOHandlers.cpp
   ${${PROJECT_NAME}_HOME}/groupsock/NetAddress.cpp
   ${${PROJECT_NAME}_HOME}/groupsock/NetInterface.cpp
)
# Group (Filter) source files
source_group ( "Source Files\\groupsock" FILES ${${PROJECT_NAME}_SOURCES_GROUPSOCK} )

# C/C++-Sources
list ( APPEND ${PROJECT_NAME}_SOURCES_LIVEMEDIA
   ${${PROJECT_NAME}_HOME}/liveMedia/AC3AudioFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AC3AudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AC3AudioRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AC3AudioStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ADTSAudioFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ADTSAudioFileSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AMRAudioFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AMRAudioFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AMRAudioFileSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AMRAudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AMRAudioRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AMRAudioSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AudioInputDevice.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/AVIFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/Base64.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/BasicUDPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/BasicUDPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/BitVector.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ByteStreamFileSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ByteStreamMemoryBufferSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ByteStreamMultiFileSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/DeviceSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/DigestAuthentication.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/DVVideoFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/DVVideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/DVVideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/DVVideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/EBMLNumber.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/EBMLNumber.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/FileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/FileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/FramedFileSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/FramedFilter.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/FramedSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/GenericMediaServer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/GSMAudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H261VideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H263plusVideoFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H263plusVideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H263plusVideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H263plusVideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H263plusVideoStreamParser.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H263plusVideoStreamParser.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/H264or5VideoFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264or5VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264or5VideoStreamDiscreteFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264or5VideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264VideoFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264VideoFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264VideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264VideoStreamDiscreteFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H264VideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H265VideoFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H265VideoFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H265VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H265VideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H265VideoStreamDiscreteFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/H265VideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/InputFile.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/JPEGVideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/JPEGVideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/JPEGVideoSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/Locale.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaDemuxedTrack.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaDemuxedTrack.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaFile.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaFileParser.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaFileParser.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaFileServerDemux.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MatroskaFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/Media.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MediaSession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MediaSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MediaSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADU.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADUdescriptor.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADUdescriptor.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADUinterleaving.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADURTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADURTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3ADUTranscoder.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3AudioFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3AudioMatroskaFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3AudioMatroskaFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3FileSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3Internals.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3Internals.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3InternalsHuffman.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3InternalsHuffman.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3InternalsHuffmanTable.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3StreamState.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3StreamState.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MP3Transcoder.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2AudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2AudioRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2AudioStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2Demux.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2DemuxedElementaryStream.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2DemuxedServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2FileServerDemux.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2VideoFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2VideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2VideoStreamDiscreteFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG1or2VideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2IndexFromTransportStream.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamAccumulator.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamFromESSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamFromPESSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamIndexFile.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamMultiplexor.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportStreamTrickModeFilter.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG2TransportUDPServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4ESVideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4ESVideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4GenericRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4GenericRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4LATMAudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4LATMAudioRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4VideoFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4VideoStreamDiscreteFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEG4VideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEGVideoStreamFramer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEGVideoStreamParser.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MPEGVideoStreamParser.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/MultiFramedRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/MultiFramedRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OggDemuxedTrack.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OggDemuxedTrack.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFile.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFileParser.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFileParser.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFileServerDemux.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFileServerMediaSubsession.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/OggFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OnDemandServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ourMD5.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/OutputFile.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/PassiveServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ProxyServerMediaSession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/QCELPAudioRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/QuickTimeFileSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/QuickTimeGenericRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTCP.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/rtcp_from_spec.c
   ${${PROJECT_NAME}_HOME}/liveMedia/rtcp_from_spec.h
   ${${PROJECT_NAME}_HOME}/liveMedia/RTPInterface.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTSPClient.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTSPCommon.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTSPRegisterSender.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTSPServer.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTSPServerRegister.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/RTSPServerSupportingHTTPStreaming.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/ServerMediaSession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/SimpleRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/SimpleRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/SIPClient.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/StreamParser.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/StreamParser.hh
   ${${PROJECT_NAME}_HOME}/liveMedia/StreamReplicator.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/T140TextRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/TCPStreamSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/TextRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/TheoraVideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/TheoraVideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/uLawAudioFilter.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VorbisAudioRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VorbisAudioRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VP8VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VP8VideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VP9VideoRTPSink.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/VP9VideoRTPSource.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/WAVAudioFileServerMediaSubsession.cpp
   ${${PROJECT_NAME}_HOME}/liveMedia/WAVAudioFileSource.cpp
)
# Group (Filter) source files
source_group ( "Source Files\\liveMedia" FILES ${${PROJECT_NAME}_SOURCES_LIVEMEDIA} )

list ( APPEND ${PROJECT_NAME}_SOURCES
   ${${PROJECT_NAME}_SOURCES_USAGEENVIRONMENT}
   ${${PROJECT_NAME}_SOURCES_BASICUSAGEENVIRONMENT}
   ${${PROJECT_NAME}_SOURCES_GROUPSOCK}
   ${${PROJECT_NAME}_SOURCES_LIVEMEDIA}
)

add_library ( ${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES} ${${PROJECT_NAME}_HEADERS} )

set ( ${PROJECT_NAME}_LINK_LIBRARIES "" )
if ( WIN32 )
   list ( APPEND ${PROJECT_NAME}_LINK_LIBRARIES
      ws2_32
   )
endif ()
target_link_libraries ( ${PROJECT_NAME} PRIVATE ${${PROJECT_NAME}_LINK_LIBRARIES} )

if ( WIN32 )
   set_target_properties ( ${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d" )
else ()
   set_target_properties ( ${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "-debug" )
endif ()

if(MSVC)
    add_definitions(/FI"export.h")
else()
    # GCC or Clang
    add_definitions(-include export.h)
endif()

# Disable warnings
if ( WIN32 AND NOT CMAKE_COMPILER_IS_GNUCXX )
   list ( APPEND ${PROJECT_NAME}_DISABLE_WARNINGS
      "/wd4018" # 'expression' : signed/unsigned mismatch
      "/wd4244" # 'conversion' conversion from 'type1' to 'type2', possible loss of data
      "/wd4267" # 'var' : conversion from 'size_t' to 'type', possible loss of data
      "/wd4312" # 'operation' : conversion from 'type1' to 'type2' of greater size
      "/wd4334" # 'operator' : result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)
      "/wd4477" # <function> : format string <format-string> requires an argument of type <type>, but variadic argument <position> has type <type> 
      "/wd4996" # 'function': was declared deprecated also 'std::<function name>': Function call with parameters that may be unsafe - this call relies on the caller to check that the passed values are correct.To disable this warning, use -D_SCL_SECURE_NO_WARNINGS.See documentation on how to use Visual C++ 'Checked Iterators'
   )
else ()
   list ( APPEND ${PROJECT_NAME}_DISABLE_WARNINGS
      "-Wno-unused-private-field"            # private field 'identifier' is not used
      "-Wno-self-assign"                     # explicitly assigning a variable of type %0 to itself
      "-Wno-self-assign-field"               # assigning field to itself
      "-Wno-format"                          # format specifies type %0 but the argument has type %1
      "-Wno-int-to-pointer-cast"             # cast to %1 from smaller integer type %0
      "-Wno-array-bounds"                    # array index %0 is before the beginning of the array
      "-Wno-tautological-undefined-compare"  # Reference cannot be bound to dereferenced null pointer in well-defined C++ code; comparison may be assumed to always evaluate to true
   )
endif ()
foreach ( warning IN LISTS ${PROJECT_NAME}_DISABLE_WARNINGS )
   set ( CMAKE_COMMON_COMPILER_FLAGS "${CMAKE_COMMON_COMPILER_FLAGS} ${warning}" )
endforeach ()
set ( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_COMMON_COMPILER_FLAGS}" )
set ( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_COMMON_COMPILER_FLAGS_RELEASE}" )
set ( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${CMAKE_COMMON_COMPILER_FLAGS_DEBUG}" )

set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_COMMON_COMPILER_FLAGS}" )
set ( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_COMMON_COMPILER_FLAGS_RELEASE}" )
set ( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_COMMON_COMPILER_FLAGS_DEBUG}" )

install ( FILES
	${${PROJECT_NAME}_HEADERS_USAGEENVIRONMENT}
	DESTINATION
	"${CMAKE_INSTALL_PREFIX}/include/live555/UsageEnvironment"
)

install ( FILES
	${${PROJECT_NAME}_HEADERS_BASICUSAGEENVIRONMENT}
	DESTINATION
	"${CMAKE_INSTALL_PREFIX}/include/live555/BasicUsageEnvironment"
)

install ( FILES
	${${PROJECT_NAME}_HEADERS_GROUPSOCK}
	DESTINATION
	"${CMAKE_INSTALL_PREFIX}/include/live555/groupsock"
)

install ( FILES
	${${PROJECT_NAME}_HEADERS_LIVEMEDIA}
	DESTINATION
	"${CMAKE_INSTALL_PREFIX}/include/live555/liveMedia"
)

install ( TARGETS
	${PROJECT_NAME}
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)

set(headers
    include/AC3AudioFileServerMediaSubsession.hh
    include/AC3AudioRTPSink.hh
    include/AC3AudioRTPSource.hh
    include/AC3AudioStreamFramer.hh
    include/ADTSAudioFileServerMediaSubsession.hh
    include/ADTSAudioFileSource.hh
    include/ADTSAudioStreamDiscreteFramer.hh
    include/AMRAudioFileServerMediaSubsession.hh
    include/AMRAudioFileSink.hh
    include/AMRAudioFileSource.hh
    include/AMRAudioRTPSink.hh
    include/AMRAudioRTPSource.hh
    include/AMRAudioSource.hh
    include/AVIFileSink.hh
    include/AudioInputDevice.hh
    include/AudioRTPSink.hh
    include/Base64.hh
    include/BasicUDPSink.hh
    include/BasicUDPSource.hh
    include/BitVector.hh
    include/ByteStreamFileSource.hh
    include/ByteStreamMemoryBufferSource.hh
    include/ByteStreamMultiFileSource.hh
    include/DVVideoFileServerMediaSubsession.hh
    include/DVVideoRTPSink.hh
    include/DVVideoRTPSource.hh
    include/DVVideoStreamFramer.hh
    include/DeviceSource.hh
    include/DigestAuthentication.hh
    include/FileServerMediaSubsession.hh
    include/FileSink.hh
    include/FramedFileSource.hh
    include/FramedFilter.hh
    include/FramedSource.hh
    include/GSMAudioRTPSink.hh
    include/GenericMediaServer.hh
    include/H261VideoRTPSource.hh
    include/H263plusVideoFileServerMediaSubsession.hh
    include/H263plusVideoRTPSink.hh
    include/H263plusVideoRTPSource.hh
    include/H263plusVideoStreamFramer.hh
    include/H264VideoFileServerMediaSubsession.hh
    include/H264VideoFileSink.hh
    include/H264VideoRTPSink.hh
    include/H264VideoRTPSource.hh
    include/H264VideoStreamDiscreteFramer.hh
    include/H264VideoStreamFramer.hh
    include/H264or5VideoFileSink.hh
    include/H264or5VideoRTPSink.hh
    include/H264or5VideoStreamDiscreteFramer.hh
    include/H264or5VideoStreamFramer.hh
    include/H265VideoFileServerMediaSubsession.hh
    include/H265VideoFileSink.hh
    include/H265VideoRTPSink.hh
    include/H265VideoRTPSource.hh
    include/H265VideoStreamDiscreteFramer.hh
    include/H265VideoStreamFramer.hh
    include/HLSSegmenter.hh
    include/HMAC_hash.hh
    include/HMAC_SHA1.hh
    include/InputFile.hh
    include/JPEG2000VideoRTPSink.hh
    include/JPEG2000VideoRTPSource.hh
    include/JPEGVideoRTPSink.hh
    include/JPEGVideoRTPSource.hh
    include/JPEGVideoSource.hh
    include/Locale.hh
    include/MP3ADU.hh
    include/MP3ADURTPSink.hh
    include/MP3ADURTPSource.hh
    include/MP3ADUTranscoder.hh
    include/MP3ADUinterleaving.hh
    include/MP3AudioFileServerMediaSubsession.hh
    include/MP3FileSource.hh
    include/MP3Transcoder.hh
    include/MPEG1or2AudioRTPSink.hh
    include/MPEG1or2AudioRTPSource.hh
    include/MPEG1or2AudioStreamFramer.hh
    include/MPEG1or2Demux.hh
    include/MPEG1or2DemuxedElementaryStream.hh
    include/MPEG1or2DemuxedServerMediaSubsession.hh
    include/MPEG1or2FileServerDemux.hh
    include/MPEG1or2VideoFileServerMediaSubsession.hh
    include/MPEG1or2VideoRTPSink.hh
    include/MPEG1or2VideoRTPSource.hh
    include/MPEG1or2VideoStreamDiscreteFramer.hh
    include/MPEG1or2VideoStreamFramer.hh
    include/MPEG2IndexFromTransportStream.hh
    include/MPEG2TransportFileServerMediaSubsession.hh
    include/MPEG2TransportStreamAccumulator.hh
    include/MPEG2TransportStreamDemux.hh
    include/MPEG2TransportStreamFramer.hh
    include/MPEG2TransportStreamFromESSource.hh
    include/MPEG2TransportStreamFromPESSource.hh
    include/MPEG2TransportStreamIndexFile.hh
    include/MPEG2TransportStreamMultiplexor.hh
    include/MPEG2TransportStreamTrickModeFilter.hh
    include/MPEG2TransportUDPServerMediaSubsession.hh
    include/MPEG4ESVideoRTPSink.hh
    include/MPEG4ESVideoRTPSource.hh
    include/MPEG4GenericRTPSink.hh
    include/MPEG4GenericRTPSource.hh
    include/MPEG4LATMAudioRTPSink.hh
    include/MPEG4LATMAudioRTPSource.hh
    include/MPEG4VideoFileServerMediaSubsession.hh
    include/MPEG4VideoStreamDiscreteFramer.hh
    include/MPEG4VideoStreamFramer.hh
    include/MPEGVideoStreamFramer.hh
    include/MatroskaFile.hh
    include/MatroskaFileServerDemux.hh
    include/Media.hh
    include/MediaSession.hh
    include/MediaSink.hh
    include/MediaSource.hh
    include/MediaTranscodingTable.hh
    include/MIKEY.hh
    include/MultiFramedRTPSink.hh
    include/MultiFramedRTPSource.hh
    include/OggFile.hh
    include/OggFileServerDemux.hh
    include/OggFileSink.hh
    include/OnDemandServerMediaSubsession.hh
    include/OutputFile.hh
    include/PassiveServerMediaSubsession.hh
    include/ProxyServerMediaSession.hh
    include/QCELPAudioRTPSource.hh
    include/QuickTimeFileSink.hh
    include/QuickTimeGenericRTPSource.hh
    include/RawVideoFrameParameters.hh
    include/RawVideoRTPSink.hh
    include/RawVideoRTPSource.hh
    include/RTCP.hh
    include/RTPInterface.hh
    include/RTPSink.hh
    include/RTPSource.hh
    include/RTSPClient.hh
    include/RTSPCommon.hh
    include/RTSPRegisterSender.hh
    include/RTSPServer.hh
    include/SIPClient.hh
    include/SRTPCryptographicContext.hh
    include/ServerMediaSession.hh
    include/SimpleRTPSink.hh
    include/SimpleRTPSource.hh
    include/StreamReplicator.hh
    include/T140TextRTPSink.hh
    include/TextRTPSink.hh
    include/TheoraVideoRTPSink.hh
    include/TheoraVideoRTPSource.hh
    include/TLSState.hh
    include/VP8VideoRTPSink.hh
    include/VP8VideoRTPSource.hh
    include/VP9VideoRTPSink.hh
    include/VP9VideoRTPSource.hh
    include/VideoRTPSink.hh
    include/VorbisAudioRTPSink.hh
    include/VorbisAudioRTPSource.hh
    include/WAVAudioFileServerMediaSubsession.hh
    include/WAVAudioFileSource.hh
    include/liveMedia.hh
    include/liveMedia_version.hh
    include/ourMD5.hh
    include/uLawAudioFilter.hh
)
add_library(liveMedia ${live555_library_type}
    ${headers}
    AC3AudioFileServerMediaSubsession.cpp
    AC3AudioRTPSink.cpp
    AC3AudioRTPSource.cpp
    AC3AudioStreamFramer.cpp
    ADTSAudioFileServerMediaSubsession.cpp
    ADTSAudioFileSource.cpp
    ADTSAudioStreamDiscreteFramer.cpp
    AMRAudioFileServerMediaSubsession.cpp
    AMRAudioFileSink.cpp
    AMRAudioFileSource.cpp
    AMRAudioRTPSink.cpp
    AMRAudioRTPSource.cpp
    AMRAudioSource.cpp
    AVIFileSink.cpp
    AudioInputDevice.cpp
    AudioRTPSink.cpp
    Base64.cpp
    BasicUDPSink.cpp
    BasicUDPSource.cpp
    BitVector.cpp
    ByteStreamFileSource.cpp
    ByteStreamMemoryBufferSource.cpp
    ByteStreamMultiFileSource.cpp
    DVVideoFileServerMediaSubsession.cpp
    DVVideoRTPSink.cpp
    DVVideoRTPSource.cpp
    DVVideoStreamFramer.cpp
    DeviceSource.cpp
    DigestAuthentication.cpp
    EBMLNumber.cpp
    EBMLNumber.hh
    FileServerMediaSubsession.cpp
    FileSink.cpp
    FramedFileSource.cpp
    FramedFilter.cpp
    FramedSource.cpp
    GSMAudioRTPSink.cpp
    GenericMediaServer.cpp
    H261VideoRTPSource.cpp
    H263plusVideoFileServerMediaSubsession.cpp
    H263plusVideoRTPSink.cpp
    H263plusVideoRTPSource.cpp
    H263plusVideoStreamFramer.cpp
    H263plusVideoStreamParser.cpp
    H263plusVideoStreamParser.hh
    H264VideoFileServerMediaSubsession.cpp
    H264VideoFileSink.cpp
    H264VideoRTPSink.cpp
    H264VideoRTPSource.cpp
    H264VideoStreamDiscreteFramer.cpp
    H264VideoStreamFramer.cpp
    H264or5VideoFileSink.cpp
    H264or5VideoRTPSink.cpp
    H264or5VideoStreamDiscreteFramer.cpp
    H264or5VideoStreamFramer.cpp
    H265VideoFileServerMediaSubsession.cpp
    H265VideoFileSink.cpp
    H265VideoRTPSink.cpp
    H265VideoRTPSource.cpp
    H265VideoStreamDiscreteFramer.cpp
    H265VideoStreamFramer.cpp
    HLSSegmenter.cpp
    HMAC_SHA1.cpp
    InputFile.cpp
    JPEG2000VideoRTPSink.cpp
    JPEG2000VideoRTPSource.cpp
    JPEGVideoRTPSink.cpp
    JPEGVideoRTPSource.cpp
    JPEGVideoSource.cpp
    Locale.cpp
    MP3ADU.cpp
    MP3ADURTPSink.cpp
    MP3ADURTPSource.cpp
    MP3ADUTranscoder.cpp
    MP3ADUdescriptor.cpp
    MP3ADUdescriptor.hh
    MP3ADUinterleaving.cpp
    MP3AudioFileServerMediaSubsession.cpp
    MP3AudioMatroskaFileServerMediaSubsession.cpp
    MP3AudioMatroskaFileServerMediaSubsession.hh
    MP3FileSource.cpp
    MP3Internals.cpp
    MP3Internals.hh
    MP3InternalsHuffman.cpp
    MP3InternalsHuffman.hh
    MP3InternalsHuffmanTable.cpp
    MP3StreamState.cpp
    MP3StreamState.hh
    MP3Transcoder.cpp
    MPEG1or2AudioRTPSink.cpp
    MPEG1or2AudioRTPSource.cpp
    MPEG1or2AudioStreamFramer.cpp
    MPEG1or2Demux.cpp
    MPEG1or2DemuxedElementaryStream.cpp
    MPEG1or2DemuxedServerMediaSubsession.cpp
    MPEG1or2FileServerDemux.cpp
    MPEG1or2VideoFileServerMediaSubsession.cpp
    MPEG1or2VideoRTPSink.cpp
    MPEG1or2VideoRTPSource.cpp
    MPEG1or2VideoStreamDiscreteFramer.cpp
    MPEG1or2VideoStreamFramer.cpp
    MPEG2IndexFromTransportStream.cpp
    MPEG2TransportFileServerMediaSubsession.cpp
    MPEG2TransportStreamAccumulator.cpp
    MPEG2TransportStreamDemux.cpp
    MPEG2TransportStreamDemuxedTrack.cpp
    MPEG2TransportStreamDemuxedTrack.hh
    MPEG2TransportStreamFramer.cpp
    MPEG2TransportStreamFromESSource.cpp
    MPEG2TransportStreamFromPESSource.cpp
    MPEG2TransportStreamIndexFile.cpp
    MPEG2TransportStreamMultiplexor.cpp
    MPEG2TransportStreamParser.cpp
    MPEG2TransportStreamParser.hh
    MPEG2TransportStreamParser_PAT.cpp
    MPEG2TransportStreamParser_PMT.cpp
    MPEG2TransportStreamParser_STREAM.cpp
    MPEG2TransportStreamTrickModeFilter.cpp
    MPEG2TransportUDPServerMediaSubsession.cpp
    MPEG4ESVideoRTPSink.cpp
    MPEG4ESVideoRTPSource.cpp
    MPEG4GenericRTPSink.cpp
    MPEG4GenericRTPSource.cpp
    MPEG4LATMAudioRTPSink.cpp
    MPEG4LATMAudioRTPSource.cpp
    MPEG4VideoFileServerMediaSubsession.cpp
    MPEG4VideoStreamDiscreteFramer.cpp
    MPEG4VideoStreamFramer.cpp
    MPEGVideoStreamFramer.cpp
    MPEGVideoStreamParser.cpp
    MPEGVideoStreamParser.hh
    MatroskaDemuxedTrack.cpp
    MatroskaDemuxedTrack.hh
    MatroskaFile.cpp
    MatroskaFileParser.cpp
    MatroskaFileParser.hh
    MatroskaFileServerDemux.cpp
    MatroskaFileServerMediaSubsession.cpp
    MatroskaFileServerMediaSubsession.hh
    Media.cpp
    MediaSession.cpp
    MediaSink.cpp
    MediaSource.cpp
    MIKEY.cpp
    MultiFramedRTPSink.cpp
    MultiFramedRTPSource.cpp
    OggDemuxedTrack.cpp
    OggDemuxedTrack.hh
    OggFile.cpp
    OggFileParser.cpp
    OggFileParser.hh
    OggFileServerDemux.cpp
    OggFileServerMediaSubsession.cpp
    OggFileServerMediaSubsession.hh
    OggFileSink.cpp
    OnDemandServerMediaSubsession.cpp
    OutputFile.cpp
    PassiveServerMediaSubsession.cpp
    ProxyServerMediaSession.cpp
    QCELPAudioRTPSource.cpp
    QuickTimeFileSink.cpp
    QuickTimeGenericRTPSource.cpp
    RawVideoRTPSink.cpp
    RawVideoRTPSource.cpp
    RTCP.cpp
    RTPInterface.cpp
    RTPSink.cpp
    RTPSource.cpp
    RTSPClient.cpp
    RTSPCommon.cpp
    RTSPRegisterSender.cpp
    RTSPServer.cpp
    RTSPServerRegister.cpp
    SIPClient.cpp
    SRTPCryptographicContext.cpp
    ServerMediaSession.cpp
    SimpleRTPSink.cpp
    SimpleRTPSource.cpp
    StreamParser.cpp
    StreamParser.hh
    StreamReplicator.cpp
    T140TextRTPSink.cpp
    TextRTPSink.cpp
    TheoraVideoRTPSink.cpp
    TheoraVideoRTPSource.cpp
    TLSState.cpp
    VP8VideoRTPSink.cpp
    VP8VideoRTPSource.cpp
    VP9VideoRTPSink.cpp
    VP9VideoRTPSource.cpp
    VideoRTPSink.cpp
    VorbisAudioRTPSink.cpp
    VorbisAudioRTPSource.cpp
    WAVAudioFileServerMediaSubsession.cpp
    WAVAudioFileSource.cpp
    ourMD5.cpp
    rtcp_from_spec.c
    rtcp_from_spec.h
    uLawAudioFilter.cpp
)
target_include_directories(liveMedia
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/liveMedia/include>
)

if ( LIVE555_MONOLITH_BUILD )
	message(STATUS "build monolith library on liveMedia.")
    if ( MSVC )
        add_definitions(-DLIVEMEDIA_API=__declspec\(dllexport\))
        # add_definitions(/FI"export.h")
        # target_compile_definitions(liveMedia PRIVATE LIVEMEDIA_EXPORTS=1)
        # remove_definitions(-DLIVEMEDIA_STATIC)
        # remove_definitions(-DLIVEMEDIA_IMPORTS)
        # add_definitions(-DLIVEMEDIA_EXPORTS)
    else ()
        add_definitions(-DLIVEMEDIA_API=)
    endif ()
else ()
	# target_compile_definitions(liveMedia PRIVATE LIVEMEDIA_STATIC=1)
	# message(STATUS "build static library on liveMedia")
    add_definitions(-DLIVEMEDIA_API=)
endif ()

# if ( MSVC )
#     add_definitions(/FI"export.h")
# else()
#     # GCC or Clang
#     add_definitions(-include export.h)
# endif()

if ( WIN32 )
    set ( LIB_EXTENSION lib )
else ()
    if( BUILD_SHARED_LIBS )
        #For the shared library:
        set ( LIB_EXTENSION so )
    else()
        #For the static library:
        set ( LIB_EXTENSION a )
    endif()
endif()

target_link_libraries(liveMedia PUBLIC
    live555_cxx_flags
    groupsock
    UsageEnvironment
)
if(LIVE555_EPOLL_SUPPORT)
    target_compile_definitions(liveMedia PRIVATE READ_FROM_FILES_SYNCHRONOUSLY)
endif()
set_target_properties(liveMedia PROPERTIES FOLDER "live555/lib")

if(NOT LIVE555_MONOLITH_BUILD)
    add_library(live555::liveMedia ALIAS liveMedia)
    live555_target_version(liveMedia AUTO)
endif()

install(TARGETS liveMedia EXPORT live555Target
    INCLUDES DESTINATION include/liveMedia
    RUNTIME DESTINATION bin/$<CONFIG>
    LIBRARY DESTINATION lib/$<CONFIG>
    ARCHIVE DESTINATION lib/$<CONFIG>
)
install(FILES ${headers}
    DESTINATION include/liveMedia
)

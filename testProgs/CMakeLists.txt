add_library(announceURL OBJECT announceURL.hh announceURL.cpp)
target_link_libraries(announceURL PUBLIC live555::live555)
set_target_properties(announceURL PROPERTIES FOLDER "live555/testProgs")

function(live555_add_test_executable _target)
    cmake_parse_arguments(arg "ANNOUNCE_URL" "" "" ${ARGN})
    add_executable(${_target} ${arg_UNPARSED_ARGUMENTS})
    target_link_libraries(${_target} PRIVATE live555::live555)
    if(arg_ANNOUNCE_URL)
        target_link_libraries(${_target} PUBLIC announceURL)
    endif()
    set_target_properties(${_target} PROPERTIES FOLDER "live555/testProgs")
    target_include_directories(${_target}
    PUBLIC
        $<BUILD_INTERFACE:${live555_SOURCE_DIR}/liveMedia/include>
    )
    if ( LIVE555_MONOLITH_BUILD )
        # target_compile_definitions(${_target} PRIVATE LIVEMEDIA_EXPORTS=1)
        message(STATUS "using monolith library on ${_target}.")
        if ( MSVC )
            add_definitions(-DLIVEMEDIA_API=__declspec\(dllimport\))
            # add_definitions(/FI"export.h")
            # target_compile_definitions(BasicUsageEnvironment PRIVATE LIVEMEDIA_IMPORTS=1)
            # remove_definitions(-DLIVEMEDIA_STATIC)
            # remove_definitions(-DLIVEMEDIA_EXPORTS)
            # add_definitions(-DLIVEMEDIA_IMPORTS)
        else ()
            add_definitions(-DLIVEMEDIA_API=)
        endif ()
    else ()
        add_definitions(-DLIVEMEDIA_API=)
    endif ()
    # if ( MSVC )
    #     add_definitions(/FI"export.h")
    # else()
    #     # GCC or Clang
    #     add_definitions(-include export.h)
    # endif()
endfunction()

# if ( WIN32 )
#     set ( LIB_EXTENSION lib )
# else ()
#     if( BUILD_SHARED_LIBS )
#         #For the shared library:
#         set ( LIB_EXTENSION so )
#     else()
#         #For the static library:
#         set ( LIB_EXTENSION a )
#     endif()
# endif()

live555_add_test_executable(openRTSP
    openRTSP.cpp
    playCommon.cpp
    playCommon.hh
)
live555_add_test_executable(playSIP
    playSIP.cpp
    playCommon.cpp
    playCommon.hh
)

live555_add_test_executable(testRTSPClient testRTSPClient.cpp)
live555_add_test_executable(testH264VideoStreamer ANNOUNCE_URL testH264VideoStreamer.cpp)
live555_add_test_executable(testOnDemandRTSPServer ANNOUNCE_URL testOnDemandRTSPServer.cpp)

# TODO: No epoll here yet
live555_add_test_executable(MPEG2TransportStreamIndexer MPEG2TransportStreamIndexer.cpp)
live555_add_test_executable(mikeyParse mikeyParse.cpp)
live555_add_test_executable(registerRTSPStream registerRTSPStream.cpp)
live555_add_test_executable(sapWatch sapWatch.cpp)
live555_add_test_executable(testAMRAudioStreamer ANNOUNCE_URL testAMRAudioStreamer.cpp)
live555_add_test_executable(testDVVideoStreamer ANNOUNCE_URL testDVVideoStreamer.cpp)
live555_add_test_executable(testH264VideoToHLSSegments testH264VideoToHLSSegments.cpp)
live555_add_test_executable(testH264VideoToTransportStream testH264VideoToTransportStream.cpp)
live555_add_test_executable(testH265VideoStreamer ANNOUNCE_URL testH265VideoStreamer.cpp)
live555_add_test_executable(testH265VideoToTransportStream testH265VideoToTransportStream.cpp)
live555_add_test_executable(testMKVSplitter testMKVSplitter.cpp)
live555_add_test_executable(testMKVStreamer ANNOUNCE_URL testMKVStreamer.cpp)
live555_add_test_executable(testMP3Receiver testMP3Receiver.cpp)
live555_add_test_executable(testMP3Streamer testMP3Streamer.cpp)
live555_add_test_executable(testMPEG1or2AudioVideoStreamer testMPEG1or2AudioVideoStreamer.cpp)
live555_add_test_executable(testMPEG1or2ProgramToTransportStream testMPEG1or2ProgramToTransportStream.cpp)
live555_add_test_executable(testMPEG1or2Splitter testMPEG1or2Splitter.cpp)
live555_add_test_executable(testMPEG1or2VideoReceiver testMPEG1or2VideoReceiver.cpp)
live555_add_test_executable(testMPEG1or2VideoStreamer testMPEG1or2VideoStreamer.cpp)
live555_add_test_executable(testMPEG2TransportReceiver testMPEG2TransportReceiver.cpp)
live555_add_test_executable(testMPEG2TransportStreamSplitter testMPEG2TransportStreamSplitter.cpp)
live555_add_test_executable(testMPEG2TransportStreamTrickPlay testMPEG2TransportStreamTrickPlay.cpp)
live555_add_test_executable(testMPEG2TransportStreamer testMPEG2TransportStreamer.cpp)
live555_add_test_executable(testMPEG4VideoStreamer ANNOUNCE_URL testMPEG4VideoStreamer.cpp)
live555_add_test_executable(testOggStreamer ANNOUNCE_URL testOggStreamer.cpp)
live555_add_test_executable(testRelay testRelay.cpp)
live555_add_test_executable(testReplicator testReplicator.cpp)
live555_add_test_executable(testWAVAudioStreamer ANNOUNCE_URL testWAVAudioStreamer.cpp)
live555_add_test_executable(vobStreamer ANNOUNCE_URL vobStreamer.cpp)

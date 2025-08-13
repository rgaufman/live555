# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the live555 streaming media library - a C++ RTSP/RTP streaming media library. It's a mature library (copyright 1996-2025) for implementing multimedia streaming applications with support for multiple protocols and media formats.

## Architecture

The codebase is organized into several key directories:

- **liveMedia/**: Core streaming media classes and protocols (RTSP, RTP, RTCP, SIP)
  - Media format support: H.264, H.265, MPEG-1/2, MP3, AAC, JPEG, etc.
  - Stream framers, RTP sinks/sources, file server subsessions
  - Key classes: RTSPClient, RTSPServer, MediaSession, RTPSink, RTPSource

- **groupsock/**: Network socket abstraction layer
  - Groupsock class for UDP multicast/unicast
  - Network interface utilities and address handling

- **UsageEnvironment/**: Base environment classes for event scheduling
  - Abstract base classes: UsageEnvironment, TaskScheduler

- **BasicUsageEnvironment/**: Concrete implementation of usage environment
  - BasicUsageEnvironment, BasicTaskScheduler classes
  - Hash table and delay queue implementations

- **testProgs/**: Example applications and test programs
  - testRTSPClient: Basic RTSP client example
  - openRTSP: Full-featured RTSP client
  - Various streaming test programs for different media formats

- **mediaServer/**: RTSP media server (live555MediaServer)
  - Serves static media files over RTSP
  - Dynamic RTSP server implementation

- **proxyServer/**: RTSP proxy server (live555ProxyServer)
  - Proxies RTSP streams with modifications for buffer sizes and port reuse

- **hlsProxy/**: HLS proxy server

## Build System

The project uses a custom Makefile-based build system:

1. **Generate Makefiles**: `./genMakefiles <platform>`
   - Available platforms: linux, linux-64bit, macosx-bigsur, macosx-catalina, freebsd, openbsd, etc.
   - See `config.*` files for all supported platforms

2. **Build**: `make -j4` (or `make` for single-threaded)

3. **Clean**: `make clean`

4. **Install**: `make install`

5. **Complete clean**: `make distclean`

### Common Build Commands

```bash
# Linux build
./genMakefiles linux
make -j4

# macOS build  
./genMakefiles macosx-bigsur
make -j4

# Build with debug info
./genMakefiles linux-gdb
make -j4
```

## Key Executables

After building, important executables are located in:

- `./testProgs/testRTSPClient` - Basic RTSP client example
- `./testProgs/openRTSP` - Full-featured RTSP client with many options
- `./proxyServer/live555ProxyServer` - RTSP proxy server
- `./mediaServer/live555MediaServer` - RTSP media server for static files
- `./testProgs/testH264VideoStreamer` - H.264 video streaming example
- Various other test programs for different media formats

## Development Notes

- The library requires OpenSSL for TLS/SSL support (linked with -lssl -lcrypto)
- Custom modifications in this fork:
  - OutPacketBuffer::maxSize increased to 2M bytes for buggy IP cameras
  - ALLOW_RTSP_SERVER_PORT_REUSE=1 for port reuse functionality
  - Optional TCP error handling improvements in RTCP.cpp

- Header files follow the pattern: each component has an include/ directory with .hh files
- The main library header is `liveMedia/include/liveMedia.hh` which includes all media classes

## Testing

Run test programs from the testProgs/ directory:

```bash
# Test RTSP client
./testProgs/testRTSPClient rtsp://example.com/stream

# Stream H.264 file
./testProgs/testH264VideoStreamer inputfile.264
```

## Common Development Workflow

1. Generate Makefiles for your platform
2. Make changes to source files
3. Run `make` to build
4. Test using programs in testProgs/
5. For server development, test with mediaServer/ or proxyServer/ executables
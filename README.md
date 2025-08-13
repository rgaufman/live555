# live555 - Enhanced Fork

This is an enhanced fork of the live555 streaming media library with several robustness improvements for production use.

## Build Instructions

```bash
./genMakefiles linux
make -j4
```

Replace "linux" with your platform, e.g. avr32-linux, cygwin, freebsd, iphoneos, linux, linux-64bit, macosx, openbsd, solaris-64bit, etc (see config.PLATFORM files)

## Key Executables

After building, you'll find these important executables:

 * `./testProgs/testRTSPClient` - Basic RTSP client for receiving streams
 * `./testProgs/openRTSP` - Full-featured RTSP client with many options
 * `./proxyServer/live555ProxyServer` - RTSP proxy server with enhanced reliability
 * `./mediaServer/live555MediaServer` - RTSP media server for serving static files

## Enhanced Features

This fork includes several improvements over the upstream live555 library:

### 1. Increased Buffer Sizes
- `OutPacketBuffer::maxSize` increased from 60KB to **2MB** across all components
- `StreamParser` bank size increased from 150KB to **600KB** 
- `fileSinkBufferSize` increased from 100KB to **600KB**

**Benefits**: Better compatibility with high-bitrate streams and buggy IP cameras that send oversized frames.

### 2. RTSP Server Port Reuse
- Added `-DALLOW_RTSP_SERVER_PORT_REUSE=1` compile flag
- Enables immediate port reuse when restarting services

**Benefits**: Faster service restarts without waiting for TIME_WAIT state. **Warning**: Never run multiple instances on the same port!

### 3. RTCP Error Handling
- Fixed CPU spinning issue with buggy RTP/RTCP-over-TCP implementations
- Added graceful buffer reset instead of infinite loops
- Enhanced logging for troubleshooting network issues

**Benefits**: Prevents 100% CPU usage when dealing with malformed RTCP packets from buggy cameras.

### 4. Dead Stream Detection (Proxy Server)
- New `-D <seconds>` option for inter-packet gap monitoring (default: 10 seconds)
- Automatic detection and reset of dead upstream streams
- Configurable timeout for different network conditions

**Usage**:
```bash
# Use default 10-second timeout
./proxyServer/live555ProxyServer rtsp://camera.example.com/stream

# Use custom 30-second timeout  
./proxyServer/live555ProxyServer -D 30 rtsp://camera.example.com/stream

# Disable dead stream detection
./proxyServer/live555ProxyServer -D 0 rtsp://camera.example.com/stream
```

**Benefits**: Automatically recovers from network interruptions, camera reboots, and upstream server issues.

## Production Reliability

These enhancements make live555 significantly more robust for production deployments:

- **Handles buggy IP cameras** with oversized frames and malformed packets
- **Automatically recovers** from network interruptions and dead streams  
- **Prevents resource exhaustion** from CPU spinning and memory issues
- **Faster service recovery** with port reuse functionality
- **Better observability** with enhanced logging for troubleshooting

## Compatibility

All changes maintain full API compatibility with upstream live555. Existing applications will benefit from the improvements without code changes.

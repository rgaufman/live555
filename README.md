# Build Instructions

```bash
./genMakefiles linux
make -j4
```

Replace "linux" with your platform, e.g. avr32-linux, cygwin, freebsd, iphoneos, linux, linux-64bit, macosx, openbsd, solaris-64bit, etc (see config.PLATFORM files)

You will find various executables:

 * ./testProgs - contain various programs such as testRTSPClient to receive an RTSP stream
 * ./proxyServer/live555ProxyServer - a great RTSP proxy server
 * ./mediaServer/live555MediaServer - an RTSP media server for serving static files over RTSP

# Changes to Master

See modifications.patch to see exactly what was changed compared to vanilla.

### Buffer sizes

OutPacketBuffer::maxSize is increased to 2,000,000 bytes which makes live555 work better with buggy IP cameras.

### Force port re-use

Added -DALLOW_RTSP_SERVER_PORT_REUSE=1 to force reusing existing port (e.g. when restarting the proxy). Please ensure
you never run multiple instances of the proxy on the same port!

### interPacketGap Patch (by Erik Montnemery)

Adds a -D <max-inter-packet-gap-time> option, to automatically restart the connection to the camera if no data is received for X seconds.

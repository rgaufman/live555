# Build Instructions

For example:

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

See modifications.patch and Proxyserver_check_interPacketGap.patch to see exactly what was changed compared to vanilla.

### Buffer sizes

OutPacketBuffer::maxSize is increased to 2,000,000 bytes which makes live555 work better with buggy IP cameras.

### Force port re-use

Added -DALLOW_RTSP_SERVER_PORT_REUSE=1 to force reusing existing port (e.g. when restarting the proxy). Please ensure
you never run multiple instances of the proxy on the same port!

### Max Inter Packet Gap Time

Erik Montnemery wrote a fantastic patch, in his own words: The new functionality is based on the implementation of the same option in openRTSP. When the -D option is used, the proxy server will reset the connection to a 'back end' server if at least one subsession *might* be in state playing, but no data has been received during max-inter-packet-gap-time seconds.

    [-D <max-inter-packet-gap-time>]

* Note 1: This option does not make sense for all streams. It is useful for IP cameras where the camera should be sending data at all times. As discussed in another thread, there’s no good way for the proxy server code to know exactly what subsession(s) are and are not currently streaming.


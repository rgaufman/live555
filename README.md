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

## -p option for proxyServer - allows specifying a listening port on the command line
 
This was rejected from the mailing list, but often RTSPProxy fails to run on
more than a few cameras with bad corruption, frames seeking back and forth and
many other adverse side effects. Being able to run multiple instances listening
on different ports is crucial.

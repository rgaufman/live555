# live555 — Enhanced Fork

Enhanced fork of the live555 streaming media library for production use.

**Upstream base:** `live.2026.04.01` (from https://download.live555.com/)
**Patch file:** [`modifications.patch`](modifications.patch) — unified diff of every change we apply on top of upstream.

## Build

```bash
./genMakefiles linux
make -j4
```

Replace `linux` with your platform — see the `config.*` files. Our `config.linux` pins `-std=c++20` because upstream uses `std::atomic_flag::test()`; if you use another platform that builds against GCC <13, add the same flag.

Key executables after build: `testProgs/testRTSPClient`, `testProgs/openRTSP`, `proxyServer/live555ProxyServer`, `mediaServer/live555MediaServer`.

## Enhancements over upstream

### Buffer sizes
`OutPacketBuffer::maxSize` bumped from 60 KB to **2 MB** (in `MediaSink.cpp`, and retained in the per-program bumps under `testProgs/` and `mediaServer/DynamicRTSPServer.cpp`). `StreamParser::BANK_SIZE` bumped to 600 KB. Needed for high-bitrate streams and cameras that send oversized frames.

### RTSP port reuse
`config.linux` sets `-DALLOW_RTSP_SERVER_PORT_REUSE=1` so service restarts don't wait on `TIME_WAIT`. Never run two instances on the same port.

### RTCP CPU-spin fix
Buggy RTP/RTCP-over-TCP implementations (certain Hikvision firmware) can pin a CPU to 100 % by emitting oversized RTCP reports faster than the handler can drain them. We reset the buffer and return instead of looping, and rate-limit the "packet overflow" warning (one line per 5 s per instance, with a suppressed-event count).

### Inter-packet gap / dead-stream detection
`live555ProxyServer` gets a `-D <seconds>` flag (default 10) that resets the upstream connection if no packets are received for that long. Detection latency is 1–2× the configured value because the check samples cumulative packet counts at that interval. `-D 0` disables it.

```bash
./proxyServer/live555ProxyServer rtsp://camera.example.com/stream        # default 10s
./proxyServer/live555ProxyServer -D 30 rtsp://camera.example.com/stream  # custom 30s
./proxyServer/live555ProxyServer -D 0 rtsp://camera.example.com/stream   # disabled
```

### RTP-over-TCP fan-out diagnostics
Rate-limited logging in `RTPInterface.cpp` surfaces previously-silent failure modes in the proxy's multi-client fan-out path: packet drops (with errno and framing state), TCP send-buffer-full stalls (500 ms blocking fallback), terminal send errors, and new-destination attaches. Per-fd state (independent per socket) means one stalled consumer can't mask drops on another. `EBADF`/`EPIPE` teardown races are filtered out.

All rate limiting goes through the shared helper at `liveMedia/include/RateLimitedLog.hh`.

## Deployment notes

### Kernel TCP send-buffer tuning for multi-client fan-out

When `live555ProxyServer` fans a single upstream stream out to multiple downstream consumers, H.264 I-frame bursts × N clients can overrun the per-socket send buffer and trip `EAGAIN` on the non-blocking framing-header send — which silently drops the RTP packet. Bump the kernel TCP send buffer to absorb realistic bursts:

```
net.core.wmem_max     = 8388608   # 8 MB
net.core.wmem_default = 2097152   # 2 MB
net.ipv4.tcp_wmem     = 4194304 2097152 8388608
```

Persist via `/etc/sysctl.d/*.conf` — a reboot with default settings regresses it.

**Telltale:** `RTPInterface::sendRTPorRTCPPacketOverTCP: dropping N-byte packet … errno=11` lines in the proxy log under fan-out load.

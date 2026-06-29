# live555 — Enhanced Fork

> **This is not the official live555 repository.** It's a third-party fork maintained for production use, offered to the community without any warranty or support guarantee. The canonical live555 distribution lives at **http://www.live555.com/liveMedia/**, and the authoritative channel for discussion and bug reports is the **live-devel** mailing list: **http://lists.live555.com/mailman/listinfo/live-devel/**.
>
> **For any genuine library bug, please also report it upstream** — the upstream authors don't see issues filed here, and their fixes flow back to the rest of the live555 userbase (VLC, MPlayer, many embedded cameras, etc.) via their releases. Reporting here in addition to upstream is welcome and sometimes useful: if we can reproduce it and write a fix, we're happy to patch it in the fork and feed it back upstream.
>
> See [Security reports](#security-reports) below for the recommended disclosure workflow.

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

### OnDemand RTSP uninitialised-read fix
Defensive init in `OnDemandServerMediaSubsession::getStreamParameters` for the local `streamBitrate` out-parameter. Several subclass overrides of `createNewStreamSource()` (Matroska, Ogg) return `NULL` on demuxer-track lookup failure without writing the out-parameter, leaving the caller to multiply it into an `increaseSendBufferTo()` call. Reported against the fork as [#65](https://github.com/rgaufman/live555/issues/65) with a valgrind trace; the same bug is present in upstream `live.2026.04.01` and should be reported to the live-devel list.

### RTSP server: Content-Length slow-read cap
`RTSPServer.cpp` rejects a request whose `Content-Length` is `>= REQUEST_BUFFER_SIZE` (20000) with `400 Bad Request`, instead of holding the connection open waiting for body bytes that can never fit the fixed request buffer. Closes a low-effort slow-read DoS where a client declares a huge body and then stalls. The existing pointer-wraparound check (the only prior guard) is a no-op on 64-bit, so absurd values like `999999999` previously hung the connection until the buffer filled.

### RTSP server: `Require:` / `Proxy-Require:` → `551 Option not supported`
We implement no server-side RTSP option-tags, so any `Require:` (or `Proxy-Require:`) header names an option we can't honour. Per RFC 2326 §12.32 we now reject such requests with `551 Option not supported` and echo the tag(s) in an `Unsupported:` header, rather than silently processing the request as though the requirement were satisfied. Both single and comma-separated multi-token headers are handled.

### Inter-packet gap / dead-stream detection
`live555ProxyServer` gets a `-D <seconds>` flag (default 10) that resets the upstream connection if no packets are received for that long. Detection latency is 1–2× the configured value because the check samples cumulative packet counts at that interval. `-D 0` disables it.

```bash
./proxyServer/live555ProxyServer rtsp://camera.example.com/stream        # default 10s
./proxyServer/live555ProxyServer -D 30 rtsp://camera.example.com/stream  # custom 30s
./proxyServer/live555ProxyServer -D 0 rtsp://camera.example.com/stream   # disabled
```

### Custom downstream stream name (`-e`)
By default `live555ProxyServer` publishes proxied streams as `rtsp://<proxy>/proxyStream` (single URL) or `proxyStream-1 .. proxyStream-N` (multiple URLs). The `-e <prefix>` flag substitutes your own prefix (max 50 characters, non-empty). Useful for embedding a human-readable name in the URL that downstream clients subscribe to.

```bash
# Single URL → rtsp://<proxy>/frontdoor
./proxyServer/live555ProxyServer -e frontdoor rtsp://camera.example.com/stream

# Multiple URLs → rtsp://<proxy>/camera-1, camera-2, camera-3
./proxyServer/live555ProxyServer -e camera \
    rtsp://cam1.example.com/stream \
    rtsp://cam2.example.com/stream \
    rtsp://cam3.example.com/stream
```

### Downstream client authentication (`-C`)
The `-C <username> <password>` flag requires downstream RTSP clients to authenticate (digest auth) before they can pull the proxied stream from this server. This is distinct from `-u`, which supplies credentials the proxy itself uses when connecting to the **upstream** camera. Internally it populates the same `UserAuthenticationDatabase` that `RTSPServer` already consults for every request.

```bash
# Proxy requires "viewer"/"secret" from its RTSP clients,
# and uses "admin"/"hunter2" to connect to the camera upstream:
./proxyServer/live555ProxyServer \
    -C viewer secret \
    -u admin hunter2 \
    rtsp://camera.example.com/stream
```

`-C` can be combined with `-e`, `-D`, `-U`/`-R`, and the other existing flags without interaction.

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

## Security reports

Bug reports — including security issues — are welcome. For the best chance of a fix reaching the wider live555 userbase, please report upstream **as well as** (or instead of) here:

**Primary channel: upstream live-devel mailing list**
http://lists.live555.com/mailman/listinfo/live-devel/

The upstream maintainer triages, fixes and discloses vulnerabilities for the core library, and their fixes flow downstream via releases at http://www.live555.com/liveMedia/public/. This fork pulls those fixes in via periodic resyncs. A vulnerability reported only here will sit dormant in every other live555 deployment (VLC, MPlayer, embedded cameras, other forks) until we happen to resync — so please don't skip the upstream report.

**This repo is also a fine place to report**, especially if:
- You'd like help reproducing, narrowing down the root cause, or drafting a patch — we sometimes get to things faster than the upstream maintainer and are happy to PR fixes back upstream once written.
- The issue is specific to something we changed in [`modifications.patch`](modifications.patch) (fork-introduced regressions are squarely our responsibility).
- You just want to keep other fork users informed after upstream has released a fix.

**For serious vulnerabilities with a PoC** we'd suggest: email the upstream maintainer (addresses on the live-devel list) with the full technical details, and open a terse tracking issue here ("security issue disclosed to upstream on YYYY-MM-DD, details withheld until fix lands") so fork users know something is in flight without the details being public. Once the upstream fix is released, the full details can be shared publicly here too.

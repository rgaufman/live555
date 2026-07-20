#!/bin/bash
# ASAN-instrumented build of the whole tree + proxy, for hunting the use-after-free that wedges
# UDP sites (dangling rtpSource in checkInterPacketGaps_). macOS host build only — never shipped.
set -euo pipefail
cd "$(dirname "$0")/.."
OPTS='-Iinclude -I../UsageEnvironment/include -I../groupsock/include -I. -I/opt/homebrew/opt/openssl@3/include -DBSD=1 -O1 -g -fsanitize=address -fno-omit-frame-pointer -DSOCKLEN_T=socklen_t -DHAVE_SOCKADDR_LEN=1 -DTIME_BASE=int -DNEED_XLOCALE_H=1'
for d in groupsock UsageEnvironment BasicUsageEnvironment liveMedia; do
  make -C "$d" C_FLAGS="$OPTS" CPLUSPLUS_FLAGS="$OPTS -std=c++20 -Wall" -j8
done
POPTS='-I../liveMedia/include -I../UsageEnvironment/include -I../groupsock/include -I../BasicUsageEnvironment/include -I. -I/opt/homebrew/opt/openssl@3/include -DBSD=1 -O1 -g -fsanitize=address -fno-omit-frame-pointer -DSOCKLEN_T=socklen_t -DHAVE_SOCKADDR_LEN=1 -DTIME_BASE=int -DNEED_XLOCALE_H=1'
make -C proxyServer C_FLAGS="$POPTS" CPLUSPLUS_FLAGS="$POPTS -std=c++20 -Wall" LINK='c++ -fsanitize=address -o ' LIBRARY_PATH=/opt/homebrew/opt/openssl@3/lib
echo ASAN-BUILD-OK

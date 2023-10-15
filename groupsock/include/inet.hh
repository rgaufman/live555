#pragma once
#include "Config.hh"
#include "NetAddress.hh"

#ifdef __cplusplus
extern "C" {
#endif

// The following are implemented in inet.c:
ipv4AddressBits our_inet_addr(char const*);

long our_random();

void our_srandom(unsigned int x);

u_int32_t our_random32(); // because "our_random()" returns a 31-bit number

#ifdef __cplusplus
}
#endif
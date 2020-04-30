// Copyright (c) 1996-2020, Live Networks, Inc.  All rights reserved
// This code may not be copied or used in any form without permission from Live Networks, Inc.
//
// A function for computing the HMAC_SHA1 digest
// Definition

#ifndef _HMAC_SHA1_HH
#define _HMAC_SHA1_HH

#ifndef NO_OPENSSL
#ifndef _HMAC_HASH_HH
#include "HMAC_hash.hh"
#endif

#define SHA1_DIGEST_LEN 20

HMAC_hash HMAC_SHA1;
#endif
#endif

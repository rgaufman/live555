/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2025, Live Networks, Inc.  All rights reserved
//
// Generic HMA_HASH functions
// Definition

#ifndef _HMAC_HASH_HH
#define _HMAC_HASH_HH

#ifndef _NET_COMMON_H
#include "NetCommon.h"
#endif

// All HMAC hash functions have the following signature:
typedef void HMAC_hash(u_int8_t const* key, unsigned keyLength,
		       u_int8_t const* text, unsigned textLength,
		       u_int8_t* resultDigest);
    // "resultDigest" must point to an array of sufficient size to hold the digest

#define HMAC_BLOCK_SIZE 64

#endif

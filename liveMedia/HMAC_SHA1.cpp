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
// Copyright (c) 1996-2021, Live Networks, Inc.  All rights reserved
//
// A function for computing the HMAC_SHA1 digest
// Implementation

#include "HMAC_SHA1.hh"

#ifndef NO_OPENSSL
#if defined(__APPLE__)
#define COMMON_DIGEST_FOR_OPENSSL
#include <CommonCrypto/CommonDigest.h>
#endif
#include <openssl/evp.h>

////////// HMAC_SHA1 implementation //////////

static void sha1(u_int8_t* resultDigest/*must be SHA1_DIGEST_LEN bytes in size*/,
		 u_int8_t const* data1, unsigned data1Length,
		 u_int8_t const* data2 = NULL, unsigned data2Length = 0) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_create();
  EVP_DigestInit(ctx, EVP_sha1());
  EVP_DigestUpdate(ctx, data1, data1Length);
  if (data2 != NULL) {
    EVP_DigestUpdate(ctx, data2, data2Length);
  }
  EVP_DigestFinal(ctx, resultDigest, NULL);
  EVP_MD_CTX_destroy(ctx);
}

void HMAC_SHA1(u_int8_t const* key, unsigned keyLength, u_int8_t const* text, unsigned textLength,
	       u_int8_t* resultDigest/*must be SHA1_DIGEST_LEN bytes in size*/) {
  if (key == NULL || keyLength == 0 || text == NULL || textLength == 0 || resultDigest == NULL) {
    return; // sanity check
  }

  // If the key is longer than the block size, hash it to make it smaller:
  u_int8_t tmpDigest[SHA1_DIGEST_LEN];
  if (keyLength > HMAC_BLOCK_SIZE) {
    sha1(tmpDigest, key, keyLength);
    key = tmpDigest;
    keyLength = SHA1_DIGEST_LEN;
  }
  // Assert: keyLength <= HMAC_BLOCK_SIZE

  // Initialize the inner and outer pads with the key:
  u_int8_t ipad[HMAC_BLOCK_SIZE];
  u_int8_t opad[HMAC_BLOCK_SIZE];
  unsigned i;
  for (i = 0; i < keyLength; ++i) {
    ipad[i] = key[i]^0x36;
    opad[i] = key[i]^0x5c;
  }
  for (; i < HMAC_BLOCK_SIZE; ++i) {
    ipad[i] = 0x36;
    opad[i] = 0x5c;
  }
  
  // Perform the inner hash:
  sha1(tmpDigest, ipad, HMAC_BLOCK_SIZE, text, textLength);

  // Perform the outer hash:
  sha1(resultDigest, opad, HMAC_BLOCK_SIZE, tmpDigest, SHA1_DIGEST_LEN);
}
#endif

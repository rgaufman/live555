// Copyright (c) 1996-2020, Live Networks, Inc.  All rights reserved
// This code may not be copied or used in any form without permission from Live Networks, Inc.
//
// The SRTP 'Cryptographic Context', used in all of our uses of SRTP.
// Implementation

#include "SRTPCryptographicContext.hh"
#ifndef NO_OPENSSL
#include "HMAC_SHA1.hh"
#include <openssl/aes.h>
#endif

#ifdef DEBUG
#include <stdio.h>
#endif

SRTPCryptographicContext
::SRTPCryptographicContext(MIKEYState const& mikeyState)
#ifndef NO_OPENSSL
  : fMIKEYState(mikeyState),
    fHaveReceivedSRTPPackets(False), fSRTCPIndex(0) {
  // Begin by doing a key derivation, to generate the keying data that we need:
  performKeyDerivation();
#else
  {
#endif
}

SRTPCryptographicContext::~SRTPCryptographicContext() {
}

Boolean SRTPCryptographicContext
::processIncomingSRTPPacket(u_int8_t* buffer, unsigned inPacketSize,
			    unsigned& outPacketSize) {
#ifndef NO_OPENSSL
  do {
    if (inPacketSize < 12) { // For SRTP, 12 is the minimum packet size (if unauthenticated)
#ifdef DEBUG
      fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTPPacket(): Error: Packet size %d is too short for SRTP!\n", inPacketSize);
#endif
      break;
    }

    unsigned const numBytesPastEncryption
      = SRTP_MKI_LENGTH + (weAuthenticate() ? SRTP_AUTH_TAG_LENGTH : 0);
    if (inPacketSize <= numBytesPastEncryption) {
#ifdef DEBUG
      fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTPPacket(): Error: Packet size %d is too short for encrpytion and/or authentication!\n", inPacketSize);
#endif
      break;
    }

    // Begin by figuring out this packet's 'index' (ROC and RTP sequence number),
    // and the ROC and RTP sequence number that will be used subsequently, provided that
    // this packet authenticates OK:
    u_int16_t const rtpSeqNum = (buffer[2]<<8)|buffer[3];
    u_int32_t nextROC, thisPacketsROC;
    u_int16_t nextHighRTPSeqNum;

    if (!fHaveReceivedSRTPPackets) {
      // First time:
      nextROC = thisPacketsROC = fROC = 0;
      nextHighRTPSeqNum = rtpSeqNum;
    } else {
      // Check whether the sequence number has rolled over, or is out-of-order:
      u_int16_t const SEQ_NUM_THRESHOLD = 0x1000;
      if (rtpSeqNum >= fPreviousHighRTPSeqNum) {
	// normal case, or out-of-order packet that crosses a rollover:
	if (rtpSeqNum - fPreviousHighRTPSeqNum < SEQ_NUM_THRESHOLD) {
	  // normal case:
	  nextROC = thisPacketsROC = fROC;
	  nextHighRTPSeqNum = rtpSeqNum;
	} else {
	  // out-of-order packet that crosses rollover:
	  nextROC = fROC;
	  thisPacketsROC = fROC-1;
	  nextHighRTPSeqNum = fPreviousHighRTPSeqNum;
	}
      } else {
	// rollover, or out-of-order packet that crosses a rollover:
	if (fPreviousHighRTPSeqNum - rtpSeqNum > SEQ_NUM_THRESHOLD) {
	  // rollover:
	  nextROC = thisPacketsROC = fROC+1;
	  nextHighRTPSeqNum = rtpSeqNum;
	} else {
	  // out-of-order packet (that doesn't cross a rollover):
	  nextROC = thisPacketsROC = fROC;
	  nextHighRTPSeqNum = fPreviousHighRTPSeqNum;
	}
      }
    }
      
    if (weAuthenticate()) {
      // Authenticate the packet.
      unsigned const numBytesToAuthenticate
	= inPacketSize - (SRTP_MKI_LENGTH + SRTP_AUTH_TAG_LENGTH); // ASSERT: > 0
      u_int8_t const* authenticationTag = &buffer[inPacketSize - SRTP_AUTH_TAG_LENGTH];

      if (!verifySRTPAuthenticationTag(buffer, numBytesToAuthenticate, thisPacketsROC, authenticationTag)) {
#ifdef DEBUG
	fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTPPacket(): Failed to authenticate incoming SRTP packet!\n");
#endif
	break;
      }
    }

    // Now that we've verified the packet, set the 'index values' for next time:
    fROC = nextROC;
    fPreviousHighRTPSeqNum = nextHighRTPSeqNum;
    fHaveReceivedSRTPPackets = True;

    if (weEncryptSRTP()) {
      // Decrypt the SRTP packet.  It has the index "thisPacketsROC" with "rtpSeqNum":
      u_int64_t index = (thisPacketsROC<<16)|rtpSeqNum;

      // Figure out the RTP header size.  This will tell us which bytes to decrypt:
      unsigned rtpHeaderSize = 12; // at least the basic 12-byte header
      rtpHeaderSize += (buffer[0]&0x0F)*4; // # CSRC identifiers
      if ((buffer[0]&0x10) != 0) {
	// There's a RTP extension header.  Add its size:
	if (inPacketSize < rtpHeaderSize + 4) {
#ifdef DEBUG
	  fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTPPacket(): Error: Packet size %d is shorter than the minimum specified RTP header size %d!\n", inPacketSize, rtpHeaderSize + 4);
#endif
	  break;
	}
	u_int16_t hdrExtLength = (buffer[rtpHeaderSize+2]<<8)|buffer[rtpHeaderSize+3];
	rtpHeaderSize += 4 + hdrExtLength*4;
      }

      unsigned const offsetToEncryptedBytes = rtpHeaderSize;
      unsigned numEncryptedBytes = inPacketSize - numBytesPastEncryption; // ASSERT: > 0
      if (offsetToEncryptedBytes > numEncryptedBytes) {
#ifdef DEBUG
	fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTPPacket(): Error: RTP header size %d (expected <= %d) is too large!\n", rtpHeaderSize, numEncryptedBytes);
#endif
	break;
      }
      numEncryptedBytes -= offsetToEncryptedBytes;
	
      u_int32_t const SSRC = (buffer[8]<<24)|(buffer[9]<<16)|(buffer[10]<<8)|buffer[11];
      decryptSRTPPacket(index, SSRC, &buffer[offsetToEncryptedBytes], numEncryptedBytes);
      outPacketSize = inPacketSize - numBytesPastEncryption; // trim to what we use
    }

    return True;
  } while (0);
#endif

  // An error occurred in the handling of the packet:
  return False;
}

Boolean SRTPCryptographicContext
::processIncomingSRTCPPacket(u_int8_t* buffer, unsigned inPacketSize,
			     unsigned& outPacketSize) {
#ifndef NO_OPENSSL
  do {
    if (inPacketSize < 12) {
      // For SRTCP, 8 is the minumum RTCP packet size, but there's also a mandatory
      //   4-byte "E+SRTCP index" word.
#ifdef DEBUG
      fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTCPPacket(): Error: Packet size %d is too short for SRTCP!\n", inPacketSize);
#endif
      break;
    }

    unsigned const numBytesPastEncryption
      = 4/*E+SRTCP index*/ + SRTP_MKI_LENGTH + (weAuthenticate() ? SRTP_AUTH_TAG_LENGTH : 0);
    if (inPacketSize <= numBytesPastEncryption) {
#ifdef DEBUG
      fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTCPPacket(): Error: Packet size %d is too short for encrpytion and/or authentication!\n", inPacketSize);
#endif
      break;
    }

    if (weAuthenticate()) {
      // Authenticate the packet.
      unsigned const numBytesToAuthenticate
	= inPacketSize - (SRTP_MKI_LENGTH + SRTP_AUTH_TAG_LENGTH); // ASSERT: > 0
      u_int8_t const* authenticationTag = &buffer[inPacketSize - SRTP_AUTH_TAG_LENGTH];

      if (!verifySRTCPAuthenticationTag(buffer, numBytesToAuthenticate, authenticationTag)) {
#ifdef DEBUG
	fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTCPPacket(): Failed to authenticate incoming SRTCP packet!\n");
#endif
	break;
      }
    }

    if (weEncryptSRTCP()) {
      // Decrypt the SRTCP packet:
      unsigned numEncryptedBytes = inPacketSize - numBytesPastEncryption; // ASSERT: > 0
      u_int8_t const* p = &buffer[numEncryptedBytes]; // E + SRTCP index
      u_int32_t E_plus_SRTCPIndex = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
      if ((E_plus_SRTCPIndex&0x80000000) != 0) {
	// The packet is encrypted
	unsigned const offsetToEncryptedBytes = 8;
	if (offsetToEncryptedBytes > numEncryptedBytes) {
#ifdef DEBUG
	  fprintf(stderr, "SRTPCryptographicContext::processIncomingSRTCPPacket(): Error: RTCP packet size %d is too small!\n", numEncryptedBytes);
#endif
	  break;
	}
	numEncryptedBytes -= offsetToEncryptedBytes;
	
	u_int32_t index = E_plus_SRTCPIndex&0x7FFFFFFF;
	u_int32_t const SSRC = (buffer[4]<<24)|(buffer[5]<<16)|(buffer[6]<<8)|buffer[7];
	decryptSRTCPPacket(index, SSRC, &buffer[offsetToEncryptedBytes], numEncryptedBytes);
      }
      outPacketSize = inPacketSize - numBytesPastEncryption; // trim to what we use
    }

    return True;
  } while (0);
#endif

  // An error occurred in the handling of the packet:
  return False;
}

Boolean SRTPCryptographicContext
::processOutgoingSRTCPPacket(u_int8_t* buffer, unsigned inPacketSize,
			     unsigned& outPacketSize) {
#ifndef NO_OPENSSL
  do {
    // Encrypt the appropriate part of the packet.
    u_int8_t eFlag = 0x00;
    if (weEncryptSRTCP()) {
      unsigned const unencryptedHeaderSize = 8;
      if (inPacketSize < unencryptedHeaderSize) { // packet is too small
	// Hack: Let small, non RTCP packets through w/o encryption; they may be used to
	// punch through NATs
	outPacketSize = inPacketSize;
	return True;
      }
      unsigned const encryptedDataSize = inPacketSize - unencryptedHeaderSize;

      u_int8_t* const dataToEncrypt = &buffer[unencryptedHeaderSize];
      u_int32_t const ssrc = (buffer[4]<<24)|(buffer[5]<<16)|(buffer[6]<<8)|buffer[7];
      encryptSRTCPPacket(fSRTCPIndex, ssrc, dataToEncrypt, encryptedDataSize);
      eFlag = 0x80;
    }

    outPacketSize = inPacketSize; // initially

    // Add 4 bytes for the 'E' flag and SRTCP index:
    buffer[outPacketSize++] = (fSRTCPIndex>>24)|eFlag;
    buffer[outPacketSize++] = fSRTCPIndex>>16;
    buffer[outPacketSize++] = fSRTCPIndex>>8;
    buffer[outPacketSize++] = fSRTCPIndex;
    ++fSRTCPIndex; // for next time

    // Add the MKI:
    buffer[outPacketSize++] = MKI()>>24;
    buffer[outPacketSize++] = MKI()>>16;
    buffer[outPacketSize++] = MKI()>>8;
    buffer[outPacketSize++] = MKI();

    // Generate and add an authentication tag over the data built so far (except the MKI)
    outPacketSize += generateSRTCPAuthenticationTag(buffer, outPacketSize-SRTP_MKI_LENGTH,
						    &buffer[outPacketSize]);

    return True;
  } while (0);
#endif
  
  // An error occurred:
  return False;
}

#ifndef NO_OPENSSL
unsigned SRTPCryptographicContext
::generateSRTCPAuthenticationTag(u_int8_t const* dataToAuthenticate, unsigned numBytesToAuthenticate,
				 u_int8_t* resultAuthenticationTag) {
  return generateAuthenticationTag(fDerivedKeys.srtcp, dataToAuthenticate, numBytesToAuthenticate,
				   resultAuthenticationTag);
}

Boolean SRTPCryptographicContext
::verifySRTPAuthenticationTag(u_int8_t* dataToAuthenticate, unsigned numBytesToAuthenticate,
			      u_int32_t roc, u_int8_t const* authenticationTag) {
  // Append the (4-byte) 'ROC' (roll-over counter) to "dataToAuthenticate" before computing
  // the authentication tag.  We can do this because we have enough space after
  // "dataToAuthenticate":
  //   - If we're encrypted, then there's assumed to be a (4-byte) MKI there.  Just overwrite
  //     that.  (If we need the MKI, we could copy it beforehand; later, allow for there being
  //     no MKI #####)
  //   - If we're not encrypted, then the ROC will overwrite part of the existing
  //     authentication tag, so we need to make a copy of this.
  u_int8_t const* existingAuthenticationTag;
  Boolean haveMKI = True; // later, allow for there being no MKI #####
  u_int8_t authenticationTagCopy[SRTP_AUTH_TAG_LENGTH];

  if (fMIKEYState.encryptSRTP() && haveMKI) {
    existingAuthenticationTag = authenticationTag;
  } else {
    memcpy(authenticationTagCopy, authenticationTag, sizeof authenticationTagCopy);
    existingAuthenticationTag = authenticationTagCopy;
  }

  dataToAuthenticate[numBytesToAuthenticate++] = roc>>24;
  dataToAuthenticate[numBytesToAuthenticate++] = roc>>16;
  dataToAuthenticate[numBytesToAuthenticate++] = roc>>8;
  dataToAuthenticate[numBytesToAuthenticate++] = roc;

  return verifyAuthenticationTag(fDerivedKeys.srtp,
				 dataToAuthenticate, numBytesToAuthenticate,
				 existingAuthenticationTag);
}

Boolean SRTPCryptographicContext
::verifySRTCPAuthenticationTag(u_int8_t const* dataToAuthenticate, unsigned numBytesToAuthenticate,
			       u_int8_t const* authenticationTag) {
return verifyAuthenticationTag(fDerivedKeys.srtcp,
			       dataToAuthenticate, numBytesToAuthenticate,
			       authenticationTag);
}

void SRTPCryptographicContext
::decryptSRTPPacket(u_int64_t index, u_int32_t ssrc, u_int8_t* data, unsigned numDataBytes) {
  cryptData(fDerivedKeys.srtp, index, ssrc, data, numDataBytes);
}

void SRTPCryptographicContext
::decryptSRTCPPacket(u_int32_t index, u_int32_t ssrc, u_int8_t* data, unsigned numDataBytes) {
  cryptData(fDerivedKeys.srtcp, (u_int64_t)index, ssrc, data, numDataBytes);
}

void SRTPCryptographicContext
::encryptSRTCPPacket(u_int32_t index, u_int32_t ssrc, u_int8_t* data, unsigned numDataBytes) {
  cryptData(fDerivedKeys.srtcp, (u_int64_t)index, ssrc, data, numDataBytes);
}

unsigned SRTPCryptographicContext
::generateAuthenticationTag(derivedKeys& keysToUse,
			    u_int8_t const* dataToAuthenticate, unsigned numBytesToAuthenticate,
			    u_int8_t* resultAuthenticationTag) {
  if (SRTP_AUTH_TAG_LENGTH > SHA1_DIGEST_LEN) return 0; // sanity check; shouldn't happen
  u_int8_t computedAuthTag[SHA1_DIGEST_LEN];
  HMAC_SHA1(keysToUse.authKey, sizeof keysToUse.authKey,
	    dataToAuthenticate, numBytesToAuthenticate,
	    computedAuthTag);

  for (unsigned i = 0; i < SRTP_AUTH_TAG_LENGTH; ++i) {
    resultAuthenticationTag[i] = computedAuthTag[i];
  }

  return SRTP_AUTH_TAG_LENGTH;
}

Boolean SRTPCryptographicContext
::verifyAuthenticationTag(derivedKeys& keysToUse,
			  u_int8_t const* dataToAuthenticate, unsigned numBytesToAuthenticate,
			  u_int8_t const* authenticationTag) {
  u_int8_t computedAuthTag[SHA1_DIGEST_LEN];
  HMAC_SHA1(keysToUse.authKey, sizeof keysToUse.authKey,
	    dataToAuthenticate, numBytesToAuthenticate,
	    computedAuthTag);

  if (SRTP_AUTH_TAG_LENGTH > SHA1_DIGEST_LEN) return False; // sanity check
  for (unsigned i = 0; i < SRTP_AUTH_TAG_LENGTH; ++i) {
    if (computedAuthTag[i] != authenticationTag[i]) return False;
  }
  return True;
}

 void SRTPCryptographicContext::cryptData(derivedKeys& keys, u_int64_t index, u_int32_t ssrc,
					 u_int8_t* data, unsigned numDataBytes) {
  // Begin by constructing the IV: (salt * 2^16) XOR (ssrc * 2^64) XOR (index * 2^16)
  u_int8_t iv[SRTP_CIPHER_KEY_LENGTH];

  memmove(iv, keys.salt, SRTP_CIPHER_SALT_LENGTH);
  iv[SRTP_CIPHER_SALT_LENGTH] = iv[SRTP_CIPHER_SALT_LENGTH + 1] = 0;
  // (This is based upon the fact that SRTP_CIPHER_KEY_LENGTH == SRTP_CIPHER_SALT_LENGTH + 2)

  iv[sizeof iv-12] ^= ssrc>>24; iv[sizeof iv-11] ^= ssrc>>16; iv[sizeof iv-10] ^= ssrc>>8; iv[sizeof iv-9] ^= ssrc;

  iv[sizeof iv-8] ^= index>>40; iv[sizeof iv-7] ^= index>>32; iv[sizeof iv-6] ^= index>>24; iv[sizeof iv-5] ^= index>>16; iv[sizeof iv-4] ^= index>>8; iv[sizeof iv-3] ^= index;

  // Now generate as many blocks of the keystream as we need, by repeatedly encrypting
  // the IV using our cipher key.  (After each step, we increment the IV by 1.)
  // We then XOR the keystream into the provided data, to do the en/decryption.
  AES_KEY key;
  AES_set_encrypt_key(keys.cipherKey, 8*SRTP_CIPHER_KEY_LENGTH, &key);

  while (numDataBytes > 0) {
    u_int8_t keyStream[SRTP_CIPHER_KEY_LENGTH];
    AES_encrypt(iv, keyStream, &key);

    unsigned numBytesToUse
      = numDataBytes < SRTP_CIPHER_KEY_LENGTH ? numDataBytes : SRTP_CIPHER_KEY_LENGTH;
    for (unsigned i = 0; i < numBytesToUse; ++i) data[i] ^= keyStream[i];
    data += numBytesToUse;
    numDataBytes -= numBytesToUse;

    // Increment the IV by 1:
    u_int8_t* ptr = &iv[sizeof iv];
    do {
      --ptr;
      ++*ptr;
    } while (*ptr == 0x00);
  }
}

void SRTPCryptographicContext::performKeyDerivation() {
  // Perform a key derivation for the master key+salt, as defined
  // by RFC 3711, section 4.3:
  deriveKeysFromMaster(masterKey(), masterSalt(), fDerivedKeys);
}

#define deriveKey(label, resultKey) deriveSingleKey(masterKey, salt, label, sizeof resultKey, resultKey)

void SRTPCryptographicContext
::deriveKeysFromMaster(u_int8_t const* masterKey, u_int8_t const* salt,
		       allDerivedKeys& allKeysResult) {
  // Derive cipher, salt, and auth keys for both SRTP and SRTCP:
  deriveKey(label_srtp_encryption, allKeysResult.srtp.cipherKey);
  deriveKey(label_srtp_msg_auth, allKeysResult.srtp.authKey);
  deriveKey(label_srtp_salt, allKeysResult.srtp.salt);

  deriveKey(label_srtcp_encryption, allKeysResult.srtcp.cipherKey);
  deriveKey(label_srtcp_msg_auth, allKeysResult.srtcp.authKey);
  deriveKey(label_srtcp_salt, allKeysResult.srtcp.salt);
}

#define KDF_PRF_CIPHER_BLOCK_LENGTH 16

void SRTPCryptographicContext
::deriveSingleKey(u_int8_t const* masterKey, u_int8_t const* salt,
		  SRTPKeyDerivationLabel label,
		  unsigned resultKeyLength, u_int8_t* resultKey) {
  // This looks a little different from the mechanism described in RFC 3711, section 4.3, but
  // it's what the 'libsrtp' code does, so I hope it's functionally equivalent:
  AES_KEY key;
  AES_set_encrypt_key(masterKey, 8*SRTP_CIPHER_KEY_LENGTH, &key);

  u_int8_t counter[KDF_PRF_CIPHER_BLOCK_LENGTH];
  // Set the first bytes of "counter" to be the 'salt'; set the remainder to zero:
  memmove(counter, salt, SRTP_CIPHER_SALT_LENGTH);
  for (unsigned i = SRTP_CIPHER_SALT_LENGTH; i < sizeof counter; ++i) {
    counter[i] = 0;
  }

  // XOR "label" into byte 7 of "counter":
  counter[7] ^= label;

  // And use the resulting "counter" as the plaintext:
  u_int8_t const* plaintext = counter;

  unsigned numBytesRemaining = resultKeyLength;
  while (numBytesRemaining > 0) {
    u_int8_t ciphertext[KDF_PRF_CIPHER_BLOCK_LENGTH];
    AES_encrypt(plaintext, ciphertext, &key);

    unsigned numBytesToCopy
      = numBytesRemaining < KDF_PRF_CIPHER_BLOCK_LENGTH ? numBytesRemaining : KDF_PRF_CIPHER_BLOCK_LENGTH;
    memmove(resultKey, ciphertext, numBytesToCopy);
    resultKey += numBytesToCopy;
    numBytesRemaining -= numBytesToCopy;
    ++counter[15]; // for next time
  }
}
#endif

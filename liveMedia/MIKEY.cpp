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
// Copyright (c) 1996-2023 Live Networks, Inc.  All rights reserved.
// A data structure that implements a MIKEY message (RFC 3830)
// Implementation

#include "MIKEY.hh"
#include <GroupsockHelper.hh> // for our_random32()

////////// MIKEYPayload definition /////////

class MIKEYPayload {
public:
  MIKEYPayload(MIKEYState& ourMIKEYState, u_int8_t payloadType);
      // create with default values
  MIKEYPayload(MIKEYState& ourMIKEYState, u_int8_t payloadType,
	       u_int8_t const* data, unsigned dataSize);
      // create as a copy of existing values

  virtual ~MIKEYPayload();

  u_int8_t const* data() const { return fData; }
  unsigned dataSize() const { return fDataSize; }

  MIKEYPayload* next() const { return fNext; }
  void setNextPayload(MIKEYPayload* nextPayload);

private:
  MIKEYState& fOurMIKEYState;
  u_int8_t fPayloadType;
  u_int8_t* fData;
  unsigned fDataSize;
  MIKEYPayload* fNext;
};


////////// MIKEYState implementation //////////

enum MIKEYPayloadType {
		       KEMAC = 1,
		       PKE = 2,
		       DH = 3,
		       SIGN = 4,
		       T = 5,
		       ID = 6,
		       CERT = 7,
		       CHASH = 8,
		       V = 9,
		       SP = 10,
		       RAND = 11,
		       ERR = 12,
		       KEY_DATA = 13,
		       HDR = 255
};

MIKEYState::MIKEYState(Boolean useEncryption)
  : // Set default encryption/authentication parameters:
  fEncryptSRTP(useEncryption),
  fEncryptSRTCP(useEncryption),
  fMKI(our_random32()),
  fUseAuthentication(True),

  fHeaderPayload(NULL), fTailPayload(NULL), fTotalPayloadByteCount(0) {
  // Fill in our 'key data' (30 bytes) with (pseudo-)random bits:
  u_int8_t* p = &fKeyData[0];
  u_int32_t random32;
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 0-3
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 4-7
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 8-11
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 12-15
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 16-19
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 20-23
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); *p++ = (random32>>8); *p++ = random32; // 24-27
  random32 = our_random32();
  *p++ = (random32>>24); *p++ = (random32>>16); // 28-29

  addNewPayload(new MIKEYPayload(*this, HDR));
  addNewPayload(new MIKEYPayload(*this, T));
  addNewPayload(new MIKEYPayload(*this, RAND));
  addNewPayload(new MIKEYPayload(*this, SP));
  addNewPayload(new MIKEYPayload(*this, KEMAC));
}

MIKEYState::~MIKEYState() {
  delete fHeaderPayload; // which will delete all the other payloads as well
}

MIKEYState* MIKEYState::createNew(u_int8_t const* messageToParse, unsigned messageSize) {
  Boolean parsedOK;
  MIKEYState* newMIKEYState = new MIKEYState(messageToParse, messageSize, parsedOK);

  if (!parsedOK) {
    delete newMIKEYState;
    newMIKEYState = NULL;
  }

  return newMIKEYState;
}

u_int8_t* MIKEYState::generateMessage(unsigned& messageSize) const {
  if (fTotalPayloadByteCount == 0) return NULL;

  // ASSERT: fTotalPayloadByteCount == the sum of all of the payloads' "fDataSize"s
  messageSize = fTotalPayloadByteCount;
  u_int8_t* resultMessage = new u_int8_t[messageSize];
  u_int8_t* p = resultMessage;
  
  for (MIKEYPayload* payload = fHeaderPayload; payload != NULL; payload = payload->next()) {
    if (payload->data() == NULL) continue;

    memcpy(p, payload->data(), payload->dataSize());
    p += payload->dataSize();
  }

  return resultMessage;
}

MIKEYState::MIKEYState(u_int8_t const* messageToParse, unsigned messageSize, Boolean& parsedOK)
  : // Set encryption/authentication parameters to default values (that may be overwritten
    // later as we parse the message):
  fEncryptSRTP(False),
  fEncryptSRTCP(False),
  fUseAuthentication(False),

  fHeaderPayload(NULL), fTailPayload(NULL), fTotalPayloadByteCount(0) {
  parsedOK = False; // unless we learn otherwise

  // Begin by parsing a HDR payload:
  u_int8_t const* ptr = messageToParse;
  u_int8_t const* const endPtr = messageToParse + messageSize;
  u_int8_t nextPayloadType;
  
  if (!parseHDRPayload(ptr, endPtr, nextPayloadType)) return;

  // Then parse each subsequent payload that we see:
  while (nextPayloadType != 0) {
    if (!parseNonHDRPayload(ptr, endPtr, nextPayloadType)) return;
  }

  // We succeeded in parsing all the data:
  parsedOK = True;
}

void MIKEYState::addNewPayload(MIKEYPayload* newPayload) {
  if (fTailPayload == NULL) {
    fHeaderPayload = newPayload;
  } else {
    fTailPayload->setNextPayload(newPayload);
  }
  fTailPayload = newPayload;

  fTotalPayloadByteCount += newPayload->dataSize();
}

#define testSize(n) if (ptr + (n) > endPtr) break

Boolean MIKEYState
::parseHDRPayload(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  do {
    testSize(10);
    nextPayloadType = ptr[2];
    u_int8_t numCryptoSessions = ptr[8];

    unsigned payloadSize = 10 + numCryptoSessions*(1+4+4);
    testSize(payloadSize);

    addNewPayload(new MIKEYPayload(*this, HDR, ptr, payloadSize));
    ptr += payloadSize;
    
    return True;
  } while (0);

  // An error occurred:
  return False;
}

static u_int32_t get4Bytes(u_int8_t const*& ptr) {
  u_int32_t result = (ptr[0]<<24)|(ptr[1]<<16)|(ptr[2]<<8)|ptr[3];
  ptr += 4;
  return result;
}

static u_int16_t get2Bytes(u_int8_t const*& ptr) {
  u_int16_t result = (ptr[0]<<8)|ptr[1];
  ptr += 2;
  return result;
}

Boolean MIKEYState
::parseNonHDRPayload(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  do {
    Boolean parseSucceeded = False; // initially
    u_int8_t const* payloadStart = ptr;
    unsigned payloadSize = 0;

    testSize(1);
    u_int8_t ourPayloadType = nextPayloadType;
    nextPayloadType = *ptr++;

    // The parsing depends on "ourPayloadType":
    switch (ourPayloadType) {
      case T: { // RFC 3830, section 6.6
	testSize(1);
	u_int8_t TS_type = *ptr++;
	unsigned TS_value_len = 0;
	switch (TS_type) {
	  case 0: // NTP-UTC
	  case 1: { // NTP
	    TS_value_len = 8;
	    break;
	  }
	  case 2: { // COUNTER
	    TS_value_len = 4;
	    break;
	  }
	}
	if (TS_value_len == 0) break; // unknown TS_type

	testSize(TS_value_len);
	payloadSize = 2 + TS_value_len;
	parseSucceeded = True;
	break;
      }
      case RAND: { // RFC 3830, section 6.11
	testSize(1);
        u_int8_t RAND_len = *ptr++;

	testSize(RAND_len);
	payloadSize = 2 + RAND_len;
	parseSucceeded = True;
	break;
      }
      case SP: { // RFC 3830, section 6.10
	testSize(4);
	++ptr; // skip over "Policy no"
	u_int8_t protType = *ptr++;
	if (protType != 0/*SRTP*/) break; // unsupported protocol type

	u_int16_t policyParam_len = get2Bytes(ptr);

	testSize(policyParam_len);
	payloadSize = 5 + policyParam_len;
	u_int8_t const* payloadEndPtr = payloadStart + payloadSize;
	// Look through the "Policy param" section, making sure that:
	//   - each of the "length" fields make sense
	//   - for "type"s that we understand, the values make sense
	Boolean parsedPolicyParamSection = False;
	while (1) {
	  testSize(2);
	  u_int8_t ppType = *ptr++;
	  u_int8_t ppLength = *ptr++;

	  testSize(ppLength);
	  if (ptr+ppLength > payloadEndPtr) break; // bad "length"s - too big for the payload

	  // Check types that we understand:
	  Boolean policyIsOK = False; // until we learn otherwise
	  switch (ppType) {
	    case 0: { // Encryption algorithm: we handle only NULL and AES-CM
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value > 1) break; // unsupported algorithm
	      if (value > 0) fEncryptSRTP = fEncryptSRTCP = True;
	          // Note: these might get changed by a subsequent "off/on" entry
	      policyIsOK = True;
	      break;
	    }
	    case 1: { // Session Encr. key length
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value != 16) break; // bad/unsupported value
	      policyIsOK = True;
	      break;
	    }
	    case 2: { // Authentication algorithm: we handle only NULL and HMAC-SHA-1
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value > 1) break; // unsupported algorithm
	      if (value > 0) fUseAuthentication = True;
	          // Note: this might get changed by a subsequent "off/on" entry
	      policyIsOK = True;
	      break;
	    }
	    case 3: { // Session Auth. key length
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value != 20) break; // bad/unsupported value
	      policyIsOK = True;
	      break;
	    }
	    case 4: { // Session Salt key length
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value != 14) break; // bad/unsupported value
	      policyIsOK = True;
	      break;
	    }
	    case 7: { // SRTP encryption off/on
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value > 1) break; // bad/unsupported value
	      fEncryptSRTP = value;
	      policyIsOK = True;
	      break;
	    }
	    case 8: { // SRTCP encryption off/on
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value > 1) break; // bad/unsupported value
	      fEncryptSRTCP = value;
	      policyIsOK = True;
	      break;
	    }
	    case 10: { // SRTP authentication off/on
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value > 1) break; // bad/unsupported value
	      fUseAuthentication = value;
	      policyIsOK = True;
	      break;
	    }
	    case 11: { // Authentication tag length
	      if (ppLength != 1) break;
	      u_int8_t value = ptr[0];
	      if (value != 10) break; // bad/unsupported value
	      policyIsOK = True;
	      break;
	    }
	    default: { // a policy type that we don't handle; still OK
	      policyIsOK = True;	
	      break;
	    }
	  }
	  if (!policyIsOK) break;

	  ptr += ppLength;
	  if (ptr == payloadEndPtr) {
	    // We've successfully checked all of the "Policy param"s
	    parsedPolicyParamSection = True;
	    break;
	  }
	}
	if (!parsedPolicyParamSection) break;
	parseSucceeded = True;
	break;
      }
      case KEMAC: {  // RFC 3830, section 6.2
	testSize(3);
	u_int8_t encrAlg = *ptr++;
	// We currently support only 'NULL' encryption on the key data:
	if (encrAlg != 0/*NULL*/) break; // unknown or unsupported key encryption

	u_int16_t encrDataLen = get2Bytes(ptr);

	testSize(encrDataLen);
	// Parse the 'key data sub-payload':
	{
	  u_int8_t const* subPtr = ptr;
	  if (encrDataLen < 4) break; // not enough space
	  ++subPtr; // skip over the "Next payload" field
	  
	  // Check the "Type" and "KV" fields; we support only TEK and SPI/MKI
	      // Note: This means that we'll reject the "a=key-mgmt" SDP MIKEY data from
	      //   an Axis camera, because that doesn't specify SPI/MKI.  But that's what
	      //   we want, because the Axis camera's "a=key-mgmt" SDP MIKEY data is meant
	      //   to be ignored by clients.
	  if (*subPtr++ != ((2<<4)|1)) break; // Type 2 (TEK) | KV 1 (SPI/MKI) 
	  u_int16_t keyDataLen = get2Bytes(subPtr);
	  // The key data length must be 30 (encryption key length (16) + salt length (14)):
	  if (keyDataLen != 30) break;

	  // Make sure we have enough space for the key data and the "SPI Length" field:
	  if (4+keyDataLen+1 > encrDataLen) break;
	  // Record the key data:
	  memmove(fKeyData, subPtr, keyDataLen);
	  subPtr += keyDataLen;

	  // Check the "SPI Length"; we support only length 4:
	  u_int8_t SPILength = *subPtr++;
	  if (SPILength != 4) break;

	  // Make a note of the MKI (the next 4 bytes):
	  if (4+keyDataLen+1+SPILength > encrDataLen) break;
	  fMKI = get4Bytes(subPtr);
	}
	ptr += encrDataLen;

	testSize(1);
	u_int8_t macAlg = *ptr++;
	unsigned macLen;
	// We currently support only a 'NULL' MAC on the key data:
	if (macAlg == 0/*NULL*/) macLen = 0;
	else break; // unknown or unsupported MAC algorithm => parse fails

	testSize(macLen);
	payloadSize = 4 + encrDataLen + 1 + macLen;
	parseSucceeded = True;
	break;
      }
      default: {
	// Unknown payload type.  The parsing fails.
	break;
      }
    }
    if (!parseSucceeded) break;
    
    addNewPayload(new MIKEYPayload(*this, ourPayloadType, payloadStart, payloadSize));
    ptr = payloadStart + payloadSize;
    
    return True;
  } while (0);

  // An error occurred:
  return False;
}


////////// MIKEYPayload implementation //////////

static void addWord(u_int8_t*& p, u_int32_t word) {
  *p++ = word>>24; *p++ = word>>16; *p++ = word>>8; *p++ = word;
}

static void addHalfWord(u_int8_t*& p, u_int16_t halfWord) {
  *p++ = halfWord>>8; *p++ = halfWord;
}

static void add1BytePolicyParam(u_int8_t*& p, u_int8_t type, u_int8_t value) {
  *p++ = type;
  *p++ = 1; // length
  *p++ = value;
}

MIKEYPayload::MIKEYPayload(MIKEYState& ourMIKEYState, u_int8_t payloadType)
  : fOurMIKEYState(ourMIKEYState), fPayloadType(payloadType), fNext(NULL) {
  switch (payloadType) {
    case HDR: { // RFC 3830, section 6.1
      fDataSize = 19;
      fData = new u_int8_t[fDataSize];
      u_int8_t* p = fData;
      *p++ = 1; // version
      *p++ = 0; // Initiator's pre-shared key message
      *p++ = 0; // no next payload (initially)
      *p++ = 0; // V=0; PRF func: MIKEY-1
      u_int32_t const CSB_ID = our_random32();
      addWord(p, CSB_ID);
      *p++ = 1; // #CS: 1
      *p++ = 0; // CS ID map type: SRTP-ID
      *p++ = 0; // Policy_no_1
      addWord(p, our_random32()); // SSRC_1
      addWord(p, 0x00000000); // ROC_1
      break;
    }
    case T: { // RFC 3830, section 6.6
      fDataSize = 10;
      fData = new u_int8_t[fDataSize];
      u_int8_t* p = fData;
      *p++ = 0; // no next payload (initially)
      *p++ = 0; // TS type: NTP-UTC

      // Get the current time, and convert it to a NTP-UTC time:
      struct timeval timeNow;
      gettimeofday(&timeNow, NULL);
      u_int32_t ntpSeconds = timeNow.tv_sec + 0x83AA7E80; // 1970 epoch -> 1900 epoch
      addWord(p, ntpSeconds);
      double fractionalPart = (timeNow.tv_usec/15625.0)*0x04000000; // 2^32/10^6
      u_int32_t ntpFractionOfSecond = (u_int32_t)(fractionalPart+0.5); // round
      addWord(p, ntpFractionOfSecond);
      break;
    }
    case RAND: { // RFC 3830, section 6.11
      fDataSize = 18;
      fData = new u_int8_t[fDataSize];
      u_int8_t* p = fData;
      *p++ = 0; // no next payload (initially)
      unsigned const numRandomWords = 4; // number of 32-bit words making up the RAND value
      *p++ = 4*numRandomWords; // RAND len (in bytes)
      for (unsigned i = 0; i < numRandomWords; ++i) {
	u_int32_t randomNumber = our_random32();
	addWord(p, randomNumber);
      }
      break;
    }
    case SP: { // RFC 3830, section 6.10
      fDataSize = 32;
      fData = new u_int8_t[fDataSize];
      u_int8_t* p = fData;
      *p++ = 0; // no next payload (initially)
      *p++ = 0; // Policy number
      *p++ = 0; // Protocol type: SRTP
      u_int16_t policyParamLen = 27;
      addHalfWord(p, policyParamLen);
      // Now add the SRTP policy parameters:
      add1BytePolicyParam(p, 0/*Encryption algorithm*/,
			  (fOurMIKEYState.encryptSRTP()||fOurMIKEYState.encryptSRTCP())
			  ? 1/*AES-CM*/ : 0/*NULL*/);
      add1BytePolicyParam(p, 1/*Session Encryption key length*/, 16);
      add1BytePolicyParam(p, 2/*Authentication algorithm*/,
			  fOurMIKEYState.useAuthentication()
			  ? 1/*HMAC-SHA-1*/ : 0/*NULL*/);
      add1BytePolicyParam(p, 3/*Session Authentication key length*/, 20);
      add1BytePolicyParam(p, 4/*Session Salt key length*/, 14);
      add1BytePolicyParam(p, 7/*SRTP encryption off/on*/, fOurMIKEYState.encryptSRTP());
      add1BytePolicyParam(p, 8/*SRTCP encryption off/on*/, fOurMIKEYState.encryptSRTCP());
      add1BytePolicyParam(p, 10/*SRTP authentication off/on*/, fOurMIKEYState.useAuthentication());
      add1BytePolicyParam(p, 11/*Authentication tag length*/, 10);
      break;
    }
    case KEMAC: {  // RFC 3830, section 6.2
      fDataSize = 44;
      fData = new u_int8_t[fDataSize];
      u_int8_t* p = fData;
      *p++ = 0; // no next payload
      *p++ = 0; // Encr alg (NULL)
      u_int16_t encrDataLen = 39;
      addHalfWord(p, encrDataLen);
      { // Key data sub-payload (RFC 3830, section 6.13):
	*p++ = 0; // no next payload
	*p++ = (2<<4)|1; // Type 2 (TEK) | KV 1 (SPI/MKI)

	// Key data len:
	u_int16_t const keyDataLen = 30;
	addHalfWord(p, keyDataLen);

	// Key data:
	memcpy(p, fOurMIKEYState.keyData(), keyDataLen);
	p += keyDataLen;
	
	{ // KV data (for SPI/MKI) (RFC 3830, section 6.14):
	  *p++ = 4; // SPI/MKI Length
	  addWord(p, fOurMIKEYState.MKI());
	}
      }
      *p++ = 0; // MAC alg (NULL)
      break;
    }
    default: {
      // Unused payload type.  Just in case, allocate 1 byte, for the
      // presumed 'next payload type' field.
      fDataSize = 1;
      fData = new u_int8_t[fDataSize];
      fData[0] = 0;
      break;
    }
  }
}

MIKEYPayload::MIKEYPayload(MIKEYState& ourMIKEYState, u_int8_t payloadType,
			   u_int8_t const* data, unsigned dataSize)
  : fOurMIKEYState(ourMIKEYState), fPayloadType(payloadType),
    fDataSize(dataSize), fNext(NULL) {
  fData = new u_int8_t[fDataSize];
  memcpy(fData, data, fDataSize);
}

MIKEYPayload::~MIKEYPayload() {
  delete[] fData;
  delete fNext;
}

void MIKEYPayload::setNextPayload(MIKEYPayload* nextPayload) {
  fNext = nextPayload;

  // We also need to set the 'next payload type' field in our data:
  u_int8_t nextPayloadType = nextPayload->fPayloadType;
  
  switch (fPayloadType) {
    case HDR: {
      fData[2] = nextPayloadType;
      break;
    }
    default: {
      if (fData != NULL) fData[0] = nextPayloadType;
      break;
    }
  }
}

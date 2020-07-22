// Parses MIKEY data (from Base64)

#include <stdio.h>
#include <stdlib.h>
#include <Base64.hh>
#include <NetCommon.h>

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

static u_int8_t getByte(u_int8_t const*& ptr) {
  u_int8_t result = ptr[0];
  ptr += 1;
  return result;
}

Boolean parseMikeyUnknown(u_int8_t const*& /*ptr*/, u_int8_t const* /*endPtr*/, u_int8_t& /*nextPayloadType*/) {
  fprintf(stderr, "\tUnknown or unhandled payload type\n");
  return False;
}

char const* payloadTypeName[256];
char const* dataTypeComment[256];
#define testSize(n) do {if (ptr + (n) > endPtr) return False; } while (0)

Boolean parseMikeyHDR(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  testSize(10); // up to the start of "CS ID map info"

  fprintf(stderr, "\tversion: %d\n", getByte(ptr));

  u_int8_t const dataType = getByte(ptr);
  fprintf(stderr, "\tdata type: %d (%s)\n", dataType, dataTypeComment[dataType]);

  nextPayloadType = getByte(ptr);
  fprintf(stderr, "\tnext payload: %d (%s)\n", nextPayloadType, payloadTypeName[nextPayloadType]);

  u_int8_t const V_PRF = getByte(ptr);
  u_int8_t const PRF = V_PRF&0x7F;
  fprintf(stderr, "\tV:%d; PRF:%d (%s)\n", V_PRF>>7, PRF, PRF == 0 ? "MIKEY-1" : "unknown");

  fprintf(stderr, "\tCSB ID:0x%08x\n", get4Bytes(ptr));

  u_int8_t numCryptoSessions = getByte(ptr);
  fprintf(stderr, "\t#CS:%d\n", numCryptoSessions);

  u_int8_t const CS_ID_map_type = getByte(ptr);
  fprintf(stderr, "\tCS ID map type:%d (%s)\n",
	  CS_ID_map_type, CS_ID_map_type == 0 ? "SRTP-ID" : "unknown");
  if (CS_ID_map_type != 0) return False;

  fprintf(stderr, "\tCS ID map info:\n");
  testSize(numCryptoSessions * (1+4+4)); // the size of the "CS ID map info"
  for (u_int8_t i = 1; i <= numCryptoSessions; ++i) {
    fprintf(stderr, "\tPolicy_no_%d: %d;\tSSRC_%d: 0x%08x; ROC_%d: 0x%08x\n",
	    i, getByte(ptr),
	    i, get4Bytes(ptr),
	    i, get4Bytes(ptr));
  }

  return True;
}

static Boolean parseKeyDataSubPayload(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  fprintf(stderr, "\tEncr data:\n");
  testSize(4); // up to the start of "Key data"

  nextPayloadType = getByte(ptr);
  fprintf(stderr, "\t\tnext payload: %d (%s)\n", nextPayloadType, payloadTypeName[nextPayloadType]);

  u_int8_t Type_KV = getByte(ptr);
  u_int8_t Type = Type_KV>>4;
  u_int8_t KV = Type_KV&0x0F;
  fprintf(stderr, "\t\tType: %d (%s)\n", Type,
	  Type == 0 ? "TGK" : Type == 1 ? "TGK+SALT" : Type == 2 ? "TEK" : Type == 3 ? "TEK+SALT" : "unknown");
  if (Type > 3) return False;
  Boolean hasSalt = Type == 1 || Type == 3;
  
  fprintf(stderr, "\t\tKey Validity: %d (%s)\n", KV,
	  KV == 0 ? "NULL" : KV == 1 ? "SPI/MKI" : KV == 2 ? "Interval" : "unknown");
  Boolean hasKV = KV != 0;

  u_int16_t keyDataLen = get2Bytes(ptr);
  fprintf(stderr, "\t\tKey data len: %d\n", keyDataLen);
  
  testSize(keyDataLen);
  fprintf(stderr, "\t\tKey data: ");
  for (unsigned i = 0; i < keyDataLen; ++i) fprintf(stderr, ":%02x", getByte(ptr));
  fprintf(stderr, "\n");
  
  if (hasSalt) {
    testSize(2);
    u_int16_t saltLen = get2Bytes(ptr);
    fprintf(stderr, "\t\tSalt len: %d\n", saltLen);

    testSize(saltLen);
    fprintf(stderr, "\t\tSalt data: ");
    for (unsigned i = 0; i < saltLen; ++i) fprintf(stderr, ":%02x", getByte(ptr));
    fprintf(stderr, "\n");
  }

  if (hasKV) {
    fprintf(stderr, "\t\tKV (key validity) data:\n");
    if (KV == 1) { // SPI/MKI
      testSize(1);
      u_int8_t SPILength = getByte(ptr);
      fprintf(stderr, "\t\t\tSPI Length: %d\n", SPILength);

      testSize(SPILength);
      fprintf(stderr, "\t\t\tSPI: ");
      for (unsigned i = 0; i < SPILength; ++i) fprintf(stderr, ":%02x", getByte(ptr));
      fprintf(stderr, "\n");
    } else if (KV == 2) { // Interval
      testSize(1);
      u_int8_t VFLength = getByte(ptr);
      fprintf(stderr, "\t\t\tVF Length: %d\n", VFLength);

      testSize(VFLength);
      fprintf(stderr, "\t\t\tVF: ");
      for (unsigned i = 0; i < VFLength; ++i) fprintf(stderr, ":%02x", getByte(ptr));
      fprintf(stderr, "\n");

      testSize(1);
      u_int8_t VTLength = getByte(ptr);
      fprintf(stderr, "\t\t\tVT Length: %d\n", VTLength);

      testSize(VTLength);
      fprintf(stderr, "\t\t\tVT: ");
      for (unsigned i = 0; i < VTLength; ++i) fprintf(stderr, ":%02x", getByte(ptr));
      fprintf(stderr, "\n");
    }
  }
    
  return True;
}

Boolean parseMikeyKEMAC(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  testSize(4); // up to the start of "Encr data"

  nextPayloadType = getByte(ptr);
  fprintf(stderr, "\tnext payload: %d (%s)\n", nextPayloadType, payloadTypeName[nextPayloadType]);

  u_int8_t encrAlg = getByte(ptr);
  fprintf(stderr, "\tEncr alg: %d (%s)\n", encrAlg,
	  encrAlg == 0 ? "NULL" : encrAlg == 1 ? "AES-CM-128" : encrAlg == 2 ? "AES-KW-128" : "unknown");

  u_int16_t encrDataLen = get2Bytes(ptr);
  fprintf(stderr, "\tencr data len: %d\n", encrDataLen);

  testSize(encrDataLen + 1/*allow for "Mac alg"*/);
  u_int8_t const* endOfKeyData = ptr + encrDataLen;
  
  // Allow for multiple key data sub-payloads
  while (ptr < endOfKeyData) {
    if (!parseKeyDataSubPayload(ptr, endOfKeyData, nextPayloadType)) return False;
  }

  u_int8_t macAlg = getByte(ptr);
  fprintf(stderr, "\tMAC alg: %d (%s)\n", macAlg,
	  macAlg == 0 ? "NULL" : macAlg == 1 ? "HMAC-SHA-1-160" : "unknown");
  if (macAlg > 1) return False;
  if (macAlg == 1) { // HMAC-SHA-1-160
    unsigned const macLen = 160/8; // bytes
    fprintf(stderr, "\t\tMAC: ");
    for (unsigned i = 0; i < macLen; ++i) fprintf(stderr, ":%02x", getByte(ptr));
    fprintf(stderr, "\n");
  }

  return True;
}

Boolean parseMikeyT(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  testSize(2); // up to the start of "TS value"

  nextPayloadType = getByte(ptr);
  fprintf(stderr, "\tnext payload: %d (%s)\n", nextPayloadType, payloadTypeName[nextPayloadType]);

  u_int8_t TS_type = getByte(ptr);
  unsigned TS_value_len;
  fprintf(stderr, "\tTS type: %d (", TS_type);
  switch (TS_type) {
    case 0: {
      fprintf(stderr, "NTP-UTC)\n");
      TS_value_len = 8; // 64 bits
      break;
    }
    case 1: {
      fprintf(stderr, "NTP)\n");
      TS_value_len = 8; // 64 bits
      break;
    }
    case 2: {
      fprintf(stderr, "COUNTER)\n");
      TS_value_len = 4; // 32 bits
      break;
    }
    default: {
      fprintf(stderr, "unknown)\n");
      return False;
    }
  }

  testSize(TS_value_len);
  fprintf(stderr, "\tTS value:");
  for (unsigned i = 0; i < TS_value_len; ++i) fprintf(stderr, ":%02x", getByte(ptr));
  fprintf(stderr, "\n");

  return True;
}

#define MAX_SRTP_POLICY_PARAM_TYPE 12
static char const* SRTPPolicyParamTypeExplanation[] = {
      "Encryption algorithm",
      "Session Encryption key length",
      "Authentication algorithm",
      "Session Authentication key length",
      "Session Salt key length",
      "SRTP Pseudo Random Function",
      "Key derivation rate",
      "SRTP encryption off/on",
      "SRTCP encryption off/on",
      "Sender's FEC order",
      "SRTP authentication off/on",
      "Authentication tag length",
      "SRTP prefix length",
};

static Boolean parseSRTPPolicyParam(u_int8_t const*& ptr, u_int8_t const* endPtr) {
  fprintf(stderr, "\tPolicy param:\n");
  while (ptr < endPtr) {
    testSize(2);

    u_int8_t ppType = getByte(ptr);
    fprintf(stderr, "\t\ttype: %d (%s); ", ppType,
	    ppType > MAX_SRTP_POLICY_PARAM_TYPE ? "unknown" : SRTPPolicyParamTypeExplanation[ppType]);

    u_int8_t ppLen = getByte(ptr);
    fprintf(stderr, "length: %d; value: ", ppLen);

    testSize(ppLen);
    u_int8_t ppVal = 0xFF;
    if (ppLen == 1) {
      ppVal = getByte(ptr);
      fprintf(stderr, "%d", ppVal);
    } else {
      for (unsigned j = 0; j < ppLen; ++j) fprintf(stderr, ":%02x", getByte(ptr));
    }

    switch (ppType) {
      case 0: { // Encryption algorithm
	fprintf(stderr, " (%s)",
		ppVal == 0 ? "NULL" : ppVal == 1 ? "AES-CM" : ppVal == 2 ? "AES-F8" : "unknown");
        break;
      }
      case 2: { // Authentication algorithm
	fprintf(stderr, " (%s)",
		ppVal == 0 ? "NULL" : ppVal == 1 ? "HMAC-SHA-1" : "unknown");
        break;
      }
      case 5: { // SRTP Pseudo Random Function
	fprintf(stderr, " (%s)",
		ppVal == 0 ? "AES-CM" : "unknown");
        break;
      }
      case 9: { // sender's FEC order
	fprintf(stderr, " (%s)",
		ppVal == 0 ? "First FEC, then SRTP" : "unknown");
        break;
      }
    }
    fprintf(stderr, "\n");
  }

  return True;
}

Boolean parseMikeySP(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  testSize(2); // up to the start of "Policy param"

  nextPayloadType = getByte(ptr);
  fprintf(stderr, "\tnext payload: %d (%s)\n", nextPayloadType, payloadTypeName[nextPayloadType]);

  fprintf(stderr, "\tPolicy number: %d\n", getByte(ptr));

  u_int8_t protocolType = getByte(ptr);
  fprintf(stderr, "\tProtocol type: %d (%s)\n", protocolType, protocolType == 0 ? "SRTP" : "unknown");
  if (protocolType != 0) return False;

  u_int16_t policyParam_len = get2Bytes(ptr);
  fprintf(stderr, "\tPolicy param len: %d\n", policyParam_len);

  testSize(policyParam_len);
  return parseSRTPPolicyParam(ptr, ptr + policyParam_len);
}

Boolean parseMikeyRAND(u_int8_t const*& ptr, u_int8_t const* endPtr, u_int8_t& nextPayloadType) {
  testSize(2); // up to the start of "RAND"

  nextPayloadType = getByte(ptr);
  fprintf(stderr, "\tnext payload: %d (%s)\n", nextPayloadType, payloadTypeName[nextPayloadType]);

  u_int8_t RAND_len = getByte(ptr);
  fprintf(stderr, "\tRAND len: %d", RAND_len);

  testSize(RAND_len);
  fprintf(stderr, "\tRAND:");
  for (unsigned i = 0; i < RAND_len; ++i) fprintf(stderr, ":%02x", getByte(ptr));
  fprintf(stderr, "\n");

  return True;
}

typedef Boolean (parseMikeyPayloadFunc)(u_int8_t const*& ptr, u_int8_t const* endPtr,
					u_int8_t& nextPayloadType);
parseMikeyPayloadFunc* payloadParser[256];

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <base64Data>\n", argv[0]);
    exit(1);
  }
  char const* base64Data = argv[1];

  unsigned mikeyDataSize;
  u_int8_t* mikeyData = base64Decode(base64Data, mikeyDataSize);

  fprintf(stderr, "Base64Data \"%s\" produces %d bytes of MIKEY data:\n", base64Data, mikeyDataSize);
  for (unsigned i = 0; i < mikeyDataSize; ++i) fprintf(stderr, ":%02x", mikeyData[i]);
  fprintf(stderr, "\n");

  for (unsigned i = 0; i < 256; ++i) {
    payloadTypeName[i] = "unknown or unhandled";
    payloadParser[i] = parseMikeyUnknown;

    dataTypeComment[i] = "unknown";
  }

  // Populate known payload types:
  payloadTypeName[0] = "Last payload";

  payloadTypeName[1] = "KEMAC";
  payloadParser[1] = parseMikeyKEMAC;

  payloadTypeName[2] = "PKE";

  payloadTypeName[3] = "DH";

  payloadTypeName[4] = "SIGN";

  payloadTypeName[5] = "T";
  payloadParser[5] = parseMikeyT;

  payloadTypeName[6] = "ID";

  payloadTypeName[7] = "CERT";

  payloadTypeName[8] = "CHASH";

  payloadTypeName[9] = "V";

  payloadTypeName[10] = "SP";
  payloadParser[10] = parseMikeySP;

  payloadTypeName[11] = "RAND";
  payloadParser[11] = parseMikeyRAND;

  payloadTypeName[12] = "ERR";

  payloadTypeName[20] = "Key data";

  payloadTypeName[21] = "General Ext.";

  // Populate known data types:
  dataTypeComment[0] = "Initiator's pre-shared key message";
  dataTypeComment[1] = "Verification message of a pre-shared key message";
  dataTypeComment[2] = "Initiator's public-key transport message";
  dataTypeComment[3] = "Verification message of a public-key message";
  dataTypeComment[4] = "Initiator's DH exchange message";
  dataTypeComment[5] = "Responder's DH exchange message";
  dataTypeComment[6] = "Error message";

  u_int8_t const* ptr = mikeyData;
  u_int8_t* const endPtr = &mikeyData[mikeyDataSize];
  u_int8_t nextPayloadType;

  do {
    // Begin by parsing an initial "HDR":
    fprintf(stderr, "HDR:\n");
    if (!parseMikeyHDR(ptr, endPtr, nextPayloadType)) break;
  
    // Then parse each successive payload:
    while (nextPayloadType != 0 /* Last payload */) {
      fprintf(stderr, "%s:\n", payloadTypeName[nextPayloadType]);
      if (!(*payloadParser[nextPayloadType])(ptr, endPtr, nextPayloadType)) break;
    }
  } while (0);

  if (ptr < endPtr) {
    fprintf(stderr, "+%ld bytes of unparsed data: ", endPtr-ptr);
    while (ptr < endPtr) fprintf(stderr, ":%02x", *ptr++);
    fprintf(stderr, "\n");
  }

  delete[] mikeyData;
  return 0;
}

// test_interleaved_framing — pins the RTP-over-TCP framing state machine's resynchronisation.
//
// THE DEFECT THESE TESTS WERE WRITTEN TO EXPOSE (written BEFORE the fix, and failing against the
// behaviour shipped in RTPInterface.cpp at the time):
//
// Production DESYNC reports from a Merit-Lilin 4K camera repeatedly began with the same bytes:
//     05 a2 ef a2 ...   05 a2 5d 3f ...   05 a2 a7 f8 ...
//     04 5e 24 00 05 a2 80 63     <-- [len 1118][$][ch 0][len 1442][RTP v2 ...]
// 0x05a2 = 1442 = a full-size frame length. Three of four samples START with a length field, and the
// fourth contains a VALID frame header two bytes further on. The camera's framing was intact; our
// read pointer was two bytes behind it.
//
// Cause: on an unregistered channel id, the reader set state = AWAITING_DOLLAR having already
// consumed '$' and the channel byte, and without consuming the size field or payload. It then hunted
// for the next 0x24 — which lands inside payload — and framing was lost for the rest of the stream.
//
// A frame is self-describing. Its size field says exactly how many bytes to discard.

#include "RTPInterleavedFraming.hh"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace RTPInterleavedFraming;

static int checks = 0, failures = 0;

static void ok(bool cond, char const* what) {
  ++checks;
  if (cond) {
    printf("  ok:   %s\n", what);
  } else {
    ++failures;
    printf("  FAIL: %s\n", what);
  }
}

// Drive the state machine over a byte buffer exactly as the reader does, honouring skipBytes.
// Returns the offset at which a frame header for a REGISTERED channel was parsed, or -1.
// `scans` counts bytes consumed by the hunt-for-'$' path — the alignment-losing behaviour.
static int runReader(unsigned char const* buf, int len, bool (*isRegistered)(unsigned char),
                     int& scans, int startOffset = 0) {
  ReadState state = AWAITING_DOLLAR;
  unsigned char sizeByte1 = 0, channel = 0;
  scans = 0;
  int i = startOffset;
  while (i < len) {
    unsigned char const c = buf[i];
    if (state == AWAITING_STREAM_CHANNEL_ID) channel = c;
    bool const reg = isRegistered(channel);
    FrameStep const s = frameStep(c, state, reg, sizeByte1);
    if (state == AWAITING_SIZE1) sizeByte1 = c;
    if (s.resyncByScan) ++scans;
    ++i;
    if (s.haveFrameHeader) return i - 4; // offset of the '$' that began this header

    // Consume the unhandled frame's payload the way the reader does. Production models this as a
    // STATE (SKIPPING_UNHANDLED_PAYLOAD) rather than an inline jump, because while skipping, the
    // bytes are known payload: the desync guard must not count them and a 0x24 among them must not
    // be read as a frame marker. Once the declared count is consumed we are back at a boundary.
    if (s.nextState == SKIPPING_UNHANDLED_PAYLOAD) {
      i += s.skipBytes;
      state = AWAITING_DOLLAR;
      continue;
    }
    state = s.nextState;
  }
  return -1;
}

static bool onlyChannel0(unsigned char c) { return c == 0; }
static bool nothingRegistered(unsigned char) { return false; }

// Build: [$][ch][size hi][size lo][payload...]
static void appendFrame(std::vector<unsigned char>& v, unsigned char ch, unsigned short size,
                        unsigned char fill) {
  v.push_back(DOLLAR);
  v.push_back(ch);
  v.push_back((unsigned char)(size >> 8));
  v.push_back((unsigned char)(size & 0xff));
  for (unsigned short n = 0; n < size; ++n) v.push_back(fill);
}

int main() {
  printf("== RTP-over-TCP interleaved framing regression tests ==\n\n");

  printf("[SYNC] a normal frame on a registered channel is parsed\n");
  {
    std::vector<unsigned char> v;
    appendFrame(v, 0, 1442, 0xAB);
    int scans = 0;
    int const at = runReader(&v[0], (int)v.size(), onlyChannel0, scans);
    ok(at == 0, "frame header found at offset 0");
    ok(scans == 0, "no byte was consumed by scanning");
  }

  printf("\n[THE BUG] a frame on an UNREGISTERED channel must be SKIPPED BY ITS OWN LENGTH,\n"
         "          leaving the reader exactly on the next frame's '$'\n");
  {
    // Channel 4 is not registered; channel 0 is. The payload is deliberately full of 0x24 ('$') —
    // exactly the trap the scanning path falls into, and real H264 payload contains 0x24 constantly.
    std::vector<unsigned char> v;
    appendFrame(v, 4, 600, DOLLAR);   // unregistered, payload is all '$'
    appendFrame(v, 0, 1442, 0xAB);    // the REAL next frame
    int scans = 0;
    int const at = runReader(&v[0], (int)v.size(), onlyChannel0, scans);
    ok(at == 604, "reader lands on the real next frame header (offset 604), not inside payload");
    ok(scans == 0, "framing was never lost, so no scanning was needed");
  }

  printf("\n[THE BUG] the exact production byte pattern: our pointer must not end up 2 bytes\n"
         "          behind a valid header (05 a2 = 1442 length field read as framing)\n");
  {
    // Reproduces `04 5e 24 00 05 a2 80 63`: a frame we do not handle, then a real one.
    std::vector<unsigned char> v;
    appendFrame(v, 4, 1118, 0x5E);
    size_t const realFrameAt = v.size();
    appendFrame(v, 0, 1442, 0x80);
    int scans = 0;
    int const at = runReader(&v[0], (int)v.size(), onlyChannel0, scans);
    ok(at == (int)realFrameAt, "lands on the genuine '$', not on the preceding length field");
    ok(scans == 0, "no alignment was lost");
  }

  printf("\n[SCAN] hunting for '$' is reachable ONLY for a genuinely stray byte\n");
  {
    // Garbage that is not a frame at all: scanning is the correct last resort here.
    std::vector<unsigned char> v;
    v.push_back(0xEF); v.push_back(0xA2); v.push_back(0xE6);
    appendFrame(v, 0, 100, 0x11);
    int scans = 0;
    int const at = runReader(&v[0], (int)v.size(), onlyChannel0, scans);
    ok(at == 3, "resynchronised onto the real frame after the stray bytes");
    ok(scans == 3, "exactly the 3 stray bytes were scanned past");
  }

  printf("\n[SKIP] a frame for a channel we never handle is skipped, however many arrive\n");
  {
    std::vector<unsigned char> v;
    for (int n = 0; n < 20; ++n) appendFrame(v, 7, 500, DOLLAR);
    appendFrame(v, 0, 300, 0x22);
    int scans = 0;
    int const at = runReader(&v[0], (int)v.size(), onlyChannel0, scans);
    ok(at == 20 * 504, "20 unhandled frames skipped exactly, landing on the handled one");
    ok(scans == 0, "not one byte was scanned");
  }

  printf("\n[REACHABILITY] every outcome the state machine declares must be attainable\n");
  {
    int scans = 0;
    std::vector<unsigned char> a;
    appendFrame(a, 0, 10, 0x01);
    bool const headerReachable = runReader(&a[0], (int)a.size(), onlyChannel0, scans) >= 0;

    std::vector<unsigned char> b;
    appendFrame(b, 9, 10, 0x01);
    int scans2 = 0;
    runReader(&b[0], (int)b.size(), nothingRegistered, scans2);

    std::vector<unsigned char> c;
    c.push_back(0x01);
    appendFrame(c, 0, 10, 0x01);
    int scans3 = 0;
    runReader(&c[0], (int)c.size(), onlyChannel0, scans3);

    ok(headerReachable, "haveFrameHeader is reachable");
    ok(scans3 > 0, "resyncByScan is reachable (stray byte)");
    ok(scans2 == 0, "an unhandled frame does NOT reach the scanning path");
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}

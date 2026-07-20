/* RTPInterleavedFraming.hh — the RTP-over-TCP interleaved framing state machine (RFC 2326 sec 10.12),
   extracted as a PURE function so its resynchronisation behaviour can be unit-tested.

   WHY THIS EXISTS.

   Production DESYNC reports on Merit-Lilin cameras repeatedly begin with the same two bytes:

       05 a2 ef a2 e6 81 ef 54
       05 a2 5d 3f 60 85 40 dc
       05 a2 a7 f8 8e ed f1 25
       04 5e 24 00 05 a2 80 63     <-- decodes as: [len 1118][$][ch 0][len 1442][RTP v2 ...]

   0x05a2 = 1442, a full-size frame length. Three of four samples START with a length field, and the
   fourth contains a complete, VALID frame header two bytes further on. The camera's framing was
   intact; our read pointer was sitting two bytes behind it, on the previous frame's length field.

   The mechanism is in RTPInterface.cpp's AWAITING_STREAM_CHANNEL_ID case: when the channel id is not
   one we registered, it sets fTCPReadingState = AWAITING_DOLLAR — having ALREADY consumed the '$'
   and the channel byte, and WITHOUT consuming the 2-byte size field or the payload that follow. The
   reader then scans forward for the next 0x24, which is overwhelmingly likely to be a byte inside
   payload rather than a real frame marker. Framing is lost for the rest of the connection, and the
   desync guard trips 2048 bytes later.

   The frame is self-describing: its size field says exactly how many bytes to discard. Skipping the
   whole frame lands the reader precisely on the next '$' with no guessing. That is what this header
   implements, and what the tests pin.

   NOTE ON BLAME: the log line for TX-DESYNC says framing loss "does NOT prove a camera fault". That
   wording is correct and must stay — this analysis says the opposite of a camera bug for the case a
   camera streams on a channel it did not negotiate, which is legal-ish behaviour our reader must
   simply handle. */

#ifndef _RTP_INTERLEAVED_FRAMING_HH
#define _RTP_INTERLEAVED_FRAMING_HH

namespace RTPInterleavedFraming {

// The framing marker: '$' (RFC 2326 sec 10.12).
static unsigned char const DOLLAR = 0x24;

enum ReadState {
  AWAITING_DOLLAR,
  AWAITING_STREAM_CHANNEL_ID,
  AWAITING_SIZE1,
  AWAITING_SIZE2,
  AWAITING_PACKET_DATA,
  // Discarding an unhandled channel's payload by the length the frame itself declared. Deliberately
  // a STATE rather than a counter consumed inline: while skipping, the bytes are known payload, so
  // the desync guard must not count them (they cannot indicate lost framing) and the reader must not
  // interpret a 0x24 among them as a frame marker.
  SKIPPING_UNHANDLED_PAYLOAD
};

// What the reader should do after consuming one byte.
struct FrameStep {
  ReadState nextState;
  // Bytes of this frame's payload to DISCARD without interpreting them as framing. Non-zero only
  // when a complete header was parsed for a channel we do not handle: the size field tells us
  // exactly how much to throw away, so the reader lands on the next '$' rather than hunting for one.
  unsigned skipBytes;
  // True when a complete, well-formed frame header was parsed for a channel we DO handle.
  bool haveFrameHeader;
  // True when the reader had to abandon framing and hunt for the next '$'. This is the resync-by-
  // guessing path, and it is exactly what loses byte alignment — it must be reachable ONLY when
  // there is genuinely nothing better to do (a stray byte where a '$' was expected).
  bool resyncByScan;
};

// Advance the state machine by one byte.
//
//   c                  the byte just read
//   state              current reading state
//   channelRegistered  whether `c` (in AWAITING_STREAM_CHANNEL_ID) is a channel we handle
//   sizeByte1          the high byte captured in AWAITING_SIZE1 (only read in AWAITING_SIZE2)
inline FrameStep frameStep(unsigned char c, ReadState state, bool channelRegistered,
                           unsigned char sizeByte1) {
  FrameStep s;
  s.nextState = state;
  s.skipBytes = 0;
  s.haveFrameHeader = false;
  s.resyncByScan = false;

  switch (state) {
    case AWAITING_DOLLAR:
      if (c == DOLLAR) {
        s.nextState = AWAITING_STREAM_CHANNEL_ID;
      } else {
        // Still hunting for a frame marker. Staying here IS the scan.
        s.nextState = AWAITING_DOLLAR;
        s.resyncByScan = true;
      }
      break;

    case AWAITING_STREAM_CHANNEL_ID:
      // Parse the size field EITHER WAY. An unregistered channel is not a reason to abandon framing:
      // the frame is self-describing, so read its length and skip its payload (see AWAITING_SIZE2).
      //
      // The shipped code instead set state = AWAITING_DOLLAR here — with '$' and the channel byte
      // ALREADY consumed and the size field never read — then hunted for the next 0x24, which in
      // H264 payload is everywhere. That is what left the reader two bytes out of step and produced
      // the production DESYNCs whose "first bytes" were a length field (05 a2 = 1442).
      s.nextState = AWAITING_SIZE1;
      break;

    case AWAITING_SIZE1:
      s.nextState = AWAITING_SIZE2;
      break;

    case AWAITING_SIZE2: {
      unsigned short const size = (unsigned short)((sizeByte1 << 8) | c);
      if (channelRegistered) {
        s.nextState = AWAITING_PACKET_DATA;
        s.haveFrameHeader = true;
      } else {
        // Complete header for a channel we do not handle: discard exactly this frame's payload and
        // resume at the next frame boundary. No scanning, no guessing, no lost alignment.
        //
        // A DECLARED SIZE OF ZERO is a real case and must not enter the skip state: there is no
        // payload to discard, and entering it would wait for a byte that never comes while the next
        // frame's '$' sat unread in front of it.
        s.skipBytes = size;
        s.nextState = (size == 0) ? AWAITING_DOLLAR : SKIPPING_UNHANDLED_PAYLOAD;
      }
      break;
    }

    case AWAITING_PACKET_DATA:
      s.nextState = AWAITING_DOLLAR;
      break;

    case SKIPPING_UNHANDLED_PAYLOAD:
      // The caller owns the remaining-byte countdown (it reads payload in bulk, not byte by byte);
      // this reports only where a byte consumed while skipping leaves us. Staying here is correct
      // until the caller's counter reaches zero, at which point it moves to AWAITING_DOLLAR itself.
      s.nextState = SKIPPING_UNHANDLED_PAYLOAD;
      break;
  }
  return s;
}

} // namespace RTPInterleavedFraming

#endif

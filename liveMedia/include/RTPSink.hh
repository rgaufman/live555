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
// Copyright (c) 1996-2025 Live Networks, Inc.  All rights reserved.
// RTP Sinks
// C++ header

#ifndef _RTP_SINK_HH
#define _RTP_SINK_HH

#ifndef _MEDIA_SINK_HH
#include "MediaSink.hh"
#endif
#ifndef _RTP_INTERFACE_HH
#include "RTPInterface.hh"
#endif
#ifndef _SRTP_CRYPTOGRAPHIC_CONTEXT_HH
#include "SRTPCryptographicContext.hh"
#endif

class RTPTransmissionStatsDB; // forward

class RTPSink: public MediaSink {
public:
  static Boolean lookupByName(UsageEnvironment& env, char const* sinkName,
			      RTPSink*& resultSink);

  // used by RTSP servers:
  Groupsock const& groupsockBeingUsed() const { return *(fRTPInterface.gs()); }
  Groupsock& groupsockBeingUsed() { return *(fRTPInterface.gs()); }

  unsigned char rtpPayloadType() const { return fRTPPayloadType; }
  unsigned rtpTimestampFrequency() const { return fTimestampFrequency; }
  void setRTPTimestampFrequency(unsigned freq) { fTimestampFrequency = freq; }
  char const* rtpPayloadFormatName() const { return fRTPPayloadFormatName; }

  unsigned numChannels() const { return fNumChannels; }

  void setupForSRTP(Boolean useEncryption, u_int32_t roc);
      // sets up keying/encryption state for streaming via SRTP, using default values.
  u_int8_t* setupForSRTP(Boolean useEncryption, u_int32_t roc,
			 unsigned& resultMIKEYStateMessageSize);
      // as above, but returns the binary MIKEY state
  void setupForSRTP(u_int8_t const* MIKEYStateMessage, unsigned MIKEYStateMessageSize,
		    u_int32_t roc);
      // as above, but takes a MIKEY state message as parameter

  virtual char const* sdpMediaType() const; // for use in SDP m= lines
  virtual char* rtpmapLine() const; // returns a string to be delete[]d
  virtual char* keyMgmtLine(); // returns a string to be delete[]d
  virtual char const* auxSDPLine();
      // optional SDP line (e.g. a=fmtp:...)

  u_int16_t currentSeqNo() const { return fSeqNo; }
  u_int32_t presetNextTimestamp();
      // ensures that the next timestamp to be used will correspond to
      // the current 'wall clock' time.

  RTPTransmissionStatsDB& transmissionStatsDB() const {
    return *fTransmissionStatsDB;
  }

  Boolean nextTimestampHasBeenPreset() const { return fNextTimestampHasBeenPreset; }
  Boolean& enableRTCPReports() { return fEnableRTCPReports; }

  void getTotalBitrate(unsigned& outNumBytes, double& outElapsedTime);
      // returns the number of bytes sent since the last time that we
      // were called, and resets the counter.

  struct timeval const& creationTime() const { return fCreationTime; }
  struct timeval const& initialPresentationTime() const { return fInitialPresentationTime; }
  struct timeval const& mostRecentPresentationTime() const { return fMostRecentPresentationTime; }
  void resetPresentationTimes();

  // Hacks to allow sending RTP over TCP (RFC 2236, section 10.12):
  void setStreamSocket(int sockNum, unsigned char streamChannelId, TLSState* tlsState) {
    fRTPInterface.setStreamSocket(sockNum, streamChannelId, tlsState);
  }
  void addStreamSocket(int sockNum, unsigned char streamChannelId, TLSState* tlsState) {
    fRTPInterface.addStreamSocket(sockNum, streamChannelId, tlsState);
  }
  void removeStreamSocket(int sockNum, unsigned char streamChannelId) {
    fRTPInterface.removeStreamSocket(sockNum, streamChannelId);
  }
  unsigned& estimatedBitrate() { return fEstimatedBitrate; } // kbps; usually 0 (i.e., unset)

  u_int32_t SSRC() const { return fSSRC; }
     // later need a means of changing the SSRC if there's a collision #####

  SRTPCryptographicContext* getCrypto() const { return fCrypto; }
  u_int32_t srtpROC() const;

protected:
  RTPSink(UsageEnvironment& env,
	  Groupsock* rtpGS, unsigned char rtpPayloadType,
	  u_int32_t rtpTimestampFrequency,
	  char const* rtpPayloadFormatName,
	  unsigned numChannels);
	// abstract base class

  virtual ~RTPSink();

  // used by RTCP:
  friend class RTCPInstance;
  friend class RTPTransmissionStats;
  u_int32_t convertToRTPTimestamp(struct timeval tv);
  unsigned packetCount() const {return fPacketCount;}
  unsigned octetCount() const {return fOctetCount;}

protected:
  RTPInterface fRTPInterface;
  unsigned char fRTPPayloadType;
  unsigned fPacketCount, fOctetCount, fTotalOctetCount /*incl RTP hdr*/;
  struct timeval fTotalOctetCountStartTime, fInitialPresentationTime, fMostRecentPresentationTime;
  u_int32_t fCurrentTimestamp;
  u_int16_t fSeqNo;

  // Optional key management and crypto state; used if we are streaming SRTP
  MIKEYState* fMIKEYState;
  SRTPCryptographicContext* fCrypto;

private:
  // redefined virtual functions:
  virtual Boolean isRTPSink() const;

private:
  u_int32_t fSSRC, fTimestampBase;
  unsigned fTimestampFrequency;
  Boolean fNextTimestampHasBeenPreset;
  Boolean fEnableRTCPReports; // whether RTCP "SR" reports should be sent for this sink (default: True)
  char const* fRTPPayloadFormatName;
  unsigned fNumChannels;
  struct timeval fCreationTime;
  unsigned fEstimatedBitrate; // set on creation if known; otherwise 0

  RTPTransmissionStatsDB* fTransmissionStatsDB;
};


class RTPTransmissionStats; // forward

class RTPTransmissionStatsDB {
public:
  unsigned numReceivers() const { return fNumReceivers; }

  class Iterator {
  public:
    Iterator(RTPTransmissionStatsDB& receptionStatsDB);
    virtual ~Iterator();

    RTPTransmissionStats* next();
        // NULL if none

  private:
    HashTable::Iterator* fIter;
  };

  // The following is called whenever a RTCP RR packet is received:
  void noteIncomingRR(u_int32_t SSRC, struct sockaddr_storage const& lastFromAddress,
                      unsigned lossStats, unsigned lastPacketNumReceived,
                      unsigned jitter, unsigned lastSRTime, unsigned diffSR_RRTime);

  // The following is called when a RTCP BYE packet is received:
  void removeRecord(u_int32_t SSRC);

  RTPTransmissionStats* lookup(u_int32_t SSRC) const;

private: // constructor and destructor, called only by RTPSink:
  friend class RTPSink;
  RTPTransmissionStatsDB(RTPSink& rtpSink);
  virtual ~RTPTransmissionStatsDB();

private:
  void add(u_int32_t SSRC, RTPTransmissionStats* stats);

private:
  friend class Iterator;
  unsigned fNumReceivers;
  RTPSink& fOurRTPSink;
  HashTable* fTable;
};

class RTPTransmissionStats {
public:
  u_int32_t SSRC() const {return fSSRC;}
  struct sockaddr_storage const& lastFromAddress() const {return fLastFromAddress;}
  unsigned lastPacketNumReceived() const {return fLastPacketNumReceived;}
  unsigned firstPacketNumReported() const {return fFirstPacketNumReported;}
  unsigned totNumPacketsLost() const {return fTotNumPacketsLost;}
  unsigned jitter() const {return fJitter;}
  unsigned lastSRTime() const { return fLastSRTime; }
  unsigned diffSR_RRTime() const { return fDiffSR_RRTime; }
  unsigned roundTripDelay() const;
      // The round-trip delay (in units of 1/65536 seconds) computed from
      // the most recently-received RTCP RR packet.
  struct timeval const& timeCreated() const {return fTimeCreated;}
  struct timeval const& lastTimeReceived() const {return fTimeReceived;}
  void getTotalOctetCount(u_int32_t& hi, u_int32_t& lo);
  void getTotalPacketCount(u_int32_t& hi, u_int32_t& lo);

  // Information which requires at least two RRs to have been received:
  unsigned packetsReceivedSinceLastRR() const;
  u_int8_t packetLossRatio() const { return fPacketLossRatio; }
     // as an 8-bit fixed-point number
  int packetsLostBetweenRR() const;

private:
  // called only by RTPTransmissionStatsDB:
  friend class RTPTransmissionStatsDB;
  RTPTransmissionStats(RTPSink& rtpSink, u_int32_t SSRC);
  virtual ~RTPTransmissionStats();

  void noteIncomingRR(struct sockaddr_storage const& lastFromAddress,
		      unsigned lossStats, unsigned lastPacketNumReceived,
                      unsigned jitter,
		      unsigned lastSRTime, unsigned diffSR_RRTime);

private:
  RTPSink& fOurRTPSink;
  u_int32_t fSSRC;
  struct sockaddr_storage fLastFromAddress;
  unsigned fLastPacketNumReceived;
  u_int8_t fPacketLossRatio;
  unsigned fTotNumPacketsLost;
  unsigned fJitter;
  unsigned fLastSRTime;
  unsigned fDiffSR_RRTime;
  struct timeval fTimeCreated, fTimeReceived;
  Boolean fAtLeastTwoRRsHaveBeenReceived;
  unsigned fOldLastPacketNumReceived;
  unsigned fOldTotNumPacketsLost;
  Boolean fFirstPacket;
  unsigned fFirstPacketNumReported;
  u_int32_t fLastOctetCount, fTotalOctetCount_hi, fTotalOctetCount_lo;
  u_int32_t fLastPacketCount, fTotalPacketCount_hi, fTotalPacketCount_lo;
};

#endif

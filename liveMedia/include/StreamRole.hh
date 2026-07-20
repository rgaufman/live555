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
// Which END of the proxy a TCP socket belongs to, and what a teardown on it is
// therefore allowed to CLAIM.
//
// A proxy has two kinds of TCP socket carrying interleaved RTP, and
// RTPInterface handles both with the same code:
//
//   UPSTREAM   -- we RECEIVE from the camera. Losing it is a real fault and we
//                 must reconnect.
//   DOWNSTREAM -- we SEND to a local consumer (recorder, detector, viewer).
//                 Losing it is routine: that process restarted or went away.
//                 The camera is untouched and there is nothing to reconnect.
//
// Without this distinction every teardown was emitted as TX-FIN / TX-RST with
// "Far-end close PROVEN". Measured on box 6397272975e8c006c6fc over one hour:
//
//     166  peer 127.0.0.1     <- our own recorder/detector reconnecting
//      20  peer 192.168.11.52 <- the worst ACTUAL camera
//      14  peer 192.168.11.66
//
// so the fault population was dominated ~8:1 by our own clients, and
// bin/check_flap tallied it into "REAL upstream stalls (camera/network/NIC)"
// with the advice "FIX the camera link, NOT the watchdog". That is our own
// behaviour reported as the customer's broken network.
//
// Header-only and dependency-light so it can be unit-tested standalone
// (test/test_stream_role.cpp), matching RTPTcpTeardown.hh and friends.

#ifndef _STREAM_ROLE_HH
#define _STREAM_ROLE_HH

#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif

#include "StreamFaultCodes.hh" // TX_CODE_* tokens these decisions map onto

enum StreamRole {
  // Order matters only in that UNKNOWN is 0, so a zero-initialised field is
  // "we do not know" rather than a confident wrong answer.
  STREAM_ROLE_UNKNOWN = 0,
  STREAM_ROLE_UPSTREAM,
  STREAM_ROLE_DOWNSTREAM
};

// Derive the role from what the socket's owning Medium IS, rather than from
// anything about the peer.
//
// Deriving it from the peer ADDRESS was considered and rejected: loopback
// happens to identify our consumers on a TetherBox today, but a downstream
// viewer can be remote, and a camera can in principle be proxied over
// loopback. The owner's direction is a structural fact; the address is a
// coincidence of deployment.
//
// An owner that is neither (or somehow both) yields UNKNOWN. It must NOT
// default to upstream -- that default is exactly the defect this file exists
// to remove.
static inline StreamRole streamRoleForOwner(Boolean isSource, Boolean isSink) {
  if (isSource && !isSink) return STREAM_ROLE_UPSTREAM;   // we receive -> camera side
  if (isSink && !isSource) return STREAM_ROLE_DOWNSTREAM; // we send -> consumer side
  return STREAM_ROLE_UNKNOWN;
}

static inline char const* streamRoleName(StreamRole role) {
  switch (role) {
    case STREAM_ROLE_UPSTREAM: return "upstream";
    case STREAM_ROLE_DOWNSTREAM: return "downstream";
    case STREAM_ROLE_UNKNOWN: return "unknown";
  }
  // No default label above, so a newly added role is a compiler warning rather
  // than a silent mislabel; this line only guards a cast-in bogus value.
  return "unknown";
}

// Map a teardown classification onto the code it may actually be logged under.
//
// Upstream keeps the existing camera-side vocabulary verbatim, so every grep,
// dashboard and diagnostic that counts real camera faults keeps working
// unchanged -- and starts being ACCURATE, because the loopback noise is no
// longer mixed in.
static inline char const* teardownCodeForRole(StreamRole role, char const* upstreamCode) {
  switch (role) {
    case STREAM_ROLE_UPSTREAM: return upstreamCode;
    case STREAM_ROLE_DOWNSTREAM: return TX_CODE_CLIENT_GONE;
    case STREAM_ROLE_UNKNOWN: return TX_CODE_ROLE_UNKNOWN;
  }
  return TX_CODE_ROLE_UNKNOWN;
}

// Whether the log line may say the far end closed on us.
//
// Only meaningful for upstream. Saying "Far-end close PROVEN" about a
// downstream socket blames the camera for our own recorder restarting, and
// saying it about an unknown role asserts something we have not established.
static inline Boolean teardownMayBlamePeer(StreamRole role) {
  return role == STREAM_ROLE_UPSTREAM;
}

// Whether losing this socket should drive a reconnect of the upstream session.
//
// A consumer leaving must never tear down the camera session: the other
// consumers are still watching and the camera never went anywhere.
static inline Boolean teardownShouldReconnect(StreamRole role) {
  return role == STREAM_ROLE_UPSTREAM;
}

#endif

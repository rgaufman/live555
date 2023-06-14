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
// Copyright (c) 1996-2023 Live Networks, Inc.  All rights reserved.
// 'Group sockets'
// Implementation

#include "Groupsock.hh"
#include "GroupsockHelper.hh"

#ifndef NO_SSTREAM
#include <sstream>
#endif
#include <stdio.h>

///////// OutputSocket //////////

OutputSocket::OutputSocket(UsageEnvironment& env, int family)
  : Socket(env, 0 /* let kernel choose port */, family),
    fSourcePort(0), fLastSentTTL(256/*hack: a deliberately invalid value*/) {
}

OutputSocket::OutputSocket(UsageEnvironment& env, Port port, int family)
  : Socket(env, port, family),
    fSourcePort(0), fLastSentTTL(256/*hack: a deliberately invalid value*/) {
}

OutputSocket::~OutputSocket() {
}

Boolean OutputSocket::write(struct sockaddr_storage const& addressAndPort, u_int8_t ttl,
			    unsigned char* buffer, unsigned bufferSize) {
  if ((unsigned)ttl == fLastSentTTL) {
    // Optimization: Don't do a 'set TTL' system call again
    if (!writeSocket(env(), socketNum(), addressAndPort, buffer, bufferSize)) return False;
  } else {
    if (!writeSocket(env(), socketNum(), addressAndPort, ttl, buffer, bufferSize)) return False;
    fLastSentTTL = (unsigned)ttl;
  }

  if (sourcePortNum() == 0) {
    // Now that we've sent a packet, we can find out what the
    // kernel chose as our ephemeral source port number:
    if (!getSourcePort(env(), socketNum(), addressAndPort.ss_family, fSourcePort)) {
      if (DebugLevel >= 1)
	env() << *this
	     << ": failed to get source port: "
	     << env().getResultMsg() << "\n";
      return False;
    }
  }

  return True;
}

// By default, we don't do reads:
Boolean OutputSocket
::handleRead(unsigned char* /*buffer*/, unsigned /*bufferMaxSize*/,
	     unsigned& /*bytesRead*/, struct sockaddr_storage& /*fromAddressAndPort*/) {
  return True;
}


///////// destRecord //////////

destRecord
::destRecord(struct sockaddr_storage const& addr, Port const& port, u_int8_t ttl, unsigned sessionId,
	     destRecord* next)
  : fNext(next), fGroupEId(addr, port.num(), ttl), fSessionId(sessionId) {
}

destRecord::~destRecord() {
  delete fNext;
}


///////// Groupsock //////////

NetInterfaceTrafficStats Groupsock::statsIncoming;
NetInterfaceTrafficStats Groupsock::statsOutgoing;

// Constructor for a source-independent multicast group
Groupsock::Groupsock(UsageEnvironment& env, struct sockaddr_storage const& groupAddr,
		     Port port, u_int8_t ttl)
  : OutputSocket(env, port, groupAddr.ss_family),
    fDests(new destRecord(groupAddr, port, ttl, 0, NULL)),
    fIncomingGroupEId(groupAddr, port.num(), ttl) {
  if (!socketJoinGroup(env, socketNum(), groupAddr)) {
    if (DebugLevel >= 1) {
      env << *this << ": failed to join group: "
	  << env.getResultMsg() << "\n";
    }
  }

  // Make sure we can get our source address:
  if (!weHaveAnIPAddress(env)) {
    if (DebugLevel >= 0) { // this is a fatal error
      env << "Unable to determine our source address: "
	  << env.getResultMsg() << "\n";
    }
  }

  if (DebugLevel >= 2) env << *this << ": created\n";
}

// Constructor for a source-specific multicast group
Groupsock::Groupsock(UsageEnvironment& env, struct sockaddr_storage const& groupAddr,
		     struct sockaddr_storage const& sourceFilterAddr,
		     Port port)
  : OutputSocket(env, port, groupAddr.ss_family),
    fDests(new destRecord(groupAddr, port, 255, 0, NULL)),
    fIncomingGroupEId(groupAddr, sourceFilterAddr, port.num()) {
  // First try a SSM join.  If that fails, try a regular join:
  if (!socketJoinGroupSSM(env, socketNum(), groupAddr, sourceFilterAddr)) {
    if (DebugLevel >= 3) {
      env << *this << ": SSM join failed: "
	  << env.getResultMsg();
      env << " - trying regular join instead\n";
    }
    if (!socketJoinGroup(env, socketNum(), groupAddr)) {
      if (DebugLevel >= 1) {
	env << *this << ": failed to join group: "
	     << env.getResultMsg() << "\n";
      }
    }
  }

  if (DebugLevel >= 2) env << *this << ": created\n";
}

Groupsock::~Groupsock() {
  if (isSSM()) {
    if (!socketLeaveGroupSSM(env(), socketNum(), groupAddress(), sourceFilterAddress())) {
      socketLeaveGroup(env(), socketNum(), groupAddress());
    }
  } else {
    socketLeaveGroup(env(), socketNum(), groupAddress());
  }

  delete fDests;

  if (DebugLevel >= 2) env() << *this << ": deleting\n";
}

destRecord* Groupsock
::createNewDestRecord(struct sockaddr_storage const& addr, Port const& port, u_int8_t ttl,
		      unsigned sessionId, destRecord* next) {
  // Default implementation:
  return new destRecord(addr, port, ttl, sessionId, next);
}

void
Groupsock::changeDestinationParameters(struct sockaddr_storage const& newDestAddr,
				       Port newDestPort, int newDestTTL, unsigned sessionId) {
  destRecord* dest;
  for (dest = fDests; dest != NULL && dest->fSessionId != sessionId; dest = dest->fNext) {}

  if (dest == NULL) { // There's no existing 'destRecord' for this "sessionId"; add a new one:
    fDests = createNewDestRecord(newDestAddr, newDestPort, newDestTTL, sessionId, fDests);
    return;
  }

  // "dest" is an existing 'destRecord' for this "sessionId"; change its values to the new ones:
  struct sockaddr_storage destAddr = dest->fGroupEId.groupAddress();
  if (!addressIsNull(newDestAddr)) {
    // Replace "destAddr" with "newDestAddr"
    if (!(newDestAddr == destAddr) && IsMulticastAddress(newDestAddr)) {
      // If the new destination is a multicast address, then we assume that
      // we want to join it also.  (If this is not in fact the case, then
      // call "multicastSendOnly()" afterwards.)
      socketLeaveGroup(env(), socketNum(), destAddr);
      socketJoinGroup(env(), socketNum(), newDestAddr);
    }
    destAddr = newDestAddr;
  }

  portNumBits destPortNum = dest->fGroupEId.portNum();
  if (newDestPort.num() != 0) {
    // Replace "destPort" with "newDestPort"
    if (newDestPort.num() != destPortNum && IsMulticastAddress(destAddr)) {
      // Also bind to the new port number:
      changePort(newDestPort);
      // And rejoin the multicast group:
      socketJoinGroup(env(), socketNum(), destAddr);
    }
    destPortNum = newDestPort.num();
  }

  u_int8_t destTTL = ttl();
  if (newDestTTL != ~0) {
    // Replace "destTTL" with "newDestTTL"
    destTTL = (u_int8_t)newDestTTL;
  }

  dest->fGroupEId = GroupEId(destAddr, destPortNum, destTTL);

  // Finally, remove any other 'destRecord's that might also have this "sessionId":
  removeDestinationFrom(dest->fNext, sessionId);
}

unsigned Groupsock
::lookupSessionIdFromDestination(struct sockaddr_storage const& destAddrAndPort) const {
  destRecord* dest = lookupDestRecordFromDestination(destAddrAndPort);
  if (dest == NULL) return 0;

  return dest->fSessionId;
}

void Groupsock::addDestination(struct sockaddr_storage const& addr, Port const& port,
			       unsigned sessionId) {
  // Default implementation:
  // If there's no existing 'destRecord' with the same "addr", "port", and "sessionId", add a new one:
  for (destRecord* dest = fDests; dest != NULL; dest = dest->fNext) {
    if (dest->fSessionId == sessionId &&
	dest->fGroupEId.groupAddress() == addr &&
	dest->fGroupEId.portNum() == port.num()) {
      return;
    }
  }
  
  fDests = createNewDestRecord(addr, port, 255, sessionId, fDests);
}

void Groupsock::removeDestination(unsigned sessionId) {
  // Default implementation:
  removeDestinationFrom(fDests, sessionId);
}

void Groupsock::removeAllDestinations() {
  delete fDests; fDests = NULL;
}

void Groupsock::multicastSendOnly() {
  // We disable this code for now, because - on some systems - leaving the multicast group seems to cause sent packets
  // to not be received by other applications (at least, on the same host).
#if 0
  socketLeaveGroup(env(), socketNum(), groupAddress(r);
  for (destRecord* dests = fDests; dests != NULL; dests = dests->fNext) {
    socketLeaveGroup(env(), socketNum(), dests->fGroupEId.groupAddress(r);
  }
#endif
}

Boolean Groupsock::output(UsageEnvironment& env, unsigned char* buffer, unsigned bufferSize) {
  do {
    // First, do the datagram send, to each destination:
    Boolean writeSuccess = True;
    for (destRecord* dests = fDests; dests != NULL; dests = dests->fNext) {
      if (!write(dests->fGroupEId.groupAddress(), dests->fGroupEId.ttl(), buffer, bufferSize)) {
	writeSuccess = False;
	break;
      }
    }
    if (!writeSuccess) break;
    statsOutgoing.countPacket(bufferSize);
    statsGroupOutgoing.countPacket(bufferSize);

    if (DebugLevel >= 3) {
      env << *this << ": wrote " << bufferSize << " bytes, ttl " << (unsigned)ttl() << "\n";
    }
    return True;
  } while (0);

  if (DebugLevel >= 0) { // this is a fatal error
    UsageEnvironment::MsgString msg = strDup(env.getResultMsg());
    env.setResultMsg("Groupsock write failed: ", msg);
    delete[] (char*)msg;
  }
  return False;
}

Boolean Groupsock::handleRead(unsigned char* buffer, unsigned bufferMaxSize,
			      unsigned& bytesRead,
			      struct sockaddr_storage& fromAddressAndPort) {
  bytesRead = 0; // initially

  int numBytes = readSocket(env(), socketNum(),
			    buffer, bufferMaxSize, fromAddressAndPort);
  if (numBytes < 0) {
    if (DebugLevel >= 0) { // this is a fatal error
      UsageEnvironment::MsgString msg = strDup(env().getResultMsg());
      env().setResultMsg("Groupsock read failed: ", msg);
      delete[] (char*)msg;
    }
    return False;
  }

  // If we're a SSM group, make sure the source address matches:
  if (isSSM() && !(fromAddressAndPort == sourceFilterAddress())) return True;

  // We'll handle this data.
  bytesRead = numBytes;

  if (!wasLoopedBackFromUs(env(), fromAddressAndPort)) {
    statsIncoming.countPacket(bytesRead);
    statsGroupIncoming.countPacket(bytesRead);
  }
  if (DebugLevel >= 3) {
    env() << *this << ": read " << bytesRead << " bytes from " << AddressString(fromAddressAndPort).val() << ", port " << ntohs(portNum(fromAddressAndPort)) << "\n";
  }

  return True;
}

Boolean Groupsock::wasLoopedBackFromUs(UsageEnvironment& env,
				       struct sockaddr_storage const& fromAddressAndPort) {
  if (fromAddressAndPort.ss_family != AF_INET) return False; // later update for IPv6
  
  struct sockaddr_in const& fromAddressAndPort4 = (struct sockaddr_in const&)fromAddressAndPort;
  if (fromAddressAndPort4.sin_addr.s_addr == ourIPv4Address(env) ||
      fromAddressAndPort4.sin_addr.s_addr == 0x7F000001/*127.0.0.1*/) {
    if (portNum(fromAddressAndPort) == sourcePortNum()) {
#ifdef DEBUG_LOOPBACK_CHECKING
      if (DebugLevel >= 3) {
	env() << *this << ": got looped-back packet\n";
      }
#endif
      return True;
    }
  }

  return False;
}

destRecord* Groupsock
::lookupDestRecordFromDestination(struct sockaddr_storage const& targetAddrAndPort) const {
  for (destRecord* dest = fDests; dest != NULL; dest = dest->fNext) {
    if (dest->fGroupEId.groupAddress() == targetAddrAndPort &&
	dest->fGroupEId.portNum() == portNum(targetAddrAndPort)) {
      return dest;
    }
  }

  return NULL;
}

void Groupsock::removeDestinationFrom(destRecord*& dests, unsigned sessionId) {
  destRecord** destsPtr = &dests;
  while (*destsPtr != NULL) {
    if (sessionId == (*destsPtr)->fSessionId) {
      // Remove the record pointed to by *destsPtr :
      destRecord* next = (*destsPtr)->fNext;
      (*destsPtr)->fNext = NULL;
      delete (*destsPtr);
      *destsPtr = next;
    } else {
      destsPtr = &((*destsPtr)->fNext);
    }
  }
}

UsageEnvironment& operator<<(UsageEnvironment& s, const Groupsock& g) {
  UsageEnvironment& s1 = s << timestampString() << " Groupsock("
			   << g.socketNum() << ": "
			   << AddressString(g.groupAddress()).val()
			   << ", " << g.port() << ", ";
  if (g.isSSM()) {
    return s1 << "SSM source: "
	      <<  AddressString(g.sourceFilterAddress()).val() << ")";
  } else {
    return s1 << (unsigned)(g.ttl()) << ")";
  }
}


////////// GroupsockLookupTable //////////


// A hash table used to index Groupsocks by socket number.

static HashTable*& getSocketTable(UsageEnvironment& env) {
  _groupsockPriv* priv = groupsockPriv(env);
  if (priv->socketTable == NULL) { // We need to create it
    priv->socketTable = HashTable::create(ONE_WORD_HASH_KEYS);
  }
  return priv->socketTable;
}

static Boolean unsetGroupsockBySocket(Groupsock const* groupsock) {
  do {
    if (groupsock == NULL) break;

    int sock = groupsock->socketNum();
    // Make sure "sock" is in bounds:
    if (sock < 0) break;

    HashTable*& sockets = getSocketTable(groupsock->env());

    Groupsock* gs = (Groupsock*)sockets->Lookup((char*)(long)sock);
    if (gs == NULL || gs != groupsock) break;
    sockets->Remove((char*)(long)sock);

    if (sockets->IsEmpty()) {
      // We can also delete the table (to reclaim space):
      delete sockets; sockets = NULL;
      reclaimGroupsockPriv(gs->env());
    }

    return True;
  } while (0);

  return False;
}

static Boolean setGroupsockBySocket(UsageEnvironment& env, int sock,
				    Groupsock* groupsock) {
  do {
    // Make sure the "sock" parameter is in bounds:
    if (sock < 0) {
      char buf[100];
      sprintf(buf, "trying to use bad socket (%d)", sock);
      env.setResultMsg(buf);
      break;
    }

    HashTable* sockets = getSocketTable(env);

    // Make sure we're not replacing an existing Groupsock (although that shouldn't happen)
    Boolean alreadyExists
      = (sockets->Lookup((char*)(long)sock) != 0);
    if (alreadyExists) {
      char buf[100];
      sprintf(buf, "Attempting to replace an existing socket (%d)", sock);
      env.setResultMsg(buf);
      break;
    }

    sockets->Add((char*)(long)sock, groupsock);
    return True;
  } while (0);

  return False;
}

static Groupsock* getGroupsockBySocket(UsageEnvironment& env, int sock) {
  do {
    // Make sure the "sock" parameter is in bounds:
    if (sock < 0) break;

    HashTable* sockets = getSocketTable(env);
    return (Groupsock*)sockets->Lookup((char*)(long)sock);
  } while (0);

  return NULL;
}

Groupsock*
GroupsockLookupTable::Fetch(UsageEnvironment& env,
			    struct sockaddr_storage const& groupAddress,
			    Port port, u_int8_t ttl,
			    Boolean& isNew) {
  isNew = False;
  Groupsock* groupsock;
  do {
    groupsock = (Groupsock*) fTable.Lookup(groupAddress, port);
    if (groupsock == NULL) { // we need to create one:
      groupsock = AddNew(env, groupAddress, nullAddress(), port, ttl);
      if (groupsock == NULL) break;
      isNew = True;
    }
  } while (0);

  return groupsock;
}

Groupsock*
GroupsockLookupTable::Fetch(UsageEnvironment& env,
			    struct sockaddr_storage const& groupAddress,
			    struct sockaddr_storage const& sourceFilterAddr, Port port,
			    Boolean& isNew) {
  isNew = False;
  Groupsock* groupsock;
  do {
    groupsock
      = (Groupsock*) fTable.Lookup(groupAddress, sourceFilterAddr, port);
    if (groupsock == NULL) { // we need to create one:
      groupsock = AddNew(env, groupAddress, sourceFilterAddr, port, 0);
      if (groupsock == NULL) break;
      isNew = True;
    }
  } while (0);

  return groupsock;
}

Groupsock*
GroupsockLookupTable::Lookup(struct sockaddr_storage const& groupAddress, Port port) {
  return (Groupsock*) fTable.Lookup(groupAddress, port);
}

Groupsock*
GroupsockLookupTable::Lookup(struct sockaddr_storage const& groupAddress,
			     struct sockaddr_storage const& sourceFilterAddr, Port port) {
  return (Groupsock*) fTable.Lookup(groupAddress, sourceFilterAddr, port);
}

Groupsock* GroupsockLookupTable::Lookup(UsageEnvironment& env, int sock) {
  return getGroupsockBySocket(env, sock);
}

Boolean GroupsockLookupTable::Remove(Groupsock const* groupsock) {
  unsetGroupsockBySocket(groupsock);

  return fTable.Remove(groupsock->groupAddress(), groupsock->sourceFilterAddress(), groupsock->port());
}

Groupsock* GroupsockLookupTable::AddNew(UsageEnvironment& env,
					struct sockaddr_storage const& groupAddress,
					struct sockaddr_storage const& sourceFilterAddress,
					Port port, u_int8_t ttl) {
  Groupsock* groupsock;
  do {
    if (addressIsNull(sourceFilterAddress)) {
      // regular, ISM groupsock
      groupsock = new Groupsock(env, groupAddress, port, ttl);
    } else {
      // SSM groupsock
      groupsock = new Groupsock(env, groupAddress, sourceFilterAddress, port);
    }

    if (groupsock == NULL || groupsock->socketNum() < 0) break;

    if (!setGroupsockBySocket(env, groupsock->socketNum(), groupsock)) break;

    fTable.Add(groupAddress, sourceFilterAddress, port, (void*)groupsock);
  } while (0);

  return groupsock;
}

GroupsockLookupTable::Iterator::Iterator(GroupsockLookupTable& groupsocks)
  : fIter(AddressPortLookupTable::Iterator(groupsocks.fTable)) {
}

Groupsock* GroupsockLookupTable::Iterator::next() {
  return (Groupsock*) fIter.next();
};

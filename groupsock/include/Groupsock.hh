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
// "groupsock"
// Copyright (c) 1996-2025 Live Networks, Inc.  All rights reserved.
// 'Group sockets'
// C++ header

#ifndef _GROUPSOCK_HH
#define _GROUPSOCK_HH

#ifndef _GROUPSOCK_VERSION_HH
#include "groupsock_version.hh"
#endif

#ifndef _NET_INTERFACE_HH
#include "NetInterface.hh"
#endif

#ifndef _GROUPEID_HH
#include "GroupEId.hh"
#endif

// An "OutputSocket" is (by default) used only to send packets.
// No packets are received on it (unless a subclass arranges this)

class OutputSocket: public Socket {
public:
  OutputSocket(UsageEnvironment& env, int family);
  virtual ~OutputSocket();

  virtual Boolean write(struct sockaddr_storage const& addressAndPort, u_int8_t ttl,
			unsigned char* buffer, unsigned bufferSize);

protected:
  OutputSocket(UsageEnvironment& env, Port port, int family);

  portNumBits sourcePortNum() const {return fSourcePort.num();}

private: // redefined virtual function
  virtual Boolean handleRead(unsigned char* buffer, unsigned bufferMaxSize,
			     unsigned& bytesRead,
			     struct sockaddr_storage& fromAddressAndPort);

private:
  Port fSourcePort;
  unsigned fLastSentTTL;
};

class destRecord {
public:
  destRecord(struct sockaddr_storage const& addr, Port const& port, u_int8_t ttl, unsigned sessionId,
	     destRecord* next);
  virtual ~destRecord();

public:
  destRecord* fNext;
  GroupEId fGroupEId;
  unsigned fSessionId;
};

// A "Groupsock" is used to both send and receive packets.
// As the name suggests, it was originally designed to send/receive
// multicast, but it can send/receive unicast as well.

class Groupsock: public OutputSocket {
public:
  Groupsock(UsageEnvironment& env, struct sockaddr_storage const& groupAddr,
	    Port port, u_int8_t ttl);
      // used for a 'source-independent multicast' group
  Groupsock(UsageEnvironment& env, struct sockaddr_storage const& groupAddr,
	    struct sockaddr_storage const& sourceFilterAddr,
	    Port port);
      // used for a 'source-specific multicast' group

  virtual ~Groupsock();

  virtual destRecord* createNewDestRecord(struct sockaddr_storage const& addr, Port const& port, u_int8_t ttl, unsigned sessionId, destRecord* next);
      // Can be redefined by subclasses that also subclass "destRecord"

  void changeDestinationParameters(struct sockaddr_storage const& newDestAddr,
				   Port newDestPort, int newDestTTL,
				   unsigned sessionId = 0);
      // By default, the destination address, port and ttl for
      // outgoing packets are those that were specified in
      // the constructor.  This works OK for multicast sockets,
      // but for unicast we usually want the destination port
      // number, at least, to be different from the source port.
      // (If a parameter is 0 (or ~0 for ttl), then no change is made to that parameter.)
      // (If no existing "destRecord" exists with this "sessionId", then we add a new "destRecord".)
  unsigned lookupSessionIdFromDestination(struct sockaddr_storage const& destAddrAndPort) const;
      // returns 0 if not found

  // As a special case, we also allow multiple destinations (addresses & ports)
  // (This can be used to implement multi-unicast.)
  virtual void addDestination(struct sockaddr_storage const& addr, Port const& port,
			      unsigned sessionId);
  virtual void removeDestination(unsigned sessionId);
  void removeAllDestinations();
  Boolean hasMultipleDestinations() const { return fDests != NULL && fDests->fNext != NULL; }

  struct sockaddr_storage const& groupAddress() const {
    return fIncomingGroupEId.groupAddress();
  }
  struct sockaddr_storage const& sourceFilterAddress() const {
    return fIncomingGroupEId.sourceFilterAddress();
  }

  Boolean isSSM() const {
    return fIncomingGroupEId.isSSM();
  }

  u_int8_t ttl() const { return fIncomingGroupEId.ttl(); }

  void multicastSendOnly(); // send, but don't receive any multicast packets

  virtual Boolean output(UsageEnvironment& env, unsigned char* buffer, unsigned bufferSize);

  static NetInterfaceTrafficStats statsIncoming;
  static NetInterfaceTrafficStats statsOutgoing;
  NetInterfaceTrafficStats statsGroupIncoming; // *not* static
  NetInterfaceTrafficStats statsGroupOutgoing; // *not* static

  Boolean wasLoopedBackFromUs(UsageEnvironment& env,
			      struct sockaddr_storage const& fromAddressAndPort);

public: // redefined virtual functions
  virtual Boolean handleRead(unsigned char* buffer, unsigned bufferMaxSize,
			     unsigned& bytesRead,
			     struct sockaddr_storage& fromAddressAndPort);

protected:
  destRecord* lookupDestRecordFromDestination(struct sockaddr_storage const& targetAddrAndPort) const;

private:
  void removeDestinationFrom(destRecord*& dests, unsigned sessionId);
    // used to implement (the public) "removeDestination()", and "changeDestinationParameters()"
protected:
  destRecord* fDests;
private:
  GroupEId fIncomingGroupEId;
};

UsageEnvironment& operator<<(UsageEnvironment& s, const Groupsock& g);

// A data structure for looking up a 'groupsock'
// by (multicast address, port), or by socket number
class GroupsockLookupTable {
public:
  Groupsock* Fetch(UsageEnvironment& env, struct sockaddr_storage const& groupAddress,
		   Port port, u_int8_t ttl, Boolean& isNew);
      // Creates a new Groupsock if none already exists
  Groupsock* Fetch(UsageEnvironment& env, struct sockaddr_storage const& groupAddress,
		   struct sockaddr_storage const& sourceFilterAddr,
		   Port port, Boolean& isNew);
      // Creates a new Groupsock if none already exists
  Groupsock* Lookup(struct sockaddr_storage const& groupAddress, Port port);
      // Returns NULL if none already exists
  Groupsock* Lookup(struct sockaddr_storage const& groupAddress,
		    struct sockaddr_storage const& sourceFilterAddr,
		    Port port);
      // Returns NULL if none already exists
  Groupsock* Lookup(UsageEnvironment& env, int sock);
      // Returns NULL if none already exists
  Boolean Remove(Groupsock const* groupsock);

  // Used to iterate through the groupsocks in the table
  class Iterator {
  public:
    Iterator(GroupsockLookupTable& groupsocks);

    Groupsock* next(); // NULL iff none

  private:
    AddressPortLookupTable::Iterator fIter;
  };

private:
  Groupsock* AddNew(UsageEnvironment& env,
		    struct sockaddr_storage const& groupAddress,
		    struct sockaddr_storage const& sourceFilterAddress,
		    Port port, u_int8_t ttl);

private:
  friend class Iterator;
  AddressPortLookupTable fTable;
};

#endif

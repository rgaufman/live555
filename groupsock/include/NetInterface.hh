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
// Network Interfaces
// C++ header

#ifndef _NET_INTERFACE_HH
#define _NET_INTERFACE_HH

#ifndef _NET_ADDRESS_HH
#include "NetAddress.hh"
#endif

class NetInterface {
public:
  virtual ~NetInterface();

  static UsageEnvironment* DefaultUsageEnvironment;
      // if non-NULL, used for each new interface

protected:
  NetInterface(); // virtual base class
};

class Socket: public NetInterface {
public:
  virtual ~Socket();
  void reset(); // closes the socket, and sets "fSocketNum" to -1

  virtual Boolean handleRead(unsigned char* buffer, unsigned bufferMaxSize,
			     unsigned& bytesRead,
			     struct sockaddr_storage& fromAddress) = 0;
      // Returns False on error; resultData == NULL if data ignored

  int socketNum() const { return fSocketNum; }

  Port port() const {
    return fPort;
  }

  UsageEnvironment& env() const { return fEnv; }

  static int DebugLevel;

protected:
  Socket(UsageEnvironment& env, Port port, int family); // virtual base class

  Boolean changePort(Port newPort); // will also cause socketNum() to change

private:
  int fSocketNum;
  UsageEnvironment& fEnv;
  Port fPort;
  int fFamily;
};

UsageEnvironment& operator<<(UsageEnvironment& s, const Socket& sock);

// A data structure for looking up a Socket by port:

class SocketLookupTable {
public:
  virtual ~SocketLookupTable();

  Socket* Fetch(UsageEnvironment& env, Port port, Boolean& isNew);
  // Creates a new Socket if none already exists
  Boolean Remove(Socket const* sock);

protected:
  SocketLookupTable(); // abstract base class
  virtual Socket* CreateNew(UsageEnvironment& env, Port port) = 0;

private:
  HashTable* fTable;
};

// A data structure for counting traffic:

class NetInterfaceTrafficStats {
public:
  NetInterfaceTrafficStats();

  void countPacket(unsigned packetSize);

  float totNumPackets() const {return fTotNumPackets;}
  float totNumBytes() const {return fTotNumBytes;}

  Boolean haveSeenTraffic() const;

private:
  float fTotNumPackets;
  float fTotNumBytes;
};

#endif

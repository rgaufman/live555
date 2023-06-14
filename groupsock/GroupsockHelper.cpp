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
// Copyright (c) 1996-2023 Live Networks, Inc.  All rights reserved.
// Helper routines to implement 'group sockets'
// Implementation

#include "GroupsockHelper.hh"

#if (defined(__WIN32__) || defined(_WIN32)) && !defined(__MINGW32__)
#include <time.h>
extern "C" int initializeWinsockIfNecessary();
#else
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#if !defined(_WIN32)
#include <netinet/tcp.h>
#ifdef __ANDROID_NDK__
#include <android/ndk-version.h>
#define ANDROID_OLD_NDK __NDK_MAJOR__ < 17
#endif
#endif
#include <fcntl.h>
#define initializeWinsockIfNecessary() 1
#endif
#if defined(__WIN32__) || defined(_WIN32) || defined(_QNX4)
#else
#include <signal.h>
#define USE_SIGNALS 1
#endif
#ifndef NO_GETIFADDRS
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <stdio.h>

// By default, use INADDR_ANY for the sending and receiving interfaces (IPv4 only):
ipv4AddressBits SendingInterfaceAddr = INADDR_ANY;
ipv4AddressBits ReceivingInterfaceAddr = INADDR_ANY;
in6_addr ReceivingInterfaceAddr6 = IN6ADDR_ANY_INIT;

static void socketErr(UsageEnvironment& env, char const* errorMsg) {
  env.setResultErrMsg(errorMsg);
}

NoReuse::NoReuse(UsageEnvironment& env)
  : fEnv(env) {
  groupsockPriv(fEnv)->reuseFlag = 0;
}

NoReuse::~NoReuse() {
  groupsockPriv(fEnv)->reuseFlag = 1;
  reclaimGroupsockPriv(fEnv);
}


_groupsockPriv* groupsockPriv(UsageEnvironment& env) {
  if (env.groupsockPriv == NULL) { // We need to create it
    _groupsockPriv* result = new _groupsockPriv;
    result->socketTable = NULL;
    result->reuseFlag = 1; // default value => allow reuse of socket numbers
    env.groupsockPriv = result;
  }
  return (_groupsockPriv*)(env.groupsockPriv);
}

void reclaimGroupsockPriv(UsageEnvironment& env) {
  _groupsockPriv* priv = (_groupsockPriv*)(env.groupsockPriv);
  if (priv->socketTable == NULL && priv->reuseFlag == 1/*default value*/) {
    // We can delete the structure (to save space); it will get created again, if needed:
    delete priv;
    env.groupsockPriv = NULL;
  }
}

static int createSocket(int domain, int type) {
  // Call "socket()" to create a socket of the specified type.
  // But also set it to have the 'close on exec' property (if we can)
  int sock;

  // In case PF_INET(6) is not defined to be AF_INET(6):
  int const domain2 = domain == AF_INET ? PF_INET : domain == AF_INET6 ? PF_INET6 : domain;

#ifdef SOCK_CLOEXEC
  sock = socket(domain2, type|SOCK_CLOEXEC, 0);
  if (sock != -1 || errno != EINVAL) return sock;
  // An "errno" of EINVAL likely means that the system wasn't happy with the SOCK_CLOEXEC; fall through and try again without it:
#endif

  sock = socket(domain2, type, 0);
#ifdef FD_CLOEXEC
  if (sock != -1) fcntl(sock, F_SETFD, FD_CLOEXEC);
#endif
  return sock;
}

int setupDatagramSocket(UsageEnvironment& env, Port port, int domain) {
  if (!initializeWinsockIfNecessary()) {
    socketErr(env, "Failed to initialize 'winsock': ");
    return -1;
  }

  int newSocket = createSocket(domain, SOCK_DGRAM);
  if (newSocket < 0) {
    socketErr(env, "unable to create datagram socket: ");
    return newSocket;
  }

  int reuseFlag = groupsockPriv(env)->reuseFlag;
  reclaimGroupsockPriv(env);
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEADDR) error: ");
    closeSocket(newSocket);
    return -1;
  }

#if defined(__WIN32__) || defined(_WIN32)
  // Windoze doesn't properly handle SO_REUSEPORT or IP_MULTICAST_LOOP
#else
#ifdef SO_REUSEPORT
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEPORT,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEPORT) error: ");
    closeSocket(newSocket);
    return -1;
  }
#endif

#ifdef IP_MULTICAST_LOOP
  const u_int8_t loop = 1;
  if (setsockopt(newSocket,
		 domain == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
		 domain == AF_INET ? IP_MULTICAST_LOOP : IPV6_MULTICAST_LOOP,
		 (const char*)&loop, sizeof loop) < 0) {
    if (domain == AF_INET) { // For some unknown reason, this might not work for IPv6
      socketErr(env, "setsockopt(IP_MULTICAST_LOOP) error: ");
      closeSocket(newSocket);
      return -1;
    }
  }
#endif
#endif

  if (domain == AF_INET) {
    // Note: Windoze requires binding, even if the port number is 0
    ipv4AddressBits addr = INADDR_ANY;
#if defined(__WIN32__) || defined(_WIN32)
#else
    if (port.num() != 0 || ReceivingInterfaceAddr != INADDR_ANY) {
#endif
      if (port.num() == 0) addr = ReceivingInterfaceAddr;
      MAKE_SOCKADDR_IN(name, addr, port.num());
      if (bind(newSocket, (struct sockaddr*)&name, sizeof name) != 0) {
	char tmpBuffer[100];
	sprintf(tmpBuffer, "IPv4 bind() error (port number: %d): ", ntohs(port.num()));
	socketErr(env, tmpBuffer);
	closeSocket(newSocket);
	return -1;
      }
#if defined(__WIN32__) || defined(_WIN32)
#else
    }
#endif
  } else { // IPv6
    in6_addr addr = IN6ADDR_ANY_INIT;
    if (port.num() != 0) {
      // For IPv6 sockets, we need the IPV6_V6ONLY flag set to 1, otherwise we would not
      // be able to have an IPv4 socket and an IPv6 socket bound to the same port:
      int const one = 1;
      (void)setsockopt(newSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&one, sizeof one);

      MAKE_SOCKADDR_IN6(name, addr, port.num());
      if (bind(newSocket, (struct sockaddr*)&name, sizeof name) != 0) {
	char tmpBuffer[100];
	sprintf(tmpBuffer, "IPv6 bind() error (port number: %d): ", ntohs(port.num()));
	socketErr(env, tmpBuffer);
	closeSocket(newSocket);
	return -1;
      }
    }
  }

  // Set the sending interface for multicasts, if it's not the default:
  if (SendingInterfaceAddr != INADDR_ANY) { // later, fix for IPv6
    struct in_addr addr;
    addr.s_addr = SendingInterfaceAddr;

    if (setsockopt(newSocket,
		   domain == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
		   domain == AF_INET ? IP_MULTICAST_IF : IPV6_MULTICAST_IF,
		   (const char*)&addr, sizeof addr) < 0) {
      socketErr(env, "error setting outgoing multicast interface: ");
      closeSocket(newSocket);
      return -1;
    }
  }

  return newSocket;
}

Boolean makeSocketNonBlocking(int sock) {
#if defined(__WIN32__) || defined(_WIN32)
  unsigned long arg = 1;
  return ioctlsocket(sock, FIONBIO, &arg) == 0;
#elif defined(VXWORKS)
  int arg = 1;
  return ioctl(sock, FIONBIO, (int)&arg) == 0;
#else
  int curFlags = fcntl(sock, F_GETFL, 0);
  return fcntl(sock, F_SETFL, curFlags|O_NONBLOCK) >= 0;
#endif
}

Boolean makeSocketBlocking(int sock, unsigned writeTimeoutInMilliseconds) {
  Boolean result;
#if defined(__WIN32__) || defined(_WIN32)
  unsigned long arg = 0;
  result = ioctlsocket(sock, FIONBIO, &arg) == 0;
#elif defined(VXWORKS)
  int arg = 0;
  result = ioctl(sock, FIONBIO, (int)&arg) == 0;
#else
  int curFlags = fcntl(sock, F_GETFL, 0);
  result = fcntl(sock, F_SETFL, curFlags&(~O_NONBLOCK)) >= 0;
#endif

  if (writeTimeoutInMilliseconds > 0) {
#ifdef SO_SNDTIMEO
#if defined(__WIN32__) || defined(_WIN32)
    DWORD msto = (DWORD)writeTimeoutInMilliseconds;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&msto, sizeof(msto) );
#else
    struct timeval tv;
    tv.tv_sec = writeTimeoutInMilliseconds/1000;
    tv.tv_usec = (writeTimeoutInMilliseconds%1000)*1000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof tv);
#endif
#endif
  }

  return result;
}

Boolean setSocketKeepAlive(int sock) {
#if defined(__WIN32__) || defined(_WIN32)
  // How do we do this in Windows?  For now, just make this a no-op in Windows:
#else
  int const keepalive_enabled = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void*)&keepalive_enabled, sizeof keepalive_enabled) < 0) {
    return False;
  }

#ifdef TCP_KEEPIDLE
  int const keepalive_time = 180;
  if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&keepalive_time, sizeof keepalive_time) < 0) {
    return False;
  }
#endif

#ifdef TCP_KEEPCNT
  int const keepalive_count = 5;
  if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, (void*)&keepalive_count, sizeof keepalive_count) < 0) {
    return False;
  }
#endif

#ifdef TCP_KEEPINTVL
  int const keepalive_interval = 20;
  if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, (void*)&keepalive_interval, sizeof keepalive_interval) < 0) {
    return False;
  }
#endif
#endif

  return True;
}

int setupStreamSocket(UsageEnvironment& env, Port port, int domain,
		      Boolean makeNonBlocking, Boolean setKeepAlive) {
  if (!initializeWinsockIfNecessary()) {
    socketErr(env, "Failed to initialize 'winsock': ");
    return -1;
  }

  int newSocket = createSocket(domain, SOCK_STREAM);
  if (newSocket < 0) {
    socketErr(env, "unable to create stream socket: ");
    return newSocket;
  }

  int reuseFlag = groupsockPriv(env)->reuseFlag;
  reclaimGroupsockPriv(env);
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEADDR) error: ");
    closeSocket(newSocket);
    return -1;
  }

  // SO_REUSEPORT doesn't really make sense for TCP sockets, so we
  // normally don't set them.  However, if you really want to do this
  // #define REUSE_FOR_TCP
#ifdef REUSE_FOR_TCP
#if defined(__WIN32__) || defined(_WIN32)
  // Windoze doesn't properly handle SO_REUSEPORT
#else
#ifdef SO_REUSEPORT
  if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEPORT,
		 (const char*)&reuseFlag, sizeof reuseFlag) < 0) {
    socketErr(env, "setsockopt(SO_REUSEPORT) error: ");
    closeSocket(newSocket);
    return -1;
  }
#endif
#endif
#endif

  if (domain == AF_INET) {
    // Note: Windoze requires binding, even if the port number is 0
#if defined(__WIN32__) || defined(_WIN32)
#else
    if (port.num() != 0 || ReceivingInterfaceAddr != INADDR_ANY) {
#endif
      MAKE_SOCKADDR_IN(name, ReceivingInterfaceAddr, port.num());
      if (bind(newSocket, (struct sockaddr*)&name, sizeof name) != 0) {
	char tmpBuffer[100];
	sprintf(tmpBuffer, "IPv4 bind() error (port number: %d): ", ntohs(port.num()));
	socketErr(env, tmpBuffer);
	closeSocket(newSocket);
	return -1;
      }
#if defined(__WIN32__) || defined(_WIN32)
#else
    }
#endif
  } else { // IPv6
    if (port.num() != 0) {
      // For IPv6 sockets, we need the IPV6_V6ONLY flag set to 1, otherwise we would not
      // be able to have an IPv4 socket and an IPv6 socket bound to the same port:
      int const one = 1;
      (void)setsockopt(newSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&one, sizeof one);

      MAKE_SOCKADDR_IN6(name, ReceivingInterfaceAddr6, port.num());
      if (bind(newSocket, (struct sockaddr*)&name, sizeof name) != 0) {
	char tmpBuffer[100];
	sprintf(tmpBuffer, "IPv6 bind() error (port number: %d): ", ntohs(port.num()));
	socketErr(env, tmpBuffer);
	closeSocket(newSocket);
	return -1;
      }
    }
  }

  if (makeNonBlocking) {
    if (!makeSocketNonBlocking(newSocket)) {
      socketErr(env, "failed to make non-blocking: ");
      closeSocket(newSocket);
      return -1;
    }
  }

  // Set the keep alive mechanism for the TCP socket, to avoid "ghost sockets" 
  //    that remain after an interrupted communication.
  if (setKeepAlive) {
    if (!setSocketKeepAlive(newSocket)) {
      socketErr(env, "failed to set keep alive: ");
      closeSocket(newSocket);
      return -1;
    }
  }

  return newSocket;
}

int readSocket(UsageEnvironment& env,
	       int socket, unsigned char* buffer, unsigned bufferSize,
	       struct sockaddr_storage& fromAddress) {
  SOCKLEN_T addressSize = sizeof fromAddress;
  int bytesRead = recvfrom(socket, (char*)buffer, bufferSize, 0,
			   (struct sockaddr*)&fromAddress,
			   &addressSize);
  if (bytesRead < 0) {
    //##### HACK to work around bugs in Linux and Windows:
    int err = env.getErrno();
    if (err == 111 /*ECONNREFUSED (Linux)*/
#if defined(__WIN32__) || defined(_WIN32)
	// What a piece of crap Windows is.  Sometimes
	// recvfrom() returns -1, but with an 'errno' of 0.
	// This appears not to be a real error; just treat
	// it as if it were a read of zero bytes, and hope
	// we don't have to do anything else to 'reset'
	// this alleged error:
	|| err == 0 || err == EWOULDBLOCK
#else
	|| err == EAGAIN
#endif
	|| err == 113 /*EHOSTUNREACH (Linux)*/) { // Why does Linux return this for datagram sock?
      return 0;
    }
    //##### END HACK
    socketErr(env, "recvfrom() error: ");
  } else if (bytesRead == 0) {
    // "recvfrom()" on a stream socket can return 0 if the remote end has closed the connection.  Treat this as an error:
    return -1;
  }

  return bytesRead;
}

Boolean writeSocket(UsageEnvironment& env,
		    int socket, struct sockaddr_storage const& addressAndPort,
		    u_int8_t ttlArg,
		    unsigned char* buffer, unsigned bufferSize) {
  // Before sending, set the socket's TTL (IPv4 only):
  if (addressAndPort.ss_family == AF_INET) {
#if defined(__WIN32__) || defined(_WIN32)
#define TTL_TYPE int
#else
#define TTL_TYPE u_int8_t
#endif
    TTL_TYPE ttl = (TTL_TYPE)ttlArg;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL,
		   (const char*)&ttl, sizeof ttl) < 0) {
      socketErr(env, "setsockopt(IP_MULTICAST_TTL) error: ");
      return False;
    }
  }
  
  return writeSocket(env, socket, addressAndPort, buffer, bufferSize);
}

Boolean writeSocket(UsageEnvironment& env,
		    int socket, struct sockaddr_storage const& addressAndPort,
		    unsigned char* buffer, unsigned bufferSize) {
  do {
    SOCKLEN_T dest_len = addressSize(addressAndPort);
    int bytesSent = sendto(socket, (char*)buffer, bufferSize, MSG_NOSIGNAL,
			   (struct sockaddr const*)&addressAndPort, dest_len);
    if (bytesSent != (int)bufferSize) {
      char tmpBuf[100];
      sprintf(tmpBuf, "writeSocket(%d), sendTo() error: wrote %d bytes instead of %u: ", socket, bytesSent, bufferSize);
      socketErr(env, tmpBuf);
      break;
    }
    
    return True;
  } while (0);

  return False;
}

void ignoreSigPipeOnSocket(int socketNum) {
  #ifdef USE_SIGNALS
  #ifdef SO_NOSIGPIPE
  int set_option = 1;
  setsockopt(socketNum, SOL_SOCKET, SO_NOSIGPIPE, &set_option, sizeof set_option);
  #else
  signal(SIGPIPE, SIG_IGN);
  #endif
  #endif
}

static unsigned getBufferSize(UsageEnvironment& env, int bufOptName,
			      int socket) {
  unsigned curSize;
  SOCKLEN_T sizeSize = sizeof curSize;
  if (getsockopt(socket, SOL_SOCKET, bufOptName,
		 (char*)&curSize, &sizeSize) < 0) {
    socketErr(env, "getBufferSize() error: ");
    return 0;
  }

  return curSize;
}
unsigned getSendBufferSize(UsageEnvironment& env, int socket) {
  return getBufferSize(env, SO_SNDBUF, socket);
}
unsigned getReceiveBufferSize(UsageEnvironment& env, int socket) {
  return getBufferSize(env, SO_RCVBUF, socket);
}

static unsigned setBufferTo(UsageEnvironment& env, int bufOptName,
			    int socket, unsigned requestedSize) {
  SOCKLEN_T sizeSize = sizeof requestedSize;
  setsockopt(socket, SOL_SOCKET, bufOptName, (char*)&requestedSize, sizeSize);

  // Get and return the actual, resulting buffer size:
  return getBufferSize(env, bufOptName, socket);
}
unsigned setSendBufferTo(UsageEnvironment& env,
			 int socket, unsigned requestedSize) {
	return setBufferTo(env, SO_SNDBUF, socket, requestedSize);
}
unsigned setReceiveBufferTo(UsageEnvironment& env,
			    int socket, unsigned requestedSize) {
	return setBufferTo(env, SO_RCVBUF, socket, requestedSize);
}

static unsigned increaseBufferTo(UsageEnvironment& env, int bufOptName,
				 int socket, unsigned requestedSize) {
  // First, get the current buffer size.  If it's already at least
  // as big as what we're requesting, do nothing.
  unsigned curSize = getBufferSize(env, bufOptName, socket);

  // Next, try to increase the buffer to the requested size,
  // or to some smaller size, if that's not possible:
  while (requestedSize > curSize) {
    SOCKLEN_T sizeSize = sizeof requestedSize;
    if (setsockopt(socket, SOL_SOCKET, bufOptName,
		   (char*)&requestedSize, sizeSize) >= 0) {
      // success
      return requestedSize;
    }
    requestedSize = (requestedSize+curSize)/2;
  }

  return getBufferSize(env, bufOptName, socket);
}
unsigned increaseSendBufferTo(UsageEnvironment& env,
			      int socket, unsigned requestedSize) {
  return increaseBufferTo(env, SO_SNDBUF, socket, requestedSize);
}
unsigned increaseReceiveBufferTo(UsageEnvironment& env,
				 int socket, unsigned requestedSize) {
  return increaseBufferTo(env, SO_RCVBUF, socket, requestedSize);
}

static void clearMulticastAllSocketOption(int socket, int domain) {
#ifdef IP_MULTICAST_ALL
  // This option is defined in modern versions of Linux to overcome a bug in the Linux kernel's default behavior.
  // When set to 0, it ensures that we receive only packets that were sent to the specified IP multicast address,
  // even if some other process on the same system has joined a different multicast group with the same port number.
  int multicastAll = 0;
  (void)setsockopt(socket,
		   domain == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
		   IP_MULTICAST_ALL, // is this the same for IPv6?
		   (void*)&multicastAll, sizeof multicastAll);
  // Ignore the call's result.  Should it fail, we'll still receive packets (just perhaps more than intended)
#endif
}

Boolean socketJoinGroup(UsageEnvironment& env, int socket,
			struct sockaddr_storage const& groupAddress){
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case

  int level, option_name;
  void const* option_value;
  SOCKLEN_T option_len;
  struct ip_mreq imr4;
  struct ipv6_mreq imr6;

  switch (groupAddress.ss_family) {
    case AF_INET: {
      imr4.imr_multiaddr.s_addr = ((struct sockaddr_in&)groupAddress).sin_addr.s_addr;
      imr4.imr_interface.s_addr = ReceivingInterfaceAddr;

      level = IPPROTO_IP;
      option_name = IP_ADD_MEMBERSHIP;
      option_value = &imr4;
      option_len = sizeof imr4;
      break;
    }
    case AF_INET6: {
      imr6.ipv6mr_multiaddr = ((struct sockaddr_in6&)groupAddress).sin6_addr;
      imr6.ipv6mr_interface = 0; // ???

      level = IPPROTO_IPV6;
      option_name = IPV6_JOIN_GROUP;
      option_value = &imr6;
      option_len = sizeof imr6;
      break;
    }
    default: {
      return False;
    }
  }
  if (setsockopt(socket, level, option_name, (const char*)option_value, option_len) < 0) {
#if defined(__WIN32__) || defined(_WIN32)
    if (env.getErrno() != 0) {
      // That piece-of-shit toy operating system (Windows) sometimes lies
      // about setsockopt() failing!
#endif
      socketErr(env, "setsockopt(IP_ADD_MEMBERSHIP) error: ");
      return False;
#if defined(__WIN32__) || defined(_WIN32)
    }
#endif
  }

  clearMulticastAllSocketOption(socket, groupAddress.ss_family);

  return True;
}

Boolean socketLeaveGroup(UsageEnvironment&, int socket,
			 struct sockaddr_storage const& groupAddress) {
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case

  int level, option_name;
  void const* option_value;
  SOCKLEN_T option_len;
  struct ip_mreq imr4;
  struct ipv6_mreq imr6;

  switch (groupAddress.ss_family) {
    case AF_INET: {
      imr4.imr_multiaddr.s_addr = ((struct sockaddr_in&)groupAddress).sin_addr.s_addr;
      imr4.imr_interface.s_addr = ReceivingInterfaceAddr;

      level = IPPROTO_IP;
      option_name = IP_DROP_MEMBERSHIP;
      option_value = &imr4;
      option_len = sizeof imr4;
      break;
    }
    case AF_INET6: {
      imr6.ipv6mr_multiaddr = ((struct sockaddr_in6&)groupAddress).sin6_addr;
      imr6.ipv6mr_interface = 0; // ???

      level = IPPROTO_IPV6;
      option_name = IPV6_LEAVE_GROUP;
      option_value = &imr6;
      option_len = sizeof imr6;
      break;
    }
    default: {
      return False;
    }
  }
  if (setsockopt(socket, level, option_name, (const char*)option_value, option_len) < 0) {
    return False;
  }

  return True;
}

// The source-specific join/leave operations require special setsockopt()
// commands, and a special structure (ip_mreq_source).  If the include files
// didn't define these, we do so here:
#if !defined(IP_ADD_SOURCE_MEMBERSHIP)
struct ip_mreq_source {
  struct  in_addr imr_multiaddr;  /* IP multicast address of group */
  struct  in_addr imr_sourceaddr; /* IP address of source */
  struct  in_addr imr_interface;  /* local IP address of interface */
};
#endif

#ifndef IP_ADD_SOURCE_MEMBERSHIP

#ifdef LINUX
#define IP_ADD_SOURCE_MEMBERSHIP   39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#else
#define IP_ADD_SOURCE_MEMBERSHIP   25
#define IP_DROP_SOURCE_MEMBERSHIP 26
#endif

#endif

Boolean socketJoinGroupSSM(UsageEnvironment& env, int socket,
			   struct sockaddr_storage const& groupAddress,
			   struct sockaddr_storage const& sourceFilterAddr) {
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case
  if (groupAddress.ss_family != AF_INET) return False; // later, support IPv6

  struct ip_mreq_source imr;
#if ANDROID_OLD_NDK
    imr.imr_multiaddr = ((struct sockaddr_in&)groupAddress).sin_addr.s_addr;
    imr.imr_sourceaddr = ((struct sockaddr_in&)sourceFilterAddr).sin_addr.s_addr;
    imr.imr_interface = ReceivingInterfaceAddr;
#else
    imr.imr_multiaddr.s_addr = ((struct sockaddr_in&)groupAddress).sin_addr.s_addr;
    imr.imr_sourceaddr.s_addr = ((struct sockaddr_in&)sourceFilterAddr).sin_addr.s_addr;
    imr.imr_interface.s_addr = ReceivingInterfaceAddr;
#endif
  if (setsockopt(socket, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
		 (const char*)&imr, sizeof (struct ip_mreq_source)) < 0) {
    socketErr(env, "setsockopt(IP_ADD_SOURCE_MEMBERSHIP) error: ");
    return False;
  }

  clearMulticastAllSocketOption(socket, groupAddress.ss_family);

  return True;
}

Boolean socketLeaveGroupSSM(UsageEnvironment& /*env*/, int socket,
			    struct sockaddr_storage const& groupAddress,
			    struct sockaddr_storage const& sourceFilterAddr) {
  if (!IsMulticastAddress(groupAddress)) return True; // ignore this case
  if (groupAddress.ss_family != AF_INET) return False; // later, support IPv6

  struct ip_mreq_source imr;
#if ANDROID_OLD_NDK
    imr.imr_multiaddr = ((struct sockaddr_in&)groupAddress).sin_addr.s_addr;
    imr.imr_sourceaddr = ((struct sockaddr_in&)sourceFilterAddr).sin_addr.s_addr;
    imr.imr_interface = ReceivingInterfaceAddr;
#else
    imr.imr_multiaddr.s_addr = ((struct sockaddr_in&)groupAddress).sin_addr.s_addr;
    imr.imr_sourceaddr.s_addr = ((struct sockaddr_in&)sourceFilterAddr).sin_addr.s_addr;
    imr.imr_interface.s_addr = ReceivingInterfaceAddr;
#endif
  if (setsockopt(socket, IPPROTO_IP, IP_DROP_SOURCE_MEMBERSHIP,
		 (const char*)&imr, sizeof (struct ip_mreq_source)) < 0) {
    return False;
  }

  return True;
}

static Boolean getSourcePort0(int socket, portNumBits& resultPortNum/*host order*/) {
  sockaddr_storage testAddr;
  setPortNum(testAddr, 0);

  SOCKLEN_T len = sizeof testAddr;
  if (getsockname(socket, (struct sockaddr*)&testAddr, &len) < 0) return False;

  resultPortNum = ntohs(portNum(testAddr));
  return True;
}

Boolean getSourcePort(UsageEnvironment& env, int socket, int domain, Port& port) {
  portNumBits portNum = 0;
  if (!getSourcePort0(socket, portNum) || portNum == 0) {
    // Hack - call bind(), then try again:
    if (domain == AF_INET) { // IPv4
      MAKE_SOCKADDR_IN(name, INADDR_ANY, 0);
      bind(socket, (struct sockaddr*)&name, sizeof name);
    } else { // IPv6
      in6_addr const in6addr_any_init = IN6ADDR_ANY_INIT;
      MAKE_SOCKADDR_IN6(name, in6addr_any_init, 0);
      bind(socket, (struct sockaddr*)&name, sizeof name);
    }

    if (!getSourcePort0(socket, portNum) || portNum == 0) {
      socketErr(env, "getsockname() error: ");
      return False;
    }
  }

  port = Port(portNum);
  return True;
}

static Boolean isBadIPv4AddressForUs(ipv4AddressBits addr) {
  // Check for some possible erroneous addresses:
  ipv4AddressBits nAddr = htonl(addr);
  return (nAddr == 0x7F000001 /* 127.0.0.1 */
	  || nAddr == 0
	  || nAddr == (ipv4AddressBits)(~0));
}

static Boolean isBadIPv6AddressForUs(ipv6AddressBits addr) {
  // We consider an IPv6 address bad if:
  //   - the first 10 bits are 0xFE8, indicating a link-local or site-local address, or
  //   - the first 15 bytes are 0, and the 16th byte is 0 (unspecified) or 1 (loopback)
  if (addr[0] == 0xFE) return (addr[1]&0x80) != 0;
  
  for (unsigned i = 0; i < 15; ++i) {
    if (addr[i] != 0) return False;
  }

    return addr[15] == 0 || addr[15] == 1;
}

static Boolean isBadAddressForUs(struct sockaddr const& addr) {
  switch (addr.sa_family) {
    case AF_INET: {
      return isBadIPv4AddressForUs(((sockaddr_in&)addr).sin_addr.s_addr);
    }
    case AF_INET6: {
      return isBadIPv6AddressForUs(((sockaddr_in6&)addr).sin6_addr.s6_addr);
    }
    default: {
      return True;
    }
  }
}

static Boolean isBadAddressForUs(NetAddress const& addr) {
  if (addr.length() == sizeof (ipv4AddressBits)) {
    return isBadIPv4AddressForUs(*(ipv4AddressBits*)(addr.data()));
  } else if (addr.length() == sizeof (ipv6AddressBits)) {
    return isBadIPv6AddressForUs(*(ipv6AddressBits*)(addr.data()));
  } else {
    return True;
  }
}

static void getOurIPAddresses(UsageEnvironment& env); // forward

static ipv4AddressBits _ourIPv4Address = 0;
#define _weHaveAnIPv4Address (_ourIPv4Address != 0)

ipv4AddressBits ourIPv4Address(UsageEnvironment& env) {
  if (ReceivingInterfaceAddr != INADDR_ANY) {
    // Hack: If we were told to receive on a specific interface address, then 
    // define this to be our ip address:
    _ourIPv4Address = ReceivingInterfaceAddr;
  }

  if (!_weHaveAnIPv4Address) {
    getOurIPAddresses(env);
  }

  return _ourIPv4Address;
}

static ipv6AddressBits _ourIPv6Address;
static Boolean _weHaveAnIPv6Address = False;

ipv6AddressBits const& ourIPv6Address(UsageEnvironment& env) {
  if (!_weHaveAnIPv6Address) {
    getOurIPAddresses(env);
  }

  return _ourIPv6Address;
}

Boolean weHaveAnIPv4Address(UsageEnvironment& env) {
  if (_weHaveAnIPv4Address || _weHaveAnIPv6Address) return _weHaveAnIPv4Address;

  getOurIPAddresses(env);
  return _weHaveAnIPv4Address;
}

Boolean weHaveAnIPv6Address(UsageEnvironment& env) {
  if (_weHaveAnIPv4Address || _weHaveAnIPv6Address) return _weHaveAnIPv6Address;

  getOurIPAddresses(env);
  return _weHaveAnIPv6Address;
}

Boolean weHaveAnIPAddress(UsageEnvironment& env) {
  if (_weHaveAnIPv4Address || _weHaveAnIPv6Address) return True;

  getOurIPAddresses(env);
  return _weHaveAnIPv4Address || _weHaveAnIPv6Address;
}

static void copyAddress(struct sockaddr_storage& to, struct sockaddr const* from) {
  // Copy a "struct sockaddr" to a "struct sockaddr_storage" (assumed to be large enough)
  if (from == NULL) return;
  
  switch (from->sa_family) {
    case AF_INET: {
#ifdef HAVE_SOCKADDR_LEN
      if (from->sa_len < sizeof (struct sockaddr_in)) return; // sanity check
      to.ss_len = sizeof (struct sockaddr_in);
#endif
      to.ss_family = AF_INET;
      ((sockaddr_in&)to).sin_addr.s_addr = ((sockaddr_in const*)from)->sin_addr.s_addr;
      break;
    }
    case AF_INET6: {
#ifdef HAVE_SOCKADDR_LEN
      if (from->sa_len < sizeof (struct sockaddr_in6)) return; // sanity check
      to.ss_len = sizeof (struct sockaddr_in6);
#endif
      to.ss_family = AF_INET6;
      for (unsigned i = 0; i < 16; ++i) {
	((sockaddr_in6&)to).sin6_addr.s6_addr[i] = ((sockaddr_in6 const*)from)->sin6_addr.s6_addr[i];
      }
      break;
    }
  }
}

void getOurIPAddresses(UsageEnvironment& env) {
  // We use two methods to (try to) get our IP addresses.
  // First, we use "getifaddrs()".  But if that doesn't work
  // (or if "getifaddrs()" is not defined), then we use an alternative (more old-fashioned)
  // mechanism: First get our host name, then try resolving this host name.
  struct sockaddr_storage foundIPv4Address = nullAddress(AF_INET);
  struct sockaddr_storage foundIPv6Address = nullAddress(AF_INET6);

  Boolean getifaddrsWorks = False; // until we learn otherwise
#ifndef NO_GETIFADDRS
  struct ifaddrs* ifap;

  if (getifaddrs(&ifap) == 0) {
    // Look through all interfaces:
    for (struct ifaddrs* p = ifap; p != NULL; p = p->ifa_next) {
      // Ignore an interface if it's not up, or is a loopback interface:
      if ((p->ifa_flags&IFF_UP) == 0 || (p->ifa_flags&IFF_LOOPBACK) != 0) continue;

      // Also ignore the interface if the address is considered 'bad' for us:
      if (p->ifa_addr == NULL || isBadAddressForUs(*p->ifa_addr)) continue;
      
      // We take the first IPv4 and first IPv6 addresses:
      if (p->ifa_addr->sa_family == AF_INET && addressIsNull(foundIPv4Address)) {
	copyAddress(foundIPv4Address, p->ifa_addr);
	getifaddrsWorks = True;
      } else if (p->ifa_addr->sa_family == AF_INET6 && addressIsNull(foundIPv6Address)) {
	copyAddress(foundIPv6Address, p->ifa_addr);
	getifaddrsWorks = True;
      }
    }
    freeifaddrs(ifap);
  }
#endif

    if (!getifaddrsWorks) do {
      // We couldn't find our address using "getifaddrs()",
      // so try instead to look it up directly - by first getting our host name,
      // and then resolving this host name
      char hostname[100];
      hostname[0] = '\0';
      int result = gethostname(hostname, sizeof hostname);
      if (result != 0 || hostname[0] == '\0') {
	env.setResultErrMsg("initial gethostname() failed");
	break;
      }

      // Try to resolve "hostname" to one or more IP addresses:
      NetAddressList addresses(hostname);
      NetAddressList::Iterator iter(addresses);

      // Look at each address, rejecting any that are bad.
      // We take the first IPv4 and first IPv6 addresses, if any.
      NetAddress const* address;
      while ((address = iter.nextAddress()) != NULL) {
	if (isBadAddressForUs(*address)) continue;

	if (address->length() == sizeof (ipv4AddressBits) && addressIsNull(foundIPv4Address)) {
	  copyAddress(foundIPv4Address, address);
	} else if (address->length() == sizeof (ipv6AddressBits) && addressIsNull(foundIPv6Address)) {
	  copyAddress(foundIPv6Address, address);
	}
      }
    } while (0);

    // Note the IPv4 and IPv6 addresses that we found:
    _ourIPv4Address = ((sockaddr_in&)foundIPv4Address).sin_addr.s_addr;

    for (unsigned i = 0; i < 16; ++i) {
      _ourIPv6Address[i] = ((sockaddr_in6&)foundIPv6Address).sin6_addr.s6_addr[i];
      if (_ourIPv6Address[i] != 0) _weHaveAnIPv6Address = True;
    }

    if (!_weHaveAnIPv4Address && !_weHaveAnIPv6Address) {
      env.setResultMsg("This computer does not have a valid IP (v4 or v6) address!");
    }

    // Use our newly-discovered IP addresses, and the current time,
    // to initialize the random number generator's seed:
    struct timeval timeNow;
    gettimeofday(&timeNow, NULL);
    unsigned seed = _ourIPv4Address^timeNow.tv_sec^timeNow.tv_usec;
    for (unsigned i = 0; i < 16; i += 4) {
      seed ^= (_ourIPv6Address[i]<<24)|(_ourIPv6Address[i+1]<<16)|(_ourIPv6Address[i+2]<<8)|_ourIPv6Address[i+3];
    }
    our_srandom(seed);
}

ipv4AddressBits chooseRandomIPv4SSMAddress(UsageEnvironment& env) {
  // First, a hack to ensure that our random number generator is seeded:
  (void)ourIPv4Address(env);

  // Choose a random address in the range [232.0.1.0, 232.255.255.255)
  // i.e., [0xE8000100, 0xE8FFFFFF)
  ipv4AddressBits const first = 0xE8000100, lastPlus1 = 0xE8FFFFFF;
  ipv4AddressBits const range = lastPlus1 - first;

  return ntohl(first + ((ipv4AddressBits)our_random())%range);
}

char const* timestampString() {
  struct timeval tvNow;
  gettimeofday(&tvNow, NULL);

#if !defined(_WIN32_WCE)
  static char timeString[9]; // holds hh:mm:ss plus trailing '\0'

  time_t tvNow_t = tvNow.tv_sec;
  char const* ctimeResult = ctime(&tvNow_t);
  if (ctimeResult == NULL) {
    sprintf(timeString, "??:??:??");
  } else {
    char const* from = &ctimeResult[11];
    int i;
    for (i = 0; i < 8; ++i) {
      timeString[i] = from[i];
    }
    timeString[i] = '\0';
  }
#else
  // WinCE apparently doesn't have "ctime()", so instead, construct
  // a timestamp string just using the integer and fractional parts
  // of "tvNow":
  static char timeString[50];
  sprintf(timeString, "%lu.%06ld", tvNow.tv_sec, tvNow.tv_usec);
#endif

  return (char const*)&timeString;
}

#if (defined(__WIN32__) || defined(_WIN32)) && !defined(__MINGW32__)
// For Windoze, we need to implement our own gettimeofday()

// used to make sure that static variables in gettimeofday() aren't initialized simultaneously by multiple threads
static LONG initializeLock_gettimeofday = 0;  

#if !defined(_WIN32_WCE)
#include <sys/timeb.h>
#endif

int gettimeofday(struct timeval* tp, int* /*tz*/) {
  static LARGE_INTEGER tickFrequency, epochOffset;

  static Boolean isInitialized = False;

  LARGE_INTEGER tickNow;

#if !defined(_WIN32_WCE)
  QueryPerformanceCounter(&tickNow);
#else
  tickNow.QuadPart = GetTickCount();
#endif
 
  if (!isInitialized) {
    if(1 == InterlockedIncrement(&initializeLock_gettimeofday)) {
#if !defined(_WIN32_WCE)
      // For our first call, use "ftime()", so that we get a time with a proper epoch.
      // For subsequent calls, use "QueryPerformanceCount()", because it's more fine-grain.
      struct timeb tb;
      ftime(&tb);
      tp->tv_sec = tb.time;
      tp->tv_usec = 1000*tb.millitm;

      // Also get our counter frequency:
      QueryPerformanceFrequency(&tickFrequency);
#else
      /* FILETIME of Jan 1 1970 00:00:00. */
      const LONGLONG epoch = 116444736000000000LL;
      FILETIME fileTime;
      LARGE_INTEGER time;
      GetSystemTimeAsFileTime(&fileTime);

      time.HighPart = fileTime.dwHighDateTime;
      time.LowPart = fileTime.dwLowDateTime;

      // convert to from 100ns time to unix timestamp in seconds, 1000*1000*10
      tp->tv_sec = (long)((time.QuadPart - epoch) / 10000000L);

      /*
        GetSystemTimeAsFileTime has just a seconds resolution,
        thats why wince-version of gettimeofday is not 100% accurate, usec accuracy would be calculated like this:
        // convert 100 nanoseconds to usec
        tp->tv_usec= (long)((time.QuadPart - epoch)%10000000L) / 10L;
      */
      tp->tv_usec = 0;

      // resolution of GetTickCounter() is always milliseconds
      tickFrequency.QuadPart = 1000;
#endif     
      // compute an offset to add to subsequent counter times, so we get a proper epoch:
      epochOffset.QuadPart
          = tp->tv_sec * tickFrequency.QuadPart + (tp->tv_usec * tickFrequency.QuadPart) / 1000000L - tickNow.QuadPart;
      
      // next caller can use ticks for time calculation
      isInitialized = True; 
      return 0;
    } else {
        InterlockedDecrement(&initializeLock_gettimeofday);
        // wait until first caller has initialized static values
        while(!isInitialized){
          Sleep(1);
        }
    }
  }

  // adjust our tick count so that we get a proper epoch:
  tickNow.QuadPart += epochOffset.QuadPart;

  tp->tv_sec =  (long)(tickNow.QuadPart / tickFrequency.QuadPart);
  tp->tv_usec = (long)(((tickNow.QuadPart % tickFrequency.QuadPart) * 1000000L) / tickFrequency.QuadPart);

  return 0;
}
#endif
#undef ANDROID_OLD_NDK

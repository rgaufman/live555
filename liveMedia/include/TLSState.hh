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
// Copyright (c) 1996-2020 Live Networks, Inc.  All rights reserved.
// State encapsulating a TLS connection
// C++ header

#ifndef _TLS_STATE_HH
#define _TLS_STATE_HH

#ifndef _NET_COMMON_H
#include "NetCommon.h"
#endif
#ifndef _BOOLEAN_HH
#include "Boolean.hh"
#endif
#ifndef NO_OPENSSL
#include <openssl/ssl.h>
#endif

class TLSState {
public:
  TLSState(class RTSPClient& client);
  virtual ~TLSState();

public:
  Boolean isNeeded;

  int connect(int socketNum); // returns: -1 (unrecoverable error), 0 (pending), 1 (done)
  int write(const char* data, unsigned count);
  int read(u_int8_t* buffer, unsigned bufferSize);

private:
  void reset();
  Boolean setup(int socketNum);

#ifndef NO_OPENSSL
private:
  class RTSPClient& fClient;
  Boolean fHasBeenSetup;
  SSL_CTX* fCtx;
  SSL* fCon;
#endif
};

#endif

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
// Copyright (c) 1996-2021, Live Networks, Inc.  All rights reserved
// "Group Endpoint Id"
// Implementation

#include "GroupEId.hh"


GroupEId::GroupEId(struct sockaddr_storage const& groupAddr,
		   portNumBits portNum, u_int8_t ttl) {
  init(groupAddr, nullAddress(), portNum, ttl);
}

GroupEId::GroupEId(struct sockaddr_storage const& groupAddr,
		   struct sockaddr_storage const& sourceFilterAddr,
		   portNumBits portNum) {
  init(groupAddr, sourceFilterAddr, portNum, 255);
}

GroupEId::GroupEId() {
  init(nullAddress(), nullAddress(), 0, 255);
}

Boolean GroupEId::isSSM() const {
  // We're a SSM group if "fSourceFilterAddress" is not a 'null' address:
  return !addressIsNull(fSourceFilterAddress);
}

portNumBits GroupEId::portNum() const { return ::portNum(fGroupAddress); }

void GroupEId::init(struct sockaddr_storage const& groupAddr,
		    struct sockaddr_storage const& sourceFilterAddr,
		    portNumBits portNum,
		    u_int8_t ttl) {
  fGroupAddress = groupAddr;
  setPortNum(fGroupAddress, portNum);
  fSourceFilterAddress = sourceFilterAddr;

  fTTL = ttl;
}

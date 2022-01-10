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
// Copyright (c) 1996-2022, Live Networks, Inc.  All rights reserved
// A common function that outputs the URL(s) that can be used to access a stream
// served by a RTSP server.
// C++ header

#ifndef _ANNOUNCE_URL_HH
#define _ANNOUNCE_URL_HH

#ifndef _LIVEMEDIA_HH
#include "liveMedia.hh"
#endif

void announceURL(RTSPServer* rtspServer, ServerMediaSession* sms);

#endif

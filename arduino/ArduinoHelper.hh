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
// Copyright (c) 2021 Phil Schatzmann  All rights reserved.

#pragma once

#ifdef ARDUINO
#include "WiFi.h"
#include <string.h>
#include <stdio.h>
//#include "Arduino.h"
#include "Config.hh"
#undef F

// Global variables
extern char error_msg[ERROR_MSG_SIZE];

// Logging support for Arduino: we just user Serial.print()
#define LOG(...) { snprintf(error_msg, ERROR_MSG_SIZE,__VA_ARGS__); Serial.println(error_msg); }
#define LOGFLUSH() Serial.flush()

// Comment out try catch
#define try
#define catch(e) if(false)

// Use this to stop the processing
// void stop(int rc=0);

// Use stop insted of exit in Arduino
#ifndef exit
#  define exit(rc) while(true);
#endif

#ifdef __cplusplus
extern "C" {
#endif

// get local IP address
uint32_t localip();

// gets the hostname using the WiFi API
int gethostname(char* str, unsigned len);

#ifdef __cplusplus
}
#endif

#endif


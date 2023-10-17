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

// Configuration Settings
#pragma once

#ifdef ARDUINO
    #define USE_SYSTEM_RANDOM
    #define NO_OPENSSL
    #define NO_OPENSSL
    #define NO_GETIFADDRS
    #define USE_SYSTEM_RANDOM
    #define DISABLE_LOOPBACK_IP_ADDRESS_CHECK
    // Support only synchronous reads
    #define READ_FROM_FILES_SYNCHRONOUSLY 
    // Allocated memory size. On an esp32 max is 126432
    #define MAX_SIZE_555 3000
    // Maximum length of a log message
    #define ERROR_MSG_SIZE 225
    // Maximum open files (1 should be good enogugh however)
    #define LIVE555_MAX_FILES 5
    // Activate DEBUG messages
    #define DEBUG
    // Activate DEBUG_ERRORS messages 
    #define DEBUG_ERRORS
    #define DEBUG_PRINT_NPT
    #define DEBUG_PRINT_EACH_RECEIVED_FRAME
    //#define DEBUG_SEND
    //#define DEBUG_PARSE
    #define MAX_CNAME_LEN 100
    // by default, print verbose output from each "RTSPClient"
    #define RTSP_CLIENT_VERBOSITY_LEVEL 1

    #define RTP_PAYLOAD_PREFERRED_SIZE 1456
    // activate stdio abstraction layer
    #define SOCKLEN_T uint32_t
    // for Arduino ESP32 >= 3.0.0
    #define USE_ATOMIC_FOR_VOLATILE
#else
    // original 
    #define MAX_SIZE_555 2000000
    #define DEBUG
    #define DEBUG_SEND
    #define DEBUG_ERRORS
#endif

#define DEBUG_LEVEL 2

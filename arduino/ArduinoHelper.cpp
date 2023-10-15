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


#ifdef ARDUINO
#include "ArduinoHelper.hh"
#include "WiFi.h"

// Allocate memory for error messages
char error_msg[ERROR_MSG_SIZE];

// /// Arduino does not support the exit method - we just stop the processing
// void stop(int rc) {
//     if (rc!=0){
//         Serial.print("return code: ");
//         Serial.println(rc);
//     }
//     Serial.println("THE END!");
//     while(true);
// }

// get local IP address
uint32_t localip(){
    return WiFi.localIP();
}

/// gethostname using the WiFi.getHostname()
extern "C" int gethostname(char* str, unsigned len){
    const char* name = WiFi.getHostname();
    strncpy(str, name ,len);
    //Serial.print("hostname: ");
    //Serial.println(name);
    return 1;
}


#endif
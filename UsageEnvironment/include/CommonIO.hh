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

// Common Layer for stdio.h
#pragma once

#include "Config.hh"
#ifdef ARDUINO
    #include "ArduinoStdio.hh"
#else
    #include "stdio.h"
    // We use the original logic with printf(stderr,...)
    #define LOG(...) printf(__VA_ARGS__)
    #define LOGFLUSH() fflush(stdout)

#endif
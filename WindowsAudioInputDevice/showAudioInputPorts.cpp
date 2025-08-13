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
// Copyright (c) 1996-2025, Live Networks, Inc.  All rights reserved
// A program that prints out this computer's audio input ports

#include "AudioInputDevice.hh"
#include <stdio.h>

int main(int argc, char** argv) {
	AudioPortNames* portNames = AudioInputDevice::getPortNames();
	if (portNames == NULL) {
		fprintf(stderr, "AudioInputDevice::getPortNames() failed!\n");
		exit(1);
	}

	printf("%d available audio input ports:\n", portNames->numPorts);
	for (unsigned i = 0; i < portNames->numPorts; ++i) {
		printf("%d\t%s\n", i, portNames->portName[i]);
	}

  return 0;
}

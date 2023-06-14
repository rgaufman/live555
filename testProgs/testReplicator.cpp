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
// Copyright (c) 1996-2023, Live Networks, Inc.  All rights reserved
// A demo application that receives a UDP multicast stream, replicates it (using the "StreamReplicator" class),
// and retransmits one replica stream to another (multicast or unicast) address & port,
// and writes the other replica stream to a file.
//
// main program

#include <liveMedia.hh>
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

UsageEnvironment* env;

// To receive a "source-specific multicast" (SSM) stream, uncomment this:
//#define USE_SSM 1

void startReplicaUDPSink(StreamReplicator* replicator, char const* outputAddressStr, portNumBits outputPortNum); // forward
void startReplicaFileSink(StreamReplicator* replicator, char const* outputFileName); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  // Create a 'groupsock' for the input multicast group,port:
  char const* inputAddressStr
#ifdef USE_SSM
    = "232.255.42.42";
#else
    = "239.255.42.42";
#endif
  NetAddressList inputAddresses(inputAddressStr);
  struct sockaddr_storage inputAddress;
  copyAddress(inputAddress, inputAddresses.firstAddress());

  Port const inputPort(8888);
  unsigned char const inputTTL = 0; // we're only reading from this mcast group

#ifdef USE_SSM
  char const* sourceAddressStr = "aaa.bbb.ccc.ddd";
                           // replace this with the real source address
  NetAddressList sourceFilterAddresses(sourceAddressStr);
  struct sockaddr_storage sourceFilterAddress;
  copyAddress(sourceFilterAddress, sourceFilterAddresses.firstAddress());

  Groupsock inputGroupsock(*env, inputAddress, sourceFilterAddress, inputPort);
#else
  Groupsock inputGroupsock(*env, inputAddress, inputPort, inputTTL);
#endif

  // Then create a liveMedia 'source' object, encapsulating this groupsock:
  FramedSource* source = BasicUDPSource::createNew(*env, &inputGroupsock);

  // And feed this into a 'stream replicator':
  StreamReplicator* replicator = StreamReplicator::createNew(*env, source);

  // Then create a network (UDP) 'sink' object to receive a replica of the input stream, and start it.
  // If you wish, you can duplicate this line - with different network addresses and ports - to create multiple output UDP streams:
  startReplicaUDPSink(replicator, "239.255.43.43", 4444);

  // Then create a file 'sink' object to receive a replica of the input stream, and start it.
  // If you wish, you can duplicate this line - with a different file name - to create multiple output files:
  startReplicaFileSink(replicator, "test.out");

  // Finally, enter the 'event loop' (which is where most of the 'real work' in a LIVE555-based application gets done):
  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void startReplicaUDPSink(StreamReplicator* replicator, char const* outputAddressStr, portNumBits outputPortNum) {
  // Begin by creating an input stream from our replicator:
  FramedSource* source = replicator->createStreamReplica();

  // Create a 'groupsock' for the destination address and port:
  NetAddressList outputAddresses(outputAddressStr);
  struct sockaddr_storage outputAddress;
  copyAddress(outputAddress, outputAddresses.firstAddress());

  Port const outputPort(outputPortNum);
  unsigned char const outputTTL = 255;

  Groupsock* outputGroupsock = new Groupsock(*env, outputAddress, outputPort, outputTTL);

  // Then create a liveMedia 'sink' object, encapsulating this groupsock:
  unsigned const maxPacketSize = 65536; // allow for large UDP packets
  MediaSink* sink = BasicUDPSink::createNew(*env, outputGroupsock, maxPacketSize);

  // Now, start playing, feeding the sink object from the source:
  sink->startPlaying(*source, NULL, NULL);
}

void startReplicaFileSink(StreamReplicator* replicator, char const* outputFileName) {
  // Begin by creating an input stream from our replicator:
  FramedSource* source = replicator->createStreamReplica();

  // Then create a 'file sink' object to receive thie replica stream:
  MediaSink* sink = FileSink::createNew(*env, outputFileName);

  // Now, start playing, feeding the sink object from the source:
  sink->startPlaying(*source, NULL, NULL);
}

/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
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
// Copyright (c) 1996-2013 Live Networks, Inc.  All rights reserved.
// A class that encapsulates a Matroska file.
// C++ header

#ifndef _MATROSKA_FILE_HH
#define _MATROSKA_FILE_HH

#ifndef _MEDIA_HH
#include "Media.hh"
#endif
#ifndef _HASH_TABLE_HH
#include "HashTable.hh"
#endif

class MatroskaTrack; // forward
class MatroskaDemux; // forward

class MatroskaFile: public Medium {
public:
  typedef void (onCreationFunc)(MatroskaFile* newFile, void* clientData);
  static void createNew(UsageEnvironment& env, char const* fileName, onCreationFunc* onCreation, void* onCreationClientData,
			char const* preferredLanguage = "eng");
    // Note: Unlike most "createNew()" functions, this one doesn't return a new object immediately.  Instead, because this class
    // requires file reading (to parse the Matroska 'Track' headers) before a new object can be initialized, the creation of a new
    // object is signalled by calling - from the event loop - an 'onCreationFunc' that is passed as a parameter to "createNew()".

  // For looking up and iterating over the file's tracks:
  class TrackTable {
  public:
    TrackTable();
    virtual ~TrackTable();

    void add(MatroskaTrack* newTrack, unsigned trackNumber);
    MatroskaTrack* lookup(unsigned trackNumber);

    unsigned numTracks() const;

    class Iterator {
    public:
      Iterator(TrackTable& ourTable);
      virtual ~Iterator();
      MatroskaTrack* next();
    private:
      HashTable::Iterator* fIter;
    };

  private:
    friend class Iterator;
    HashTable* fTable;
  };

  MatroskaTrack* lookup(unsigned trackNumber) { return fTracks.lookup(trackNumber); } // shortcut

  // Create a demultiplexor for extracting tracks from this file.  (Separate clients will typically have separate demultiplexors.)
  MatroskaDemux* newDemux();

  // Parameters of the file ('Segment'); set when the file is parsed:
  unsigned timecodeScale() { return fTimecodeScale; } // in nanoseconds
  float segmentDuration() { return fSegmentDuration; } // in units of "timecodeScale()"
  float fileDuration(); // in seconds
  TrackTable& tracks() { return fTracks; }
  
  char const* fileName() const { return fFileName; }

  unsigned chosenVideoTrackNumber() { return fChosenVideoTrackNumber; }
  unsigned chosenAudioTrackNumber() { return fChosenAudioTrackNumber; }
  unsigned chosenSubtitleTrackNumber() { return fChosenSubtitleTrackNumber; }

private:
  MatroskaFile(UsageEnvironment& env, char const* fileName, onCreationFunc* onCreation, void* onCreationClientData,
	       char const* preferredLanguage);
      // called only by createNew()
  virtual ~MatroskaFile();

  static void handleEndOfTrackHeaderParsing(void* clientData);
  void handleEndOfTrackHeaderParsing();

  void addCuePoint(double cueTime, u_int64_t clusterOffsetInFile, unsigned blockNumWithinCluster);
  Boolean lookupCuePoint(double& cueTime, u_int64_t& resultClusterOffsetInFile, unsigned& resultBlockNumWithinCluster);
  void printCuePoints(FILE* fid);

  void removeDemux(MatroskaDemux* demux);

private:
  friend class MatroskaFileParser;
  friend class MatroskaDemux;
  char const* fFileName;
  onCreationFunc* fOnCreation;
  void* fOnCreationClientData;
  char const* fPreferredLanguage;

  unsigned fTimecodeScale; // in nanoseconds
  float fSegmentDuration; // in units of "fTimecodeScale"
  u_int64_t fSegmentDataOffset, fClusterOffset, fCuesOffset;

  TrackTable fTracks;
  HashTable* fDemuxesTable;
  class CuePoint* fCuePoints;
  unsigned fChosenVideoTrackNumber, fChosenAudioTrackNumber, fChosenSubtitleTrackNumber;
  class MatroskaFileParser* fParserForInitialization;
};

// We define our own track type codes as bits (powers of 2), so we can use the set of track types as a bitmap, representing a set:
// (Note that MATROSKA_TRACK_TYPE_OTHER must be last, and have the largest value.)
#define MATROSKA_TRACK_TYPE_VIDEO 0x01
#define MATROSKA_TRACK_TYPE_AUDIO 0x02
#define MATROSKA_TRACK_TYPE_SUBTITLE 0x04
#define MATROSKA_TRACK_TYPE_OTHER 0x08

class MatroskaTrack {
public:
  MatroskaTrack();
  virtual ~MatroskaTrack();

  // track parameters
  unsigned trackNumber;
  u_int8_t trackType;
  Boolean isEnabled, isDefault, isForced;
  unsigned defaultDuration;
  char* name;
  char* language;
  char* codecID;
  unsigned samplingFrequency;
  unsigned numChannels;
  char const* mimeType;
  unsigned codecPrivateSize;
  u_int8_t* codecPrivate;
  unsigned headerStrippedBytesSize;
  u_int8_t* headerStrippedBytes;
  unsigned subframeSizeSize; // 0 means: frames do not have subframes (the default behavior)
  Boolean haveSubframes() const { return subframeSizeSize > 0; }
};

class MatroskaDemux: public Medium {
public:
  FramedSource* newDemuxedTrack(unsigned trackNumber);
    // Note: We assume that:
    // - Every track created by "newDemuxedTrack()" is later read
    // - All calls to "newDemuxedTrack()" are made before any track is read

  class MatroskaDemuxedTrack* lookupDemuxedTrack(unsigned trackNumber);

protected:
  friend class MatroskaFile;
  MatroskaDemux(MatroskaFile& ourFile); // we're created only by a "MatroskaFile" (a friend)
  virtual ~MatroskaDemux();

private:
  friend class MatroskaDemuxedTrack;
  void removeTrack(unsigned trackNumber);
  void continueReading(); // called by a demuxed track to tell us that it has a pending read ("doGetNextFrame()")
  void seekToTime(double& seekNPT);

  static void handleEndOfFile(void* clientData);
  void handleEndOfFile();

private:
  MatroskaFile& fOurFile;
  class MatroskaFileParser* fOurParser;
  HashTable* fDemuxedTracksTable;
};

#endif

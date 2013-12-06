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
// Implementation

#include "MatroskaFileParser.hh"
#include "MatroskaDemuxedTrack.hh"
#include <ByteStreamFileSource.hh>

////////// CuePoint definition //////////

class CuePoint {
public:
  CuePoint(double cueTime, u_int64_t clusterOffsetInFile, unsigned blockNumWithinCluster/* 1-based */);
  virtual ~CuePoint();

  static void addCuePoint(CuePoint*& root, double cueTime, u_int64_t clusterOffsetInFile, unsigned blockNumWithinCluster/* 1-based */,
			  Boolean& needToReviseBalanceOfParent);
    // If "cueTime" == "root.fCueTime", replace the existing data, otherwise add to the left or right subtree.
    // (Note that this is a static member function because - as a result of tree rotation - "root" might change.)

  Boolean lookup(double& cueTime, u_int64_t& resultClusterOffsetInFile, unsigned& resultBlockNumWithinCluster);

  static void fprintf(FILE* fid, CuePoint* cuePoint); // used for debugging; it's static to allow for "cuePoint == NULL"

private:
  // The "CuePoint" tree is implemented as an AVL Tree, to keep it balanced (for efficient lookup).
  CuePoint* fSubTree[2]; // 0 => left; 1 => right
  CuePoint* left() const { return fSubTree[0]; }
  CuePoint* right() const { return fSubTree[1]; }
  char fBalance; // height of right subtree - height of left subtree

  static void rotate(unsigned direction/*0 => left; 1 => right*/, CuePoint*& root); // used to keep the tree in balance

  double fCueTime;
  u_int64_t fClusterOffsetInFile;
  unsigned fBlockNumWithinCluster; // 0-based
};

UsageEnvironment& operator<<(UsageEnvironment& env, const CuePoint* cuePoint); // used for debugging


////////// MatroskaTrackTable definition /////////

// For looking up and iterating over the file's tracks:
class MatroskaTrackTable {
public:
  MatroskaTrackTable();
  virtual ~MatroskaTrackTable();

  void add(MatroskaTrack* newTrack, unsigned trackNumber);
  MatroskaTrack* lookup(unsigned trackNumber);

  unsigned numTracks() const;

  class Iterator {
  public:
    Iterator(MatroskaTrackTable& ourTable);
    virtual ~Iterator();
    MatroskaTrack* next();
  private:
    HashTable::Iterator* fIter;
  };

private:
  friend class Iterator;
  HashTable* fTable;
};



////////// MatroskaFile implementation //////////

void MatroskaFile
::createNew(UsageEnvironment& env, char const* fileName, onCreationFunc* onCreation, void* onCreationClientData,
	    char const* preferredLanguage) {
  new MatroskaFile(env, fileName, onCreation, onCreationClientData, preferredLanguage);
}

MatroskaFile::MatroskaFile(UsageEnvironment& env, char const* fileName, onCreationFunc* onCreation, void* onCreationClientData,
			   char const* preferredLanguage)
  : Medium(env),
    fFileName(strDup(fileName)), fOnCreation(onCreation), fOnCreationClientData(onCreationClientData),
    fPreferredLanguage(strDup(preferredLanguage)),
    fTimecodeScale(1000000), fSegmentDuration(0.0), fSegmentDataOffset(0), fClusterOffset(0), fCuesOffset(0), fCuePoints(NULL),
    fChosenVideoTrackNumber(0), fChosenAudioTrackNumber(0), fChosenSubtitleTrackNumber(0) {
  fTrackTable = new MatroskaTrackTable;
  fDemuxesTable = HashTable::create(ONE_WORD_HASH_KEYS);

  FramedSource* inputSource = ByteStreamFileSource::createNew(envir(), fileName);
  if (inputSource == NULL) {
    // The specified input file does not exist!
    fParserForInitialization = NULL;
    handleEndOfTrackHeaderParsing(); // we have no file, and thus no tracks, but we still need to signal this
  } else {
    // Initialize ourselves by parsing the file's 'Track' headers:
    fParserForInitialization = new MatroskaFileParser(*this, inputSource, handleEndOfTrackHeaderParsing, this, NULL);
  }
}

MatroskaFile::~MatroskaFile() {
  delete fParserForInitialization;
  delete fCuePoints;

  // Delete any outstanding "MatroskaDemux"s, and the table for them:
  MatroskaDemux* demux;
  while ((demux = (MatroskaDemux*)fDemuxesTable->RemoveNext()) != NULL) {
    delete demux;
  }
  delete fDemuxesTable;
  delete fTrackTable;

  delete[] (char*)fPreferredLanguage;
  delete[] (char*)fFileName;
}

void MatroskaFile::handleEndOfTrackHeaderParsing(void* clientData) {
  ((MatroskaFile*)clientData)->handleEndOfTrackHeaderParsing();
}

class TrackChoiceRecord {
public:
  unsigned trackNumber;
  u_int8_t trackType;
  unsigned choiceFlags;
};

void MatroskaFile::handleEndOfTrackHeaderParsing() {
  // Having parsed all of our track headers, iterate through the tracks to figure out which ones should be played.
  // The Matroska 'specification' is rather imprecise about this (as usual).  However, we use the following algorithm:
  // - Use one (but no more) enabled track of each type (video, audio, subtitle).  (Ignore all tracks that are not 'enabled'.)
  // - For each track type, choose the one that's 'forced'.
  //     - If more than one is 'forced', choose the first one that matches our preferred language, or the first if none matches.
  //     - If none is 'forced', choose the one that's 'default'.
  //     - If more than one is 'default', choose the first one that matches our preferred language, or the first if none matches.
  //     - If none is 'default', choose the first one that matches our preferred language, or the first if none matches.
  unsigned numTracks = fTrackTable->numTracks();
  if (numTracks > 0) {
    TrackChoiceRecord* trackChoice = new TrackChoiceRecord[numTracks];
    unsigned numEnabledTracks = 0;
    MatroskaTrackTable::Iterator iter(*fTrackTable);
    MatroskaTrack* track;
    while ((track = iter.next()) != NULL) {
      if (!track->isEnabled || track->trackType == 0 || track->codecID == NULL) continue; // track not enabled, or not fully-defined

      trackChoice[numEnabledTracks].trackNumber = track->trackNumber;
      trackChoice[numEnabledTracks].trackType = track->trackType;

      // Assign flags for this track so that, when sorted, the largest value becomes our choice:
      unsigned choiceFlags = 0;
      if (fPreferredLanguage != NULL && track->language != NULL && strcmp(fPreferredLanguage, track->language) == 0) {
	// This track matches our preferred language:
	choiceFlags |= 1;
      }
      if (track->isForced) {
	choiceFlags |= 4;
      } else if (track->isDefault) {
	choiceFlags |= 2;
      }
      trackChoice[numEnabledTracks].choiceFlags = choiceFlags;

      ++numEnabledTracks;
    }

    // Choose the desired track for each track type:
    for (u_int8_t trackType = 0x01; trackType != MATROSKA_TRACK_TYPE_OTHER; trackType <<= 1) {
      int bestNum = -1;
      int bestChoiceFlags = -1;
      for (unsigned i = 0; i < numEnabledTracks; ++i) {
	if (trackChoice[i].trackType == trackType && (int)trackChoice[i].choiceFlags > bestChoiceFlags) {
	  bestNum = i;
	  bestChoiceFlags = (int)trackChoice[i].choiceFlags;
	}
      }
      if (bestChoiceFlags >= 0) { // There is a track for this track type
	if (trackType == MATROSKA_TRACK_TYPE_VIDEO) fChosenVideoTrackNumber = trackChoice[bestNum].trackNumber;
	else if (trackType == MATROSKA_TRACK_TYPE_AUDIO) fChosenAudioTrackNumber = trackChoice[bestNum].trackNumber;
	else fChosenSubtitleTrackNumber = trackChoice[bestNum].trackNumber;
      }
    }

    delete[] trackChoice;
  }
  
#ifdef DEBUG
  if (fChosenVideoTrackNumber > 0) fprintf(stderr, "Chosen video track: #%d\n", fChosenVideoTrackNumber); else fprintf(stderr, "No chosen video track\n");
  if (fChosenAudioTrackNumber > 0) fprintf(stderr, "Chosen audio track: #%d\n", fChosenAudioTrackNumber); else fprintf(stderr, "No chosen audio track\n");
  if (fChosenSubtitleTrackNumber > 0) fprintf(stderr, "Chosen subtitle track: #%d\n", fChosenSubtitleTrackNumber); else fprintf(stderr, "No chosen subtitle track\n");
#endif

  // Delete our parser, because it's done its job now:
  delete fParserForInitialization; fParserForInitialization = NULL;

  // Finally, signal our caller that we've been created and initialized:
  if (fOnCreation != NULL) (*fOnCreation)(this, fOnCreationClientData);
}

MatroskaTrack* MatroskaFile::lookup(unsigned trackNumber) const {
  return fTrackTable->lookup(trackNumber);
}

MatroskaDemux* MatroskaFile::newDemux() {
  MatroskaDemux* demux = new MatroskaDemux(*this);
  fDemuxesTable->Add((char const*)demux, demux);

  return demux;
}

void MatroskaFile::removeDemux(MatroskaDemux* demux) {
  fDemuxesTable->Remove((char const*)demux);
}

float MatroskaFile::fileDuration() {
  if (fCuePoints == NULL) return 0.0; // Hack, because the RTSP server code assumes that duration > 0 => seekable. (fix this) #####

  return segmentDuration()*(timecodeScale()/1000000000.0f);
}

void MatroskaFile::addTrack(MatroskaTrack* newTrack, unsigned trackNumber) {
  fTrackTable->add(newTrack, trackNumber);
}

void MatroskaFile::addCuePoint(double cueTime, u_int64_t clusterOffsetInFile, unsigned blockNumWithinCluster) {
  Boolean dummy = False; // not used
  CuePoint::addCuePoint(fCuePoints, cueTime, clusterOffsetInFile, blockNumWithinCluster, dummy);
}

Boolean MatroskaFile::lookupCuePoint(double& cueTime, u_int64_t& resultClusterOffsetInFile, unsigned& resultBlockNumWithinCluster) {
  if (fCuePoints == NULL) return False;

  (void)fCuePoints->lookup(cueTime, resultClusterOffsetInFile, resultBlockNumWithinCluster);
  return True;
}

void MatroskaFile::printCuePoints(FILE* fid) {
  CuePoint::fprintf(fid, fCuePoints);
}


////////// MatroskaTrackTable implementation //////////

MatroskaTrackTable::MatroskaTrackTable()
  : fTable(HashTable::create(ONE_WORD_HASH_KEYS)) {
}

MatroskaTrackTable::~MatroskaTrackTable() {
  // Remove and delete all of our "MatroskaTrack" descriptors, and the hash table itself:
  MatroskaTrack* track;
  while ((track = (MatroskaTrack*)fTable->RemoveNext()) != NULL) {
    delete track;
  }
  delete fTable;
} 

void MatroskaTrackTable::add(MatroskaTrack* newTrack, unsigned trackNumber) {
  if (newTrack != NULL && newTrack->trackNumber != 0) fTable->Remove((char const*)newTrack->trackNumber);
  MatroskaTrack* existingTrack = (MatroskaTrack*)fTable->Add((char const*)trackNumber, newTrack);
  delete existingTrack; // in case it wasn't NULL
}

MatroskaTrack* MatroskaTrackTable::lookup(unsigned trackNumber) {
  return (MatroskaTrack*)fTable->Lookup((char const*)trackNumber);
}

unsigned MatroskaTrackTable::numTracks() const { return fTable->numEntries(); }

MatroskaTrackTable::Iterator::Iterator(MatroskaTrackTable& ourTable) {
  fIter = HashTable::Iterator::create(*(ourTable.fTable));
}

MatroskaTrackTable::Iterator::~Iterator() {
  delete fIter;
}

MatroskaTrack* MatroskaTrackTable::Iterator::next() {
  char const* key;
  return (MatroskaTrack*)fIter->next(key);
}


////////// MatroskaTrack implementation //////////

MatroskaTrack::MatroskaTrack()
  : trackNumber(0/*not set*/), trackType(0/*unknown*/),
    isEnabled(True), isDefault(True), isForced(False),
    defaultDuration(0),
    name(NULL), language(NULL), codecID(NULL),
    samplingFrequency(0), numChannels(2), mimeType(""),
    codecPrivateSize(0), codecPrivate(NULL), headerStrippedBytesSize(0), headerStrippedBytes(NULL),
    subframeSizeSize(0) {
}

MatroskaTrack::~MatroskaTrack() {
  delete[] name; delete[] language; delete[] codecID;
  delete[] codecPrivate;
  delete[] headerStrippedBytes;
}


////////// MatroskaDemux implementation //////////

MatroskaDemux::MatroskaDemux(MatroskaFile& ourFile)
  : Medium(ourFile.envir()),
    fOurFile(ourFile), fDemuxedTracksTable(HashTable::create(ONE_WORD_HASH_KEYS)),
    fNextTrackTypeToCheck(0x1) {
  fOurParser = new MatroskaFileParser(ourFile, ByteStreamFileSource::createNew(envir(), ourFile.fileName()),
				      handleEndOfFile, this, this);
}

MatroskaDemux::~MatroskaDemux() {
  // Begin by acting as if we've reached the end of the source file.  This should cause all of our demuxed tracks to get closed.
  handleEndOfFile();

  // Then delete our table of "MatroskaDemuxedTrack"s
  // - but not the "MatroskaDemuxedTrack"s themselves; that should have already happened:
  delete fDemuxedTracksTable;

  delete fOurParser;
  fOurFile.removeDemux(this);
}

FramedSource* MatroskaDemux::newDemuxedTrack() {
  unsigned dummyResultTrackNumber;
  return newDemuxedTrack(dummyResultTrackNumber);
}

FramedSource* MatroskaDemux::newDemuxedTrack(unsigned& resultTrackNumber) {
  FramedSource* result;
  resultTrackNumber = 0;

  for (result = NULL; result == NULL && fNextTrackTypeToCheck != MATROSKA_TRACK_TYPE_OTHER;
       fNextTrackTypeToCheck <<= 1) {
    if (fNextTrackTypeToCheck == MATROSKA_TRACK_TYPE_VIDEO) resultTrackNumber = fOurFile.chosenVideoTrackNumber();
    else if (fNextTrackTypeToCheck == MATROSKA_TRACK_TYPE_AUDIO) resultTrackNumber = fOurFile.chosenAudioTrackNumber();
    else if (fNextTrackTypeToCheck == MATROSKA_TRACK_TYPE_SUBTITLE) resultTrackNumber = fOurFile.chosenSubtitleTrackNumber();

    result = newDemuxedTrackByTrackNumber(resultTrackNumber);
  }

  return result;
}

FramedSource* MatroskaDemux::newDemuxedTrackByTrackNumber(unsigned trackNumber) {
  if (trackNumber == 0) return NULL;

  FramedSource* track = new MatroskaDemuxedTrack(envir(), trackNumber, *this);
  fDemuxedTracksTable->Add((char const*)trackNumber, track);
  return track;
}

MatroskaDemuxedTrack* MatroskaDemux::lookupDemuxedTrack(unsigned trackNumber) {
  return (MatroskaDemuxedTrack*)fDemuxedTracksTable->Lookup((char const*)trackNumber);
}

void MatroskaDemux::removeTrack(unsigned trackNumber) {
  fDemuxedTracksTable->Remove((char const*)trackNumber);
  if (fDemuxedTracksTable->numEntries() == 0) {
    // We no longer have any demuxed tracks, so delete ourselves now:
    delete this;
  }
}

void MatroskaDemux::continueReading() {
  fOurParser->continueParsing();  
}

void MatroskaDemux::seekToTime(double& seekNPT) {
  if (fOurParser != NULL) fOurParser->seekToTime(seekNPT);
}

void MatroskaDemux::handleEndOfFile(void* clientData) {
  ((MatroskaDemux*)clientData)->handleEndOfFile();
}

void MatroskaDemux::handleEndOfFile() {
  // Iterate through all of our 'demuxed tracks', handling 'end of input' on each one.
  // Hack: Because this can cause the hash table to get modified underneath us, we don't call the handlers until after we've
  // first iterated through all of the tracks.
  unsigned numTracks = fDemuxedTracksTable->numEntries();
  if (numTracks == 0) return;
  MatroskaDemuxedTrack** tracks = new MatroskaDemuxedTrack*[numTracks];

  HashTable::Iterator* iter = HashTable::Iterator::create(*fDemuxedTracksTable);
  unsigned i;
  char const* trackNumber;

  for (i = 0; i < numTracks; ++i) {
    tracks[i] = (MatroskaDemuxedTrack*)iter->next(trackNumber);
  }
  delete iter;

  for (i = 0; i < numTracks; ++i) {
    if (tracks[i] == NULL) continue; // sanity check; shouldn't happen
    FramedSource::handleClosure(tracks[i]);
  }

  delete[] tracks;
}


////////// CuePoint implementation //////////

CuePoint::CuePoint(double cueTime, u_int64_t clusterOffsetInFile, unsigned blockNumWithinCluster)
  : fBalance(0),
    fCueTime(cueTime), fClusterOffsetInFile(clusterOffsetInFile), fBlockNumWithinCluster(blockNumWithinCluster - 1) {
  fSubTree[0] = fSubTree[1] = NULL;
}

CuePoint::~CuePoint() {
  delete fSubTree[0]; delete fSubTree[1];
}

#ifndef ABS
#define ABS(x) (x)<0 ? -(x) : (x)
#endif

void CuePoint::addCuePoint(CuePoint*& root, double cueTime, u_int64_t clusterOffsetInFile, unsigned blockNumWithinCluster,
			   Boolean& needToReviseBalanceOfParent) {
  needToReviseBalanceOfParent = False; // by default; may get changed below

  if (root == NULL) {
    root = new CuePoint(cueTime, clusterOffsetInFile, blockNumWithinCluster);
    needToReviseBalanceOfParent = True;
  } else if (cueTime == root->fCueTime) {
    // Replace existing data:
    root->fClusterOffsetInFile = clusterOffsetInFile;
    root->fBlockNumWithinCluster = blockNumWithinCluster - 1;
  } else {
    // Add to our left or right subtree:
    int direction = cueTime > root->fCueTime; // 0 (left) or 1 (right)
    Boolean needToReviseOurBalance = False;
    addCuePoint(root->fSubTree[direction], cueTime, clusterOffsetInFile, blockNumWithinCluster, needToReviseOurBalance);

    if (needToReviseOurBalance) {
      // We need to change our 'balance' number, perhaps while also performing a rotation to bring ourself back into balance:
      if (root->fBalance == 0) {
	// We were balanced before, but now we're unbalanced (by 1) on the "direction" side:
	root->fBalance = -1 + 2*direction; // -1 for "direction" 0; 1 for "direction" 1
	needToReviseBalanceOfParent = True;
      } else if (root->fBalance == 1 - 2*direction) { // 1 for "direction" 0; -1 for "direction" 1
	// We were unbalanced (by 1) on the side opposite to where we added an entry, so now we're balanced:
	root->fBalance = 0;
      } else {
	// We were unbalanced (by 1) on the side where we added an entry, so now we're unbalanced by 2, and have to rebalance:
	if (root->fSubTree[direction]->fBalance == -1 + 2*direction) { // -1 for "direction" 0; 1 for "direction" 1
	  // We're 'doubly-unbalanced' on this side, so perform a single rotation in the opposite direction:
	  root->fBalance = root->fSubTree[direction]->fBalance = 0;
	  rotate(1-direction, root);
	} else {
	  // This is the Left-Right case (for "direction" 0) or the Right-Left case (for "direction" 1); perform two rotations:
	  char newParentCurBalance = root->fSubTree[direction]->fSubTree[1-direction]->fBalance;
	  if (newParentCurBalance == 1 - 2*direction) { // 1 for "direction" 0; -1 for "direction" 1
	    root->fBalance = 0;
	    root->fSubTree[direction]->fBalance = -1 + 2*direction; // -1 for "direction" 0; 1 for "direction" 1
	  } else if (newParentCurBalance == 0) {
	    root->fBalance = 0;
	    root->fSubTree[direction]->fBalance = 0;
	  } else {
	    root->fBalance = 1 - 2*direction; // 1 for "direction" 0; -1 for "direction" 1
	    root->fSubTree[direction]->fBalance = 0;
	  }
	  rotate(direction, root->fSubTree[direction]);

	  root->fSubTree[direction]->fBalance = 0; // the new root will be balanced
	  rotate(1-direction, root);
	}
      }
    }
  }
}

Boolean CuePoint::lookup(double& cueTime, u_int64_t& resultClusterOffsetInFile, unsigned& resultBlockNumWithinCluster) {
  if (cueTime < fCueTime) {
    if (left() == NULL) {
      resultClusterOffsetInFile = 0;
      resultBlockNumWithinCluster = 0;
      return False;
    } else {
      return left()->lookup(cueTime, resultClusterOffsetInFile, resultBlockNumWithinCluster);
    }
  } else {
    if (right() == NULL || !right()->lookup(cueTime, resultClusterOffsetInFile, resultBlockNumWithinCluster)) {
      // Use this record:
      cueTime = fCueTime;
      resultClusterOffsetInFile = fClusterOffsetInFile;
      resultBlockNumWithinCluster = fBlockNumWithinCluster;
    }
    return True;
  }
}

void CuePoint::fprintf(FILE* fid, CuePoint* cuePoint) {
  if (cuePoint != NULL) {
    ::fprintf(fid, "[");
    fprintf(fid, cuePoint->left());

    ::fprintf(fid, ",%.1f{%d},", cuePoint->fCueTime, cuePoint->fBalance);

    fprintf(fid, cuePoint->right());
    ::fprintf(fid, "]");
  }
}

void CuePoint::rotate(unsigned direction/*0 => left; 1 => right*/, CuePoint*& root) {
  CuePoint* pivot = root->fSubTree[1-direction]; // ASSERT: pivot != NULL
  root->fSubTree[1-direction] = pivot->fSubTree[direction];
  pivot->fSubTree[direction] = root;
  root = pivot;
}

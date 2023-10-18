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

// stdio replacement using the SD library

#pragma once
#include "FileAccess.hh"
#include "FileAccessFILE.hh"
#include "SD.h"
#include "SPI.h"

/**
 * @brief stdio Access to SD library
 *
 */

class FileAccessSD : public File555 {
public:
  FileAccessSD(const char *file, const char *mode) : File555(file, mode) {
    fopen(file, mode);
  }

  void fopen(const char *path, const char *mode) override {
    LOG("fopen: %s", path);
    sdFile = SD.open(path, mode);
  }

  int fclose(void) override {
    if (!sdFile)
      return -1;
    sdFile.close();
    return 1;
  }

  int fgetc(void) override { return sdFile.read(); }

  int fputc(int c) override { return sdFile.write((char)c); }

  int fseek(long offset, int origin) override {
    if (!sdFile)
      return 0;
    if (!sdFile)
      return -1;

    int pos = 0;
    SeekMode mode = SeekSet;
    switch (origin) {
    case SEEK_SET:
      mode = SeekSet;
      break;
    case SEEK_CUR:
      mode = SeekCur;
      break;
    case SEEK_END:
      mode = SeekEnd;
      break;
    }
    return sdFile.seek(offset, mode);
  }

  long int ftell(void) override {
    if (!sdFile)
      return -1;
    return sdFile.position();
  }

  size_t fread(void *ptr, size_t size, size_t count) override {
    if (!sdFile)
      return 0;
    return sdFile.read((uint8_t *)ptr, size * count);
  }

  size_t fwrite(const void *ptr, size_t size, size_t count) override {
    if (!sdFile)
      return 0;
    return sdFile.write((const uint8_t *)ptr, size * count);
  }

  int ferror(void) override { return 0; }

  void clearerr(void) override {}

  int ungetc(int c) override {
    // not supported
    return -1;
  };

  /// This function returns a non-zero value when End-of-File indicator
  /// associated with the stream is set,
  int feof(void) override {
    if (!sdFile)
      return 1;
    return sdFile.available() == 0;
  }

  void rewind(void) override { fseek(SEEK_SET, 0); }

  int fflush(void) override {
    sdFile.flush();
    return 0;
  }

  size_t size(void) { return sdFile.size(); }

  bool isOpen() override { return sdFile; }

protected:
  SDFile sdFile;
};

/**
 * Driver for SD
 */
class FileDriverSD : public FileDriver {
public:
  FileDriverSD(int sdPin = -1, const char *path = "") : FileDriver(path) {
    sd_pin = sdPin;
  }

  FileAccessSD *fopen(const char *file, const char *mode = "r") {
    if (!is_setup)
      setup();
    FileAccessSD *result = new FileAccessSD(resolvePath(file).c_str(), mode);
    return result->isOpen() ? result : nullptr;
  };

protected:
  int sd_pin = -1;
  bool is_setup = false;

  void setup() {
    if (sd_pin == -1) {
      if (!SD.begin()) {
        LOG("SD.begin failed");
      }
    } else {
      if (!SD.begin(sd_pin)) {
        LOG("SD.begin failed");
      }
    }
    is_setup = true;
  }
};

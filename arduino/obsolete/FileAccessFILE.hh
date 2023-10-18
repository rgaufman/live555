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
#include "stdio.h"
#include <sys/stat.h>

/**
 * @brief stdio Access with the built in stdio.h library of actual
 * microcontroller
 */

class FileAccessFILE : public File555 {
public:
  FileAccessFILE() = default;

  FileAccessFILE(const char *file, const char *mode) : File555(file, mode) {
    fopen(file, mode);
  }

  void fopen(const char *file, const char *mode) override {
    this->file_name = file;
    fp = ::fopen(file_name, mode);
    LOG("fopen: %s -> %s", file, fp==nullptr ? "error": "ok");
  }

  int fclose(void) override {
    int rc = ::fclose(fp);
    fp = nullptr;
    return rc;
  }

  int fgetc(void) override { return ::fgetc(fp); }

  int fputc(int c) override { return ::fputc(c, fp); }

  int fseek(long offset, int origin) override {
    return ::fseek(fp, offset, origin);
  }

  long int ftell(void) override { return ::ftell(fp); }

  size_t fread(void *ptr, size_t size, size_t count) override {
    return ::fread(ptr, size, count, fp);
  }

  size_t fwrite(const void *ptr, size_t size, size_t count) override {
    return ::fwrite(ptr, size, count, fp);
  }

  int ferror(void) override { return ::ferror(fp); }

  int ungetc(int c) override { return ::ungetc(c, fp); };

  int feof(void) override { return ::feof(fp); }

  void rewind(void) override { ::rewind(fp); }

  int fflush(void) override { return ::fflush(fp); }

  size_t size(void) override {
    if (file_size == 0) {
      struct stat sb;
      if (stat(file_name, &sb) == 0) {
        file_size = sb.st_size;
      } else {
        file_size = 0;
      }
    }
    return file_size;
  }

  void clearerr(void) override { return ::clearerr(fp); }

  bool isOpen() override { return fp != nullptr; }

protected:
  size_t file_size = 0;
  const char *file_name = "";
  FILE *fp = nullptr;
};

class FileDriverFILE : public FileDriver {
public:
  FileDriverFILE() : FileDriver("") {}
  FileDriverFILE(const char *path) : FileDriver(path) {}

  FileAccessFILE *fopen(const char *file, const char *mode = "r") {
    FileAccessFILE *result =
        new FileAccessFILE(resolvePath(file).c_str(), mode);
    return result->isOpen() ? result : nullptr;
  };
};

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

#include "Stream.h"
#include "FileAccess.hh"

/**
 * @brief stdio Access using an single Arduino Stream 
 */

class FileAccessStream : public File555 {
    public:
        FileAccessStream(Stream &stream){
            setStream(stream);
        }
        void setStream(Stream &stream){
            p_stream = &stream;
            fopen(nullptr, nullptr);
        }

        void fopen(const char *path, const char *mode) override {
            is_open = true;
            current_pos = 0;
        }

        int fclose(void) override {
            is_open = true;
            return 0;
        }

        /// not implemented
        int fgetc(void) override {
            return -1;
        }

        int fputc(int c) override {
            return -1;
        }

        // not needed, so not implemented
        int fseek(long offset, int origin) override{
            return -1;
        }

        long int ftell(void) override {
            return current_pos;
        }

        size_t fread(void *ptr, size_t size, size_t count)override {
            int actual_read = p_stream->readBytes((uint8_t*)ptr, size*count);
            current_pos += actual_read;
            return actual_read;
        }

        size_t fwrite(const void *ptr, size_t size, size_t count) override {
            size_t result = 0;
            // write to stream
            size_t actual_written = p_stream->write((const uint8_t*)ptr, size*count);
            current_pos += actual_written;
            return actual_written;
        }

        /// A non-zero value is returned in the case that the error indicator associated with the stream is set
        int ferror(void) override {
            return has_error;
        }

        void clearerr(void) override {
            has_error = false;
        }

        /// Not implemented
        int ungetc(int c) override {
            LOG("ungetc not implemented\n");
            return -1;
        };

        // This function returns a non-zero value when End-of-File 
        int feof(void) override {
            bool hasData = p_stream->available()>0;
            return !hasData;
        }

        void rewind(void) override {
            LOG("rewind not implemented\n");
        }

        int fflush(void) {
            p_stream->flush();
            return 0;
        }

        size_t size() override {
            if (!is_open) return 0;
            // a stream is theoretically open ended - so we just return a big number
            return 100000000;
        }

        bool isOpen() override {
            return is_open;
        }

    protected:
        Stream *p_stream = nullptr;
        bool is_open=false;
        bool has_error = false;
        int current_pos = 0;

};

/**
 * Driver for FileAccessStream
 */
class FileDriverStream {
public:
  FileAccessStream *fopen(Stream &stream) {
    return new FileAccessStream(stream);
  };

};



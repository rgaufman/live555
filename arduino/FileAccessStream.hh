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
#if defined(ARDUINO) && defined(USE_STREAM)
#define DEACTIVATE_STDIO_ABSTRACTION
#include <SPI.h>
#include "SdFat.h"
#include "sdios.h"
#include "ArduinoStdio.hh"

/**
 * @brief stdio Access using an single Arduino Stream 
 */

class FileAccessStream : public AbstractFile {
    public:
        FileAccessStream(Stream &stream){
        }

        virtual void* fopen(const char *path, const char *mode) override {
            is_open = true;
            current_pos = 0;
            return this;
        }

        virtual int fclose(void *fp) override {
            is_open = true;
            return (checkFile(fp)) ? 0 : EOF;
        }

        /// not implemented
        virtual int fgetc(void *fp) override {
            LOG("fgetc not implemented\n");
            return EOF;
        }

        virtual int fputc(int c, void *fp) override {
            LOG("fputc not implemented\n");
            return EOF;
        }

        // not needed, so not implemented
        virtual int fseek(void *fp, long offset, int origin) override{
            LOG("fseek not implemented\n");
            return EOF;
        }

        virtual long int ftell(void *fp) override {
            if (!checkFile(fp)) return EOF;
            return current_pos;
        }

        virtual size_t fread(void *ptr, size_t size, size_t count, void *fp)override {
            if (!checkFile(fp)) return 0;
            int actual_read = p_stream->readBytes(ptr, size*count);
            current_pos += actual_read;
            return actual_read;
        }

        virtual size_t fwrite(const void *ptr, size_t size, size_t count, void *fp) override {
            size_t result = 0;
            // write to stream
            size_t actual_written = p_stream->write(ptr, size*count);
            current_pos += actual_written;
            return actual_written;
        }

        /// A non-zero value is returned in the case that the error indicator associated with the stream is set
        virtual int ferror(void *fp) override {
            return has_error;
        }

        virtual void clearerr(void *fp) {
            if (!checkFile(fp)) return;
            has_error = false;
        }


        /// Not implemented
        virtual int ungetc(int c, void *fp) override {
            LOG("ungetc not implemented\n");
            return EOF;
        };

        // This function returns a non-zero value when End-of-File 
        virtual int feof(void *fp) override {
            if (!checkFile(fp)) return EOF;
            bool hasData = p_stream->available()>0;
            return !hasData;
        }

        virtual void rewind(void *fp) override {
            LOG("rewind not implemented\n");
        }

        virtual int fflush(void *fp) {
            p_stream->flush();
            return 0;
        }

        virtual size_t fileSize(void *ptr) {
            if (!checkFile(fp)) return 0;
            // a stream is theoretically open ended - so we just return a big number
            return 100000000;
        }

    protected:
        Stream *p_stream=nullptr;
        bool is_open=false;
        int current_pos = 0;
        bool has_error = false;


        bool checkFile(void *ptr){
            return ptr==this && is_open();
        }
};
#undef DEACTIVATE_STDIO_ABSTRACTION

#endif

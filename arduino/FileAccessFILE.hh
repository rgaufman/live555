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
#if defined(ARDUINO)
#define DEACTIVATE_STDIO_ABSTRACTION
#include <SPI.h>
#include "SdFat.h"
#include "sdios.h"
#include "ArduinoStdio.hh"

/**
 * @brief stdio Access with the built in stdio.h library of actual microcontroller
 */

class FileAccessFILE : public AbstractFile {
    public:
        FileAccessFILE(){
        }

        virtual void* fopen(const char *path, const char *mode) override {
            struct stat sb;
            if (stat(fileName, &sb) == 0) {
                fileSize = sb.st_size;
            } else {
                fileSize = 0;
            }
            return fopen(path, mode);
        }

        virtual int fclose(void *fp) override {
            FILE *p = (FILE*)fp;
            int rc = p->fclose();
            return rc;
        }

        virtual int fgetc(void *fp) override {
            return static_cast<FILE*>(fp)->fgetc();
        }

        virtual int fputc(int c, void *fp) override {
            return static_cast<FILE*>(fp)->fputc(c);
        }

        virtual int fseek(void *fp, long offset, int origin) override{
            return static_cast<FILE*>(fp)->fseek(offset, origin);
        }

        virtual long int ftell(void *fp) override {
            return static_cast<FILE*>(fp)->ftell();
        }

        virtual size_t fread(void *ptr, size_t size, size_t count, void *fp)override {
            return static_cast<FILE*>(fp)->fread((char*)ptr, size, count);
        }

        virtual size_t fwrite(const void *ptr, size_t size, size_t count, void *fp) override {
            return static_cast<FILE*>(fp)->fwrite((const uint8_t*)ptr, size, count);
        }

        virtual int ferror(void *fp) override {
            return static_cast<FILE*>(fp)->ferror();
        }

        virtual int ungetc(int c, void *fp) override {
            return static_cast<FILE*>(fp)->ungetc(c);
        };

        virtual int feof(void *fp) override {
            return static_cast<FILE*>(fp)->feof();
        }

        virtual void rewind(void *fp) override {
            static_cast<FILE*>(fp)->rewind();
        }

        virtual int fflush(void *fp) {
            return static_cast<FILE*>(fp)->flush();
        }

        virtual size_t fileSize(void *fp) {
            return fileSize;
        }

        virtual void clearerr(void *fp){
            return static_cast<FILE*>(fp)->clearerr(fp);
        }


    protected:
        size_t fileSize=0;


};
#undef DEACTIVATE_STDIO_ABSTRACTION

#endif

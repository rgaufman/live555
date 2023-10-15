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
#include "SD.h"
#include "ArduinoStdio.hh"

/**
 * @brief stdio Access to SD library
 * 
 */

class FileAccessSD : public AbstractFile {
    public:
        FileAccessSD(int sdPin=-1){
            if (sdPin==-1){
                SD.begin();
            } else {
                SD.begin(sdPin);
            }
        }

        virtual void* fopen(const char *path, const char *mode) override {
            SDFile tmp = SD.open(path, mode);
            SDFile fp = new SDFile(tmp);
            if (!addFile(fp)){
                LOG("Could not register file\n")
            }
            return fp;

        }

        virtual int fclose(void *fp) override {
            SDFile *p = (SDFile*)fp;
            p->close();
            deleteFile(fp);
            delete p;
            return 1;
        }

        virtual int fgetc(void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<SDFile*>(fp)->read();
        }

        virtual int fputc(int c, void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<SDFile*>(fp)->write(c);
        }

        virtual int fseek(void *fp, long offset, int origin) override{
            if (!checkFile(fp)) return EOF;
            int pos = 0;
            switch (origin) {
                case SEEK_SET:
                    pos = offset;
                    break;
                case SEEK_CUR:
                    pos = offset + static_cast<SDFile*>(fp)->position();
                    break;
                case SEEK_END:
                    pos = static_cast<SDFile*>(fp)->size() - offset;
                    break;
            } 
            return static_cast<SDFile*>(fp)->seek(pos);
        }

        virtual long int  ftell(void *fp) override {
             if (!checkFile(fp)) return EOF;
           return static_cast<SDFile*>(fp)->position();
        }

        virtual size_t fread(void *ptr, size_t size, size_t nmemb, void *fp)override {
            if (!checkFile(fp)) return 0;
            return static_cast<SDFile*>(fp)->readBytes((char*)ptr, size*nmemb);
        }

        virtual size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *fp) override {
            if (!checkFile(fp)) return 0;
            return static_cast<SDFile*>(fp)->write((const uint8_t*)ptr, size*nmemb);
        }

        virtual int ferror(void *fp) override {
            return checkFile(fp);
        }

        virtual void clearerr(void *fp){
            static_cast<FILE*>(fp)->clearerr(fp);
        }

        virtual int ungetc(int c, void *fp) override {
            // not supported
            return EOF;
        };

        /// This function returns a non-zero value when End-of-File indicator associated with the stream is set,
        virtual int feof(void *fp) override {
            if (!checkFile(fp)) return 0;
            SDFile *p = (SDFile*)fp;
            return p->available()==0;
        }

        virtual void rewind(void *fp) override {
            if (checkFile(fp)){
                fseek(fp,SEEK_SET,0);
            }
        }

        virtual int fflush(void *fp) {
            if (!checkFile(fp)) return EOF;
            static_cast<SDFile*>(fp)->flush();
            return 0;
        }

        virtual size_t fileSize(void *fp) {
            if (!checkFile(fp)) return 0;
            return static_cast<SDFile*>(fp)->size();
        }
};
#undef DEACTIVATE_STDIO_ABSTRACTION

#endif

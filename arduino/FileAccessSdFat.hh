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
#include "Config.hh"
/**
 * @brief stdio Access to SdFat library: https://github.com/greiman/SdFat
 * Furtunatly SdFat provides a StdioStream class
 */

class FileAccessSdFat : public AbstractFile {
    public:
        /// Default constructor
        FileAccessSdFat(int chipSelect = SS, int speedMHz = 4){
            if (!sd.begin(SdSpiConfig(chipSelect, DEDICATED_SPI, SD_SCK_MHZ(speedMHz)))){
                LOG("SD.begin failed\n");
            } else {
                LOG("SD.begin success\n");
            }
        }

        /// Setup by providing a SdSpiConfig object
        FileAccessSdFat(SdSpiConfig &sd_config){
            if (!sd.begin(sd_config)){
                LOG("SD.begin failed\n");
            } else {
                LOG("SD.begin success\n");
            }
        }

        virtual void* fopen(const char *path, const char *mode) override {
            LOG("fopen: %s\n", path);

            StdioStream* result = new StdioStream();
            bool ok = result->fopen(path, mode);
            if (ok){
                if (!addFile(result)){
                    LOG("addFile:failed\n");
                }                
            } else {
                LOG("Could not open file: %s\n", path);
                delete result;
            }
            return ok ? result : nullptr;
        }

        virtual int fclose(void *fp) override {
            StdioStream *p = (StdioStream*)fp;
            int rc = p->fclose();
            deleteFile(fp);
            delete p;
            return rc;
        }

        virtual int fgetc(void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->fgetc();
        }

        virtual int fputc(int c, void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->fputc(c);
        }

        virtual int fseek(void *fp, long offset, int origin) override{
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->fseek(offset, origin);
        }

        virtual long int  ftell(void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->ftell();
        }

        virtual size_t fread(void *ptr, size_t size, size_t count, void *fp)override {
            if (!checkFile(fp)) return 0;
            return static_cast<StdioStream*>(fp)->fread((char*)ptr, size, count);
        }

        virtual size_t fwrite(const void *ptr, size_t size, size_t count, void *fp) override {
            if (!checkFile(fp)) return 0;
            return static_cast<StdioStream*>(fp)->fwrite((const uint8_t*)ptr, size, count);
        }

        virtual int ferror(void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->ferror();
        }

        virtual void clearerr(void *fp) {
            if (!checkFile(fp)) return;
            static_cast<StdioStream*>(fp)->clearerr();
        }

        virtual int ungetc(int c, void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->ungetc(c);
        };

        virtual int feof(void *fp) override {
            if (!checkFile(fp)) return EOF;
            return static_cast<StdioStream*>(fp)->feof();
        }

        virtual void rewind(void *fp) override {
            fseek(fp, 0, SEEK_SET);
        }

        virtual int fflush(void *fp) {
            static_cast<StdioStream*>(fp)->fflush();
            return (checkFile(fp)) ? 0 : EOF;
        }

        virtual size_t fileSize(void *fp) {
            if (!checkFile(fp)) return 0;
            size_t result = static_cast<FatFile*>(fp)->fileSize();
            return  result;
        }


    protected:
        SdFs sd;

};
#undef DEACTIVATE_STDIO_ABSTRACTION

#endif

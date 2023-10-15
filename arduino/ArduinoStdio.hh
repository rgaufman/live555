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

// stdio.h emulation for arduino. Unfortunateley Arduino does not support stdio operations
// but it has different APIs which support SD drives. This is an attempt to make stdio configurable
// so that we can link it to a specific SD implementation

#pragma once
#ifdef ARDUINO
#include "ArduinoHelper.hh"

/**
 * @brief Abstract SD File Layer. Use the setFileDriver() method to define the actually set concrete
 * implementation. We also provide a simple way to register and check file pointers.
 */
class AbstractFile {
    public:
        AbstractFile() = default;

        virtual void* fopen(const char *path, const char *mode) = 0;
        virtual int fclose(void *fp) = 0;
        virtual int fgetc(void *fp) = 0;
        virtual int fputc(int c, void *fp) = 0;
        virtual int fseek(void *fp, long offset, int whence) = 0;
        virtual long int  ftell(void *fp) = 0;
        virtual size_t fread(void *ptr, size_t size, size_t nmemb, void *fp) = 0;
        virtual size_t fwrite(const void *ptr, size_t size, size_t nmemb, void *fp) = 0;
        virtual int ferror(void *fp) = 0;
        virtual void clearerr(void *fp) = 0;
        virtual int ungetc(int c, void *fp) = 0;
        virtual int feof(void *fp) = 0;
        virtual void rewind(void *fp) = 0;
        virtual int fflush(void *fp) = 0;
        virtual size_t fileSize(void *ptr) = 0;

    protected:
        void* files[LIVE555_MAX_FILES] =  {}; // intialize with 0

        /// registered the valid file pointer in an array
        bool addFile(void *fp){
            for (int j=0;j<LIVE555_MAX_FILES;j++){
                if (files[j]==0){
                    files[j]=fp;
                    return true;
                }
            }
            return false;
        }

        /// removes the file pointer from the array of valid files
        bool deleteFile(void *fp){
            for (int j=0;j<LIVE555_MAX_FILES;j++){
                if (files[j]==fp){
                    files[j]=nullptr;
                    return true;
                }
            }
            return false;
        }

        /// checks if the file pointer is valid
        bool checkFile(void *fp){
            for (int j=0;j<LIVE555_MAX_FILES;j++){
                if (files[j]==fp){
                    return true;
                }
            }
            Serial.println("checkFile(): false");
            return false;
        }

};

extern AbstractFile *IO555;

// Defines the actual (SD) file driver implementation
void setFileDriver(AbstractFile *driver);

#endif
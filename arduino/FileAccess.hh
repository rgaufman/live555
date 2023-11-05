#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include "ArduinoHelper.hh"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include "SD_MMC.h"

/**
 * @brief Abstract SD File Layer using the ESP32 vfs (virtual file system). 
 * Use the set555FileDriver() method to define the actually set concrete implementation. 
 * We can specify a path prefix because the SD drives do not have an actual 
 * working directory
 **/

class FileDriver {
public:
  FileDriver(const char *path) { this->path = path; };

  virtual FILE *fopen(const char *path, const char *mode) = 0;

  size_t fsize(const char *path){      struct stat sb;
      size_t file_size = 0; 
      if (stat(resolvePath(path).c_str(), &sb) == 0) {
        file_size = sb.st_size;
      } else {
        file_size = 0;
      }
      return file_size;
  }

protected:
  const char *path;

  std::string resolvePath(const char *file) {
    std::string result(path);
    if (!endsWith(result, "/") && !startsWith(file,"/")){
      result.append("/");
    }
    result.append(file);
#ifdef DEBUG
    LOG("==> file name: %s", result.c_str());
#endif
    return result;
  }

  bool endsWith(const std::string& str,const std::string& subStr) {
      size_t strLen = str.length();
      size_t subStrLen = subStr.length();
      // Use the compare() member function to
      // check if the string ends with the substring
      return  strLen >= subStrLen && str.compare(strLen - subStrLen, subStrLen, subStr) == 0;
  }

  bool startsWith(const std::string& str,const std::string& subStr) {
      return str.find(subStr) == 0;
  }
};

/**
* @brief Provide fopen with predefined prefix   
*/

class FileDriverFILE : public FileDriver {
public:
  FileDriverFILE() : FileDriver("") {}
  FileDriverFILE(const char *path) : FileDriver(path) {}

  FILE *fopen(const char *file, const char *mode = "r") {
      return ::fopen(resolvePath(file).c_str(), mode);
  };
};

/**
* @brief setup vfs using SD  
*/
class FileDriverSD : public FileDriver {
public:
  FileDriverSD(int sdPin = -1, const char *path = "/sd") : FileDriver(path) {
    sd_pin = sdPin;
  }

  FILE *fopen(const char *file, const char *mode = "r") {
    if (!is_setup)
      setup();
    return ::fopen(resolvePath(file).c_str(), mode);
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

/**
* @brief setup vfs using SDMMC  
*/
class FileDriverSDMMC : public FileDriver {
public:
  FileDriverSDMMC(const char *path = "/sdmmc") : FileDriver(path) {}

  FILE *fopen(const char *file, const char *mode = "r") {
    if (!is_setup)
      setup();
    return ::fopen(resolvePath(file).c_str(), mode);
  };

protected:
  bool is_setup = false;

  void setup() {
    // start sdmmc on mount point
    if (SD_MMC.begin("/sdmmc")) {
      is_setup = true;
    } else {
      LOG("SDMMC.begin failed");
    }
  }
};


/// Defines the SD/FILE abstraction that is used by Arduino
extern void set555FileDriver(FileDriver &driver);

/// Determines the driver
extern FileDriver* get555FileDriver();

/// Opens a file
extern FILE *open555File(const char *path, const char *mode);

/// Determines the size of a file
extern size_t get555FileSize(const char *path);



#pragma once
#include "stdint.h"
#include <string>
#include "ArduinoHelper.hh"

/**
 * @brief Abstract SD File Layer. Use the set555FileDriver() method to define the
 *actually set concrete implementation. We also provide a simple way to register
 *and check file pointers.
 **/

class File555 {
public:
  File555() = default;
  File555(const char *path, const char *mode) { fopen(path, mode); };

  virtual void fopen(const char *path, const char *mode) {};
  virtual int fclose() = 0;
  virtual int fgetc(void) = 0;
  virtual int fputc(int c) = 0;
  virtual int fseek(long offset, int whence) = 0;
  virtual long int ftell(void) = 0;
  virtual size_t fread(void *ptr, size_t size, size_t nmemb) = 0;
  virtual size_t fwrite(const void *ptr, size_t size, size_t nmemb) = 0;
  virtual int ferror(void) = 0;
  virtual void clearerr(void) = 0;
  virtual int ungetc(int c) = 0;
  virtual int feof(void) = 0;
  virtual void rewind(void) = 0;
  virtual int fflush(void) = 0;
  virtual size_t size(void) = 0;
  virtual bool isOpen() = 0;
};

class FileDriver {
public:
  FileDriver(const char *path) { this->path = path; };

  virtual File555 *fopen(const char *path, const char *mode) = 0;

  int fclose(File555 *fp) {
    int rc = fp->fclose();
    delete (fp);
    return rc;
  }

protected:
  const char *path;

  std::string resolvePath(const char *file) {
    std::string result(path);
    if (!endsWith(result, "/") && !startsWith(file,"/")){
      result.append("/");
    }
    result.append(file);
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

static FileDriver *IO555 = nullptr;

/// Defines the SD/FILE abstraction that is used by Arduino
inline void set555FileDriver(FileDriver *driver) { IO555 = driver; }

inline File555 *fileOpen(const char *path, const char *mode) {
  return IO555->fopen(path, mode);
}

inline int fileClose(File555 *fp) { return IO555->fclose(fp); }

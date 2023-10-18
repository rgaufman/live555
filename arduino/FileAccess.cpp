#include "FileAccess.hh"

// FileDriver
FileDriver *IO555 = nullptr;

/// Defines the FileDriver (SD/FILE abstraction) that is used by Arduino
void set555FileDriver(FileDriver &driver) { IO555 = &driver; }
FileDriver* get555FileDriver() { return IO555; }

/// Opens a file
FILE *open555File(const char *path, const char *mode) {
  if (get555FileDriver()==nullptr) {
    LOG("Call set555FileDriver!");
    return nullptr;
  }
  return get555FileDriver()->fopen(path, mode);
}

/// Determines the size of a file
size_t get555FileSize(const char *path) {
  if (get555FileDriver()==nullptr) {
    LOG("Call set555FileDriver!");
    return 0;
  }
  return get555FileDriver()->fsize(path);
}

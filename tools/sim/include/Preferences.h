/*
  The simulator's version of the chip's flash store. With the environment variable
  SIM_FLASH_FILE set, values are kept in that file, so they survive from one run of the
  simulator to the next, the way flash survives power-off.
*/
#pragma once

#include <map>
#include <string>
#include "Arduino.h"

class Preferences {
 public:
  void begin(const char *, bool) { load(); }
  uint32_t getUInt(const char *key, uint32_t fallback) { return _values.count(key) ? _values[key] : fallback; }
  void putUInt(const char *key, uint32_t value) {
    _values[key] = value;
    save();
  }

 private:
  std::map<std::string, uint32_t> _values;
  void load() {
    const char *path = getenv("SIM_FLASH_FILE");
    FILE *file = path ? fopen(path, "r") : nullptr;
    if (!file) return;
    char key[64];
    unsigned long value;
    while (fscanf(file, "%63s %lu", key, &value) == 2) _values[key] = (uint32_t)value;
    fclose(file);
  }
  void save() {
    const char *path = getenv("SIM_FLASH_FILE");
    FILE *file = path ? fopen(path, "w") : nullptr;
    if (!file) return;
    for (auto &entry : _values) fprintf(file, "%s %lu\n", entry.first.c_str(), (unsigned long)entry.second);
    fclose(file);
  }
};

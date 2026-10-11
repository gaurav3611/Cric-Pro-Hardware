/*
  The Bluetooth radio of the simulator. It has the handful of classes and functions the
  sketches use. Nothing is sent anywhere: every notified value is written to the packet
  log, and the scenario decides when a pretend dashboard connects and leaves.
*/
#pragma once

#include <string>
#include <vector>
#include "Arduino.h"

class BLEServer;
namespace sim {
void bleNotify(const std::string &uuid, const uint8_t *data, size_t size);
void bleRegisterServer(BLEServer *server);
void bleAdvertising(const char *name);
extern bool bleIsAdvertising;
}  // namespace sim

class BLEUUID {
 public:
  BLEUUID(uint16_t shortId) {
    char text[8];
    snprintf(text, sizeof(text), "%04x", shortId);
    value = text;
  }
  BLEUUID(const char *text) : value(text) {}
  std::string value;
};

class BLEDescriptor {};

class BLECharacteristic {
 public:
  static const uint32_t PROPERTY_READ = 1, PROPERTY_NOTIFY = 16;
  explicit BLECharacteristic(const std::string &uuid) : _uuid(uuid) {}
  void addDescriptor(BLEDescriptor *) {}
  void setValue(const uint8_t *data, size_t size) { _value.assign(data, data + size); }
  void notify() { sim::bleNotify(_uuid, _value.data(), _value.size()); }

 private:
  std::string _uuid;
  std::vector<uint8_t> _value;
};

class BLEService {
 public:
  BLECharacteristic *createCharacteristic(const char *uuid, uint32_t) { return new BLECharacteristic(uuid); }
  BLECharacteristic *createCharacteristic(BLEUUID uuid, uint32_t) { return new BLECharacteristic(uuid.value); }
  void start() {}
};

class BLEServerCallbacks {
 public:
  virtual ~BLEServerCallbacks() {}
  virtual void onConnect(BLEServer *) {}
  virtual void onDisconnect(BLEServer *) {}
};

class BLEServer {
 public:
  void setCallbacks(BLEServerCallbacks *callbacks) { _callbacks = callbacks; }
  BLEService *createService(const char *) { return new BLEService(); }
  BLEService *createService(BLEUUID) { return new BLEService(); }
  // Called by the scenario.
  void simConnect() { sim::bleIsAdvertising = false; if (_callbacks) _callbacks->onConnect(this); }
  void simDisconnect() { if (_callbacks) _callbacks->onDisconnect(this); }

 private:
  BLEServerCallbacks *_callbacks = nullptr;
};

class BLEAdvertising {
 public:
  void addServiceUUID(const char *) {}
  void setScanResponse(bool) {}
  void setMinPreferred(uint16_t) {}
  void setMaxPreferred(uint16_t) {}
};

class BLEDevice {
 public:
  static void init(const char *name) { _name = name; }
  static BLEServer *createServer() {
    BLEServer *server = new BLEServer();
    sim::bleRegisterServer(server);
    return server;
  }
  static BLEAdvertising *getAdvertising() {
    static BLEAdvertising advertising;
    return &advertising;
  }
  static void startAdvertising() { sim::bleAdvertising(_name.c_str()); }

 private:
  static inline std::string _name;
};

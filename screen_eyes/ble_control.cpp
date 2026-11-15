#include "ble_control.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {

constexpr int kQueueLength = 16;

struct CommandMsg {
  char text[BLE_MAX_COMMAND];
};

QueueHandle_t gQueue = nullptr;
NimBLECharacteristic* gStatus = nullptr;
volatile bool gConnected = false;

void enqueue(const char* text, size_t len) {
  // Trim spaces around the command.
  while (len && (*text == ' ' || *text == '\t')) { text++; len--; }
  while (len && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r')) len--;
  if (!len) return;

  CommandMsg msg;
  if (len >= sizeof(msg.text)) len = sizeof(msg.text) - 1;
  memcpy(msg.text, text, len);
  msg.text[len] = '\0';
  if (xQueueSend(gQueue, &msg, 0) != pdTRUE) {
    Serial.println("BLE: command queue full, dropped");
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    gConnected = true;
    Serial.printf("BLE: connected %s\n", info.getAddress().toString().c_str());
    enqueue("blink", 5);  // visual feedback
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    gConnected = server->getConnectedCount() > 0;
    Serial.printf("BLE: disconnected (reason %d), advertising again\n", reason);
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    Serial.printf("BLE: MTU %u\n", mtu);
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& info) override {
    // Split by '\n' or ';' so several commands can arrive in one write.
    std::string value = chr->getValue();
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); i++) {
      if (i == value.size() || value[i] == '\n' || value[i] == ';') {
        enqueue(value.data() + start, i - start);
        start = i + 1;
      }
    }
  }
};

ServerCallbacks gServerCallbacks;
CommandCallbacks gCommandCallbacks;

}  // namespace

void bleBegin(const char* deviceName) {
  gQueue = xQueueCreate(kQueueLength, sizeof(CommandMsg));

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(247);  // the phone may negotiate a larger MTU for long replies

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&gServerCallbacks);
  server->advertiseOnDisconnect(true);

  NimBLEService* service = server->createService(BLE_SERVICE_UUID);

  NimBLECharacteristic* command = service->createCharacteristic(
      BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  command->setCallbacks(&gCommandCallbacks);

  gStatus = service->createCharacteristic(
      BLE_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  gStatus->setValue("OK ready");

  server->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // The 128-bit service UUID fills most of the 31-byte advertising packet,
  // so the name goes into the scan response (must be enabled before setName).
  adv->enableScanResponse(true);
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setName(deviceName);
  adv->start();

  Serial.printf("BLE: advertising as \"%s\"\n", deviceName);
}

bool blePopCommand(char* buf, size_t len) {
  if (!gQueue || len == 0) return false;
  CommandMsg msg;
  if (xQueueReceive(gQueue, &msg, 0) != pdTRUE) return false;
  strncpy(buf, msg.text, len - 1);
  buf[len - 1] = '\0';
  return true;
}

void bleSendReply(const char* text) {
  if (!gStatus) return;
  gStatus->setValue(text);
  if (gConnected) gStatus->notify();
}

bool bleConnected() { return gConnected; }

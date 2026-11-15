#pragma once

#include <cstddef>

// Bluetooth Low Energy control channel (NimBLE).
//
// GATT service with two characteristics:
//   command (write / write without response): text commands, same as Serial;
//            several commands may be sent at once, separated by '\n' or ';'.
//   status  (read / notify): reply to the last command ("OK ..." / "ERR ...").
//
// BLE callbacks run in the NimBLE task, so received commands are queued and
// executed from loop() via blePopCommand(). See docs/protocol.md.

#define BLE_SERVICE_UUID "8f1d0001-6a4e-4c39-b8a2-5e3c7d9f1a20"
#define BLE_COMMAND_UUID "8f1d0002-6a4e-4c39-b8a2-5e3c7d9f1a20"
#define BLE_STATUS_UUID  "8f1d0003-6a4e-4c39-b8a2-5e3c7d9f1a20"

constexpr size_t BLE_MAX_COMMAND = 96;

void bleBegin(const char* deviceName);

// Returns true and copies the next queued command into buf, if any.
bool blePopCommand(char* buf, size_t len);

// Stores the reply in the status characteristic and notifies subscribers.
void bleSendReply(const char* text);

bool bleConnected();

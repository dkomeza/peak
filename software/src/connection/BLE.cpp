#include "BLE.h"

#include <NimBLEDevice.h>

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // Notify from ESP32 (TX)
#define CHARACTERISTIC_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Write to ESP32 (RX)
#define BLE_TX_MAX_LEN 20                                             // BLE max notify payload, split longer strings if needed

NimBLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

static NimBLEServer *pServer;

class ServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
  {
    Serial.printf("Client address: %s\n", connInfo.getAddress().toString().c_str());
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    deviceConnected = true;
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
  {
    Serial.printf("Client disconnected - start advertising\n");
    NimBLEDevice::startAdvertising();
    deviceConnected = false;
    pTxCharacteristic->setValue(""); // Clear TX characteristic value on disconnect
  }
} serverCallbacks;

class RxCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
  {
    auto value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      Serial.printf("Received data from client: %s\n", value.c_str());
      // Echo the received data back to the client
      pTxCharacteristic->setValue(value);
      pTxCharacteristic->notify();
    }
    else
    {
      Serial.println("Received empty data");
    }
  }
} rxCallbacks;

// Helper to send data over BLE TX characteristic in chunks
void bleSend(const char *data, size_t len)
{
  Serial.println("Sending data over BLE...");
  if (!deviceConnected || !pTxCharacteristic)
    return;

  size_t offset = 0;
  while (offset < len)
  {
    size_t chunkSize = (len - offset) > BLE_TX_MAX_LEN ? BLE_TX_MAX_LEN : (len - offset);
    pTxCharacteristic->setValue((uint8_t *)(data + offset), chunkSize);
    pTxCharacteristic->notify();
    offset += chunkSize;
    delay(10); // small delay to allow BLE stack to process notifications
  }
  Serial.printf("Sent %zu bytes over BLE\n", len);
}

void BLE::setup()
{
  NimBLEDevice::init("PEAK");

  NimBLEServer *pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  NimBLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_TX_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(&rxCallbacks);
  pService->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setName("PEAK"); // Set the device name
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();

  Serial.println("BLE setup complete. Waiting for client connection...");
}

void BLE::printf(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  char buffer[256]; // Adjust size as needed
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  bleSend(buffer, strlen(buffer));
}
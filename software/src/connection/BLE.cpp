#include "BLE.h"

#include <NimBLEDevice.h>

#include "datatypes.h"
#include "CAN.h"
#include "utils/crc.h"

int MTU_SIZE = 128;
int PACKET_SIZE = MTU_SIZE - 3;

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // Notify from ESP32 (TX)
#define CHARACTERISTIC_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // Write to ESP32 (RX)

NimBLECharacteristic* pTxCharacteristic;
bool deviceConnected = false;

class ServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override
  {
    NimBLEDevice::startAdvertising();
    deviceConnected = true;
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override
  {
    Serial.printf("Client disconnected - start advertising\n");
    NimBLEDevice::startAdvertising();
    deviceConnected = false;
    pTxCharacteristic->setValue(""); // Clear TX characteristic value on disconnect
  }

  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override
  {
    MTU_SIZE = MTU;
    PACKET_SIZE = MTU_SIZE - 3;
  }
} serverCallbacks;

uint32_t packetSize = 0;

class RxCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override
  {
    auto value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      if (strcmp(value.c_str(), "ping") == 0)
      {
        Serial.println("Received ping, sending pong");
        pTxCharacteristic->setValue("pong");
        pTxCharacteristic->notify();
      }
      else if (strcmp(value.c_str(), "flash") == 0)
      {
        xTaskCreatePinnedToCore([](void* arg)
          {
            vTaskDelete(NULL); }, "VESC Flash task", 4096, NULL, 1, NULL, IO_CORE);
      }
      else if (strcmp(value.c_str(), "reset") == 0)
      {
        xTaskCreatePinnedToCore([](void* arg)
          {
            // VESC::reset();
            vTaskDelete(NULL); }, "VESC Reset task", 4096, NULL, 1, NULL, IO_CORE);
      }
    }
  }
} rxCallbacks;

void bleSend(const char* data, size_t len)
{
  if (!deviceConnected || !pTxCharacteristic)
    return;

  size_t offset = 0;
  while (offset < len)
  {
    size_t chunkSize = (len - offset) > PACKET_SIZE ? PACKET_SIZE : (len - offset);
    pTxCharacteristic->setValue((uint8_t*)(data + offset), chunkSize);
    pTxCharacteristic->notify();
    offset += chunkSize;
    delay(5); // small delay to allow BLE stack to process notifications
  }

  delay(5); // Allow some time for the BLE stack to process
}

void BLE::setup()
{
  NimBLEDevice::init("PEAK");

  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  NimBLEService* pService = pServer->createService(SERVICE_UUID);
  NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    NIMBLE_PROPERTY::READ |
    NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxCharacteristic->setCallbacks(&rxCallbacks);
  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setName("PEAK"); // Set the device name
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();
}

void BLE::printf(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  char buffer[256]; // Adjust size as needed
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  bleSend(buffer, strlen(buffer));
}

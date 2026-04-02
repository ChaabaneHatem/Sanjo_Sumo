#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

int scanTime = 5; // Scan duration in seconds
BLEScan* pBLEScan;

// HID Service UUID for gamepads/controllers
BLEUUID hidServiceUUID((uint16_t)0x1812);

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Check if the device advertises the HID service (common for gamepads like Xbox controllers)
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(hidServiceUUID)) {
      Serial.printf("Possible Gamepad/Xbox Controller: %s \n", advertisedDevice.toString().c_str());
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE scan for gamepads...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // Active scan for faster results
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  Serial.print("Scan complete! Devices scanned: ");
  Serial.println(foundDevices.getCount());
  pBLEScan->clearResults(); // Free memory
  delay(2000); // Wait before next scan
}
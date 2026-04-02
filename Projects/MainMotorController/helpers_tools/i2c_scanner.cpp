#include <Wire.h>
#include <Arduino.h>

#define SDA_PIN 43
#define SCL_PIN 44

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN, 100000); // join i2c bus with SDA and SCL pins

  Serial.println("Testing I2C bus...");
 
  Serial.printf("SDA state: %d\n", digitalRead(SDA_PIN));
  Serial.printf("SCL state: %d\n", digitalRead(SCL_PIN));
}

void loop() {
  byte error, address;
  int nDevices = 0;
  Serial.printf("SDA state: %d\n", digitalRead(SDA_PIN));
  Serial.printf("SCL state: %d\n", digitalRead(SCL_PIN));
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  } else {
    Serial.println("Done scanning.\n");
  }

  delay(2000); // wait before next scan
}

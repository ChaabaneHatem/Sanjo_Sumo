# MainMotorController

## Overview

**MainMotorController** is a PlatformIO project for the LilyGO T-Display-S3 board, designed for educational robotics labs (GEI-1049, GEI-1093, GEI-1089). It acts as a motor controller, interfacing with an Xbox Series X controller (via BLE) and an STM32 microcontroller (via I2C), and provides real-time feedback on the built-in TFT display.

## Features

- **Xbox Controller BLE Support:** Connects to an Xbox Series X controller for wireless robot control.
- **I2C Motor Control:** Sends motor speed and direction commands to an STM32 microcontroller.
- **TFT Display:** Visualizes connection status, battery, and motor states.
- **FreeRTOS Tasks:** Uses concurrent tasks for I2C, display, and controller management.

## Hardware Requirements

- LilyGO T-Display-S3 (ESP32-S3)
- STM32 microcontroller (for motor driving, connected via I2C)
- Xbox Series X controller (Bluetooth)
- USB-C cable

## How It Works

1. **Xbox BLE Connection:**
   - The ESP32-S3 connects to the Xbox controller via BLE.
   - Joystick and trigger inputs are read and processed.
   - Button A/B set motor directions; triggers set motor speeds.

2. **Motor Command Transmission:**
   - Motor speed and direction variables are updated based on controller input.
   - These values are sent to the STM32 over I2C at 25Hz.
   - STM32 can send feedback (not yet fully implemented).

3. **Display Updates:**
   - The TFT shows connection status, battery level, and live motor data.
   - Visual bar graphs represent motor speeds.

4. **FreeRTOS Task Structure:**
   - `I2CTask`: Handles communication with STM32.
   - `DisplayTask`: Updates the TFT display.
   - `XboxProcessTask` (in `xbox_controller_ble.cpp`): Handles BLE events and input parsing.

## File Structure

```
MainMotorController/
├── main.cpp                # Main application logic
├── main.h                  # Shared declarations
├── pins_config.h           # Pin definitions
├── xbox_controller_ble.cpp # Xbox BLE logic
├── xbox_controller_ble.h   # Xbox BLE declarations
├── NotoSansMonoSCB20.h     # Font for display
├── helpers_tools/          # Utility scripts/tools
└── STM32_Code/             # STM32 firmware (for reference)
```

## Getting Started

1. Connect the T-Display-S3 to your PC via USB-C.
2. Open the project in VS Code with PlatformIO.
3. Select the `MainMotorController` environment.
4. Build and upload the firmware.
5. Power on the Xbox controller and pair it (see serial output for status).
6. The display will show connection status and motor data.

## Customization

- Edit `pins_config.h` to match your hardware wiring.
- Update I2C address or logic as needed for your STM32 firmware.
- Extend the display logic for more feedback or UI features.
## Notes
- **AMOLED Variant Support:**  
    The project now supports the T-Display-S3 AMOLED variant.  
    To enable it:
    1. Define `AMOLED_VERSION` in `main.h`.
    2. Update the board type in `platformio.ini` to match your AMOLED hardware.

- Add support for T-Display-S3 AMOLED variant.

## Author

Ahmed KHELIFI | Ingénieur

---
For educational use only.

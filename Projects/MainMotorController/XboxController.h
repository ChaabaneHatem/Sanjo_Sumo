#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// NOTE: XboxSeriesXControllerESP32_asukiaaa.hpp is intentionally NOT included here.
// Including it from multiple .cpp files creates duplicate 'static' namespace variables
// (e.g. advDevice) that cause the scan callback and onLoop() to reference different
// copies → controller never connects.  The include lives only in XboxController.cpp.

/** Snapshot of Xbox Series X controller input. Thread-safe to copy. */
struct XboxState {
    // Analog sticks
    int32_t joyLHori = 0, joyLVert = 0;
    int32_t joyRHori = 0, joyRVert = 0;

    // Triggers (0–1023)
    uint32_t trigLT = 0, trigRT = 0;

    // Face buttons
    bool btnA = false, btnB = false, btnX = false, btnY = false;

    // Shoulder buttons
    bool btnLB = false, btnRB = false;

    // Menu buttons
    bool btnSelect = false, btnStart = false, btnShare = false;

    // Stick clicks
    bool btnLS = false, btnRS = false;

    // Status
    uint8_t  battery    = 0;
    uint32_t receivedAt = 0;
    bool     connected  = false;
};

/**
 * Manages Xbox Series X controller connection over BLE.
 *
 * Spawns two internal FreeRTOS tasks:
 *   - XboxConnect (core 0): manages BLE pairing and reconnection
 *   - XboxProcess (core 1): reads HID notifications and calls the callback
 *
 * Usage:
 *   XboxController xbox;
 *   xbox.begin([](const XboxState& s) { ... });
 */
class XboxController {
public:
    /** Called from XboxProcess task on every new HID notification. */
    using InputCallback = void (*)(const XboxState&);

    /**
     * Optionally pass the controller's BLE MAC address to bypass
     * the manufacturer-data filter and connect directly by address.
     * Leave empty ("") to use automatic discovery.
     * Format: "xx:xx:xx:xx:xx:xx"  (lowercase, from serial debug output)
     */
    explicit XboxController(const String& targetAddr = "");
    ~XboxController();

    /** Start BLE scanning and spawn internal tasks.
     *  @param cb  Optional callback invoked on every new input frame. */
    void begin(InputCallback cb = nullptr);

    /** Thread-safe snapshot of the latest controller state. */
    void getState(XboxState& out) const;

    bool isConnected();

private:
    // Singleton pointer for static task trampolines
    static XboxController* _instance;

    // PIMPL: Core lives only in XboxController.cpp to avoid the static-in-header
    // multiple-TU problem.  Forward-declared here so no library header needed.
    struct CoreWrapper;
    CoreWrapper*              _pCore    = nullptr;
    String                    _targetAddr;

    mutable SemaphoreHandle_t _mutex    = nullptr;
    XboxState                 _state;
    InputCallback             _callback = nullptr;

    static void _connectTaskEntry(void* pv);
    static void _processTaskEntry(void* pv);

    void _connectLoop();
    void _processLoop();

    // Sets _state.connected under mutex — shared by connect/disconnect transitions.
    void _setConnectedState(bool connected);
};

#include "XboxController.h"
#include "main.h"  // XBOX_TASK_FREQ, XBOX_CONNECT_TASK_FREQ

// Only this TU includes the library header — this ensures all 'static' namespace
// variables (advDevice, pConnectedClient, …) exist exactly once.
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>

// ─── PIMPL ───────────────────────────────────────────────────────────────────

struct XboxController::CoreWrapper {
    XboxSeriesXControllerESP32_asukiaaa::Core core;
    explicit CoreWrapper(const String& addr) : core(addr) {}
};

// ─── Statics ─────────────────────────────────────────────────────────────────

XboxController* XboxController::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

XboxController::XboxController(const String& targetAddr)
    : _targetAddr(targetAddr) {}

XboxController::~XboxController() {
    delete _pCore;
}

void XboxController::begin(InputCallback cb) {
    _instance = this;
    _callback = cb;
    _mutex    = xSemaphoreCreateMutex();
    _pCore    = new CoreWrapper(_targetAddr);
    _pCore->core.begin();

    xTaskCreatePinnedToCore(_connectTaskEntry, "XboxConnect", 4096, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(_processTaskEntry, "XboxProcess", 4096, nullptr, 1, nullptr, 1);
}

void XboxController::getState(XboxState& out) const {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        out = _state;
        xSemaphoreGive(_mutex);
    }
}

bool XboxController::isConnected() {
    if (!_pCore) return false;
    return _pCore->core.isConnected();
}

// ─── Static trampolines ───────────────────────────────────────────────────────

void XboxController::_connectTaskEntry(void*) { _instance->_connectLoop(); }
void XboxController::_processTaskEntry(void*) { _instance->_processLoop(); }

void XboxController::_setConnectedState(bool connected) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _state.connected = connected;
        xSemaphoreGive(_mutex);
    }
}

// ─── Task implementations ─────────────────────────────────────────────────────

void XboxController::_connectLoop() {
    Serial.println("[Xbox] ConnectTask started – scanning for controller…");
    bool wasConnected = false;

    for (;;) {
        _pCore->core.onLoop();  // drives BLE scanning and connection attempt

        const bool connected = _pCore->core.isConnected();

        if (connected && !wasConnected) {
            wasConnected = true;
            Serial.printf("[Xbox] Connected | Addr: %s | Battery: %u%%\n",
                          _pCore->core.buildDeviceAddressStr().c_str(),
                          _pCore->core.battery);
            _setConnectedState(true);

        } else if (!connected && wasConnected) {
            wasConnected = false;
            Serial.println("[Xbox] Disconnected");
            _setConnectedState(false);

        } else if (!connected) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / XBOX_CONNECT_TASK_FREQ));
    }
}

void XboxController::_processLoop() {
    Serial.println("[Xbox] ProcessTask started");

    for (;;) {
        _pCore->core.onLoop();

        if (_pCore->core.isConnected() && !_pCore->core.isWaitingForFirstNotification()) {
            const auto& n = _pCore->core.xboxNotif;

            XboxState local;
            local.joyLHori  = n.joyLHori;
            local.joyLVert  = n.joyLVert;
            local.joyRHori  = n.joyRHori;
            local.joyRVert  = n.joyRVert;
            local.trigLT    = n.trigLT;
            local.trigRT    = n.trigRT;
            local.btnA      = n.btnA;
            local.btnB      = n.btnB;
            local.btnX      = n.btnX;
            local.btnY      = n.btnY;
            local.btnLB     = n.btnLB;
            local.btnRB     = n.btnRB;
            local.btnSelect = n.btnSelect;
            local.btnStart  = n.btnStart;
            local.btnShare  = n.btnShare;
            local.btnLS     = n.btnLS;
            local.btnRS     = n.btnRS;
            local.battery    = _pCore->core.battery;
            local.receivedAt = _pCore->core.getReceiveNotificationAt();
            local.connected  = true;

            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                _state = local;
                xSemaphoreGive(_mutex);
            }

            if (_callback) {
                _callback(local);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / XBOX_TASK_FREQ));
    }
}

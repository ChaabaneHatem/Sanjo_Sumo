#include "Encoder.h"

// Shared ISR state – one entry per encoder instance
static volatile int32_t gEncCounts[2] = {0, 0};
static uint8_t          gEncPinA[2]   = {0, 0};
static uint8_t          gEncPinB[2]   = {0, 0};

// Inline direction logic: read both pins when A changes.
// A != B → forward (+1),  A == B → reverse (-1)
static void IRAM_ATTR isrTick(uint8_t idx) {
    const bool a = digitalRead(gEncPinA[idx]);
    const bool b = digitalRead(gEncPinB[idx]);
    gEncCounts[idx] += (a != b) ? 1 : -1;
}

void IRAM_ATTR Encoder::_isr0() { isrTick(0); }
void IRAM_ATTR Encoder::_isr1() { isrTick(1); }

// ─────────────────────────────────────────────────────────────────────────────

Encoder::Encoder(uint8_t pinA, uint8_t pinB, uint8_t idx)
    : _pinA(pinA), _pinB(pinB), _idx(idx)
{}

void Encoder::begin() {
    gEncPinA[_idx] = _pinA;
    gEncPinB[_idx] = _pinB;
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pinA),
                    _idx == 0 ? _isr0 : _isr1,
                    CHANGE);
}

void Encoder::update() {
    const int32_t snap = gEncCounts[_idx];
    _delta        = (int16_t)(snap - _lastSnapshot);
    _lastSnapshot = snap;
    _count        = snap;
}

void Encoder::resetCount() {
    gEncCounts[_idx] = 0;
    _lastSnapshot    = 0;
    _count           = 0;
    _delta           = 0;
}

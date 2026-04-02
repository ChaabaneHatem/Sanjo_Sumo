#pragma once
#include <Arduino.h>

/**
 * Quadrature encoder via GPIO interrupts (CHANGE on channel A).
 *
 * Supports up to 2 encoder instances (idx 0 and 1).
 * Direction is determined by reading channel B when A changes:
 *   A != B → forward (+1),  A == B → reverse (-1)
 *
 * Usage:
 *   Encoder enc(PIN_A, PIN_B, 0);
 *   enc.begin();
 *   enc.update();        // call at fixed interval to snapshot delta
 *   enc.getDelta();      // counts since last update()
 *   enc.getCount();      // total accumulated counts
 */
class Encoder {
public:
    // idx must be 0 (left motor) or 1 (right motor)
    Encoder(uint8_t pinA, uint8_t pinB, uint8_t idx);

    void    begin();
    void    update();
    int32_t getCount() const { return _count; }
    int16_t getDelta() const { return _delta;  }
    void    resetCount();

private:
    uint8_t _pinA, _pinB, _idx;
    int32_t _count        = 0;
    int16_t _delta        = 0;
    int32_t _lastSnapshot = 0;

    static void IRAM_ATTR _isr0();
    static void IRAM_ATTR _isr1();
};

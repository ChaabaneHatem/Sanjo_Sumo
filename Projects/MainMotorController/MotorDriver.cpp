#include "MotorDriver.h"

MotorDriver::MotorDriver(uint8_t pwmPin, uint8_t in1Pin, uint8_t in2Pin,
                         uint8_t pwmChannel, uint32_t pwmFreqHz,
                         uint8_t pwmResBits, uint8_t pwmMax)
    : _pwmPin(pwmPin)
    , _in1Pin(in1Pin)
    , _in2Pin(in2Pin)
    , _pwmChannel(pwmChannel)
    , _pwmFreqHz(pwmFreqHz)
    , _pwmResBits(pwmResBits)
    , _pwmMax(pwmMax)
{}

void MotorDriver::begin() {
    pinMode(_in1Pin, OUTPUT);
    pinMode(_in2Pin, OUTPUT);
    digitalWrite(_in1Pin, LOW);
    digitalWrite(_in2Pin, LOW);

    // Arduino ESP32 v2.x LEDC API
    ledcSetup(_pwmChannel, _pwmFreqHz, _pwmResBits);
    ledcAttachPin(_pwmPin, _pwmChannel);
    ledcWrite(_pwmChannel, 0);
}

void MotorDriver::setSpeed(int16_t speed) {
    const int16_t clamped = constrain(speed, -(int16_t)_pwmMax, (int16_t)_pwmMax);
    if (clamped == _speed) return;   // no change – skip GPIO and PWM register writes
    _speed = clamped;
    uint8_t duty = (uint8_t)abs(_speed);

    if (_speed > 0) {
        // Forward
        digitalWrite(_in1Pin, HIGH);
        digitalWrite(_in2Pin, LOW);
    } else if (_speed < 0) {
        // Reverse
        digitalWrite(_in1Pin, LOW);
        digitalWrite(_in2Pin, HIGH);
    } else {
        // Active brake
        digitalWrite(_in1Pin, LOW);
        digitalWrite(_in2Pin, LOW);
        duty = 0;
    }

    ledcWrite(_pwmChannel, duty);
}

void MotorDriver::stop() {
    setSpeed(0);
}

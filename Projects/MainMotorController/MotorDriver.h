#pragma once
#include <Arduino.h>

/**
 * Controls a single DC motor via an H-bridge driver (TB6612FNG).
 * Uses the ESP32 LEDC peripheral for PWM generation.
 *
 * Wiring (TB6612FNG):
 *   pwmPin  → PWMA / PWMB  (speed)
 *   in1Pin  → AIN1 / BIN1  (direction bit 1)
 *   in2Pin  → AIN2 / BIN2  (direction bit 2)
 *
 * Note: The STBY pin must be set HIGH externally (in setup) to enable the driver.
 */
class MotorDriver {
public:
    /**
     * @param pwmPin      GPIO connected to PWM input of the driver
     * @param in1Pin      GPIO connected to IN1 of the driver
     * @param in2Pin      GPIO connected to IN2 of the driver
     * @param pwmChannel  LEDC channel (0–7, must be unique per motor instance)
     * @param pwmFreqHz   PWM frequency (20 kHz = above audible range)
     * @param pwmResBits  PWM resolution in bits (8 = 0..255 duty range)
     * @param pwmMax      Max duty cycle — voltage clamping for 6V motors on 7.4V LiPo
     *                    Formula: pwmMax = (6.0 / 8.4) * 255 ≈ 182, use 200 as soft limit
     */
    MotorDriver(uint8_t pwmPin, uint8_t in1Pin, uint8_t in2Pin,
                uint8_t  pwmChannel = 0,
                uint32_t pwmFreqHz  = 20000,
                uint8_t  pwmResBits = 8,
                uint8_t  pwmMax     = 200);

    /** Initialize GPIO pins and attach LEDC PWM channel. Call once in setup(). */
    void begin();

    /**
     * Set motor speed and direction.
     * @param speed  Positive = forward, negative = reverse, 0 = brake.
     *               Magnitude is clamped to [0, pwmMax].
     */
    void setSpeed(int16_t speed);

    /** Apply active brake (IN1=LOW, IN2=LOW, PWM=0). */
    void stop();

    /** Returns the last commanded speed value. */
    int16_t getSpeed() const { return _speed; }

private:
    const uint8_t  _pwmPin;
    const uint8_t  _in1Pin;
    const uint8_t  _in2Pin;
    const uint8_t  _pwmChannel;
    const uint32_t _pwmFreqHz;
    const uint8_t  _pwmResBits;
    const uint8_t  _pwmMax;
    int16_t        _speed = 0;
};

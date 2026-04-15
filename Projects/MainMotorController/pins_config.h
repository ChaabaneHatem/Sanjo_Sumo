#pragma once

// ─── Button ──────────────────────────────────────────────────────────────────
#define PIN_BUTTON_1  0    // BOOT button (bottom right)

// ─── LED RGB (common cathode) ─────────────────────────────────────────────────
// LEDC channels 2/3/4  (0 and 1 reserved for motors)
#define PIN_LED_R   4    // Red   – ADC2_0 / Touch0
#define PIN_LED_G  15    // Green – ADC2_3 / Touch3 / HSPI_CS0
#define PIN_LED_B  23    // Blue  – MOSI  / V_SPI_D
#define LED_PWM_CHANNEL_R  2
#define LED_PWM_CHANNEL_G  3
#define LED_PWM_CHANNEL_B  4
#define LED_PWM_FREQ_HZ    5000
#define LED_PWM_RES_BITS   8

// ─── I2C ─────────────────────────────────────────────────────────────────────
#define PIN_IIC_SCL      22   // SCL
#define PIN_IIC_SDA      21   // SDA
#define TCA9548A_ADDR  0x70   // A2=GND, A1=GND, A0=GND (adresse de base)
#define OLED_I2C_ADDR   0x3C   // SSD1306 128×64 principal  (SA0=GND → 0x3C)
#define OLED2_I2C_ADDR  0x3D   // SSD1315 128×64 secondaire (SA0=VCC → 0x3D)
#define INA219_I2C_ADDR 0x45  // DFRobot SEN0291 – A1=VCC, A0=VCC → 0x45

// ─── Capteurs ligne analogiques (ADC1 uniquement – WiFi safe) ────────────────
#define PIN_LINE_MID    34   // ADC1_CH6 – centre   – input only
#define PIN_LINE_RIGHT  35   // ADC1_CH7 – droite   – input only
#define PIN_LINE_LEFT   36   // ADC1_CH0 – gauche   – input only

// ─── Encoders – N20 quadrature ───────────────────────────────────────────────
// All four support INPUT_PULLUP (not input-only pins)
#define ENC_A_C1  16   // Motor A left  – channel A  (RXD2)
#define ENC_A_C2  17   // Motor A left  – channel B  (TXD2)
#define ENC_B_C1  18   // Motor B right – channel A  (SCK / V_SPI_CLK)
#define ENC_B_C2  19   // Motor B right – channel B  (MISO / V_SPI_Q)

// ─── Motor Driver – TB6612FNG ─────────────────────────────────────────────────
// Motor A (Left)
#define MOTOR_A_PWM  25   // PWMA – DAC1 / ADC2_8
#define MOTOR_A_IN1  26   // AIN1 – DAC2 / ADC2_9
#define MOTOR_A_IN2  27   // AIN2 – ADC2_7

// Motor B (Right)
#define MOTOR_B_PWM  32   // PWMB – ADC1_4
#define MOTOR_B_IN1  33   // BIN1 – ADC1_5
#define MOTOR_B_IN2  13   // BIN2 – ADC2_4

// Driver enable
#define MOTOR_STBY   14   // STBY – ADC2_6

// ─── Buzzer passif ────────────────────────────────────────────────────────────
#define PIN_BUZZER   2    // GPIO2 – fil + → GPIO2, fil – → GND (résistance 100Ω conseillée)

// ─── Motor PWM configuration ──────────────────────────────────────────────────
#define MOTOR_PWM_FREQ_HZ   20000
#define MOTOR_PWM_RES_BITS  8
#define MOTOR_PWM_MAX       255

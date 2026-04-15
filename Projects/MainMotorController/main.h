#pragma once

// ─── Task frequencies (Hz) ────────────────────────────────────────────────────
#define XBOX_TASK_FREQ          50   // Xbox HID processing
#define XBOX_CONNECT_TASK_FREQ   2   // BLE connection management
#define ENCODER_TASK_FREQ        1   // Encoder RPM read + log  (1 Hz)
#define SENSOR_TASK_FREQ       100   // ToF + line sensor read (100Hz = 10ms)
#define STATUS_TASK_FREQ         1   // Xbox connectivity status log
#define OLED_TASK_FREQ           5   // Rafraîchissement écran OLED (Hz)
#define WATT_TASK_FREQ           2   // Lecture INA219 wattmètre (Hz)

// ─── Buzzer ───────────────────────────────────────────────────────────────────
// Canaux LEDC 0-4 utilisés (moteurs + LEDs) → canal 5 libre pour buzzer
#define BUZZER_LEDC_CHANNEL  5

// ─── Encoder – N20 CGM 12-N20VA-08200E ───────────────────────────────────────
// ISR sur CHANGE du canal A uniquement → 7 PPR × 2 fronts = 14 counts/tour MOTEUR
// Réducteur 30:1 déduit du no-load 530 RPM à 6V × (8.4V/6V) ≈ 750 RPM avec 2S
// Si ton moteur a un ratio différent, ajuste ENCODER_GEAR_RATIO uniquement.
#define ENCODER_PPR            14    // counts/tour arbre MOTEUR (CHANGE sur canal A)
#define ENCODER_GEAR_RATIO     30    // ratio réducteur (30:1)
#define ENCODER_WHEEL_DIAM_MM  26.0f // diamètre roue en mm

// ─── Mode robot ──────────────────────────────────────────────────────────────
#define ROBOT_MODE_MANUAL  0   // contrôle Xbox
#define ROBOT_MODE_AUTO    1   // évitement bordure automatique

// ─── Capteurs ligne – seuil détection bordure blanche ────────────────────────
// ADC 12-bit : arena noire ≈ >2500  |  bordure blanche ≈ <300
// Ligne blanche détectée si valeur SOUS le seuil
#define LINE_THRESHOLD     800    // valeur en-dessous = bordure blanche détectée
#define AUTO_TASK_FREQ     20     // Hz – boucle auto (50ms/cycle)
#define AUTO_SPEED_PCT     50     // % vitesse avance en auto

// ─── WiFi / TCP (Lab 1) ───────────────────────────────────────────────────────
#define WIFI_SSID  "sanjo"
#define WIFI_PASS  "sanjo123"
#define TCP_PORT   8080

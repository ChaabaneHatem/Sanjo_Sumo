/*
 * Sanjo Sumo – Phase 1
 * ──────────────────────────────────────────────────────────────────────────────
 * Hardware : ESP32 CH340C (38-pin DevKit) + TB6612FNG motor driver + N20 encoders
 * Features :
 *   - Xbox Series X controller via BLE → tank drive (LT/RT)
 *   - WiFi TCP server (port 8080) → commandes HMI JSON
 *   - Quadrature encoder speed reading
 *   - Motor self-test via BOOT button
 *
 * Contrôle Xbox :
 *   LT (trigger) → roue gauche avant   (proportionnel)
 *   RT (trigger) → roue droite avant   (proportionnel)
 *   LB (bumper)  → roue gauche arrière (pleine vitesse)
 *   RB (bumper)  → roue droite arrière (pleine vitesse)
 *   A            → cycle 6 vitesses : 17/33/50/67/83/100%
 *   BOOT button  → test moteur séquentiel (+25 PWM chaque appui)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_INA219.h>

#include "main.h"
#include "pins_config.h"
#include "MotorDriver.h"
#include "XboxController.h"
#include "Encoder.h"

// ─── Hardware instances ───────────────────────────────────────────────────────

MotorDriver motorLeft (MOTOR_A_PWM, MOTOR_A_IN1, MOTOR_A_IN2,
                       /*channel=*/0, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RES_BITS, MOTOR_PWM_MAX);
MotorDriver motorRight(MOTOR_B_PWM, MOTOR_B_IN1, MOTOR_B_IN2,
                       /*channel=*/1, MOTOR_PWM_FREQ_HZ, MOTOR_PWM_RES_BITS, MOTOR_PWM_MAX);

XboxController xbox("74:c4:12:b1:c0:3a");  // Xbox Series X – adresse BLE fixe

Encoder encLeft (ENC_A_C1, ENC_A_C2, 0);
Encoder encRight(ENC_B_C1, ENC_B_C2, 1);

VL53L0X tof[2];   // [0] = canal TCA ch0, [1] = canal TCA ch1

Adafruit_SSD1306 oled (128, 64, &Wire, -1);
Adafruit_SSD1306 oled2(128, 64, &Wire, -1);
Adafruit_INA219  ina219(INA219_I2C_ADDR);

// ─── Globals + thread-safe TSLOG ─────────────────────────────────────────────

static volatile bool     gMotorTestActive  = false;

// ─── Test distance ─────────────────────────────────────────────────────────
static volatile bool  gDistTestActive  = false;  // moteurs en course
static volatile bool  gDistTestDone    = false;  // résultats prêts à afficher
static volatile float gDistTestL_cm   = 0.0f;   // distance roue gauche (cm)
static volatile float gDistTestR_cm   = 0.0f;   // distance roue droite (cm)
static volatile float gDistTestVmax   = 0.0f;   // vitesse max mesurée (cm/s)
static SemaphoreHandle_t gSerialMutex     = nullptr;

static volatile uint16_t gTofDistMm[2]   = {0, 0};
static volatile bool     gTofTimeout[2]  = {true, true};
static volatile uint16_t gLineMid   = 0;   // capteur ligne centre  (GPIO34)
static volatile uint16_t gLineRight = 0;   // capteur ligne droite  (GPIO35)
static volatile uint16_t gLineLeft  = 0;   // capteur ligne gauche  (GPIO36)
static volatile float    gVelLeftCms     = 0.0f;  // vitesse roue gauche (cm/s, signée, repère robot)
static volatile float    gVelRightCms    = 0.0f;  // vitesse roue droite (cm/s, signée)
static volatile uint8_t  gDisplayMode    = 0;     // 0=données, 1=visage, 2=wattmètre

// ─── INA219 globals ───────────────────────────────────────────────────────────
static volatile bool  gInaOk        = false;
static volatile float gInaShuntMv   = 0.0f;   // tension shunt (mV)
static volatile float gInaBusV      = 0.0f;   // tension bus (V) ≈ Vbat
static volatile float gInaCurrentMa = 0.0f;   // courant (mA)
static volatile float gInaPowerMw   = 0.0f;   // puissance (mW)
static volatile float gAccEnergyWh  = 0.0f;   // énergie accumulée (Wh)
static volatile float gAccChargeMah = 0.0f;   // charge accumulée (mAh)

// Sélecteur de vitesse : 6 paliers (index 0..5)
static const uint8_t kGears[6] = { 17, 33, 50, 67, 83, 100 };
static volatile uint8_t  gGear          = 2;   // par défaut : 50%
static volatile uint8_t  gSpeedPct      = 50;

static volatile bool    gXboxConnected = false;   // suivi connexion Xbox (pour telemetrie TCP)
static volatile uint8_t gRobotMode    = ROBOT_MODE_MANUAL;  // MANUAL ou AUTO

// Convertit le pourcentage en valeur PWM [0, MOTOR_PWM_MAX]
static inline int16_t hmiSpeed() {
    return (int16_t)((MOTOR_PWM_MAX * gSpeedPct) / 100);
}

#define TSLOG(fmt, ...) \
    do { \
        if (gSerialMutex && xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(50)) == pdTRUE) { \
            Serial.printf(fmt "\n", ##__VA_ARGS__); \
            xSemaphoreGive(gSerialMutex); \
        } \
    } while(0)

// ─── Buzzer – queue-based async ──────────────────────────────────────────────

struct BuzzerNote { uint16_t freq; uint16_t ms; };

#define BUZZER_QUEUE_LEN 24
static QueueHandle_t gBuzzerQueue = nullptr;

// Envoie une note dans la queue (non-bloquant)
static inline void buzzerBeep(uint16_t freq, uint16_t ms) {
    if (!gBuzzerQueue) return;
    const BuzzerNote n = {freq, ms};
    xQueueSend(gBuzzerQueue, &n, 0);
}

// Envoie une mélodie (tableau de notes) dans la queue
static inline void buzzerMelody(const BuzzerNote* m, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) buzzerBeep(m[i].freq, m[i].ms);
}

// ── Mélodies prédéfinies ──────────────────────────────────────────────────────
// Note: freq=0 = silence

static const BuzzerNote kMelodyBoot[]       = {{262,80},{0,20},{330,80},{0,20},{392,80},{0,20},{523,200}};
static const BuzzerNote kMelodyConnect[]    = {{440,70},{0,20},{660,140}};
static const BuzzerNote kMelodyDisconnect[] = {{523,70},{0,15},{392,70},{0,15},{262,150}};
static const BuzzerNote kSoundAttack[]      = {{880,35},{0,10},{1047,70}};
static const BuzzerNote kSoundRetreat[]     = {{523,55},{0,10},{392,55},{0,10},{262,100}};
static const BuzzerNote kSoundSpin[]        = {{700,40},{900,40},{700,40},{900,40},{1100,60}};
static const BuzzerNote kSoundGear[]        = {{500,40}};  // pitch ajusté par gear
static const BuzzerNote kSoundAutoOn[]      = {{600,60},{0,20},{800,60},{0,20},{1000,120}};
static const BuzzerNote kSoundAutoOff[]     = {{1000,60},{0,20},{800,60},{0,20},{600,120}};

// ─── TCA9548A mux – sélection de canal I2C ───────────────────────────────────

static void tcaSelect(uint8_t channel) {
    Wire.beginTransmission(TCA9548A_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

// ─── ControleurLedRgb ────────────────────────────────────────────────────────

class ControleurLedRgb {
    uint8_t _pinR, _pinG, _pinB;
    uint8_t _r = 0, _g = 0, _b = 0;
public:
    ControleurLedRgb(uint8_t pinR, uint8_t pinG, uint8_t pinB)
        : _pinR(pinR), _pinG(pinG), _pinB(pinB) {}
    void begin() {
        ledcSetup(LED_PWM_CHANNEL_R, LED_PWM_FREQ_HZ, LED_PWM_RES_BITS);
        ledcSetup(LED_PWM_CHANNEL_G, LED_PWM_FREQ_HZ, LED_PWM_RES_BITS);
        ledcSetup(LED_PWM_CHANNEL_B, LED_PWM_FREQ_HZ, LED_PWM_RES_BITS);
        ledcAttachPin(_pinR, LED_PWM_CHANNEL_R);
        ledcAttachPin(_pinG, LED_PWM_CHANNEL_G);
        ledcAttachPin(_pinB, LED_PWM_CHANNEL_B);
        setRgb(0, 0, 0);
    }
    void setRgb(uint8_t r, uint8_t g, uint8_t b) {
        _r = r; _g = g; _b = b;
        ledcWrite(LED_PWM_CHANNEL_R, r);
        ledcWrite(LED_PWM_CHANNEL_G, g);
        ledcWrite(LED_PWM_CHANNEL_B, b);
    }
    void allumer()    { setRgb(255, 255, 255); }
    void eteindre()   { setRgb(0, 0, 0);       }
    bool estAllumee() { return _r || _g || _b;  }
};

static ControleurLedRgb led(PIN_LED_R, PIN_LED_G, PIN_LED_B);

// ─── Motor self-test ─────────────────────────────────────────────────────────

static void testMotors(int speed, int durationMs) {
    gMotorTestActive = true;
    TSLOG("[Test] SPD=%-3d  DUR=%dms", speed, durationMs);

    struct Step { MotorDriver* m; int16_t spd; const char* label; };
    const Step steps[] = {
        { &motorLeft,   (int16_t) speed, "L avant"  },
        { &motorLeft,   (int16_t)-speed, "L arriere"},
        { &motorRight,  (int16_t) speed, "R avant"  },
        { &motorRight,  (int16_t)-speed, "R arriere"},
    };
    for (const auto& s : steps) {
        TSLOG("[Test] %-10s spd=%+d", s.label, s.spd);
        s.m->setSpeed(s.spd);
        delay(durationMs);
        s.m->stop();
        delay(200);
    }
    TSLOG("[Test] Done");
    gMotorTestActive = false;
}

// ─── Xbox input callback ──────────────────────────────────────────────────────
// LT  → roue gauche avant  (proportionnel au niveau d'appui)
// RT  → roue droite avant  (proportionnel au niveau d'appui)
// LB  → roue gauche arrière (tenu, vitesse = gSpeedPct)
// RB  → roue droite arrière (tenu, vitesse = gSpeedPct)
// A   → cycle 6 vitesses : 17/33/50/67/83/100%

static void onXboxInput(const XboxState& s) {
    gXboxConnected = s.connected;
    if (gMotorTestActive) return;

    // ── B : toggle MANUAL / AUTO ──────────────────────────────────────────────
    static bool prevB = false;
    if (s.btnB && !prevB) {
        if (gRobotMode == ROBOT_MODE_MANUAL) {
            gRobotMode = ROBOT_MODE_AUTO;
            motorLeft.stop(); motorRight.stop();   // arrêt propre avant auto
            buzzerMelody(kSoundAutoOn,  sizeof(kSoundAutoOn)  / sizeof(BuzzerNote));
            TSLOG("[Mode] AUTO – évitement bordure actif");
        } else {
            gRobotMode = ROBOT_MODE_MANUAL;
            motorLeft.stop(); motorRight.stop();
            buzzerMelody(kSoundAutoOff, sizeof(kSoundAutoOff) / sizeof(BuzzerNote));
            TSLOG("[Mode] MANUEL – contrôle Xbox");
        }
    }
    prevB = s.btnB;

    // ── Y : switch interface OLED ──────────────────────────────────────────────
    static bool prevY = false;
    if (s.btnY && !prevY) {
        gDisplayMode = (gDisplayMode + 1) % 3;
        buzzerBeep(1200, 40);
        const char* modeStr[] = {"DONNEES", "VISAGE", "WATTMETRE"};
        TSLOG("[OLED] Mode: %s", modeStr[gDisplayMode]);
    }
    prevY = s.btnY;

    // ── A : cycle vitesse (actif aussi en AUTO) ────────────────────────────────
    static bool prevA = false;
    if (s.btnA && !prevA) {
        gGear = (gGear + 1) % 6;
        gSpeedPct = kGears[gGear];
        buzzerBeep((uint16_t)(300 + gGear * 80), 55);
        TSLOG("[Xbox] Gear %d/6  →  %d%%", (int)gGear + 1, (int)gSpeedPct);
    }
    prevA = s.btnA;

    // ── En mode AUTO : Xbox ne contrôle pas les moteurs ───────────────────────
    if (gRobotMode == ROBOT_MODE_AUTO) return;

    // ── Calcul vitesse roue gauche / droite ───────────────────────────────────
    // trigLT / trigRT : 10 bits → 0..1023  (maxTrig = 0x3FF)
    // LB / RB prioritaires (arrière) si tenus
    const int32_t TRIG_MAX  = 1023;
    const int32_t DEADZONE  = 20;
    int32_t rawLT = (int32_t)s.trigLT;
    int32_t rawRT = (int32_t)s.trigRT;
    if (rawLT < DEADZONE) rawLT = 0;
    if (rawRT < DEADZONE) rawRT = 0;

    const int16_t maxSpd = (int16_t)(MOTOR_PWM_MAX * gSpeedPct / 100);

    int32_t spdL = map(rawLT, 0, TRIG_MAX, 0, maxSpd);  // avant proportionnel
    int32_t spdR = map(rawRT, 0, TRIG_MAX, 0, maxSpd);  // avant proportionnel

    if (s.btnLB) spdL = -maxSpd;   // arrière gauche (écrase LT si tenu)
    if (s.btnRB) spdR = -maxSpd;   // arrière droit  (écrase RT si tenu)

    // Moteurs montés en miroir : inverser le signe du moteur gauche
    motorLeft.setSpeed((int16_t)(-spdL));
    motorRight.setSpeed((int16_t)spdR);

    // Log uniquement sur changement (évite le spam à 50 Hz)
    static int32_t prevL = 0, prevR = 0;
    if (spdL != prevL || spdR != prevR) {
        TSLOG("[Motor] L:%+4d  R:%+4d  gear=%d(%d%%)  bat:%3u%%",
              spdL, spdR, (int)gGear + 1, (int)gSpeedPct, s.battery);
        prevL = spdL;
        prevR = spdR;
    }
}

// ─── TCP command handler ──────────────────────────────────────────────────────
// {"commande":"avancer",       "duree_ms":1500}
// {"commande":"reculer",       "duree_ms":1500}
// {"commande":"pivoter_gauche","duree_ms":500}
// {"commande":"pivoter_droite","duree_ms":500}
// {"commande":"arreter"}
// {"commande":"set_vitesse",   "valeur":70}

static String handleCommand(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json))
        return "{\"statut\":\"erreur\",\"message\":\"JSON invalide\"}";

    const char* cmd = doc["commande"];
    if (!cmd)
        return "{\"statut\":\"erreur\",\"message\":\"cle commande manquante\"}";

    const uint32_t dureeMs = doc["duree_ms"] | 0;

    // HMI envoie "vitesse" en PWM direct (0-255) ; sinon on utilise gSpeedPct
    const int rawSpd = doc["vitesse"] | -1;
    const int16_t spd = (rawSpd >= 0)
        ? (int16_t)constrain(rawSpd, 0, (int)MOTOR_PWM_MAX)
        : hmiSpeed();

    // Moteur gauche monté en miroir : signe inversé pour même référentiel robot
    // avant gauche = motorLeft négatif, motorRight positif
    if (strcmp(cmd, "avancer") == 0) {
        TSLOG("[Cmd] avancer  %ums  spd=%d", dureeMs, (int)spd);
        motorLeft.setSpeed(-spd);
        motorRight.setSpeed(spd);
        if (dureeMs > 0) { vTaskDelay(pdMS_TO_TICKS(dureeMs)); motorLeft.stop(); motorRight.stop(); }

    } else if (strcmp(cmd, "reculer") == 0) {
        TSLOG("[Cmd] reculer  %ums  spd=%d", dureeMs, (int)spd);
        motorLeft.setSpeed(spd);
        motorRight.setSpeed(-spd);
        if (dureeMs > 0) { vTaskDelay(pdMS_TO_TICKS(dureeMs)); motorLeft.stop(); motorRight.stop(); }

    } else if (strcmp(cmd, "pivoter_gauche") == 0) {
        TSLOG("[Cmd] pivoter_gauche  %ums", dureeMs);
        motorLeft.setSpeed(spd);    // gauche arrière
        motorRight.setSpeed(spd);   // droite avant
        if (dureeMs > 0) { vTaskDelay(pdMS_TO_TICKS(dureeMs)); motorLeft.stop(); motorRight.stop(); }

    } else if (strcmp(cmd, "pivoter_droite") == 0) {
        TSLOG("[Cmd] pivoter_droite  %ums", dureeMs);
        motorLeft.setSpeed(-spd);   // gauche avant
        motorRight.setSpeed(-spd);  // droite arrière
        if (dureeMs > 0) { vTaskDelay(pdMS_TO_TICKS(dureeMs)); motorLeft.stop(); motorRight.stop(); }

    } else if (strcmp(cmd, "arreter") == 0 || strcmp(cmd, "urgence") == 0) {
        TSLOG("[Cmd] %s", cmd);
        motorLeft.stop();
        motorRight.stop();

    } else if (strcmp(cmd, "set_vitesse") == 0) {
        gSpeedPct = (uint8_t)constrain(doc["valeur"].as<int>(), 0, 100);
        TSLOG("[Cmd] set_vitesse  %d%%  (pwm=%d)", (int)gSpeedPct, hmiSpeed());

    } else {
        TSLOG("[TCP] commande inconnue '%s'", cmd);
        return "{\"statut\":\"erreur\",\"message\":\"commande inconnue\"}";
    }

    return "{\"statut\":\"ok\"}";
}

// ─── Télémétrie TCP – push vers le HMI ───────────────────────────────────────
// Format JSON par ligne (\n) attendu par le HMI :
//   {"type":"tof",   "nom":"avant_gauche","brut":342,"traite":"342 mm"}
//   {"type":"tof",   "nom":"avant_droite","brut":500,"traite":"500 mm"}
//   {"type":"ligne", "nom":"avant_gauche","brut":2048,"traite":"2048"}
//   {"type":"status","direction":"AVANT","vitesse_L":45.2,"vitesse_R":46.1,
//                    "xbox":true,"wifi":true,"gear":3,"vitesse_pct":50,
//                    "vbat":8.20,"courant_mA":450,"puissance_W":3.69}

static void pushTelemetry(WiFiClient& client) {
    JsonDocument doc;
    String line;

    // ── ToF [0] → avant_gauche  |  [1] → avant_droite ────────────────────────
    static const char* kTofNames[2] = {"avant_gauche", "avant_droite"};
    for (uint8_t i = 0; i < 2; i++) {
        doc.clear();
        doc["type"] = "tof";
        doc["nom"]  = kTofNames[i];
        if (gTofTimeout[i]) {
            doc["brut"]   = 0;
            doc["traite"] = "---- mm";
        } else {
            char buf[12];
            doc["brut"] = (int)gTofDistMm[i];
            snprintf(buf, sizeof(buf), "%u mm", gTofDistMm[i]);
            doc["traite"] = buf;
        }
        line = "";
        serializeJson(doc, line);
        client.println(line);
    }

    // ── Capteurs ligne (3 messages) ───────────────────────────────────────────
    {
        static const struct { const char* nom; const volatile uint16_t* val; } kLines[3] = {
            {"avant_gauche", &gLineLeft },
            {"avant_centre", &gLineMid  },
            {"avant_droite", &gLineRight},
        };
        char buf[8];
        for (uint8_t i = 0; i < 3; i++) {
            doc.clear();
            doc["type"]   = "ligne";
            doc["nom"]    = kLines[i].nom;
            doc["brut"]   = (int)*kLines[i].val;
            snprintf(buf, sizeof(buf), "%u", *kLines[i].val);
            doc["traite"] = buf;
            line = "";
            serializeJson(doc, line);
            client.println(line);
        }
    }

    // ── Status général ────────────────────────────────────────────────────────
    {
        const float vL = gVelLeftCms;
        const float vR = gVelRightCms;
        const char* dir;
        if      (fabsf(vL) < 2.0f && fabsf(vR) < 2.0f) dir = "ARRET";
        else if (vL >  2.0f && vR >  2.0f)              dir = "AVANT";
        else if (vL < -2.0f && vR < -2.0f)              dir = "ARRIERE";
        else if (vL >  vR + 5.0f)                       dir = "DROITE";
        else if (vR >  vL + 5.0f)                       dir = "GAUCHE";
        else                                             dir = "...";

        doc.clear();
        doc["type"]        = "status";
        doc["direction"]   = dir;
        doc["vitesse_L"]   = (float)((int)(vL * 10)) / 10.0f;
        doc["vitesse_R"]   = (float)((int)(vR * 10)) / 10.0f;
        doc["xbox"]        = gXboxConnected;
        doc["wifi"]        = (WiFi.status() == WL_CONNECTED);
        doc["gear"]        = (int)gGear + 1;
        doc["vitesse_pct"] = (int)gSpeedPct;
        doc["vbat"]        = (float)((int)(gInaBusV * 100)) / 100.0f;
        doc["courant_mA"]  = (int)gInaCurrentMa;
        doc["puissance_W"] = (float)((int)(gInaPowerMw / 10)) / 100.0f;
        line = "";
        serializeJson(doc, line);
        client.println(line);
    }
}

// ─── TCP task ─────────────────────────────────────────────────────────────────

static void TcpTask(void*) {
    WiFiServer server(TCP_PORT);
    server.begin();
    TSLOG("[TCP] Pret  port=%d  IP=%s", TCP_PORT, WiFi.localIP().toString().c_str());

    for (;;) {
        WiFiClient client = server.available();
        if (client) {
            TSLOG("[TCP] Connexion: %s", client.remoteIP().toString().c_str());
            uint32_t lastTelemetryMs = 0;
            while (client.connected()) {
                // Commandes entrantes
                if (client.available()) {
                    String msg = client.readStringUntil('\n');
                    msg.trim();
                    if (msg.length() > 0) {
                        const String resp = handleCommand(msg);
                        client.println(resp);
                    }
                }
                // Télémétrie périodique → 500 ms
                const uint32_t now = millis();
                if (now - lastTelemetryMs >= 500) {
                    pushTelemetry(client);
                    lastTelemetryMs = now;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            client.stop();
            TSLOG("[TCP] Client deconnecte");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ─── Encoder task – 1 Hz ─────────────────────────────────────────────────────
// ISR: CHANGE canal A → 14 counts/tour moteur
// Sortie réducteur: 14 × 30 = 420 counts/tour roue
// Vitesse (cm/s) = delta × π × 26mm / (420 × 10)  (période = 1s à 1 Hz)

static void EncoderTask(void*) {
    // RPM arbre sortie par count/période
    constexpr float kRpmOut = 60.0f * ENCODER_TASK_FREQ
                              / (float)(ENCODER_PPR * ENCODER_GEAR_RATIO);
    // cm/s par count/période  = kRpmOut × π × D(mm) / 600
    constexpr float kCms    = kRpmOut * (float)M_PI * ENCODER_WHEEL_DIAM_MM / 600.0f;

    for (;;) {
        encLeft.update();
        encRight.update();

        const float velL = encLeft.getDelta()  * kCms;
        const float velR = encRight.getDelta() * kCms;
        // Moteur gauche monté en miroir → inverser signe pour repère robot
        gVelLeftCms  = -velL;
        gVelRightCms =  velR;

        if (!gMotorTestActive) {
            TSLOG("[Enc] L:%+6.1fcm/s  R:%+6.1fcm/s  |  L:%+7ld  R:%+7ld cnt",
                  gVelLeftCms, gVelRightCms,
                  encLeft.getCount(), encRight.getCount());
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / ENCODER_TASK_FREQ));
    }
}

// ─── Sensor task – 2 Hz ──────────────────────────────────────────────────────

static void SensorTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(333));
    for (;;) {
        // -- ToF #1 (TCA canal 0) ---------------------------------------------
        tcaSelect(0);
        uint16_t d0 = tof[0].readRangeContinuousMillimeters();
        bool     t0 = tof[0].timeoutOccurred();
        gTofDistMm[0]  = t0 ? 0 : d0;
        gTofTimeout[0] = t0;

        // -- ToF #2 (TCA canal 1) ---------------------------------------------
        tcaSelect(1);
        uint16_t d1 = tof[1].readRangeContinuousMillimeters();
        bool     t1 = tof[1].timeoutOccurred();
        gTofDistMm[1]  = t1 ? 0 : d1;
        gTofTimeout[1] = t1;

        TSLOG("[Sensor] ToF#1=%4umm %s  ToF#2=%4umm %s",
              gTofDistMm[0], t0 ? "TIMEOUT" : "OK     ",
              gTofDistMm[1], t1 ? "TIMEOUT" : "OK     ");

        gLineMid   = (uint16_t)analogRead(PIN_LINE_MID);
        gLineRight = (uint16_t)analogRead(PIN_LINE_RIGHT);
        gLineLeft  = (uint16_t)analogRead(PIN_LINE_LEFT);
        // Log désactivé (100Hz – trop de spam)

        vTaskDelay(pdMS_TO_TICKS(1000 / SENSOR_TASK_FREQ));
    }
}

// ─── OLED – mode 0 : données ─────────────────────────────────────────────────

static void oledDrawData(const XboxState& xs, float vL, float vR) {
    const bool bleOk  = xs.connected;
    const bool wifiOk = (WiFi.status() == WL_CONNECTED);

    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    oled.setCursor(0, 0);
    oled.print(bleOk  ? "BLE:OK " : "BLE:-- ");
    oled.print(wifiOk ? "WiFi:OK" : "WiFi:--");
    oled.print(gRobotMode == ROBOT_MODE_AUTO ? " [AUTO]" : " [MAN] ");

    oled.setCursor(0, 9);
    if (wifiOk) {
        oled.print(WiFi.localIP().toString());
    } else if (bleOk) {
        oled.printf("Bat:%3u%%  G:%d/6(%d%%)", xs.battery, (int)gGear + 1, (int)gSpeedPct);
    } else {
        oled.print("Scan BLE...");
    }

    oled.drawFastHLine(0, 18, 128, SSD1306_WHITE);

    oled.setCursor(0, 21);
    oled.printf("L:%+6.1f R:%+6.1f cm/s", vL, vR);

    const char* dir;
    if      (fabsf(vL) < 2.0f && fabsf(vR) < 2.0f) dir = "ARRET  ";
    else if (vL >  2.0f && vR >  2.0f)              dir = "AVANT  ";
    else if (vL < -2.0f && vR < -2.0f)              dir = "ARRIERE";
    else if (vL >  vR + 5.0f)                       dir = "DROITE ";
    else if (vR >  vL + 5.0f)                       dir = "GAUCHE ";
    else                                             dir = "...    ";

    oled.setCursor(0, 30);
    oled.printf("Dir:%-7s G:%d %d%%", dir, (int)gGear + 1, (int)gSpeedPct);

    oled.drawFastHLine(0, 40, 128, SSD1306_WHITE);

    oled.setCursor(0, 43);
    if (gTofTimeout[0])
        oled.print("T1:----  ");
    else
        oled.printf("T1:%4umm ", gTofDistMm[0]);
    if (gTofTimeout[1])
        oled.print("T2:----");
    else
        oled.printf("T2:%4umm", gTofDistMm[1]);

    oled.setCursor(0, 53);
    oled.printf("G:%4u M:%4u D:%4u", (unsigned)gLineLeft, (unsigned)gLineMid, (unsigned)gLineRight);
}

// ─── OLED – mode 1 : visage ───────────────────────────────────────────────────
// Tête centrée (64,27) r=24. Texte d'état y=55. Y=btn Xbox pour switcher.

enum class RobotFace : uint8_t { SEARCH, IDLE, ATTACK, RETREAT, SPIN };

static void faceHead() {
    oled.drawCircle(64, 27, 24, SSD1306_WHITE);
}

static void drawFaceSearch() {
    faceHead();
    // œil gauche ouvert, œil droit = clin d'œil horizontal (il cherche)
    oled.drawCircle(51, 23, 5, SSD1306_WHITE);
    oled.fillCircle(52, 24, 2, SSD1306_WHITE);
    oled.drawLine(70, 22, 81, 22, SSD1306_WHITE);   // wink
    // sourcils haussés en V inversé
    oled.drawLine(45, 13, 52, 17, SSD1306_WHITE);
    oled.drawLine(52, 17, 59, 15, SSD1306_WHITE);
    oled.drawLine(69, 15, 76, 17, SSD1306_WHITE);
    oled.drawLine(76, 17, 83, 13, SSD1306_WHITE);
    // bouche ondulée (confus)
    oled.drawLine(52, 37, 56, 33, SSD1306_WHITE);
    oled.drawLine(56, 33, 60, 37, SSD1306_WHITE);
    oled.drawLine(60, 37, 64, 33, SSD1306_WHITE);
    oled.drawLine(64, 33, 68, 37, SSD1306_WHITE);
    oled.drawLine(68, 37, 72, 33, SSD1306_WHITE);
    // gros point d'interrogation
    oled.setTextSize(2);
    oled.setCursor(90, 16);
    oled.print("?");
    oled.setTextSize(1);
    oled.setCursor(14, 55);
    oled.print("SCAN MANETTE...");
}

static void drawFaceIdle() {
    faceHead();
    // yeux normaux avec pupilles
    oled.drawCircle(51, 23, 4, SSD1306_WHITE);
    oled.fillCircle(52, 24, 2, SSD1306_WHITE);
    oled.drawCircle(77, 23, 4, SSD1306_WHITE);
    oled.fillCircle(78, 24, 2, SSD1306_WHITE);
    // sourcils légèrement arqués
    oled.drawLine(46, 16, 56, 14, SSD1306_WHITE);
    oled.drawLine(72, 14, 82, 16, SSD1306_WHITE);
    // sourire
    oled.drawLine(53, 36, 57, 39, SSD1306_WHITE);
    oled.drawLine(57, 39, 64, 41, SSD1306_WHITE);
    oled.drawLine(64, 41, 71, 39, SSD1306_WHITE);
    oled.drawLine(71, 39, 75, 36, SSD1306_WHITE);
    oled.setCursor(20, 55);
    oled.print("EN ATTENTE...");
}

static void drawFaceAttack() {
    faceHead();
    // sourcils en V (colère) – épais (deux lignes)
    oled.drawLine(43, 11, 57, 19, SSD1306_WHITE);
    oled.drawLine(44, 12, 58, 20, SSD1306_WHITE);
    oled.drawLine(85, 11, 71, 19, SSD1306_WHITE);
    oled.drawLine(84, 12, 70, 20, SSD1306_WHITE);
    // yeux plissés (rectangles fins)
    oled.fillRect(46, 23, 12, 3, SSD1306_WHITE);
    oled.fillRect(70, 23, 12, 3, SSD1306_WHITE);
    // dents : rectangle blanc + séparateurs noirs
    oled.fillRect(51, 37, 26, 7, SSD1306_WHITE);
    oled.drawLine(57, 37, 57, 44, SSD1306_BLACK);
    oled.drawLine(64, 37, 64, 44, SSD1306_BLACK);
    oled.drawLine(71, 37, 71, 44, SSD1306_BLACK);
    // lignes d'action sur les côtés
    oled.drawLine(29, 20, 38, 23, SSD1306_WHITE);
    oled.drawLine(27, 27, 37, 27, SSD1306_WHITE);
    oled.drawLine(99, 20, 90, 23, SSD1306_WHITE);
    oled.drawLine(101, 27, 91, 27, SSD1306_WHITE);
    oled.setCursor(27, 55);
    oled.print("ATTAQUE !!!");
}

static void drawFaceRetreat() {
    faceHead();
    // sourcils haussés et ondulés (peur)
    oled.drawLine(44, 11, 51, 15, SSD1306_WHITE);
    oled.drawLine(51, 15, 58, 12, SSD1306_WHITE);
    oled.drawLine(70, 12, 77, 15, SSD1306_WHITE);
    oled.drawLine(77, 15, 84, 11, SSD1306_WHITE);
    // grands yeux ronds (terrifiés)
    oled.drawCircle(51, 24, 7, SSD1306_WHITE);
    oled.fillCircle(51, 25, 3, SSD1306_WHITE);
    oled.drawCircle(77, 24, 7, SSD1306_WHITE);
    oled.fillCircle(77, 25, 3, SSD1306_WHITE);
    // bouche ouverte en O
    oled.drawCircle(64, 40, 5, SSD1306_WHITE);
    // gouttes de sueur
    oled.fillCircle(34, 16, 2, SSD1306_WHITE);
    oled.fillTriangle(32, 16, 36, 16, 34, 23, SSD1306_WHITE);
    oled.fillCircle(38, 8, 1, SSD1306_WHITE);
    oled.fillTriangle(37, 8, 39, 8, 38, 13, SSD1306_WHITE);
    oled.setCursor(32, 55);
    oled.print("FUITE !!!");
}

static void drawFaceSpin() {
    faceHead();
    // yeux X (étourdis)
    oled.drawLine(45, 18, 57, 28, SSD1306_WHITE);
    oled.drawLine(57, 18, 45, 28, SSD1306_WHITE);
    oled.drawLine(71, 18, 83, 28, SSD1306_WHITE);
    oled.drawLine(83, 18, 71, 28, SSD1306_WHITE);
    // étoiles autour de la tête
    for (int i = 0; i < 3; i++) {
        const int8_t sx[] = {38, 64, 90};
        const int8_t sy[] = { 3,  1,  3};
        oled.drawLine(sx[i]-2, sy[i],   sx[i]+2, sy[i],   SSD1306_WHITE);
        oled.drawLine(sx[i],   sy[i]-2, sx[i],   sy[i]+2, SSD1306_WHITE);
        oled.drawLine(sx[i]-1, sy[i]-1, sx[i]+1, sy[i]+1, SSD1306_WHITE);
        oled.drawLine(sx[i]+1, sy[i]-1, sx[i]-1, sy[i]+1, SSD1306_WHITE);
    }
    // bouche ondulée
    oled.drawLine(51, 37, 55, 33, SSD1306_WHITE);
    oled.drawLine(55, 33, 59, 37, SSD1306_WHITE);
    oled.drawLine(59, 37, 63, 33, SSD1306_WHITE);
    oled.drawLine(63, 33, 67, 37, SSD1306_WHITE);
    oled.drawLine(67, 37, 71, 33, SSD1306_WHITE);
    oled.drawLine(71, 33, 75, 37, SSD1306_WHITE);
    oled.setCursor(24, 55);
    oled.print("TOURNOIE ~_~");
}

static RobotFace determineFace(bool bleOk, float vL, float vR) {
    if (!bleOk)                               return RobotFace::SEARCH;
    const float T = 5.0f;
    if (vL >  T && vR < -T)                  return RobotFace::SPIN;    // spin droite
    if (vL < -T && vR >  T)                  return RobotFace::SPIN;    // spin gauche
    if (vL >  T && vR >  T)                  return RobotFace::ATTACK;
    if (vL < -T && vR < -T)                  return RobotFace::RETREAT;
    return RobotFace::IDLE;
}

// ─── OLED – mode 2 : wattmètre ───────────────────────────────────────────────
// DFRobot SEN0291 INA219 – shunt 10 mΩ
// I(mA) = Vshunt(mV) × 100    (car R=0.01 Ω → I=V/R)
// P(mW) = Vbus(V)   × I(mA)

static void oledDrawWatt() {
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    if (!gInaOk) {
        oled.setCursor(16, 24);
        oled.print("INA219 non detecte");
        oled.setCursor(20, 36);
        oled.print("Verif cablage I2C");
        return;
    }

    const float busV     = gInaBusV;
    const float shuntMv  = gInaShuntMv;
    const float currentMa = gInaCurrentMa;
    const float powerW   = gInaPowerMw / 1000.0f;
    const float energyWh = gAccEnergyWh;
    const float chargeMah = gAccChargeMah;

    // Ligne 0 – titre
    oled.setCursor(28, 0);
    oled.print("--- WATTMETRE ---");

    // Ligne 1 – tension batterie
    oled.setCursor(0, 9);
    oled.printf("Vbat : %5.2f V", busV);

    // Ligne 2 – courant
    oled.setCursor(0, 18);
    if (fabsf(currentMa) >= 1000.0f)
        oled.printf("I    : %5.2f A", currentMa / 1000.0f);
    else
        oled.printf("I    : %5.1f mA", currentMa);

    // Ligne 3 – puissance
    oled.setCursor(0, 27);
    oled.printf("P    : %5.2f W", powerW);

    oled.drawFastHLine(0, 37, 128, SSD1306_WHITE);

    // Ligne 4 – énergie cumulée
    oled.setCursor(0, 40);
    oled.printf("Eh   : %6.3f Wh", energyWh);

    // Ligne 5 – charge cumulée
    oled.setCursor(0, 49);
    oled.printf("Qh   : %6.1f mAh", chargeMah);

    // Ligne 6 – tension shunt (debug)
    oled.setCursor(0, 58);
    oled.printf("Vsh  : %+6.2f mV", shuntMv);
}

// ─── Auto task – évitement bordure ───────────────────────────────────────────
// Arena 77cm noire, bordure blanche 2.5cm.
// Capteurs : gLineLeft(G36) | gLineMid(G34) | gLineRight(G35)
// Ligne détectée si valeur ADC > LINE_THRESHOLD.
//
// Machine à états :
//   FORWARD  → avance jusqu'à détection
//   REVERSE  → recule 200ms
//   TURN     → tourne en place 300ms (direction selon capteur qui a déclenché)

static void AutoTask(void*) {
    // États :
    //   FORWARD  → avance droit
    //   FOLLOW   → arc doux le long de la bordure (un côté détecté)
    //   REVERSE  → recule si capteur milieu ou les deux côtés touchent
    //   TURN     → tourne sur place après recul
    enum class AutoState : uint8_t { FORWARD, FOLLOW, REVERSE, TURN };

    AutoState state  = AutoState::FORWARD;
    bool      turnCW = true;
    uint32_t  stateMs = 0;

    for (;;) {
        if (gRobotMode != ROBOT_MODE_AUTO) {
            state = AutoState::FORWARD;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Vitesse recalculée à chaque cycle → btn A actif en AUTO
        const int16_t fwdSpd  = (int16_t)(MOTOR_PWM_MAX * gSpeedPct / 100);
        const int16_t turnSpd = (int16_t)(MOTOR_PWM_MAX * gSpeedPct / 100);
        // Arc : roue intérieure ralentie à 30% de la roue extérieure
        const int16_t arcFast = fwdSpd;
        const int16_t arcSlow = (int16_t)(fwdSpd * 30 / 100);

        // Bordure blanche = valeur BASSE
        const bool lineL = gLineLeft  < LINE_THRESHOLD;
        const bool lineM = gLineMid   < LINE_THRESHOLD;
        const bool lineR = gLineRight < LINE_THRESHOLD;
        const uint32_t now = millis();

        switch (state) {
            case AutoState::FORWARD:
                if (lineM || (lineL && lineR)) {
                    // Milieu ou les deux → recul
                    turnCW = !turnCW;
                    motorLeft.stop(); motorRight.stop();
                    state = AutoState::REVERSE; stateMs = now;
                } else if (lineL) {
                    // Gauche touche la bordure → arc vers la droite (suivi CW)
                    turnCW = true;
                    state = AutoState::FOLLOW; stateMs = now;
                } else if (lineR) {
                    // Droite touche la bordure → arc vers la gauche (suivi CCW)
                    turnCW = false;
                    state = AutoState::FOLLOW; stateMs = now;
                } else {
                    // Aucune ligne → avance droit
                    motorLeft.setSpeed(-fwdSpd);
                    motorRight.setSpeed(fwdSpd);
                }
                break;

            case AutoState::FOLLOW:
                // Arc doux : reste tangent à la bordure
                // Si la ligne disparaît ou s'aggrave → retour FORWARD ou REVERSE
                if (lineM || (lineL && lineR)) {
                    motorLeft.stop(); motorRight.stop();
                    state = AutoState::REVERSE; stateMs = now;
                } else if (!lineL && !lineR) {
                    // Plus de bordure → reprend droit
                    state = AutoState::FORWARD;
                } else {
                    if (turnCW) {
                        // Arc droite : roue gauche rapide, droite lente
                        motorLeft.setSpeed(-arcFast);
                        motorRight.setSpeed(arcSlow);
                    } else {
                        // Arc gauche : roue droite rapide, gauche lente
                        motorLeft.setSpeed(-arcSlow);
                        motorRight.setSpeed(arcFast);
                    }
                }
                break;

            case AutoState::REVERSE:
                if (now - stateMs < 250) {
                    motorLeft.setSpeed(fwdSpd);
                    motorRight.setSpeed(-fwdSpd);
                } else {
                    motorLeft.stop(); motorRight.stop();
                    state = AutoState::TURN; stateMs = now;
                }
                break;

            case AutoState::TURN:
                if (now - stateMs < 400) {
                    if (turnCW) {
                        motorLeft.setSpeed(-turnSpd);
                        motorRight.setSpeed(-turnSpd);
                    } else {
                        motorLeft.setSpeed(turnSpd);
                        motorRight.setSpeed(turnSpd);
                    }
                } else {
                    motorLeft.stop(); motorRight.stop();
                    state = AutoState::FORWARD;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / AUTO_TASK_FREQ));
    }
}

// ─── Watt task – 2 Hz ────────────────────────────────────────────────────────

static void WattTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(600));
    uint32_t lastMs = millis();

    for (;;) {
        if (gInaOk) {
            const float shuntMv  = ina219.getShuntVoltage_mV();
            const float busV     = ina219.getBusVoltage_V();
            // Shunt 10 mΩ : I(mA) = Vshunt(mV) / 0.010 Ω / 1000 = Vshunt(mV) × 100
            const float currentMa = shuntMv * 100.0f;
            const float powerMw   = busV * currentMa;

            const uint32_t now = millis();
            const float dtH = (float)(now - lastMs) / 3600000.0f;
            lastMs = now;

            gInaShuntMv   = shuntMv;
            gInaBusV      = busV;
            gInaCurrentMa = currentMa;
            gInaPowerMw   = powerMw;
            if (currentMa > 0.0f) {
                gAccEnergyWh  += (powerMw / 1000.0f) * dtH;
                gAccChargeMah += currentMa * dtH;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / WATT_TASK_FREQ));
    }
}

// ─── OLED task – 5 Hz ────────────────────────────────────────────────────────

static void OledTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));

    RobotFace prevFace     = RobotFace::IDLE;
    RobotFace candidateFace = RobotFace::IDLE;
    uint8_t   stableFrames  = 0;
    uint32_t  lastSoundMs   = 0;

    uint32_t distDoneMs = 0;   // timestamp fin du test (pour affichage 10s)

    for (;;) {
        oled.clearDisplay();

        // ── Test distance en cours ────────────────────────────────────────
        if (gDistTestActive) {
            oled.setTextSize(1);
            oled.setTextColor(SSD1306_WHITE);
            oled.setCursor(16, 10);  oled.print("TEST DISTANCE");
            oled.setCursor(28, 24);  oled.print("EN COURS...");
            oled.setCursor(20, 38);  oled.print("5 secondes @ 100%");
            oled.display();
            memcpy(oled2.getBuffer(), oled.getBuffer(), 128 * 64 / 8);
            oled2.display();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── Résultat test distance (affiché 10s) ─────────────────────────
        if (gDistTestDone) {
            if (distDoneMs == 0) distDoneMs = millis();
            if (millis() - distDoneMs < 10000) {
                oled.setTextSize(1);
                oled.setTextColor(SSD1306_WHITE);
                oled.setCursor(22, 0);   oled.print("RESULTAT TEST 5s");
                oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
                oled.setCursor(0, 14);   oled.printf("Dist L : %6.1f cm", gDistTestL_cm);
                oled.setCursor(0, 24);   oled.printf("Dist R : %6.1f cm", gDistTestR_cm);
                oled.setCursor(0, 34);   oled.printf("Moy    : %6.1f cm", (gDistTestL_cm + gDistTestR_cm) * 0.5f);
                oled.drawFastHLine(0, 45, 128, SSD1306_WHITE);
                oled.setCursor(0, 48);   oled.printf("Vmax   : %6.1f cm/s", gDistTestVmax);
                oled.setCursor(0, 57);   oled.printf("Vtheo  :  ~101.0 cm/s");
                oled.display();
                memcpy(oled2.getBuffer(), oled.getBuffer(), 128 * 64 / 8);
                oled2.display();
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            gDistTestDone = false;
            distDoneMs    = 0;
        }

        XboxState xs;
        xbox.getState(xs);
        const float vL = gVelLeftCms;
        const float vR = gVelRightCms;

        if (gDisplayMode == 0) {
            // ── Mode données ──────────────────────────────────────────────
            oledDrawData(xs, vL, vR);

        } else if (gDisplayMode == 1) {
            // ── Mode visage ───────────────────────────────────────────────
            const RobotFace face = determineFace(xs.connected, vL, vR);

            // Debounce : 2 frames consécutives identiques avant changement d'état
            if (face == candidateFace) {
                if (stableFrames < 2) stableFrames++;
            } else {
                candidateFace = face;
                stableFrames  = 1;
            }
            const RobotFace stableFace = (stableFrames >= 2) ? candidateFace : prevFace;

            // Son au changement d'état stable + cooldown 1.5s (anti-bombe nucléaire)
            if (stableFace != prevFace && (millis() - lastSoundMs) >= 1500) {
                switch (stableFace) {
                    case RobotFace::ATTACK:
                        buzzerMelody(kSoundAttack, sizeof(kSoundAttack)/sizeof(kSoundAttack[0]));
                        break;
                    case RobotFace::RETREAT:
                        buzzerMelody(kSoundRetreat, sizeof(kSoundRetreat)/sizeof(kSoundRetreat[0]));
                        break;
                    case RobotFace::SPIN:
                        buzzerMelody(kSoundSpin, sizeof(kSoundSpin)/sizeof(kSoundSpin[0]));
                        break;
                    default: break;
                }
                lastSoundMs = millis();
                prevFace = stableFace;
            }

            switch (stableFace) {
                case RobotFace::SEARCH:  drawFaceSearch();  break;
                case RobotFace::IDLE:    drawFaceIdle();    break;
                case RobotFace::ATTACK:  drawFaceAttack();  break;
                case RobotFace::RETREAT: drawFaceRetreat(); break;
                case RobotFace::SPIN:    drawFaceSpin();    break;
            }

            // Indicateur de mode + gear en haut à droite (petit)
            oled.setCursor(96, 0);
            oled.printf("G%d %d%%", (int)gGear + 1, (int)gSpeedPct);

        } else {
            // ── Mode wattmètre (mode 2) ───────────────────────────────────
            oledDrawWatt();
        }

        oled.display();
        // Miroir sur le second écran : copie le buffer pixel-par-pixel
        memcpy(oled2.getBuffer(), oled.getBuffer(), 128 * 64 / 8);
        oled2.display();
        vTaskDelay(pdMS_TO_TICKS(1000 / OLED_TASK_FREQ));
    }
}

// ─── Buzzer task ─────────────────────────────────────────────────────────────

static void BuzzerTask(void*) {
    BuzzerNote n;
    for (;;) {
        if (xQueueReceive(gBuzzerQueue, &n, portMAX_DELAY) == pdTRUE) {
            if (n.freq > 0) ledcWriteTone(BUZZER_LEDC_CHANNEL, n.freq);
            else            ledcWrite(BUZZER_LEDC_CHANNEL, 0);
            vTaskDelay(pdMS_TO_TICKS(n.ms));
            ledcWrite(BUZZER_LEDC_CHANNEL, 0);
        }
    }
}

// ─── Status task – 1 Hz ──────────────────────────────────────────────────────
// Log Xbox une seule fois au changement d'état (connexion / déconnexion)

static void StatusTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(666));
    bool prevConnected = false;
    for (;;) {
        XboxState s;
        xbox.getState(s);
        if (s.connected && !prevConnected) {
            TSLOG("[Xbox] Connecte  bat=%3u%%  gear=%d/6(%d%%)", s.battery, (int)gGear + 1, (int)gSpeedPct);
            buzzerMelody(kMelodyConnect, sizeof(kMelodyConnect)/sizeof(kMelodyConnect[0]));
        } else if (!s.connected && prevConnected) {
            TSLOG("[Xbox] Deconnecte");
            buzzerMelody(kMelodyDisconnect, sizeof(kMelodyDisconnect)/sizeof(kMelodyDisconnect[0]));
        }
        prevConnected = s.connected;
        vTaskDelay(pdMS_TO_TICKS(1000 / STATUS_TASK_FREQ));
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(300);
    gSerialMutex = xSemaphoreCreateMutex();

    Serial.println("\n[Setup] Sanjo Sumo – Phase 1");

    // ── Motor driver ──────────────────────────────────────────────────────────
    pinMode(MOTOR_STBY, OUTPUT);
    digitalWrite(MOTOR_STBY, HIGH);
    motorLeft.begin();
    motorRight.begin();
    Serial.printf("[Motor] STBY=GPIO%d  A:PWM%d/IN%d,%d  B:PWM%d/IN%d,%d\n",
                  MOTOR_STBY,
                  MOTOR_A_PWM, MOTOR_A_IN1, MOTOR_A_IN2,
                  MOTOR_B_PWM, MOTOR_B_IN1, MOTOR_B_IN2);

    // ── Encoders ──────────────────────────────────────────────────────────────
    encLeft.begin();
    encRight.begin();
    Serial.printf("[Enc]   A:C1=%d,C2=%d  B:C1=%d,C2=%d  PPR=%d\n",
                  ENC_A_C1, ENC_A_C2, ENC_B_C1, ENC_B_C2, ENCODER_PPR);

    // ── I2C + OLED ────────────────────────────────────────────────────────────
    Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
    Serial.printf("[I2C]   SDA=%d  SCL=%d  TCA=0x%02X\n",
                  PIN_IIC_SDA, PIN_IIC_SCL, TCA9548A_ADDR);

    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 20);
        oled.println("  Sanjo Sumo - Boot");
        oled.display();
        Serial.printf("[OLED]  OK  addr=0x%02X\n", OLED_I2C_ADDR);
    } else {
        Serial.println("[OLED]  ERREUR – verifier addr (0x3C ou 0x3D)");
    }

    if (oled2.begin(SSD1306_SWITCHCAPVCC, OLED2_I2C_ADDR)) {
        oled2.clearDisplay();
        oled2.display();
        Serial.printf("[OLED2] OK  addr=0x%02X\n", OLED2_I2C_ADDR);
    } else {
        Serial.printf("[OLED2] non detecte addr=0x%02X (desactive)\n", OLED2_I2C_ADDR);
    }


    // ── INA219 wattmètre (DFRobot SEN0291, shunt 10 mΩ) ─────────────────────
    gInaOk = ina219.begin();
    if (gInaOk) {
        Serial.printf("[INA219] OK  addr=0x%02X\n", INA219_I2C_ADDR);
    } else {
        Serial.println("[INA219] ERREUR – verifier cablage (IN+→bat+, IN-→charge, SDA/SCL)");
    }

    // ── VL53L0X via TCA9548A (adresse mux = 0x70) ────────────────────────────
    for (uint8_t i = 0; i < 2; i++) {
        tcaSelect(i);
        delay(10);
        tof[i].setTimeout(500);
        if (!tof[i].init()) {
            Serial.printf("[ToF]   #%d ERREUR (TCA canal %d)\n", i + 1, i);
        } else {
            tof[i].startContinuous();
            Serial.printf("[ToF]   #%d OK     (TCA canal %d)\n", i + 1, i);
        }
    }

    // ── Buzzer ────────────────────────────────────────────────────────────────
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
    gBuzzerQueue = xQueueCreate(BUZZER_QUEUE_LEN, sizeof(BuzzerNote));
    // Mélodie de démarrage (bloquant, avant les tâches)
    for (const auto& n : kMelodyBoot) {
        if (n.freq > 0) ledcWriteTone(BUZZER_LEDC_CHANNEL, n.freq);
        else            ledcWrite(BUZZER_LEDC_CHANNEL, 0);
        delay(n.ms);
    }
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    Serial.printf("[Buzz]  GPIO%d  canal LEDC %d\n", PIN_BUZZER, BUZZER_LEDC_CHANNEL);

    // ── Capteurs ligne ────────────────────────────────────────────────────────
    analogReadResolution(12);
    Serial.printf("[Ligne] MID=GPIO%d  RIGHT=GPIO%d  LEFT=GPIO%d  ADC 12-bit\n",
                  PIN_LINE_MID, PIN_LINE_RIGHT, PIN_LINE_LEFT);

    // ── Xbox BLE (démarré avant WiFi) ─────────────────────────────────────────
    xbox.begin(onXboxInput);
    Serial.println("[Xbox]  BLE scan démarré");
    delay(200);

    // ── BOOT button ───────────────────────────────────────────────────────────
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    Serial.printf("[Btn]   BOOT=GPIO%d  appui = test moteur\n", PIN_BUTTON_1);

    // ── WiFi ──────────────────────────────────────────────────────────────────
    Serial.printf("[WiFi]  Connexion a '%s'...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    const uint32_t wifiTimeout = millis() + 10000;
    while (WiFi.status() != WL_CONNECTED && millis() < wifiTimeout) {
        delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi]  Connecte  IP=%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi]  ECHEC – TCP desactive");
    }

    // ── Tasks ─────────────────────────────────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
        xTaskCreatePinnedToCore(TcpTask,     "TcpTask",     4096, nullptr, 1, nullptr, 0);
    }
    xTaskCreatePinnedToCore(BuzzerTask,  "BuzzerTask",  2048, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(EncoderTask, "EncoderTask", 2048, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(SensorTask,  "SensorTask",  3072, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(StatusTask,  "StatusTask",  2048, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(WattTask,    "WattTask",    2048, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(OledTask,    "OledTask",    5120, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(AutoTask,    "AutoTask",    2048, nullptr, 2, nullptr, 1);
    Serial.println("[Tasks] Demarre");

    Serial.println("[Setup] Pret\n");
}

// ─── Test distance ────────────────────────────────────────────────────────────
// Appui BOOT : moteurs avant à 100% pendant 5s, mesure distance via encodeurs.
// cm/count = π × 26mm / (14 × 30 × 10)  =  81.68 / 4200  ≈ 0.01945 cm

static void runDistanceTest() {
    constexpr float kCmPerCount = (float)M_PI * ENCODER_WHEEL_DIAM_MM
                                  / (float)(ENCODER_PPR * ENCODER_GEAR_RATIO * 10);
    constexpr uint32_t kDurMs   = 5000;
    constexpr uint16_t kSampleMs = 200;  // fenêtre de vitesse max

    gDistTestActive = true;
    gDistTestDone   = false;
    TSLOG("[DistTest] START  PWM=255  duree=%ums", kDurMs);

    // Reset compteurs
    encLeft.resetCount();
    encRight.resetCount();

    float vmaxL = 0.0f, vmaxR = 0.0f;
    int32_t prevL = 0, prevR = 0;
    uint32_t tPrev = millis();

    // Moteurs à fond – gauche miroir donc négatif
    motorLeft.setSpeed(-MOTOR_PWM_MAX);
    motorRight.setSpeed(MOTOR_PWM_MAX);

    const uint32_t tStart = millis();
    while (millis() - tStart < kDurMs) {
        delay(kSampleMs);
        encLeft.update();
        encRight.update();

        const int32_t cntL = encLeft.getCount();
        const int32_t cntR = encRight.getCount();
        const uint32_t tNow = millis();
        const float dt = (tNow - tPrev) / 1000.0f;
        tPrev = tNow;

        // Vitesse instantanée sur la fenêtre (cm/s)
        const float vL = fabsf((cntL - prevL) * kCmPerCount / dt);
        const float vR = fabsf((cntR - prevR) * kCmPerCount / dt);
        if (vL > vmaxL) vmaxL = vL;
        if (vR > vmaxR) vmaxR = vR;
        prevL = cntL;
        prevR = cntR;
    }

    motorLeft.stop();
    motorRight.stop();

    encLeft.update();
    encRight.update();

    const float distL = fabsf(encLeft.getCount()  * kCmPerCount);
    const float distR = fabsf(encRight.getCount() * kCmPerCount);

    gDistTestL_cm  = distL;
    gDistTestR_cm  = distR;
    gDistTestVmax  = (vmaxL + vmaxR) * 0.5f;
    gDistTestActive = false;
    gDistTestDone   = true;

    TSLOG("[DistTest] L=%.1fcm  R=%.1fcm  Vmax=%.1fcm/s", distL, distR, gDistTestVmax);
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    static bool lastBtn = HIGH;

    const bool btn = digitalRead(PIN_BUTTON_1);
    if (btn == LOW && lastBtn == HIGH) {
        delay(20);
        if (digitalRead(PIN_BUTTON_1) == LOW) {
            TSLOG("[Btn] Test distance 5s");
            runDistanceTest();
        }
    }
    lastBtn = btn;
    delay(10);
}

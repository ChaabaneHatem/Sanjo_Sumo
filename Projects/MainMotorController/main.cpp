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
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"
#include <WiFi.h>
#include <WiFiServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <VL53L0X.h>
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

XboxController xbox;  // scan BLE – se connecte à n'importe quelle manette Xbox

Encoder encLeft (ENC_A_C1, ENC_A_C2, 0);
Encoder encRight(ENC_B_C1, ENC_B_C2, 1);

VL53L0X tof[5];   // [0]=avant-centre  [1]=côté-droit  [2]=avant-droit  [3]=avant-gauche  [4]=côté-gauche

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
static SemaphoreHandle_t gI2cMutex        = nullptr;  // protège Wire (ToF + OLED + INA219)

static constexpr uint8_t TOF_N = 5;  // total canaux TCA
// Indices des capteurs avant : 0=avant-centre, 2=avant-droit, 3=avant-gauche
static constexpr uint8_t kTofFront[]  = {0, 2, 3};
static constexpr uint8_t TOF_FRONT_N  = sizeof(kTofFront);
static volatile uint16_t gTofDistMm[TOF_N]  = {};
static volatile bool     gTofTimeout[TOF_N] = {true, true, true, true, true};
static bool              gTofInitOk[TOF_N]  = {};  // true si init() OK en setup()
static volatile uint16_t gLineMid   = 0;   // capteur ligne centre  (GPIO34)
static volatile uint16_t gLineRight = 0;   // capteur ligne droite  (GPIO35)
static volatile uint16_t gLineLeft  = 0;   // capteur ligne gauche  (GPIO36)
static volatile float    gVelLeftCms     = 0.0f;  // vitesse roue gauche (cm/s, signée, repère robot)
static volatile float    gVelRightCms    = 0.0f;  // vitesse roue droite (cm/s, signée)

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
static volatile uint8_t  gRobotMode   = ROBOT_MODE_MANUAL;  // MANUAL ou AUTO

// États FSM combat (lisible par OLED/TCP)
enum EtatCombat : uint8_t { COMBAT_ARRET, COMBAT_RECHERCHE, COMBAT_ATTAQUE, COMBAT_DEFENSE };
static volatile EtatCombat gEtatCombat = COMBAT_ARRET;

// Convertit le pourcentage en valeur PWM [0, MOTOR_PWM_MAX]
static inline int16_t hmiSpeed() {
    return (int16_t)((MOTOR_PWM_MAX * gSpeedPct) / 100);
}

// ── Mode debug : commenter la ligne suivante pour désactiver tous les logs runtime ──
#define TSLOG_ENABLED

#ifdef TSLOG_ENABLED
#define TSLOG(fmt, ...) \
    do { \
        if (gSerialMutex && xSemaphoreTake(gSerialMutex, pdMS_TO_TICKS(50)) == pdTRUE) { \
            Serial.printf(fmt "\n", ##__VA_ARGS__); \
            xSemaphoreGive(gSerialMutex); \
        } \
    } while(0)
#else
#define TSLOG(fmt, ...) do {} while(0)
#endif

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
    delayMicroseconds(300);  // stabilisation bus après changement de canal
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
            motorLeft.stop(); motorRight.stop();
            buzzerMelody(kSoundAutoOn,  sizeof(kSoundAutoOn)  / sizeof(BuzzerNote));
            TSLOG("[Mode] AUTO");
        } else {
            gRobotMode = ROBOT_MODE_MANUAL;
            motorLeft.stop(); motorRight.stop();
            buzzerMelody(kSoundAutoOff, sizeof(kSoundAutoOff) / sizeof(BuzzerNote));
            TSLOG("[Mode] MANUEL");
        }
    }
    prevB = s.btnB;

    // ── X : toggle SUIVI LIGNE ───────────────────────────────────────────────
    static bool prevX = false;
    if (s.btnX && !prevX) {
        if (gRobotMode == ROBOT_MODE_LINE) {
            gRobotMode = ROBOT_MODE_MANUAL;
            motorLeft.stop(); motorRight.stop();
            buzzerMelody(kSoundAutoOff, sizeof(kSoundAutoOff) / sizeof(BuzzerNote));
            TSLOG("[Mode] MANUEL");
        } else {
            gRobotMode = ROBOT_MODE_LINE;
            motorLeft.stop(); motorRight.stop();
            buzzerMelody(kSoundAutoOn, sizeof(kSoundAutoOn) / sizeof(BuzzerNote));
            TSLOG("[Mode] SUIVI LIGNE");
        }
    }
    prevX = s.btnX;

    // ── Y : (non utilisé) ────────────────────────────────────────────────────
    static bool prevY = false;
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
    if (gRobotMode == ROBOT_MODE_AUTO || gRobotMode == ROBOT_MODE_LINE) return;

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
        gRobotMode = ROBOT_MODE_MANUAL;
        motorLeft.stop();
        motorRight.stop();

    } else if (strcmp(cmd, "mode_auto") == 0) {
        TSLOG("[Cmd] mode_auto");
        motorLeft.stop(); motorRight.stop();
        gRobotMode = ROBOT_MODE_AUTO;
        buzzerMelody(kSoundAutoOn, sizeof(kSoundAutoOn) / sizeof(BuzzerNote));

    } else if (strcmp(cmd, "mode_manuel") == 0) {
        TSLOG("[Cmd] mode_manuel");
        motorLeft.stop(); motorRight.stop();
        gRobotMode = ROBOT_MODE_MANUAL;
        buzzerMelody(kSoundAutoOff, sizeof(kSoundAutoOff) / sizeof(BuzzerNote));

    } else if (strcmp(cmd, "mode_ligne") == 0) {
        TSLOG("[Cmd] mode_ligne");
        motorLeft.stop(); motorRight.stop();
        gRobotMode = ROBOT_MODE_LINE;
        buzzerMelody(kSoundAutoOn,
             sizeof(kSoundAutoOn) / sizeof(BuzzerNote));

    } else if (strcmp(cmd, "suivre_ligne") == 0) {
        TSLOG("[Cmd] suivre_ligne → MODE AUTO");
        motorLeft.stop(); motorRight.stop();
        gRobotMode = ROBOT_MODE_AUTO;
        buzzerMelody(kSoundAutoOn, sizeof(kSoundAutoOn) / sizeof(BuzzerNote));

    } else if (strcmp(cmd, "tuning") == 0) {
        // Paramètres reçus du RPi – réservé pour asservissement PD
        TSLOG("[Cmd] tuning kp=%.1f kd=%.1f vbase=%d vmin=%d vmax=%d seuil=%d",
              doc["kp"].as<float>(), doc["kd"].as<float>(),
              doc["vbase"].as<int>(), doc["vmin"].as<int>(), 
              doc["vmax"].as<int>(), doc["seuil"].as<int>());

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

    // ── ToF : 5 capteurs ──────────────────────────────────────────────────────
    static const char* kTofNames[TOF_N] = {
        "avant_centre", "cote_droit", "avant_droit", "avant_gauche", "cote_gauche"
    };
    for (uint8_t i = 0; i < TOF_N; i++) {
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
        doc["mode"]        = (gRobotMode == ROBOT_MODE_AUTO) ? "auto"
                           : (gRobotMode == ROBOT_MODE_LINE) ? "ligne" : "manuel";
        // Champs FSM attendus par le HMI
        static const char* kEtatStr[] = {"ARRET","RECHERCHE","ATTAQUE","DEFENSE"};
        doc["etat"]        = kEtatStr[(int)gEtatCombat];
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
    // Attendre la connexion WiFi (max 15s)
    uint8_t wait = 0;
    while (WiFi.status() != WL_CONNECTED && wait < 30) { vTaskDelay(pdMS_TO_TICKS(500)); wait++; }
    if (WiFi.status() != WL_CONNECTED) { TSLOG("[TCP] WiFi absent – tâche arrêtée"); vTaskDelete(nullptr); return; }

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

// ─── ToF task – 2 Hz (I2C séparé des ADC ligne) ──────────────────────────────
// Séparé du LineSensorTask pour éviter que les blocages I2C affectent
// la détection de ligne (et inversement).

// Réinitialise les deux capteurs VL53L0X sous le mutex I2C.
// Appelé après TOF_REINIT_THRESH cycles consécutifs de timeout.
static void tofReinit() {
    TSLOG("[ToF]  REINIT – reset TCA + capteurs...");

    // Désactiver tous les canaux TCA pour libérer le bus
    // Ne PAS appeler Wire.end()/Wire.begin() : corrompt le stack BLE (Core 0)
    Wire.beginTransmission(TCA9548A_ADDR);
    Wire.write(0x00);
    Wire.endTransmission();
    vTaskDelay(pdMS_TO_TICKS(20));

    for (uint8_t i = 0; i < TOF_N; i++) {
        tcaSelect(i);
        vTaskDelay(pdMS_TO_TICKS(30));
        bool ok = false;
        for (uint8_t attempt = 0; attempt < 3 && !ok; attempt++) {
            if (tof[i].init()) {
                tof[i].setTimeout(25);
                tof[i].setMeasurementTimingBudget(20000);
                tof[i].startContinuous(20);
                gTofInitOk[i] = true;
                TSLOG("[ToF]  #%d réinitialisé OK (essai %d)", i, attempt + 1);
                ok = true;
            } else {
                gTofInitOk[i] = false;
                vTaskDelay(pdMS_TO_TICKS(30));
            }
        }
        if (!ok) TSLOG("[ToF]  #%d ERREUR reinit (3 essais)", i);
    }
    vTaskDelay(pdMS_TO_TICKS(30));
}

static void ToFTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));
    static constexpr uint8_t TOF_REINIT_THRESH = 5;
    uint8_t consecutiveTimeouts = 0;

    for (;;) {
        uint16_t d[TOF_N] = {};
        bool     t[TOF_N];
        for (uint8_t i = 0; i < TOF_N; i++) t[i] = true;

        if (xSemaphoreTake(gI2cMutex, pdMS_TO_TICKS(150)) == pdTRUE) {
            for (uint8_t i = 0; i < TOF_N; i++) {
                if (gTofInitOk[i]) {
                    tcaSelect(i);
                    d[i] = tof[i].readRangeContinuousMillimeters();
                    t[i] = tof[i].timeoutOccurred();
                }
            }
            xSemaphoreGive(gI2cMutex);
        }

        for (uint8_t i = 0; i < TOF_N; i++) {
            gTofDistMm[i]  = t[i] ? 0 : d[i];
            gTofTimeout[i] = t[i];
        }

        uint8_t errCount = 0, initCount = 0;
        for (uint8_t i = 0; i < TOF_N; i++) {
            if (gTofInitOk[i]) { initCount++; if (t[i] || d[i] >= 8000) errCount++; }
        }
        if (initCount > 0 && errCount == initCount) {
            if (++consecutiveTimeouts >= TOF_REINIT_THRESH) {
                consecutiveTimeouts = 0;
                if (xSemaphoreTake(gI2cMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    tofReinit();
                    xSemaphoreGive(gI2cMutex);
                }
            }
        } else {
            consecutiveTimeouts = 0;
        }


        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

// ─── Line sensor task – 100 Hz (ADC seulement, pas d'I2C) ────────────────────

static void SensorTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(333));
    uint8_t logDiv = 0;
    for (;;) {
        gLineMid   = (uint16_t)analogRead(PIN_LINE_MID);
        gLineRight = (uint16_t)analogRead(PIN_LINE_RIGHT);
        gLineLeft  = (uint16_t)analogRead(PIN_LINE_LEFT);


        vTaskDelay(pdMS_TO_TICKS(1000 / SENSOR_TASK_FREQ));  // 10ms = 100Hz
    }
}


// ─── Pattern Strategy – Labo 6 ───────────────────────────────────────────────
// Wrapper moteurs (miroir gauche compensé)
struct Bot {
    static void avancer(int v)    { motorLeft.setSpeed(-(int16_t)v); motorRight.setSpeed((int16_t)v); }
    static void reculer(int v)    { motorLeft.setSpeed((int16_t)v);  motorRight.setSpeed(-(int16_t)v); }
    static void pivoterCW(int v)  { motorLeft.setSpeed(-(int16_t)v); motorRight.setSpeed(-(int16_t)v); }
    static void pivoterCCW(int v) { motorLeft.setSpeed((int16_t)v);  motorRight.setSpeed((int16_t)v); }
    static void arreter()         { motorLeft.stop(); motorRight.stop(); }
};

class IStrategieRobot {
public:
    virtual ~IStrategieRobot() = default;
    virtual void demarrer()    = 0;
    virtual void mettreAJour() = 0;
    virtual void arreter()     = 0;
    virtual bool estTerminee() = 0;
    virtual const char* nom()  = 0;
};

// Attaque : vitesse graduée selon distance capteur central (ToF[0])
//   > 250 mm → approche douce  40%
//   100-250mm → pression       55%
//    < 100 mm → sprint final   70%
class StrategieCharge : public IStrategieRobot {
    bool     _done        = false;
    bool     _pushing     = false;
    uint32_t _pushUntilMs = 0;
public:
    void demarrer() override {
        _done = false; _pushing = false; _pushUntilMs = 0;
        Bot::avancer(MOTOR_PWM_MAX * 22 / 100);
    }
    void mettreAJour() override {
        const uint16_t raw0 = gTofTimeout[0] ? 9999 : gTofDistMm[0];
        const uint16_t dist = (raw0 < 20) ? 9999 : raw0;
        const uint32_t now  = millis();

        if (dist < 150) { _pushing = true; _pushUntilMs = now + 1500; }

        if (_pushing && now < _pushUntilMs) {
            Bot::avancer(MOTOR_PWM_MAX * 55 / 100);   // sprint – inchangé
        } else {
            _pushing = false;
            if      (dist >= TOF_LOST_MM) Bot::avancer(MOTOR_PWM_MAX * 17 / 100);
            else if (dist > 300)          Bot::avancer(MOTOR_PWM_MAX * 22 / 100);
            else if (dist > 150)          Bot::avancer(MOTOR_PWM_MAX * 33 / 100);
            else                          Bot::avancer(MOTOR_PWM_MAX * 55 / 100);
        }
    }
    void arreter()     override { Bot::arreter(); _done = true; }
    bool estTerminee() override { return _done; }
    const char* nom()  override { return "Charge"; }
};

// Recherche : avance + pivot alterné pour couvrir l'arène ouvertement
class StrategieRotation : public IStrategieRobot {
    enum Phase { AVANCE, PIVOT } _phase = AVANCE;
    bool     _done = false;
    uint32_t _t    = 0;
public:
    void demarrer() override {
        _done = false; _phase = AVANCE; _t = millis();
        Bot::avancer(MOTOR_PWM_MAX * 22 / 100);
    }
    void mettreAJour() override {
        const uint32_t dt = millis() - _t;
        if (_phase == AVANCE && dt >= 600) {
            _phase = PIVOT; _t = millis();
            Bot::pivoterCW(MOTOR_PWM_MAX * 45 / 100);   // pivot prononcé
        } else if (_phase == PIVOT && dt >= 500) {
            _phase = AVANCE; _t = millis();
            Bot::avancer(MOTOR_PWM_MAX * 22 / 100);
        }
    }
    void arreter()     override { _done = true; }
    bool estTerminee() override { return _done; }
    const char* nom()  override { return "Rotation"; }
};

// Défense : recule en biais selon le côté du bord détecté
class StrategieEvitement : public IStrategieRobot {
    bool     _coteG;
    bool     _done = false;
    uint32_t _t    = 0;
public:
    explicit StrategieEvitement(bool coteGauche) : _coteG(coteGauche) {}
    void demarrer() override {
        _done = false; _t = millis();
        if (_coteG) {
            motorLeft.setSpeed((int16_t)MOTOR_PWM_MAX);
            motorRight.setSpeed(-(int16_t)(MOTOR_PWM_MAX * 40 / 100));
        } else {
            motorLeft.setSpeed((int16_t)(MOTOR_PWM_MAX * 40 / 100));
            motorRight.setSpeed(-(int16_t)MOTOR_PWM_MAX);
        }
    }
    void mettreAJour() override {
        if (millis() - _t >= AUTO_REVERSE_MS) { _done = true; }
    }
    void arreter()     override { _done = true; }
    bool estTerminee() override { return _done; }
    const char* nom()  override { return "Evitement"; }
};

// ─── ControleurCombat – FSM Labo 6 ───────────────────────────────────────────
// Priorités : DEFENSE (bord) > ATTAQUE (ToF < 400mm) > RECHERCHE

class ControleurCombat {
    IStrategieRobot* _strat = nullptr;
    EtatCombat       _etat  = COMBAT_ARRET;

    void changerStrategie(IStrategieRobot* nouvelle) {
        if (_strat) { _strat->arreter(); delete _strat; }
        _strat = nouvelle;
        if (_strat) _strat->demarrer();
        gEtatCombat = _etat;
    }

public:
    ~ControleurCombat() { arreterCombat(); }

    void demarrer() {
        _etat = COMBAT_RECHERCHE;
        changerStrategie(new StrategieRotation());
        TSLOG("[Combat] START → RECHERCHE");
    }

    void arreterCombat() {
        if (_strat) { _strat->arreter(); delete _strat; _strat = nullptr; }
        Bot::arreter();
        _etat = COMBAT_ARRET;
        gEtatCombat = COMBAT_ARRET;
    }

    void mettreAJour() {
        if (_etat == COMBAT_ARRET || !_strat) return;

        const bool bordL = gLineLeft  < LINE_THRESHOLD;
        const bool bordM = gLineMid   < LINE_THRESHOLD;
        const bool bordR = gLineRight < LINE_THRESHOLD;
        const bool bord  = bordL || bordM || bordR;

        // Capteur centre uniquement – filtre < 60mm (faux positif chassis)
        const uint16_t raw0    = gTofTimeout[0] ? 9999 : gTofDistMm[0];
        const uint16_t minFront = (raw0 < 20) ? 9999 : raw0;
        const bool adversaire = minFront < TOF_ATTACK_MM;
        const bool perdu      = minFront >= TOF_LOST_MM;

        // Priorité 1 – bord détecté → DEFENSE (prime sur tout)
        if (bord && _etat != COMBAT_DEFENSE) {
            const bool coteG = bordL || (bordM && !bordR);
            _etat = COMBAT_DEFENSE;
            changerStrategie(new StrategieEvitement(coteG));
            TSLOG("[Combat] DEFENSE %s  L:%d M:%d R:%d", coteG ? "G" : "D", bordL, bordM, bordR);
        }
        // Priorité 2 – adversaire détecté → ATTAQUE
        else if (adversaire && _etat == COMBAT_RECHERCHE) {
            _etat = COMBAT_ATTAQUE;
            changerStrategie(new StrategieCharge());
            TSLOG("[Combat] ATTAQUE  min=%umm  [C:%u D:%u G:%u]", minFront, gTofDistMm[0], gTofDistMm[2], gTofDistMm[3]);
        }
        // Priorité 3 – adversaire perdu en attaque → RECHERCHE
        else if (perdu && _etat == COMBAT_ATTAQUE) {
            _etat = COMBAT_RECHERCHE;
            changerStrategie(new StrategieRotation());
            TSLOG("[Combat] RECHERCHE (perdu)");
        }
        // Priorité 4 – défense terminée → RECHERCHE
        else if (_etat == COMBAT_DEFENSE && _strat->estTerminee()) {
            _etat = COMBAT_RECHERCHE;
            changerStrategie(new StrategieRotation());
            TSLOG("[Combat] RECHERCHE (dégagé)");
        }

        _strat->mettreAJour();
    }

    EtatCombat  etat()     const { return _etat; }
    const char* nomStrat() const { return _strat ? _strat->nom() : "aucune"; }
};

// ─── Suivi ligne arène – avance lentement, recule si bord détecté ────────────
static void suivreLigne() {
    const bool bL = gLineLeft  < LINE_THRESHOLD;
    const bool bM = gLineMid   < LINE_THRESHOLD;
    const bool bR = gLineRight < LINE_THRESHOLD;

    if (bL || bM || bR) {
        if (bL || (bM && !bR)) {
            motorLeft.setSpeed(-(int16_t)(MOTOR_PWM_MAX * 15 / 100));
            motorRight.setSpeed(-(int16_t)(MOTOR_PWM_MAX * 42 / 100));
        } else {
            motorLeft.setSpeed(-(int16_t)(MOTOR_PWM_MAX * 42 / 100));
            motorRight.setSpeed(-(int16_t)(MOTOR_PWM_MAX * 15 / 100));
        }
    } else {
        Bot::avancer(MOTOR_PWM_MAX * 28 / 100);
    }
}

// ─── Auto task ────────────────────────────────────────────────────────────────

static void AutoTask(void*) {
    ControleurCombat combat;
    bool wasActive = false;

    for (;;) {
        // Mode suivi ligne
        if (gRobotMode == ROBOT_MODE_LINE) {
            if (wasActive) { combat.arreterCombat(); wasActive = false; }
            suivreLigne();
            vTaskDelay(pdMS_TO_TICKS(1000 / AUTO_TASK_FREQ));
            continue;
        }

        if (gRobotMode != ROBOT_MODE_AUTO) {
            if (wasActive) { combat.arreterCombat(); wasActive = false; }
            motorLeft.stop(); motorRight.stop();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!wasActive) { combat.demarrer(); wasActive = true; }
        combat.mettreAJour();
        vTaskDelay(pdMS_TO_TICKS(1000 / AUTO_TASK_FREQ));
    }
}

// ─── Watt task – 2 Hz ────────────────────────────────────────────────────────

static void WattTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(600));
    uint32_t lastMs = millis();
    uint8_t  inaFailCount = 0;

    for (;;) {
        if (gInaOk && inaFailCount < 5 &&
            xSemaphoreTake(gI2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            const float shuntMv  = ina219.getShuntVoltage_mV();
            const float busV     = ina219.getBusVoltage_V();
            xSemaphoreGive(gI2cMutex);
            inaFailCount = 0;  // lecture OK – réinitialise compteur

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
            TSLOG("[Watt] Vbat=%.2fV  I=%.0fmA  P=%.2fW  Eh=%.4fWh  Qh=%.1fmAh",
                  busV, currentMa, powerMw / 1000.0f, gAccEnergyWh, gAccChargeMah);
        } else if (gInaOk) {
            // mutex timeout ou inaFailCount >= 5 : bus I2C occupé ou bloqué
            inaFailCount++;
            if (inaFailCount >= 5) {
                // Arrête de réessayer pendant 10s pour éviter la fuite heap I2C
                TSLOG("[Watt]  Bus I2C bloque – pause 10s");
                vTaskDelay(pdMS_TO_TICKS(10000));
                inaFailCount = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / WATT_TASK_FREQ));
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
    // Log IP au démarrage
    if (WiFi.status() == WL_CONNECTED)
        TSLOG("[WiFi]  IP=%s  port=%d", WiFi.localIP().toString().c_str(), TCP_PORT);
    else
        TSLOG("[WiFi]  Non connecte");

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
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // désactive brownout detector
    Serial.begin(115200);
    delay(300);

    // ── Raison du dernier reset ───────────────────────────────────────────────
    const esp_reset_reason_t rr = esp_reset_reason();
    const char* rrStr = "INCONNU";
    switch (rr) {
        case ESP_RST_POWERON:   rrStr = "POWER_ON";    break;
        case ESP_RST_EXT:       rrStr = "RESET_EXT";   break;
        case ESP_RST_SW:        rrStr = "SW_RESET";    break;
        case ESP_RST_PANIC:     rrStr = "PANIC";       break;
        case ESP_RST_INT_WDT:   rrStr = "INT_WDT";     break;
        case ESP_RST_TASK_WDT:  rrStr = "TASK_WDT";    break;
        case ESP_RST_WDT:       rrStr = "WDT";         break;
        case ESP_RST_BROWNOUT:  rrStr = "BROWNOUT";    break;
        default: break;
    }
    Serial.printf("[Reset] Raison: %s (%d)\n", rrStr, (int)rr);
    gSerialMutex = xSemaphoreCreateMutex();
    gI2cMutex    = xSemaphoreCreateMutex();

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

    // ── I2C ───────────────────────────────────────────────────────────────────
    Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
    Wire.setClock(400000);
    Serial.printf("[I2C]   SDA=%d  SCL=%d  TCA=0x%02X\n",
                  PIN_IIC_SDA, PIN_IIC_SCL, TCA9548A_ADDR);

    // ── INA219 DÉSACTIVÉ ─────────────────────────────────────────────────────
    Serial.println("[INA219] desactive");

    // ── VL53L0X via TCA9548A (adresse mux = 0x70) ────────────────────────────
    auto i2cBusRecover = []() {
        Wire.end();
        delay(5);
        pinMode(PIN_IIC_SCL, OUTPUT);
        pinMode(PIN_IIC_SDA, OUTPUT);
        for (int p = 0; p < 9; p++) {
            digitalWrite(PIN_IIC_SCL, HIGH); delayMicroseconds(5);
            digitalWrite(PIN_IIC_SCL, LOW);  delayMicroseconds(5);
        }
        digitalWrite(PIN_IIC_SDA, LOW);
        digitalWrite(PIN_IIC_SCL, HIGH); delayMicroseconds(5);
        digitalWrite(PIN_IIC_SDA, HIGH); delayMicroseconds(5);
        delay(5);
        Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
        Wire.setClock(400000);
        delay(10);
        Serial.println("[I2C]   Bus recover OK");
    };

    static const char* kTofLabels[TOF_N] = {
        "avant-centre", "cote-droit", "avant-droit", "avant-gauche", "cote-gauche"
    };
    for (uint8_t i = 0; i < TOF_N; i++) {
        tcaSelect(i);
        delay(10);
        tof[i].setTimeout(25);
        bool ok = false;
        for (uint8_t attempt = 0; attempt < 3 && !ok; attempt++) {
            if (tof[i].init()) {
                tof[i].setMeasurementTimingBudget(20000);
                tof[i].startContinuous(20);
                gTofInitOk[i] = true;
                Serial.printf("[ToF]   #%d (%s) OK\n", i, kTofLabels[i]);
                ok = true;
            } else {
                i2cBusRecover();
                tcaSelect(i);
                delay(20);
            }
        }
        if (!ok) { gTofInitOk[i] = false; Serial.printf("[ToF]   #%d absent\n", i); }
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

    // ── BOOT button ───────────────────────────────────────────────────────────
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    Serial.printf("[Btn]   BOOT=GPIO%d  appui = test moteur\n", PIN_BUTTON_1);

    // ── Xbox BLE (démarrer AVANT WiFi pour priorité radio) ───────────────────
    xbox.begin(onXboxInput);
    Serial.println("[Xbox]  BLE scan demarre");
    delay(200);  // laisser NimBLE s'initialiser

    // ── WiFi (sleep mode pour coexistence BLE) ────────────────────────────────
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);   // modem sleep → libère le radio entre les paquets WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi]  Connexion %s", WIFI_SSID);
    {
        uint8_t tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 20) {
            delay(500); Serial.print("."); tries++;
        }
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi]  IP=%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi]  Echec – mode sans fil desactive");
    }

    // ── Tasks ─────────────────────────────────────────────────────────────────
    xTaskCreatePinnedToCore(TcpTask,     "TcpTask",     4096, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(BuzzerTask,  "BuzzerTask",  2048, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(EncoderTask, "EncoderTask", 2048, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(StatusTask,  "StatusTask",  2048, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(ToFTask,     "ToFTask",     3072, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(SensorTask,  "SensorTask",  2048, nullptr, 2, nullptr, 0);  // ADC 100Hz, Core 0
    xTaskCreatePinnedToCore(AutoTask,    "AutoTask",    3072, nullptr, 2, nullptr, 1);
    Serial.println("[Tasks] Demarre");

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[Setup] IP: %s  port TCP: %d\n", WiFi.localIP().toString().c_str(), TCP_PORT);
    }
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
    // Activation automatique du mode combat 5s après le démarrage
    static bool sAutoStartDone = false;
    if (!sAutoStartDone && millis() > 2000) {
        sAutoStartDone = true;
        gRobotMode = ROBOT_MODE_AUTO;
        buzzerMelody(kSoundAutoOn, sizeof(kSoundAutoOn) / sizeof(BuzzerNote));
        TSLOG("[Boot] → MODE AUTO (2s)");
    }

    static bool lastBtn = HIGH;

    const bool btn = digitalRead(PIN_BUTTON_1);
    if (btn == LOW && lastBtn == HIGH) {
        delay(20);  // antirebond
        if (digitalRead(PIN_BUTTON_1) == LOW) {
            if (gRobotMode == ROBOT_MODE_MANUAL) {
                gRobotMode = ROBOT_MODE_AUTO;
                motorLeft.stop(); motorRight.stop();
                buzzerMelody(kSoundAutoOn,  sizeof(kSoundAutoOn)  / sizeof(BuzzerNote));
                TSLOG("[Btn] → MODE AUTO");
            } else {
                gRobotMode = ROBOT_MODE_MANUAL;
                motorLeft.stop(); motorRight.stop();
                buzzerMelody(kSoundAutoOff, sizeof(kSoundAutoOff) / sizeof(BuzzerNote));
                TSLOG("[Btn] → MODE MANUEL");
            }
        }
    }
    lastBtn = btn;
    delay(10);
}

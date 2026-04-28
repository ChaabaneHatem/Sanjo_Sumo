/**
 * Laboratoire 6 — Combat !!
 * Fichier : main_combat.cpp (ESP32)
 *
 * Pattern Strategy + Machine à états finis (FSM)
 *
 * Complétez les sections marquées TODO.
 * Ne modifiez pas les sections marquées NE PAS MODIFIER.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <VL53L0X.h>

#ifdef COMM_WIFI
  #include <WiFi.h>
  #include <WiFiServer.h>
  WiFiServer serveur(8080);
  WiFiClient client;
#endif
#ifdef COMM_BT
  #include <BluetoothSerial.h>
  BluetoothSerial SerialBT;
#endif

// ============================================================
// NE PAS MODIFIER — Broches
// ============================================================
const int PWMA = 25, AIN1 = 26, AIN2 = 27;
const int PWMB = 32, BIN1 = 33, BIN2 = 14;
const int STBY = 12;

// Capteurs de ligne (TCRT5000)
const int LIGNE_AV_G = 34;
const int LIGNE_AV_D = 35;
const int SEUIL_LIGNE = 1800;

// Broches XSHUT des TOF (pour adressage I2C multiple)
const int XSHUT_AV_G  = 4;
const int XSHUT_AV_C  = 5;
const int XSHUT_AV_D  = 18;
// ============================================================


// ============================================================
// NE PAS MODIFIER — ControleurMoteur (du labo 3)
// ============================================================
class ControleurMoteur
{
private:
    int _pwm, _in1, _in2, _canal;

public:
    ControleurMoteur(int pwm, int in1, int in2, int canal)
        : _pwm(pwm), _in1(in1), _in2(in2), _canal(canal)
    {
        ledcSetup(_canal, 5000, 8);
        ledcAttachPin(_pwm, _canal);
        pinMode(_in1, OUTPUT);
        pinMode(_in2, OUTPUT);
        arreter();
    }

    ~ControleurMoteur() { arreter(); ledcDetachPin(_pwm); }

    void avancer(int v) { v = constrain(v,0,255); digitalWrite(_in1,HIGH); digitalWrite(_in2,LOW);  ledcWrite(_canal,v); }
    void reculer(int v) { v = constrain(v,0,255); digitalWrite(_in1,LOW);  digitalWrite(_in2,HIGH); ledcWrite(_canal,v); }
    void arreter()      {                          digitalWrite(_in1,HIGH); digitalWrite(_in2,HIGH); ledcWrite(_canal,0); }
};

// ============================================================
// NE PAS MODIFIER — ControleurRobot simplifié
// ============================================================
class ControleurRobot
{
private:
    ControleurMoteur* _g;
    ControleurMoteur* _d;

public:
    ControleurRobot(ControleurMoteur* g, ControleurMoteur* d) : _g(g), _d(d) {}

    void avancer(int v = 200)        { _g->avancer(v); _d->avancer(v); }
    void reculer(int v = 200)        { _g->reculer(v); _d->reculer(v); }
    void pivoterGauche(int v = 200)  { _g->reculer(v); _d->avancer(v); }
    void pivoterDroite(int v = 200)  { _g->avancer(v); _d->reculer(v); }
    void arreter()                   { _g->arreter();  _d->arreter(); }
};
// ============================================================


// ============================================================
// NE PAS MODIFIER — Interface IStrategieRobot
//
// Toutes les stratégies héritent de cette interface.
// ControleurCombat ne voit que ce pointeur — c'est le polymorphisme.
// ============================================================
class IStrategieRobot
{
public:
    virtual ~IStrategieRobot() = default;

    // Appelée une fois quand la stratégie devient active
    virtual void demarrer(ControleurRobot* robot) = 0;

    // Appelée à chaque itération — non-bloquante, comme millis()
    virtual void mettreAJour(ControleurRobot* robot) = 0;

    // Appelée quand la stratégie cesse d'être active
    virtual void arreter(ControleurRobot* robot) = 0;

    // Retourne true quand la stratégie a terminé sa séquence
    virtual bool estTerminee() = 0;

    // Nom court pour débogage
    virtual const char* nom() = 0;
};
// ============================================================


// ============================================================
// TODO 1 — StrategieCharge (attaque)
//
// Fonce vers l'adversaire à pleine vitesse.
// Termine après DUREE_MS millisecondes OU si le bord est détecté
// (la FSM se chargera de la transition).
//
// Attributs suggérés :
//   unsigned long _debut;
//   bool _terminee;
//   const unsigned long DUREE_MS = 2000;
// ============================================================
class StrategieCharge : public IStrategieRobot
{
private:
    unsigned long _debut    = 0;
    bool          _terminee = false;
    const unsigned long DUREE_MS = 2000;

public:
    void demarrer(ControleurRobot* robot) override
    {
        _debut    = millis();
        _terminee = false;
        // TODO 1a — Démarrez les moteurs à pleine vitesse (avancer)
    }

    void mettreAJour(ControleurRobot* robot) override
    {
        // TODO 1b — Si DUREE_MS écoulées, marquez _terminee = true et arrêtez
    }

    void arreter(ControleurRobot* robot) override
    {
        robot->arreter();
        _terminee = true;
    }

    bool estTerminee() override { return _terminee; }
    const char* nom()  override { return "Charge"; }
};


// ============================================================
// TODO 2 — StrategieRotation (attaque/recherche)
//
// Tourne sur place pour chercher l'adversaire.
// Pas de durée fixe — la FSM interrompt quand l'adversaire est détecté.
// ============================================================
class StrategieRotation : public IStrategieRobot
{
private:
    bool _terminee = false;

public:
    void demarrer(ControleurRobot* robot) override
    {
        _terminee = false;
        // TODO 2a — Démarrez une rotation à vitesse modérée (pivoterDroite)
    }

    void mettreAJour(ControleurRobot* robot) override
    {
        // Rien — la FSM interrompt cette stratégie si nécessaire
    }

    void arreter(ControleurRobot* robot) override
    {
        robot->arreter();
        _terminee = true;
    }

    bool estTerminee() override { return _terminee; }
    const char* nom()  override { return "Rotation"; }
};


// ============================================================
// TODO 3 — StrategieRecul (défense)
//
// Séquence : recule pendant DUREE_RECUL_MS, puis pivote 180°.
// Utilisez millis() — pas de delay() !
//
// Attributs suggérés :
//   enum Phase { RECUL, PIVOT, TERMINE } _phase;
//   unsigned long _debutPhase;
//   const unsigned long DUREE_RECUL_MS = 500;
//   const unsigned long DUREE_PIVOT_MS = 400;
// ============================================================
class StrategieRecul : public IStrategieRobot
{
private:
    enum Phase { RECUL, PIVOT, TERMINE } _phase = RECUL;
    unsigned long _debutPhase = 0;
    const unsigned long DUREE_RECUL_MS = 500;
    const unsigned long DUREE_PIVOT_MS = 400;

public:
    void demarrer(ControleurRobot* robot) override
    {
        _phase      = RECUL;
        _debutPhase = millis();
        // TODO 3a — Commencez à reculer
    }

    void mettreAJour(ControleurRobot* robot) override
    {
        // TODO 3b — Gérez les transitions de phase avec millis()
        // Phase RECUL → après DUREE_RECUL_MS → Phase PIVOT (pivoterDroite)
        // Phase PIVOT → après DUREE_PIVOT_MS → Phase TERMINE (arrêter)
    }

    void arreter(ControleurRobot* robot) override
    {
        robot->arreter();
        _phase = TERMINE;
    }

    bool estTerminee() override { return _phase == TERMINE; }
    const char* nom()  override { return "Recul"; }
};


// ============================================================
// TODO 4 — StrategieEvitement (défense)
//
// Recule en biais selon le côté du bord détecté.
// Le ControleurCombat indiquera le côté via le constructeur ou un setter.
//
// Comportement : si bord détecté à gauche → reculer en virant à droite
//                si bord détecté à droite → reculer en virant à gauche
// ============================================================
class StrategieEvitement : public IStrategieRobot
{
private:
    bool _coteGauche    = true;   // true = bord à gauche
    bool _terminee      = false;
    unsigned long _debut = 0;
    const unsigned long DUREE_MS = 600;

public:
    // TODO 4a — Constructeur avec paramètre côté
    StrategieEvitement(bool bordeauGauche) : _coteGauche(bordeauGauche) {}

    void demarrer(ControleurRobot* robot) override
    {
        _debut    = millis();
        _terminee = false;
        // TODO 4b — Selon _coteGauche, reculez en biais approprié
        //   Si bord à gauche : moteur gauche plus lent (effet virer à droite en reculant)
        //   Si bord à droite : moteur droit plus lent
    }

    void mettreAJour(ControleurRobot* robot) override
    {
        // TODO 4c — Terminez après DUREE_MS
        if (millis() - _debut >= DUREE_MS) { robot->arreter(); _terminee = true; }
    }

    void arreter(ControleurRobot* robot) override { robot->arreter(); _terminee = true; }
    bool estTerminee() override { return _terminee; }
    const char* nom()  override { return "Evitement"; }
};


// ============================================================
// NE PAS MODIFIER — États de la FSM
// ============================================================
enum EtatCombat { ARRET, RECHERCHE, ATTAQUE, DEFENSE };
// ============================================================


// ============================================================
// TODO 5 — ControleurCombat (FSM + polymorphisme)
//
// Contient :
//   IStrategieRobot* _strategie  ← pointeur polymorphe
//   EtatCombat _etat
//   ControleurRobot* _robot
//   VL53L0X* _tofAvantCentre
//
// Méthodes :
//   void changerStrategie(IStrategieRobot* nouvelleStrategie)
//     → appelle _strategie->arreter(), delete _strategie, affecte, appelle demarrer()
//   void mettreAJour()  ← lit capteurs, décide transitions, appelle _strategie->mettreAJour()
//   void demarrer() / void arreter()
// ============================================================
class ControleurCombat
{
private:
    IStrategieRobot* _strategie        = nullptr;
    EtatCombat       _etat             = ARRET;
    ControleurRobot* _robot            = nullptr;
    VL53L0X*         _tofAvantCentre   = nullptr;

    const int SEUIL_ATTAQUE_MM = 400;   // Adversaire détecté si TOF < cette valeur
    const int SEUIL_PERDU_MM   = 600;   // Adversaire perdu si TOF > cette valeur

public:
    // === NE PAS MODIFIER — Constructeur ===
    ControleurCombat(ControleurRobot* robot, VL53L0X* tof)
        : _robot(robot), _tofAvantCentre(tof)
    {}

    ~ControleurCombat()
    {
        if (_strategie) { _strategie->arreter(_robot); delete _strategie; }
    }

    void demarrer()
    {
        // TODO 5a — Passez à l'état RECHERCHE avec une StrategieRotation
        changerStrategie(new StrategieRotation());
        _etat = RECHERCHE;
    }

    void arreter()
    {
        if (_strategie) _strategie->arreter(_robot);
        _robot->arreter();
        _etat = ARRET;
    }
    // =====================================

    // ============================================================
    // TODO 5b — changerStrategie(IStrategieRobot* nouvelle)
    //
    // 1. Si _strategie existe : appeler arreter() et delete
    // 2. Affecter _strategie = nouvelle
    // 3. Appeler demarrer() sur la nouvelle stratégie
    // ============================================================
    void changerStrategie(IStrategieRobot* nouvelle)
    {
        if (_strategie)
        {
            _strategie->arreter(_robot);
            delete _strategie;
        }
        _strategie = nouvelle;
        if (_strategie) _strategie->demarrer(_robot);
    }

    // ============================================================
    // TODO 5c — mettreAJour()
    //
    // 1. Si ARRET → rien
    // 2. Lire capteurs ligne (TCRT5000)
    // 3. Lire TOF avant-centre
    // 4. Priorités (dans cet ordre) :
    //    a. Si bord détecté (ADC > SEUIL_LIGNE sur av_g OU av_d) :
    //       → changerStrategie(new StrategieRecul()) ou StrategieEvitement
    //       → _etat = DEFENSE
    //    b. Sinon si état RECHERCHE et TOF < SEUIL_ATTAQUE :
    //       → changerStrategie(new StrategieCharge())
    //       → _etat = ATTAQUE
    //    c. Sinon si état ATTAQUE et TOF > SEUIL_PERDU :
    //       → changerStrategie(new StrategieRotation())
    //       → _etat = RECHERCHE
    //    d. Sinon si état DEFENSE et stratégie terminée :
    //       → changerStrategie(new StrategieRotation())
    //       → _etat = RECHERCHE
    // 5. Appeler _strategie->mettreAJour(_robot)
    // ============================================================
    void mettreAJour()
    {
        if (_etat == ARRET || !_strategie) return;

        // TODO 5c — Implémentez ici


    }

    EtatCombat etat() const { return _etat; }
    const char* nomStrategie() const { return _strategie ? _strategie->nom() : "aucune"; }
};
// ============================================================


// ============================================================
// NE PAS MODIFIER — Variables globales
// ============================================================
ControleurMoteur* moteurG    = nullptr;
ControleurMoteur* moteurD    = nullptr;
ControleurRobot*  robot      = nullptr;
VL53L0X           tofAvantC;
ControleurCombat* combat     = nullptr;
// ============================================================


// ============================================================
// NE PAS MODIFIER — Envoi de message
// ============================================================
void envoyerMessage(const String& msg)
{
#ifdef COMM_WIFI
    if (client && client.connected()) client.println(msg);
#endif
#ifdef COMM_BT
    SerialBT.println(msg);
#endif
}
// ============================================================


// ============================================================
// TODO 6 — Traitement commandes RPi
//
// Commandes attendues :
//   {"commande":"demarrer"}
//   {"commande":"arreter"}
//
// Répondez avec {"statut":"ok","etat":"<RECHERCHE|ATTAQUE|DEFENSE|ARRET>"}
// ============================================================
void traiterCommande(const String& json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    const char* cmd = doc["commande"] | "";

    // TODO 6 — Implémentez "demarrer" et "arreter"


}
// ============================================================


void setup()
{
    Serial.begin(115200);

    // === NE PAS MODIFIER — Moteurs ===
    pinMode(STBY, OUTPUT);
    digitalWrite(STBY, HIGH);
    moteurG = new ControleurMoteur(PWMA, AIN1, AIN2, 0);
    moteurD = new ControleurMoteur(PWMB, BIN1, BIN2, 1);
    robot   = new ControleurRobot(moteurG, moteurD);
    // ==================================

    // === NE PAS MODIFIER — Capteurs ligne ===
    pinMode(LIGNE_AV_G, INPUT);
    pinMode(LIGNE_AV_D, INPUT);
    // ========================================

    // === NE PAS MODIFIER — TOF I2C ===
    Wire.begin();
    // Activation séquentielle via XSHUT pour assigner adresse unique
    pinMode(XSHUT_AV_G, OUTPUT); digitalWrite(XSHUT_AV_G, LOW);
    pinMode(XSHUT_AV_C, OUTPUT); digitalWrite(XSHUT_AV_C, LOW);
    pinMode(XSHUT_AV_D, OUTPUT); digitalWrite(XSHUT_AV_D, LOW);
    delay(10);

    // Activer TOF avant-centre → adresse 0x30
    digitalWrite(XSHUT_AV_C, HIGH); delay(10);
    tofAvantC.init(); tofAvantC.setAddress(0x30);
    tofAvantC.startContinuous(20);

    // (Ajoutez tofAvantG et tofAvantD si besoin — même procédé)
    // ==================================

    // === NE PAS MODIFIER — Combat ===
    combat = new ControleurCombat(robot, &tofAvantC);
    // =================================

#ifdef COMM_WIFI
    WiFi.begin("RobotSumo", "sumo2026");
    Serial.print("Connexion WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nIP : " + WiFi.localIP().toString());
    serveur.begin();
#endif
#ifdef COMM_BT
    SerialBT.begin("RobotSumo");
    Serial.println("Bluetooth prêt");
#endif
}


void loop()
{
#ifdef COMM_WIFI
    if (!client || !client.connected()) client = serveur.available();
    if (client && client.available())
    {
        String msg = client.readStringUntil('\n');
        traiterCommande(msg);
    }
#endif
#ifdef COMM_BT
    if (SerialBT.available())
    {
        String msg = SerialBT.readStringUntil('\n');
        traiterCommande(msg);
    }
#endif

    // === NE PAS MODIFIER — Boucle de combat ===
    if (combat) combat->mettreAJour();
    // ===========================================
}

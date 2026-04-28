/**
 * Laboratoire 6 — Combat !!
 * Fichier : Models/CommandeRobot.cs
 *
 * Commandes vers MainMotorController (firmware Sanjo Sumo) :
 *   CommandeRobot.ModeAuto()            -> {"commande":"mode_auto"}
 *   CommandeRobot.ModeManuel()          -> {"commande":"mode_manuel"}
 *   CommandeRobot.ModeLigne()           -> {"commande":"mode_ligne"}
 *   CommandeRobot.Arreter()             -> {"commande":"arreter"}
 *   CommandeRobot.Avancer(1000)         -> {"commande":"avancer","duree_ms":1000}
 *   CommandeRobot.Reculer(1000)         -> {"commande":"reculer","duree_ms":1000}
 *   CommandeRobot.PivoterGauche(500)    -> {"commande":"pivoter_gauche","duree_ms":500}
 *   CommandeRobot.PivoterDroite(500)    -> {"commande":"pivoter_droite","duree_ms":500}
 *   CommandeRobot.SetVitesse(70)        -> {"commande":"set_vitesse","valeur":70}
 */

using System.Text.Json;
using System.Text.Json.Serialization;

namespace AvaloniaCombat.Models
{
    public class CommandeRobot
    {
        [JsonPropertyName("commande")]
        public string Commande { get; set; } = "";

        [JsonPropertyName("duree_ms")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int DureeMs { get; set; }

        [JsonPropertyName("vitesse")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int Vitesse { get; set; }

        [JsonPropertyName("valeur")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public object? Valeur { get; set; }

        public string VersJson() => JsonSerializer.Serialize(this);

        public static CommandeRobot ModeAuto()   => new() { Commande = "mode_auto" };
        public static CommandeRobot ModeManuel() => new() { Commande = "mode_manuel" };
        public static CommandeRobot ModeLigne()  => new() { Commande = "mode_ligne" };
        public static CommandeRobot Arreter()    => new() { Commande = "arreter" };

        public static CommandeRobot Avancer(int dureeMs = 0, int vitesse = 0)
            => new() { Commande = "avancer", DureeMs = dureeMs, Vitesse = vitesse };

        public static CommandeRobot Reculer(int dureeMs = 0, int vitesse = 0)
            => new() { Commande = "reculer", DureeMs = dureeMs, Vitesse = vitesse };

        public static CommandeRobot PivoterGauche(int dureeMs = 0)
            => new() { Commande = "pivoter_gauche", DureeMs = dureeMs };

        public static CommandeRobot PivoterDroite(int dureeMs = 0)
            => new() { Commande = "pivoter_droite", DureeMs = dureeMs };

        public static CommandeRobot SetVitesse(int pct)
            => new() { Commande = "set_vitesse", Valeur = pct };
    }
}

/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : Models/CommandeRobot.cs
 *
 * Format commande attendu par l'ESP32 (firmware Lab 4) :
 *   {"commande":"urgence","vitesse":0,"duree_ms":0}
 */

using System.Text.Json;
using System.Text.Json.Serialization;

namespace AvaloniaAsservissement.Models
{
    public class CommandeRobot
    {
        [JsonPropertyName("commande")]
        public string Commande { get; set; } = "";

        [JsonPropertyName("vitesse")]
        public int Vitesse { get; set; }

        [JsonPropertyName("duree_ms")]
        public int DureeMs { get; set; }

        [JsonPropertyName("kp")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public float Kp { get; set; }

        [JsonPropertyName("kd")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public float Kd { get; set; }

        [JsonPropertyName("vbase")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int VBase { get; set; }

        [JsonPropertyName("vmin")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int VMin { get; set; }

        [JsonPropertyName("vmax")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int VMax { get; set; }

        [JsonPropertyName("seuil")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int Seuil { get; set; }

        public string VersJson() => JsonSerializer.Serialize(this);

        public static CommandeRobot Urgence()
            => new() { Commande = "urgence", Vitesse = 0, DureeMs = 0 };

        public static CommandeRobot DemiTour(string direction)
            => new() { Commande = direction, Vitesse = 200, DureeMs = 500 };

        public static CommandeRobot SuivreLigne()
            => new() { Commande = "suivre_ligne" };

        public static CommandeRobot Tuning(float kp, float kd, int vbase, int vmin, int vmax, int seuil)
            => new() { Commande = "tuning", Kp = kp, Kd = kd, VBase = vbase, VMin = vmin, VMax = vmax, Seuil = seuil };
    }
}

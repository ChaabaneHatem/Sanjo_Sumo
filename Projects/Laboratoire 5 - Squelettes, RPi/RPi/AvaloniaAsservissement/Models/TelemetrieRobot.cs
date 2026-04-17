/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : Models/TelemetrieRobot.cs
 *
 * === NE PAS MODIFIER ===
 * Modele de donnees pour la telemetrie recue de l'ESP32.
 *
 * Format JSON recu :
 *   {"type":"tel","ligne":[1,0,0],"ligne_raw":[500,2500,800],
 *    "bat_mv":3943,"rpm_g":45,"rpm_d":47,"routine":"arret",
 *    "kp":120,"kd":300,...}
 *
 * Proprietes cles pour le labo 5 :
 *   Ligne     → tableau binaire [avG, avD, arr] (1 = blanc detecte)
 *   LigneRaw  → valeurs ADC brutes [avG, avD, arr] (0-4095)
 *   BatMv     → tension batterie en millivolts
 *   RpmG/RpmD → vitesse moteurs en RPM
 *   Routine   → nom de la routine active ("arret", "demitour_g", ...)
 */

using System.Text.Json;
using System.Text.Json.Serialization;

namespace AvaloniaAsservissement.Models
{
    public class TelemetrieRobot
    {
        [JsonPropertyName("type")]
        public string Type { get; set; } = "";

        [JsonPropertyName("ligne")]
        public int[]? Ligne { get; set; }

        [JsonPropertyName("ligne_raw")]
        public int[]? LigneRaw { get; set; }

        [JsonPropertyName("bat_mv")]
        public int BatMv { get; set; }

        [JsonPropertyName("routine")]
        public string Routine { get; set; } = "arret";

        [JsonPropertyName("enc_g")]
        public long EncG { get; set; }

        [JsonPropertyName("enc_d")]
        public long EncD { get; set; }

        [JsonPropertyName("rpm_g")]
        public int RpmG { get; set; }

        [JsonPropertyName("rpm_d")]
        public int RpmD { get; set; }

        /// <summary>
        /// Deserialise une ligne JSON en TelemetrieRobot.
        /// Retourne null si le type n'est pas "tel" ou si le JSON est invalide.
        /// </summary>
        public static TelemetrieRobot? DepuisJson(string json)
        {
            try
            {
                var t = JsonSerializer.Deserialize<TelemetrieRobot>(json);
                return (t?.Type == "tel") ? t : null;
            }
            catch { return null; }
        }
    }
}

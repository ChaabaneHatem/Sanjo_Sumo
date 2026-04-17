/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : Models/LectureCapteur.cs
 *
 * Modele pour les messages capteurs individuels recus de l'ESP32.
 * Format JSON : {"type":"ligne","nom":"avant_gauche","brut":1024,"traite":"1024"}
 */

using System.Text.Json;
using System.Text.Json.Serialization;

namespace AvaloniaAsservissement.Models
{
    public class LectureCapteur
    {
        [JsonPropertyName("type")]
        public string Type { get; set; } = "";

        [JsonPropertyName("nom")]
        public string Nom { get; set; } = "";

        [JsonPropertyName("brut")]
        public int Brut { get; set; }

        [JsonPropertyName("traite")]
        public string Traite { get; set; } = "";

        public static LectureCapteur? DepuisJson(string json)
        {
            try { return JsonSerializer.Deserialize<LectureCapteur>(json); }
            catch { return null; }
        }
    }
}

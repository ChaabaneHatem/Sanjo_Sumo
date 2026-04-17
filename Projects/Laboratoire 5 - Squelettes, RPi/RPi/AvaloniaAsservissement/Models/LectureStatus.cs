/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : Models/LectureStatus.cs
 *
 * Format JSON recu :
 *   {"type":"status","direction":"AVANT","vitesse_L":18.5,"vitesse_R":18.5,
 *    "xbox":true,"wifi":true,"gear":3,"vitesse_pct":50,
 *    "vbat":7.82,"courant_mA":116,"puissance_W":0.9}
 */

using System.Text.Json;
using System.Text.Json.Serialization;

namespace AvaloniaAsservissement.Models
{
    public class LectureStatus
    {
        [JsonPropertyName("type")]
        public string Type { get; set; } = "";

        [JsonPropertyName("direction")]
        public string Direction { get; set; } = "";

        [JsonPropertyName("vitesse_L")]
        public float VitesseL { get; set; }

        [JsonPropertyName("vitesse_R")]
        public float VitesseR { get; set; }

        [JsonPropertyName("xbox")]
        public bool Xbox { get; set; }

        [JsonPropertyName("gear")]
        public int Gear { get; set; }

        [JsonPropertyName("vitesse_pct")]
        public int VitessePct { get; set; }

        [JsonPropertyName("vbat")]
        public float Vbat { get; set; }

        [JsonPropertyName("courant_mA")]
        public int CourantMa { get; set; }

        [JsonPropertyName("puissance_W")]
        public float PuissanceW { get; set; }

        public static LectureStatus? DepuisJson(string json)
        {
            try { return JsonSerializer.Deserialize<LectureStatus>(json); }
            catch { return null; }
        }
    }
}

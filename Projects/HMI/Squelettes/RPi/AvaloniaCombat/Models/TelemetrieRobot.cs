/**
 * Laboratoire 6 — Combat !!
 * Fichier : Models/TelemetrieRobot.cs
 *
 * Modèle unifié pour les messages JSON multi-types envoyés par l'ESP32.
 *
 * Types reçus :
 *   {"type":"tof",    "nom":"avant_gauche","brut":342,"traite":"342 mm"}
 *   {"type":"ligne",  "nom":"avant_centre","brut":2048,"traite":"2048"}
 *   {"type":"status", "direction":"AVANT", "vitesse_L":45.2, "vbat":8.2, ...}
 *   {"statut":"ok"}   ← réponse commande (pas de "type")
 *
 * Champs FSM (à ajouter par le firmware dans "status") :
 *   "etat"      : "ARRET" | "RECHERCHE" | "ATTAQUE" | "DEFENSE"
 *   "strategie" : "Rotation" | "Charge" | "Recul" | "Evitement"
 */

using System.Text.Json;
using System.Text.Json.Serialization;

namespace AvaloniaCombat.Models
{
    public class TelemetrieRobot
    {
        // ── Champ discriminant ───────────────────────
        [JsonPropertyName("type")]
        public string? Type { get; set; }

        // ── Réponse commande ─────────────────────────
        [JsonPropertyName("statut")]
        public string? Statut { get; set; }

        // ── FSM (ajoutés par le firmware dans "status") ──
        [JsonPropertyName("etat")]
        public string Etat { get; set; } = "ARRET";

        [JsonPropertyName("strategie")]
        public string? Strategie { get; set; }

        // ── Capteur individuel (TOF ou ligne) ────────
        [JsonPropertyName("nom")]
        public string? Nom { get; set; }

        [JsonPropertyName("brut")]
        public int Brut { get; set; }

        [JsonPropertyName("traite")]
        public string? Traite { get; set; }

        // ── Statut général (type="status") ───────────
        [JsonPropertyName("direction")]
        public string? Direction { get; set; }

        [JsonPropertyName("vitesse_L")]
        public float VitesseL { get; set; }

        [JsonPropertyName("vitesse_R")]
        public float VitesseR { get; set; }

        [JsonPropertyName("xbox")]
        public bool Xbox { get; set; }

        [JsonPropertyName("wifi")]
        public bool Wifi { get; set; }

        [JsonPropertyName("mode")]
        public string? Mode { get; set; }

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

        public static TelemetrieRobot? DepuisJson(string json)
        {
            try { return JsonSerializer.Deserialize<TelemetrieRobot>(json); }
            catch { return null; }
        }
    }
}

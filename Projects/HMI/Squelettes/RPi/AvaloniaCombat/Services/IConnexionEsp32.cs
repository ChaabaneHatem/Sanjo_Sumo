/**
 * Laboratoire 6 — Combat !!
 * Fichier : Services/IConnexionEsp32.cs
 *
 * === NE PAS MODIFIER ===
 * Interface de communication avec l'ESP32.
 * Permet de changer de transport (TCP, MQTT, etc.) sans modifier le ViewModel.
 */

using System.Threading.Tasks;

namespace AvaloniaCombat.Services
{
    interface IConnexionEsp32
    {
        Task ConnecterAsync();
        Task EnvoyerAsync(string messageJson);
        Task<string?> RecevoirAsync();
        void Deconnecter();
    }
}

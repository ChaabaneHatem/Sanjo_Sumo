/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : Services/ConnexionService.cs
 *
 * === NE PAS MODIFIER ===
 * Adaptateur entre le ViewModel et IConnexionEsp32.
 * Gere la creation de la connexion TCP avec l'adresse IP fournie.
 *
 * Inclut un scanner reseau pour trouver les ESP32 sur le sous-reseau 10.42.0.x.
 */

using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Threading.Tasks;

namespace AvaloniaAsservissement.Services
{
    public class ConnexionService
    {
        private IConnexionEsp32? _connexion;
        private readonly int _port;

        public ConnexionService(int port = 8080) { _port = port; }

        public Task ConnecterAsync(string adresseIp)
        {
            _connexion = new ConnexionTcp(adresseIp, _port);
            return _connexion.ConnecterAsync();
        }

        public Task          EnvoyerAsync(string message) => _connexion!.EnvoyerAsync(message);
        public Task<string?> RecevoirAsync()               => _connexion!.RecevoirAsync();
        public void          Deconnecter()                 => _connexion?.Deconnecter();

        /// <summary>
        /// Scanne le sous-reseau 10.42.0.x pour trouver les ESP32
        /// qui ecoutent sur le port TCP specifie.
        /// </summary>
        public static async Task<List<string>> ScannerReseau(int port = 8080, int timeoutMs = 300)
        {
            var resultats = new List<string>();
            var taches = new List<Task>();

            for (int i = 1; i < 255; i++)
            {
                string ip = $"10.42.0.{i}";
                taches.Add(Task.Run(async () =>
                {
                    try
                    {
                        using var client = new TcpClient();
                        var connectTask = client.ConnectAsync(ip, port);
                        if (await Task.WhenAny(connectTask, Task.Delay(timeoutMs)) == connectTask
                            && client.Connected)
                        {
                            lock (resultats) { resultats.Add(ip); }
                        }
                    }
                    catch { }
                }));
            }

            await Task.WhenAll(taches);
            resultats.Sort();
            return resultats;
        }
    }
}

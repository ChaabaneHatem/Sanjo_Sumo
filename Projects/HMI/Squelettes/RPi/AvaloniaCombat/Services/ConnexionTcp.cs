/**
 * Laboratoire 6 — Combat !!
 * Fichier : Services/ConnexionTcp.cs
 *
 * === NE PAS MODIFIER ===
 * Implementation TCP de IConnexionEsp32.
 * Connexion keep-alive avec timeout de 3 secondes.
 * Lecture bufferisee ligne par ligne (JSON termine par \n).
 */

using System;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace AvaloniaCombat.Services
{
    class ConnexionTcp : IConnexionEsp32
    {
        private readonly string _adresseIp;
        private readonly int    _port;
        private TcpClient?      _client;
        private NetworkStream?  _flux;
        private readonly StringBuilder _tampon = new();

        public ConnexionTcp(string adresseIp, int port)
        {
            _adresseIp = adresseIp;
            _port      = port;
        }

        public async Task ConnecterAsync()
        {
            _client = new TcpClient();
            _client.Client.SetSocketOption(
                SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);
            var connectTask = _client.ConnectAsync(_adresseIp, _port);
            if (await Task.WhenAny(connectTask, Task.Delay(3000)) != connectTask)
            {
                _client.Close();
                throw new TimeoutException("Connexion TCP timeout (3s)");
            }
            await connectTask;
            _flux = _client.GetStream();
            _tampon.Clear();
        }

        public async Task EnvoyerAsync(string messageJson)
        {
            if (_flux is null) return;
            byte[] donnees = Encoding.UTF8.GetBytes(messageJson + "\n");
            await _flux.WriteAsync(donnees);
        }

        public async Task<string?> RecevoirAsync()
        {
            if (_flux is null) return null;
            var buf = new byte[512];

            while (true)
            {
                string contenu = _tampon.ToString();
                int idx = contenu.IndexOf('\n');
                if (idx >= 0)
                {
                    string ligne = contenu.Substring(0, idx).Trim();
                    _tampon.Remove(0, idx + 1);
                    return ligne;
                }

                int n = await _flux.ReadAsync(buf);
                if (n == 0) return null;
                _tampon.Append(Encoding.UTF8.GetString(buf, 0, n));
            }
        }

        public void Deconnecter()
        {
            _flux?.Close();
            _client?.Close();
            _flux   = null;
            _client = null;
            _tampon.Clear();
        }
    }
}

/**
 * Laboratoire 6 — Combat !!
 * Fichier : ViewModels/MainViewModel.cs
 */

using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Windows.Input;
using Avalonia.Media;
using Avalonia.Threading;
using AvaloniaCombat.Models;
using AvaloniaCombat.Services;

namespace AvaloniaCombat.ViewModels
{
    // === NE PAS MODIFIER — RelayCommand ===
    public class RelayCommand : ICommand
    {
        private readonly Action _execute;
        public RelayCommand(Action execute) { _execute = execute; }
        public event EventHandler? CanExecuteChanged;
        public bool CanExecute(object? p) => true;
        public void Execute(object? p) => _execute();
    }
    // ======================================

    public class MainViewModel : INotifyPropertyChanged
    {
        // === NE PAS MODIFIER — Infrastructure MVVM ===
        public event PropertyChangedEventHandler? PropertyChanged;
        protected void NotifierChangement([CallerMemberName] string? nom = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nom));

        private readonly ConnexionService _connexion = new(8080);
        private CancellationTokenSource? _jetonAnnulation;
        private bool _estConnecte = false;
        // =============================================


        // ── Propriétés de connexion ──────────────────

        private string _adresseIp = "10.42.0.53";
        public string AdresseIp
        {
            get => _adresseIp;
            set { _adresseIp = value; NotifierChangement(); }
        }

        private string _statutConnexion = "● Déconnecté";
        public string StatutConnexion
        {
            get => _statutConnexion;
            set { _statutConnexion = value; NotifierChangement(); }
        }

        private IBrush _couleurStatut = Brushes.OrangeRed;
        public IBrush CouleurStatut
        {
            get => _couleurStatut;
            set { _couleurStatut = value; NotifierChangement(); }
        }

        private string _texteBoutonConnexion = "Connecter";
        public string TexteBoutonConnexion
        {
            get => _texteBoutonConnexion;
            set { _texteBoutonConnexion = value; NotifierChangement(); }
        }

        private string _messageStatut = "";
        public string MessageStatut
        {
            get => _messageStatut;
            set { _messageStatut = value; NotifierChangement(); }
        }


        // ── Propriétés de télémétrie ─────────────────

        private string _batterie = "-- V";
        public string Batterie
        {
            get => _batterie;
            set { _batterie = value; NotifierChangement(); }
        }

        private string _tofAvG = "---";
        public string TofAvG { get => _tofAvG; set { _tofAvG = value; NotifierChangement(); } }

        private string _tofAvC = "---";
        public string TofAvC { get => _tofAvC; set { _tofAvC = value; NotifierChangement(); } }

        private string _tofAvD = "---";
        public string TofAvD { get => _tofAvD; set { _tofAvD = value; NotifierChangement(); } }

        private string _tofCtG = "---";
        public string TofCtG { get => _tofCtG; set { _tofCtG = value; NotifierChangement(); } }

        private string _tofCtD = "---";
        public string TofCtD { get => _tofCtD; set { _tofCtD = value; NotifierChangement(); } }

        private IBrush _couleurLigneAvG = Brushes.DimGray;
        public IBrush CouleurLigneAvG { get => _couleurLigneAvG; set { _couleurLigneAvG = value; NotifierChangement(); } }

        private IBrush _couleurLigneAvD = Brushes.DimGray;
        public IBrush CouleurLigneAvD { get => _couleurLigneAvD; set { _couleurLigneAvD = value; NotifierChangement(); } }

        private IBrush _couleurLigneArrC = Brushes.DimGray;
        public IBrush CouleurLigneArrC { get => _couleurLigneArrC; set { _couleurLigneArrC = value; NotifierChangement(); } }


        // ── Propriétés statut robot ──────────────────

        private string _direction = "---";
        public string Direction { get => _direction; set { _direction = value; NotifierChangement(); } }

        private string _vitesseG = "-- cm/s";
        public string VitesseG { get => _vitesseG; set { _vitesseG = value; NotifierChangement(); } }

        private string _vitesseD = "-- cm/s";
        public string VitesseD { get => _vitesseD; set { _vitesseD = value; NotifierChangement(); } }

        private IBrush _couleurXbox = Brushes.DimGray;
        public IBrush CouleurXbox { get => _couleurXbox; set { _couleurXbox = value; NotifierChangement(); } }

        private IBrush _couleurWifi = Brushes.DimGray;
        public IBrush CouleurWifi { get => _couleurWifi; set { _couleurWifi = value; NotifierChangement(); } }

        private int _gear = 0;
        public int Gear { get => _gear; set { _gear = value; NotifierChangement(); } }

        private int _vitessePct = 0;
        public int VitessePct { get => _vitessePct; set { _vitessePct = value; NotifierChangement(); } }


        // ── Propriétés FSM / Mode ────────────────────

        private string _etatFsm = "---";
        public string EtatFsm
        {
            get => _etatFsm;
            set { _etatFsm = value; NotifierChangement(); }
        }

        private IBrush _couleurEtat = Brushes.Gray;
        public IBrush CouleurEtat
        {
            get => _couleurEtat;
            set { _couleurEtat = value; NotifierChangement(); }
        }

        private IBrush _couleurFondEtat = new SolidColorBrush(Color.Parse("#161B22"));
        public IBrush CouleurFondEtat
        {
            get => _couleurFondEtat;
            set { _couleurFondEtat = value; NotifierChangement(); }
        }

        private string _strategieActive = "---";
        public string StrategieActive
        {
            get => _strategieActive;
            set { _strategieActive = value; NotifierChangement(); }
        }


        // ── Commandes ────────────────────────────────

        public ICommand CommandeConnexion           { get; }
        public ICommand CommandeDemarrer            { get; }
        public ICommand CommandeArreter             { get; }
        public ICommand CommandeUrgence             { get; }
        public ICommand CommandeStrategieCharge     { get; }
        public ICommand CommandeStrategieCirculaire { get; }
        public ICommand CommandeStrategieRecul      { get; }
        public ICommand CommandeStrategieEvitement  { get; }

        public MainViewModel()
        {
            CommandeConnexion           = new RelayCommand(BasculerConnexion);
            CommandeDemarrer            = new RelayCommand(() => Envoyer(CommandeRobot.ModeAuto()));
            CommandeArreter             = new RelayCommand(() => Envoyer(CommandeRobot.Arreter()));
            CommandeUrgence             = new RelayCommand(() => Envoyer(CommandeRobot.Arreter()));
            CommandeStrategieCharge     = new RelayCommand(() => Envoyer(CommandeRobot.ModeManuel()));
            CommandeStrategieCirculaire = new RelayCommand(() => Envoyer(CommandeRobot.ModeLigne()));
            CommandeStrategieRecul      = new RelayCommand(() => Envoyer(CommandeRobot.Avancer(1000)));
            CommandeStrategieEvitement  = new RelayCommand(() => Envoyer(CommandeRobot.Reculer(1000)));
        }


        // ── Logique de connexion ─────────────────────

        private async void BasculerConnexion()
        {
            if (!_estConnecte)
            {
                try
                {
                    await _connexion.ConnecterAsync(_adresseIp);
                    _estConnecte = true;
                    StatutConnexion       = "● Connecté";
                    CouleurStatut         = Brushes.LightGreen;
                    TexteBoutonConnexion  = "Déconnecter";
                    MessageStatut         = "Connexion établie";
                    _jetonAnnulation      = new CancellationTokenSource();
                    DemarrerLectureAsync();
                }
                catch (Exception ex)
                {
                    MessageStatut = $"Erreur : {ex.Message}";
                }
            }
            else
            {
                _jetonAnnulation?.Cancel();
                _connexion.Deconnecter();
                _estConnecte         = false;
                StatutConnexion      = "● Déconnecté";
                CouleurStatut        = Brushes.OrangeRed;
                TexteBoutonConnexion = "Connecter";
                MessageStatut        = "Déconnecté";
            }
        }

        private async void DemarrerLectureAsync()
        {
            var jeton = _jetonAnnulation!.Token;
            try
            {
                while (!jeton.IsCancellationRequested)
                {
                    string? json = await _connexion.RecevoirAsync();
                    if (json == null) break;

                    var t = TelemetrieRobot.DepuisJson(json);
                    if (t == null) continue;

                    await Dispatcher.UIThread.InvokeAsync(() => TraiterMessage(t));
                }
            }
            catch (OperationCanceledException) { }
            catch (Exception ex)
            {
                await Dispatcher.UIThread.InvokeAsync(() =>
                    MessageStatut = $"Erreur lecture : {ex.Message}");
            }
        }

        private void TraiterMessage(TelemetrieRobot t)
        {
            switch (t.Type)
            {
                case "tof":
                    var tofVal = t.Brut == 0 || t.Brut >= 8000 ? "---" : $"{t.Brut} mm";
                    switch (t.Nom)
                    {
                        case "avant_gauche": TofAvG = tofVal; break;
                        case "avant_centre": TofAvC = tofVal; break;
                        case "avant_droite": TofAvD = tofVal; break;
                        case "cote_gauche":  TofCtG = tofVal; break;
                        case "cote_droit":   TofCtD = tofVal; break;
                    }
                    break;

                case "ligne":
                    IBrush couleur = t.Brut < 800 ? Brushes.Red : Brushes.DimGray;
                    switch (t.Nom)
                    {
                        case "avant_gauche": CouleurLigneAvG  = couleur; break;
                        case "avant_centre": CouleurLigneAvD  = couleur; break;
                        case "avant_droite": CouleurLigneArrC = couleur; break;
                    }
                    break;

                case "status":
                    Batterie    = t.Vbat > 0 ? $"{t.Vbat:F1} V" : "-- V";
                    Direction   = t.Direction ?? "---";
                    VitesseG    = $"{t.VitesseL:F1} cm/s";
                    VitesseD    = $"{t.VitesseR:F1} cm/s";
                    Gear        = t.Gear;
                    VitessePct  = t.VitessePct;
                    CouleurXbox = t.Xbox ? Brushes.LightGreen : Brushes.DimGray;
                    CouleurWifi = t.Wifi ? Brushes.LightGreen : Brushes.DimGray;
                    if (t.Mode != null)
                        MettreAJourFsm(t.Mode);
                    break;

                // Réponse commande {"statut":"ok"} — ignorer silencieusement
            }
        }

        private void MettreAJourFsm(string mode)
        {
            EtatFsm         = mode.ToUpper();
            StrategieActive = Gear > 0 ? $"Gear {Gear} — {VitessePct}%" : "---";

            CouleurEtat = EtatFsm switch
            {
                "AUTO"   => new SolidColorBrush(Color.Parse("#FFD700")),
                "LIGNE"  => new SolidColorBrush(Color.Parse("#4488FF")),
                "MANUEL" => new SolidColorBrush(Color.Parse("#888888")),
                _        => Brushes.Gray
            };

            CouleurFondEtat = EtatFsm switch
            {
                "AUTO"   => new SolidColorBrush(Color.Parse("#1C1A00")),
                "LIGNE"  => new SolidColorBrush(Color.Parse("#00001C")),
                _        => new SolidColorBrush(Color.Parse("#161B22"))
            };
        }

        private async void Envoyer(CommandeRobot cmd)
        {
            if (!_estConnecte) return;
            try
            {
                await _connexion.EnvoyerAsync(cmd.VersJson());
                MessageStatut = $"Envoyé : {cmd.Commande}";
            }
            catch (Exception ex)
            {
                MessageStatut = $"Erreur envoi : {ex.Message}";
            }
        }
    }
}

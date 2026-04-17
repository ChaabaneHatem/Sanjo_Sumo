/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : ViewModels/MainViewModel.cs
 *
 * ViewModel principal (pattern MVVM).
 * Implemente INotifyPropertyChanged pour notifier la vue des changements.
 *
 * Completez les sections marquees TODO.
 * Ne modifiez pas les sections marquees NE PAS MODIFIER.
 *
 * Rappel MVVM :
 *   - Le ViewModel ne connait pas la Vue (pas de reference a un controle)
 *   - Toute modification d'une propriete doit appeler OnChanged()
 *   - Les mises a jour depuis un thread secondaire (reseau) utilisent
 *     Dispatcher.UIThread.InvokeAsync() pour toucher le thread UI
 *
 * Ce ViewModel gere :
 *   - La connexion TCP a l'ESP32 (via ConnexionService)
 *   - L'affichage des capteurs de ligne (ADC brut + indicateurs couleur)
 *   - Le tuning PD en temps reel (Kp, Kd, Vbase, Vmin, Vmax, Seuil)
 *   - Le lancement des routines (demi-tour G/D)
 */

using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia.Media;
using Avalonia.Threading;
using AvaloniaAsservissement.Models;
using AvaloniaAsservissement.Services;

namespace AvaloniaAsservissement.ViewModels
{
    // ============================================================
    // NE PAS MODIFIER — ICommand minimal (RelayCommand)
    //
    // Permet de lier une methode a un bouton via {Binding ...}
    // sans dependance externe.
    // ============================================================
    public class RelayCommand : ICommand
    {
        private readonly Action _execute;

        public RelayCommand(Action execute) { _execute = execute; }

        public event EventHandler? CanExecuteChanged;

        public bool CanExecute(object? parameter) => true;

        public void Execute(object? parameter) => _execute();
    }
    // ============================================================


    public class MainViewModel : INotifyPropertyChanged
    {
        // === NE PAS MODIFIER — Infrastructure INotifyPropertyChanged ===
        public event PropertyChangedEventHandler? PropertyChanged;

        protected void OnChanged([CallerMemberName] string? nom = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nom));
        }
        // ===============================================================


        // === NE PAS MODIFIER — Service de connexion ===
        private readonly ConnexionService _service = new(8080);
        private CancellationTokenSource? _jeton;
        private bool _estConnecte = false;
        // ==============================================


        // ============================================================
        // TODO 1 — Propriete AdresseIp (string)
        // ============================================================
        private string _adresseIp = "10.42.0.X";
        public string AdresseIp
        {
            get => _adresseIp;
            set { _adresseIp = value; OnChanged(); }
        }



        // ============================================================
        // TODO 2 — Proprietes de connexion (4 proprietes)
        // ============================================================
        private string _statutConnexion = "Déconnecté";
        public string StatutConnexion
        {
            get => _statutConnexion;
            set { _statutConnexion = value; OnChanged(); }
        }

        private IBrush _couleurStatut = Brushes.OrangeRed;
        public IBrush CouleurStatut
        {
            get => _couleurStatut;
            set { _couleurStatut = value; OnChanged(); }
        }

        private string _texteBoutonConnexion = "Se connecter";
        public string TexteBoutonConnexion
        {
            get => _texteBoutonConnexion;
            set { _texteBoutonConnexion = value; OnChanged(); }
        }

        private string _messageStatut = "Entrez l'IP et connectez-vous.";
        public string MessageStatut
        {
            get => _messageStatut;
            set { _messageStatut = value; OnChanged(); }
        }



        // ============================================================
        // TODO 3 — Proprietes des capteurs de ligne (6 proprietes)
        // ============================================================
        private string _rawAvG = "----";
        public string RawAvG { get => _rawAvG; set { _rawAvG = value; OnChanged(); } }

        private string _rawAvD = "----";
        public string RawAvD { get => _rawAvD; set { _rawAvD = value; OnChanged(); } }

        private string _rawAvC = "----";
        public string RawAvC { get => _rawAvC; set { _rawAvC = value; OnChanged(); } }

        private IBrush _couleurAvG = Brushes.Gray;
        public IBrush CouleurAvG { get => _couleurAvG; set { _couleurAvG = value; OnChanged(); } }

        private IBrush _couleurAvD = Brushes.Gray;
        public IBrush CouleurAvD { get => _couleurAvD; set { _couleurAvD = value; OnChanged(); } }

        private IBrush _couleurAvC = Brushes.Gray;
        public IBrush CouleurAvC { get => _couleurAvC; set { _couleurAvC = value; OnChanged(); } }



        // ============================================================
        // TODO 4 — Proprietes vitesse moteurs + routine + batterie
        // ============================================================
        private string _rpmG = "----";
        public string RpmG { get => _rpmG; set { _rpmG = value; OnChanged(); } }

        private string _rpmD = "----";
        public string RpmD { get => _rpmD; set { _rpmD = value; OnChanged(); } }

        private string _routineStatus = "arret";
        public string RoutineStatus { get => _routineStatus; set { _routineStatus = value; OnChanged(); } }

        private string _batterie = "--.- V";
        public string Batterie { get => _batterie; set { _batterie = value; OnChanged(); } }

        private string _direction = "--";
        public string Direction { get => _direction; set { _direction = value; OnChanged(); } }

        private string _vitesses = "-- / --";
        public string Vitesses { get => _vitesses; set { _vitesses = value; OnChanged(); } }



        // ============================================================
        // TODO 5 — Proprietes de tuning PD (6 paires valeur + texte)
        // ============================================================
        private double _kp = 50;
        public double Kp
        {
            get => _kp;
            set { _kp = value; OnChanged(); OnChanged(nameof(KpTexte)); }
        }
        public string KpTexte => $"{_kp:F0}";

        private double _kd = 100;
        public double Kd
        {
            get => _kd;
            set { _kd = value; OnChanged(); OnChanged(nameof(KdTexte)); }
        }
        public string KdTexte => $"{_kd:F0}";

        private int _vBase = 140;
        public int VBase
        {
            get => _vBase;
            set { _vBase = value; OnChanged(); OnChanged(nameof(VBaseTexte)); }
        }
        public string VBaseTexte => $"{_vBase}";

        private int _vMin = 40;
        public int VMin
        {
            get => _vMin;
            set { _vMin = value; OnChanged(); OnChanged(nameof(VMinTexte)); }
        }
        public string VMinTexte => $"{_vMin}";

        private int _vMax = 255;
        public int VMax
        {
            get => _vMax;
            set { _vMax = value; OnChanged(); OnChanged(nameof(VMaxTexte)); }
        }
        public string VMaxTexte => $"{_vMax}";

        private int _seuil = 1000;
        public int Seuil
        {
            get => _seuil;
            set { _seuil = value; OnChanged(); OnChanged(nameof(SeuilTexte)); }
        }
        public string SeuilTexte => $"{_seuil}";



        // ============================================================
        // TODO 6 — Commandes ICommand (6 commandes)
        //
        // Declarez 6 proprietes ICommand (readonly) :
        //   CommandeConnexion   → appelle BasculerConnexion()
        //   CommandeScanner     → appelle LancerScan()
        //   CommandeDemiTourG   → appelle LancerRoutine("demitour_g")
        //   CommandeDemiTourD   → appelle LancerRoutine("demitour_d")
        //   CommandeStop        → appelle Envoyer(CommandeRobot.Urgence().VersJson())
        //   CommandeTuning      → appelle EnvoyerTuning()
        //
        // Initialisez-les dans le constructeur avec new RelayCommand(...)
        // ============================================================
        public ICommand CommandeConnexion   { get; }
        public ICommand CommandeScanner     { get; }
        public ICommand CommandeDemiTourG   { get; }
        public ICommand CommandeDemiTourD   { get; }
        public ICommand CommandeStop        { get; }
        public ICommand CommandeTuning      { get; }
        public ICommand CommandeSuivreLigne { get; }


        // === NE PAS MODIFIER — Constructeur ===
        public MainViewModel()
        {
            // TODO 6 — Initialisez les commandes ici (remplacez les null)
            CommandeConnexion = new RelayCommand(BasculerConnexion);
            CommandeScanner   = new RelayCommand(LancerScan);
            CommandeDemiTourG = new RelayCommand(() => Envoyer(CommandeRobot.DemiTour("pivoter_gauche").VersJson()));
            CommandeDemiTourD = new RelayCommand(() => Envoyer(CommandeRobot.DemiTour("pivoter_droite").VersJson()));
            CommandeStop      = new RelayCommand(() => Envoyer(CommandeRobot.Urgence().VersJson()));
            CommandeTuning      = new RelayCommand(EnvoyerTuning);
            CommandeSuivreLigne = new RelayCommand(() => Envoyer(CommandeRobot.SuivreLigne().VersJson()));
        }
        // ======================================


        // ============================================================
        // TODO 7 — Methode LancerScan()
        //
        // Scanne le reseau pour trouver les ESP32.
        // Si deja connecte, ne rien faire.
        //
        //   1. Afficher "Scan 10.42.0.x..." dans MessageStatut
        //   2. Appeler ConnexionService.ScannerReseau(8080, 400)
        //   3. Si resultats non vides, mettre le premier dans AdresseIp
        //   4. Afficher les resultats dans MessageStatut
        //
        // Rappel : cette methode est async void (declenchee par un bouton).
        // Les mises a jour UI doivent passer par Dispatcher.UIThread.InvokeAsync.
        // ============================================================
        private async void LancerScan()
        {
            if (_estConnecte) return;
            MessageStatut = "Scan 10.42.0.x...";
            var resultats = await ConnexionService.ScannerReseau(8080, 400);
            await Dispatcher.UIThread.InvokeAsync(() =>
            {
                if (resultats.Count > 0)
                {
                    AdresseIp = resultats[0];
                    MessageStatut = $"Trouvé : {string.Join(", ", resultats)}";
                }
                else
                {
                    MessageStatut = "Aucun ESP32 trouvé sur 10.42.0.x";
                }
            });
        }


        // ============================================================
        // TODO 8 — Methode BasculerConnexion()
        //
        // Si deconnecte :
        //   - Afficher "Connexion a {AdresseIp}:8080..." dans MessageStatut
        //   - Appeler await _service.ConnecterAsync(AdresseIp)
        //   - Mettre a jour : _estConnecte, StatutConnexion, CouleurStatut,
        //     TexteBoutonConnexion, MessageStatut
        //   - Demarrer la boucle de lecture : DemarrerLecture()
        //
        // Si connecte :
        //   - Annuler le jeton (_jeton?.Cancel())
        //   - Appeler _service.Deconnecter()
        //   - Mettre a jour les proprietes (etat deconnecte)
        //
        // Enveloppez dans un try/catch et mettez MessageStatut en cas d'erreur.
        // ============================================================
        private async void BasculerConnexion()
        {
            if (!_estConnecte)
            {
                try
                {
                    MessageStatut = $"Connexion a {AdresseIp}:8080...";
                    await _service.ConnecterAsync(AdresseIp);

                    _estConnecte = true;
                    StatutConnexion      = "Connecté";
                    CouleurStatut        = Brushes.LightGreen;
                    TexteBoutonConnexion = "Se déconnecter";
                    MessageStatut = "Connecté — télémétrie en cours...";
                    DemarrerLecture();
                }
                catch (Exception ex)
                {
                    MessageStatut = $"Erreur : {ex.Message}";
                }
            }
            else
            {
                _jeton?.Cancel();
                _service.Deconnecter();
                _estConnecte = false;
                StatutConnexion      = "Déconnecté";
                CouleurStatut        = Brushes.OrangeRed;
                TexteBoutonConnexion = "Se connecter";
                MessageStatut = "Déconnecté.";
            }
        }


        // ============================================================
        // TODO 9 — Methode DemarrerLecture()
        //
        // Lance une boucle async qui :
        //   1. Cree un nouveau CancellationTokenSource → _jeton
        //   2. Boucle tant que le jeton n'est pas annule :
        //      a. Appelle string? json = await _service.RecevoirAsync()
        //      b. Si null → connexion fermee, lever une IOException
        //      c. Deserialise avec TelemetrieRobot.DepuisJson(json.Trim())
        //      d. Si non null, appelle AppliquerTelemetrie(tel) via
        //         Dispatcher.UIThread.InvokeAsync
        //   3. Gere OperationCanceledException silencieusement (fin normale)
        //   4. Gere les autres exceptions : remet l'etat deconnecte
        //      et affiche le message d'erreur
        // ============================================================
        private async void DemarrerLecture()
        {
            _jeton = new CancellationTokenSource();
            var token = _jeton.Token;

            try
            {
                while (!token.IsCancellationRequested)
                {
                    string? json = await _service.RecevoirAsync();
                    if (json == null) throw new System.IO.IOException("Connexion fermée");

                    var trimmed = json.Trim();
                    var lecture = LectureCapteur.DepuisJson(trimmed);
                    if (lecture != null)
                    {
                        if (lecture.Type == "ligne")
                            await Dispatcher.UIThread.InvokeAsync(() => AppliquerLigne(lecture));
                        else if (lecture.Type == "tof")
                            await Dispatcher.UIThread.InvokeAsync(() => AppliquerTof(lecture));
                    }
                    else
                    {
                        var status = LectureStatus.DepuisJson(trimmed);
                        if (status != null && status.Type == "status")
                            await Dispatcher.UIThread.InvokeAsync(() => AppliquerStatus(status));
                    }


                }
            }
            catch (OperationCanceledException)
            {
                // Fin normale — ne rien faire
            }
            catch (Exception ex)
            {
                await Dispatcher.UIThread.InvokeAsync(() =>
                {
                    _estConnecte = false;
                    StatutConnexion      = "Déconnecté";
                    CouleurStatut        = Brushes.OrangeRed;
                    TexteBoutonConnexion = "Se connecter";
                    MessageStatut = $"Connexion perdue : {ex.Message}";
                });
            }
        }


        // ============================================================
        // TODO 10 — Methode AppliquerTelemetrie(TelemetrieRobot t)
        //
        // Met a jour les proprietes de l'interface a partir de la telemetrie.
        // Cette methode est appelee sur le thread UI (via Dispatcher).
        //
        //   a) Capteurs de ligne (si t.LigneRaw a 3+ elements) :
        //      - RawAvG = valeur formatee (ex. $"{t.LigneRaw[0],4}")
        //      - CouleurAvG = Brushes.Red si t.Ligne[0] == 1, sinon Brushes.DimGray
        //      (idem pour AvD et Arr)
        //
        //   b) Moteurs : RpmG = $"{t.RpmG} RPM", RpmD = $"{t.RpmD} RPM"
        //
        //   c) Routine : RoutineStatus = t.Routine
        //
        //   d) Batterie : Batterie = $"{t.BatMv / 1000.0:F2} V"
        // ============================================================
        private void AppliquerLigne(LectureCapteur l)
        {
            // Blanc détecté si valeur ADC > 2000
            IBrush couleur = l.Brut < 800 ? Brushes.Red : Brushes.DimGray;  // blanc=valeur basse → rouge=bordure
            switch (l.Nom)
            {
                case "avant_gauche":
                    RawAvG = $"{l.Brut,4}"; CouleurAvG = couleur; break;
                case "avant_centre":
                    RawAvC = $"{l.Brut,4}"; CouleurAvC = couleur; break;
                case "avant_droite":
                    RawAvD = $"{l.Brut,4}"; CouleurAvD = couleur; break;
            }
        }

        private void AppliquerTof(LectureCapteur l)
        {
            // TOF affiché dans le message de statut pour l'instant
        }

        private void AppliquerStatus(LectureStatus s)
        {
            Direction = s.Direction;
            Vitesses  = $"{s.VitesseL:F1} / {s.VitesseR:F1} cm/s";
            Batterie  = $"{s.Vbat:F2} V";
        }


        // ============================================================
        // TODO 11 — Methode EnvoyerTuning()
        //
        // Envoie les parametres PD actuels a l'ESP32.
        //   1. Si non connecte, afficher "Non connecte." et sortir
        //   2. Creer la commande : CommandeRobot.Tuning(
        //          (float)Kp, (float)Kd, VBase, VMin, VMax, Seuil)
        //   3. Envoyer via _service.EnvoyerAsync(cmd.VersJson())
        //   4. Afficher confirmation dans MessageStatut
        //
        // Enveloppez dans un try/catch.
        // ============================================================
        private async void EnvoyerTuning()
        {
            if (!_estConnecte) { MessageStatut = "Non connecté."; return; }
            try
            {
                var cmd = CommandeRobot.Tuning((float)Kp, (float)Kd, VBase, VMin, VMax, Seuil);
                await _service.EnvoyerAsync(cmd.VersJson());
                MessageStatut = $"Tuning envoyé — Kp={Kp:F2} Kd={Kd:F2} Vbase={VBase} Vmin={VMin} Vmax={VMax} Seuil={Seuil}";
            }
            catch (Exception ex)
            {
                MessageStatut = $"Erreur envoi tuning : {ex.Message}";
            }
        }


        // ============================================================
        // TODO 12 — Methode LancerRoutine(string nom)
        //
        // Lance une routine de suivi de ligne sur le robot.
        //   1. Si non connecte, afficher "Non connecte." et sortir
        //   2. Envoyer CommandeRobot.Routine(nom).VersJson()
        //      → selectionne la routine sur l'ESP32
        //   3. Envoyer CommandeRobot.Declencheur().VersJson()
        //      → demarre la routine
        //   4. Afficher confirmation dans MessageStatut
        //
        // Enveloppez dans un try/catch.
        // ============================================================


        // === NE PAS MODIFIER — Envoi generique ===
        private async void Envoyer(string json)
        {
            if (!_estConnecte) { MessageStatut = "Non connecte."; return; }
            try { await _service.EnvoyerAsync(json); }
            catch (Exception ex) { MessageStatut = $"Erreur envoi : {ex.Message}"; }
        }
        // ==========================================
    }
}

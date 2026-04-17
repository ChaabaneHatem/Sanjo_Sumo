/**
 * Laboratoire 5 — Asservissement de ligne
 * Fichier : Program.cs
 *
 * === NE PAS MODIFIER ===
 * Point d'entree de l'application Avalonia Desktop.
 */

using Avalonia;

namespace AvaloniaAsservissement
{
    class Program
    {
        public static void Main(string[] args) =>
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);

        public static AppBuilder BuildAvaloniaApp() =>
            AppBuilder.Configure<App>()
                .UsePlatformDetect()
                .LogToTrace();
    }
}

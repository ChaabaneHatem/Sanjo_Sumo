/**
 * Laboratoire 6 — Combat !!
 * Fichier : Program.cs
 *
 * === NE PAS MODIFIER ===
 * Point d'entree de l'application Avalonia Desktop.
 */

using Avalonia;

namespace AvaloniaCombat
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

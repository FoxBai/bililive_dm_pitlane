using System.IO;
using System.Windows;

namespace PitlaneDanmaku.Windows;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        DispatcherUnhandledException += (_, args) =>
        {
            ReportStartupFailure(args.Exception);
            args.Handled = true;
            Shutdown(1);
        };

        try
        {
            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }
        catch (Exception ex)
        {
            ReportStartupFailure(ex);
            Shutdown(1);
        }
    }

    private static void ReportStartupFailure(Exception ex)
    {
        var directory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "PitlaneDanmaku");
        Directory.CreateDirectory(directory);

        var path = Path.Combine(directory, "startup-error.txt");
        File.WriteAllText(path, ex.ToString());

        MessageBox.Show(
            $"{ex.Message}{Environment.NewLine}{Environment.NewLine}{path}",
            "Pitlane Danmaku startup error",
            MessageBoxButton.OK,
            MessageBoxImage.Error);
    }
}

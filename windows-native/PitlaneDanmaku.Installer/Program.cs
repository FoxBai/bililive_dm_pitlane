using System.Diagnostics;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace PitlaneDanmaku.Installer;

internal static class Program
{
    private const string AppName = "Pitlane Danmaku";
    private const string Publisher = "FoxBai";
    private const string MainExecutableName = "PitlaneDanmaku.Windows.exe";
    private const string UninstallerName = "PitlaneDanmaku.Uninstall.exe";
    private const string ManifestName = "install-manifest.txt";
    private const string PayloadResourceName = "PitlaneDanmaku.Payload.zip";
    private const string UninstallRegistryPath = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\PitlaneDanmaku";

    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();

        if (TryGetArgumentValue(args, "/uninstall-final", out var finalInstallDir))
        {
            var parentPid = TryGetArgumentValue(args, "/parent-pid", out var pidText) &&
                            int.TryParse(pidText, out var parsedPid)
                ? parsedPid
                : null as int?;
            RunFinalUninstall(finalInstallDir, parentPid, args.Contains("/quiet", StringComparer.OrdinalIgnoreCase));
            return;
        }

        if (args.Contains("/uninstall", StringComparer.OrdinalIgnoreCase))
        {
            var installDir = ReadInstallLocation();
            if (string.IsNullOrWhiteSpace(installDir))
            {
                MessageBox.Show("没有找到已安装的 Pitlane Danmaku。", AppName, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            if (args.Contains("/quiet", StringComparer.OrdinalIgnoreCase))
            {
                RelaunchFinalUninstaller(installDir, quiet: true);
                return;
            }

            Application.Run(new UninstallForm(installDir));
            return;
        }

        Application.Run(new InstallForm());
    }

    private sealed class InstallForm : Form
    {
        private readonly TextBox _installPathBox = new();
        private readonly CheckBox _desktopShortcutBox = new();
        private readonly Button _installButton = new();
        private readonly ProgressBar _progressBar = new();

        public InstallForm()
        {
            Text = $"{AppName} 安装器";
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            Width = 620;
            Height = 300;

            var title = new Label
            {
                Text = $"{AppName} {ProductVersionText}",
                Font = new Font(Font.FontFamily, 16, FontStyle.Bold),
                AutoSize = true,
                Dock = DockStyle.Top,
                Padding = new Padding(0, 0, 0, 12)
            };

            var pathLabel = new Label
            {
                Text = "安装目录",
                AutoSize = true,
                Dock = DockStyle.Top
            };

            _installPathBox.Text = DefaultInstallDirectory;
            _installPathBox.Dock = DockStyle.Fill;

            var browseButton = new Button
            {
                Text = "浏览...",
                Width = 92,
                Dock = DockStyle.Right
            };
            browseButton.Click += (_, _) => BrowseInstallDirectory();

            var pathPanel = new Panel
            {
                Dock = DockStyle.Top,
                Height = 32,
                Padding = new Padding(0, 4, 0, 0)
            };
            pathPanel.Controls.Add(_installPathBox);
            pathPanel.Controls.Add(browseButton);

            _desktopShortcutBox.Text = "在桌面创建快捷方式";
            _desktopShortcutBox.Checked = true;
            _desktopShortcutBox.AutoSize = true;
            _desktopShortcutBox.Dock = DockStyle.Top;
            _desktopShortcutBox.Padding = new Padding(0, 12, 0, 6);

            _progressBar.Dock = DockStyle.Top;
            _progressBar.Height = 18;
            _progressBar.Style = ProgressBarStyle.Marquee;
            _progressBar.Visible = false;

            _installButton.Text = "安装";
            _installButton.Width = 100;
            _installButton.Height = 34;
            _installButton.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
            _installButton.Click += (_, _) => Install();

            var cancelButton = new Button
            {
                Text = "取消",
                Width = 100,
                Height = 34,
                Anchor = AnchorStyles.Right | AnchorStyles.Bottom,
                DialogResult = DialogResult.Cancel
            };

            var buttonPanel = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.RightToLeft,
                Dock = DockStyle.Bottom,
                Height = 46
            };
            buttonPanel.Controls.Add(cancelButton);
            buttonPanel.Controls.Add(_installButton);

            var body = new Panel
            {
                Dock = DockStyle.Fill,
                Padding = new Padding(18)
            };
            body.Controls.Add(_progressBar);
            body.Controls.Add(_desktopShortcutBox);
            body.Controls.Add(pathPanel);
            body.Controls.Add(pathLabel);
            body.Controls.Add(title);
            body.Controls.Add(buttonPanel);

            Controls.Add(body);
            AcceptButton = _installButton;
            CancelButton = cancelButton;
        }

        private void BrowseInstallDirectory()
        {
            using var dialog = new FolderBrowserDialog
            {
                Description = "选择 Pitlane Danmaku 的安装目录",
                SelectedPath = _installPathBox.Text
            };

            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                _installPathBox.Text = dialog.SelectedPath;
            }
        }

        private void Install()
        {
            var installDir = _installPathBox.Text.Trim();
            if (string.IsNullOrWhiteSpace(installDir))
            {
                MessageBox.Show(this, "请选择安装目录。", AppName, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            try
            {
                _installButton.Enabled = false;
                _progressBar.Visible = true;
                Cursor = Cursors.WaitCursor;

                InstallPayload(installDir, _desktopShortcutBox.Checked);
                MessageBox.Show(this, "安装完成。", AppName, MessageBoxButtons.OK, MessageBoxIcon.Information);
                Close();
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, $"安装失败：{ex.Message}", AppName, MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                Cursor = Cursors.Default;
                _progressBar.Visible = false;
                _installButton.Enabled = true;
            }
        }
    }

    private sealed class UninstallForm : Form
    {
        private readonly string _installDir;

        public UninstallForm(string installDir)
        {
            _installDir = installDir;
            Text = $"{AppName} 卸载器";
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            Width = 520;
            Height = 210;

            var label = new Label
            {
                Text = $"将卸载 {AppName}：\r\n{installDir}",
                Dock = DockStyle.Fill,
                Padding = new Padding(18),
                TextAlign = ContentAlignment.MiddleLeft
            };

            var uninstallButton = new Button
            {
                Text = "卸载",
                Width = 100,
                Height = 34
            };
            uninstallButton.Click += (_, _) =>
            {
                RelaunchFinalUninstaller(_installDir, quiet: false);
                Close();
            };

            var cancelButton = new Button
            {
                Text = "取消",
                Width = 100,
                Height = 34,
                DialogResult = DialogResult.Cancel
            };

            var buttonPanel = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.RightToLeft,
                Dock = DockStyle.Bottom,
                Height = 54,
                Padding = new Padding(0, 8, 12, 0)
            };
            buttonPanel.Controls.Add(cancelButton);
            buttonPanel.Controls.Add(uninstallButton);

            Controls.Add(label);
            Controls.Add(buttonPanel);
            AcceptButton = uninstallButton;
            CancelButton = cancelButton;
        }
    }

    private static void InstallPayload(string installDir, bool createDesktopShortcut)
    {
        installDir = Path.GetFullPath(installDir);
        Directory.CreateDirectory(installDir);

        var installedFiles = ExtractPayload(installDir);
        var uninstallerPath = Path.Combine(installDir, UninstallerName);
        File.Copy(Application.ExecutablePath, uninstallerPath, overwrite: true);
        installedFiles.Add(UninstallerName);

        var mainExePath = Path.Combine(installDir, MainExecutableName);
        if (createDesktopShortcut)
        {
            CreateShortcut(DesktopShortcutPath, mainExePath, installDir, AppName);
        }
        else if (File.Exists(DesktopShortcutPath))
        {
            File.Delete(DesktopShortcutPath);
        }

        var manifestPath = Path.Combine(installDir, ManifestName);
        installedFiles.Add(ManifestName);
        File.WriteAllLines(manifestPath, installedFiles.OrderBy(path => path, StringComparer.OrdinalIgnoreCase));

        WriteUninstallRegistry(installDir, mainExePath, uninstallerPath);
    }

    private static List<string> ExtractPayload(string installDir)
    {
        using var payload = OpenPayloadStream();
        using var archive = new ZipArchive(payload, ZipArchiveMode.Read);
        var installedFiles = new List<string>();

        foreach (var entry in archive.Entries)
        {
            if (string.IsNullOrWhiteSpace(entry.Name))
            {
                continue;
            }

            var destination = Path.GetFullPath(Path.Combine(installDir, entry.FullName));
            if (!IsUnderDirectory(destination, installDir))
            {
                throw new InvalidOperationException($"安装包包含非法路径：{entry.FullName}");
            }

            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            entry.ExtractToFile(destination, overwrite: true);
            installedFiles.Add(entry.FullName.Replace('/', Path.DirectorySeparatorChar));
        }

        return installedFiles;
    }

    private static Stream OpenPayloadStream()
    {
        var assembly = Assembly.GetExecutingAssembly();
        var stream = assembly.GetManifestResourceStream(PayloadResourceName);
        if (stream is null)
        {
            throw new InvalidOperationException("安装器缺少 payload.zip，请先运行 build-installer.ps1 生成安装包。");
        }

        return stream;
    }

    private static void RelaunchFinalUninstaller(string installDir, bool quiet)
    {
        var tempPath = Path.Combine(Path.GetTempPath(), $"PitlaneDanmaku.Uninstall.{Guid.NewGuid():N}.exe");
        File.Copy(Application.ExecutablePath, tempPath, overwrite: true);

        var currentPid = Environment.ProcessId;
        var arguments = $"/uninstall-final \"{installDir}\" /parent-pid {currentPid}" + (quiet ? " /quiet" : "");
        Process.Start(new ProcessStartInfo(tempPath, arguments)
        {
            UseShellExecute = true
        });
    }

    private static void RunFinalUninstall(string installDir, int? parentPid, bool quiet)
    {
        try
        {
            WaitForParentUninstaller(parentPid);
            RemoveInstallation(installDir);
            if (!quiet)
            {
                MessageBox.Show("卸载完成。", AppName, MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
        }
        catch (Exception ex)
        {
            if (!quiet)
            {
                MessageBox.Show($"卸载失败：{ex.Message}", AppName, MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }

    private static void RemoveInstallation(string installDir)
    {
        installDir = Path.GetFullPath(installDir);
        var manifestPath = Path.Combine(installDir, ManifestName);

        if (File.Exists(manifestPath))
        {
            var files = File.ReadAllLines(manifestPath)
                .Where(line => !string.IsNullOrWhiteSpace(line))
                .Reverse()
                .ToArray();

            foreach (var relativePath in files)
            {
                var filePath = Path.GetFullPath(Path.Combine(installDir, relativePath));
                if (IsUnderDirectory(filePath, installDir) && File.Exists(filePath))
                {
                    DeleteFileWithRetry(filePath);
                }
            }
        }

        if (File.Exists(manifestPath))
        {
            DeleteFileWithRetry(manifestPath);
        }

        DeleteShortcut(DesktopShortcutPath);
        DeleteUninstallRegistry();
        DeleteEmptyDirectories(installDir);
    }

    private static void DeleteEmptyDirectories(string installDir)
    {
        if (!Directory.Exists(installDir))
        {
            return;
        }

        foreach (var directory in Directory.EnumerateDirectories(installDir, "*", SearchOption.AllDirectories)
                     .OrderByDescending(path => path.Length))
        {
            if (!Directory.EnumerateFileSystemEntries(directory).Any())
            {
                DeleteDirectoryWithRetry(directory);
            }
        }

        if (!Directory.EnumerateFileSystemEntries(installDir).Any())
        {
            DeleteDirectoryWithRetry(installDir);
        }
    }

    private static void CreateShortcut(string shortcutPath, string targetPath, string workingDirectory, string description)
    {
        var shellType = Type.GetTypeFromProgID("WScript.Shell")
            ?? throw new InvalidOperationException("无法创建快捷方式：WScript.Shell 不可用。");

        object? shell = null;
        object? shortcut = null;
        try
        {
            shell = Activator.CreateInstance(shellType);
            shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, [shortcutPath]);
            shortcut!.GetType().InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, [targetPath]);
            shortcut.GetType().InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, [workingDirectory]);
            shortcut.GetType().InvokeMember("Description", BindingFlags.SetProperty, null, shortcut, [description]);
            shortcut.GetType().InvokeMember("IconLocation", BindingFlags.SetProperty, null, shortcut, [$"{targetPath},0"]);
            shortcut.GetType().InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, []);
        }
        finally
        {
            ReleaseComObject(shortcut);
            ReleaseComObject(shell);
        }
    }

    private static void DeleteShortcut(string shortcutPath)
    {
        if (File.Exists(shortcutPath))
        {
            DeleteFileWithRetry(shortcutPath);
        }
    }

    private static void WaitForParentUninstaller(int? parentPid)
    {
        if (parentPid is null)
        {
            Thread.Sleep(1200);
            return;
        }

        try
        {
            using var process = Process.GetProcessById(parentPid.Value);
            process.WaitForExit(10000);
        }
        catch (ArgumentException)
        {
            // The parent process already exited.
        }
    }

    private static void DeleteFileWithRetry(string path)
    {
        RetryIo(() =>
        {
            if (!File.Exists(path))
            {
                return;
            }

            File.SetAttributes(path, FileAttributes.Normal);
            File.Delete(path);
        }, path);
    }

    private static void DeleteDirectoryWithRetry(string path)
    {
        RetryIo(() =>
        {
            if (Directory.Exists(path))
            {
                Directory.Delete(path);
            }
        }, path);
    }

    private static void RetryIo(Action action, string path)
    {
        const int attempts = 50;
        Exception? lastError = null;

        for (var attempt = 1; attempt <= attempts; attempt++)
        {
            try
            {
                action();
                return;
            }
            catch (IOException ex)
            {
                lastError = ex;
            }
            catch (UnauthorizedAccessException ex)
            {
                lastError = ex;
            }

            Thread.Sleep(200);
        }

        throw new IOException($"无法删除：{path}", lastError);
    }

    private static void WriteUninstallRegistry(string installDir, string mainExePath, string uninstallerPath)
    {
        using var key = Registry.CurrentUser.CreateSubKey(UninstallRegistryPath);
        key.SetValue("DisplayName", AppName);
        key.SetValue("DisplayVersion", ProductVersionText);
        key.SetValue("Publisher", Publisher);
        key.SetValue("InstallLocation", installDir);
        key.SetValue("DisplayIcon", mainExePath);
        key.SetValue("UninstallString", $"\"{uninstallerPath}\" /uninstall");
        key.SetValue("QuietUninstallString", $"\"{uninstallerPath}\" /uninstall /quiet");
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        key.SetValue("EstimatedSize", CalculateDirectorySizeKb(installDir), RegistryValueKind.DWord);
    }

    private static void DeleteUninstallRegistry()
    {
        Registry.CurrentUser.DeleteSubKeyTree(UninstallRegistryPath, throwOnMissingSubKey: false);
    }

    private static string? ReadInstallLocation()
    {
        using var key = Registry.CurrentUser.OpenSubKey(UninstallRegistryPath);
        return key?.GetValue("InstallLocation") as string;
    }

    private static int CalculateDirectorySizeKb(string directory)
    {
        var bytes = Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories)
            .Sum(path => new FileInfo(path).Length);
        return Math.Max(1, (int)Math.Min(int.MaxValue, bytes / 1024));
    }

    private static bool IsUnderDirectory(string path, string directory)
    {
        var fullPath = Path.GetFullPath(path);
        var fullDirectory = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return fullPath.Equals(fullDirectory, StringComparison.OrdinalIgnoreCase) ||
               fullPath.StartsWith(fullDirectory + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ||
               fullPath.StartsWith(fullDirectory + Path.AltDirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    private static void ReleaseComObject(object? value)
    {
        if (value is not null && Marshal.IsComObject(value))
        {
            Marshal.FinalReleaseComObject(value);
        }
    }

    private static bool TryGetArgumentValue(string[] args, string name, out string value)
    {
        for (var index = 0; index < args.Length - 1; index++)
        {
            if (args[index].Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                value = args[index + 1];
                return true;
            }
        }

        value = "";
        return false;
    }

    private static string DefaultInstallDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Pitlane Danmaku");

    private static string DesktopShortcutPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), $"{AppName}.lnk");

    private static string ProductVersionText =>
        typeof(Program).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion
        ?? typeof(Program).Assembly.GetName().Version?.ToString(3)
        ?? "0.0.0";
}

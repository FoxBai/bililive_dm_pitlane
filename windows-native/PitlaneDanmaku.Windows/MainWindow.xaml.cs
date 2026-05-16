using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Microsoft.Win32;
using PitlaneDanmaku.Windows.Models;
using PitlaneDanmaku.Windows.Services;

namespace PitlaneDanmaku.Windows;

public partial class MainWindow : Window
{
    private readonly LogService _log = new();
    private readonly AppSettings _settings;
    private readonly AssetCatalog _assets;
    private readonly MessagePipeline _pipeline;
    private readonly LocalObsServer _obsServer;
    private BilibiliWebRoomClient? _bilibiliClient;
    private OverlayWindow? _overlayWindow;

    public MainWindow()
    {
        InitializeComponent();

        _settings = SettingsStore.Load();
        _assets = new AssetCatalog(_log);
        _pipeline = new MessagePipeline(_settings, _log);
        _obsServer = new LocalObsServer(_assets, _log);

        LoadSettingsToUi();
        AssetText.Text = $"素材：{_assets.Cars.Count} 辆赛车，评论框 {(_assets.CommentFramePath.Length > 0 ? "已加载" : "缺失")}；字体 HarmonyOS Sans SC";
        ObsUrlBox.Text = _obsServer.OverlayUrl;

        _log.EntryAdded += OnLogEntryAdded;
        _pipeline.MessageReady += OnMessageReady;
        _pipeline.Start();

        Loaded += async (_, _) => await RestartObsServerAsync();
        Closing += MainWindow_Closing;
    }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        ApplySettingsFromUi();
        if (string.IsNullOrWhiteSpace(_settings.RoomInput))
        {
            _log.Warn("请先填写 B站直播间 ID 或 URL。");
            return;
        }

        DisconnectBilibiliClient();
        _bilibiliClient = new BilibiliWebRoomClient(_log);
        _bilibiliClient.MessageReceived += OnBilibiliMessageReceived;
        await _bilibiliClient.StartAsync(_settings);
        StatusText.Text = "正在连接 B站直播间";
        _log.Info("B站直播间连接任务已启动。");
    }

    private void DisconnectButton_Click(object sender, RoutedEventArgs e)
    {
        DisconnectBilibiliClient();
        StatusText.Text = "已断开";
        _log.Info("已断开 B站直播间。");
    }

    private async void RestartObsButton_Click(object sender, RoutedEventArgs e)
    {
        ApplySettingsFromUi();
        await RestartObsServerAsync();
    }

    private void OpenOverlayButton_Click(object sender, RoutedEventArgs e)
    {
        ApplySettingsFromUi();
        if (_overlayWindow is { IsVisible: true })
        {
            _overlayWindow.Activate();
            return;
        }

        if (_overlayWindow is not null)
        {
            _overlayWindow.ApplySettings(_settings);
            _overlayWindow.Show();
            _overlayWindow.Activate();
            _log.Info("桌面透明悬浮窗已显示。");
            return;
        }

        _overlayWindow = new OverlayWindow(_assets, _settings);
        _overlayWindow.Closed += (_, _) => _overlayWindow = null;
        _overlayWindow.Show();
        _log.Info("桌面透明悬浮窗已打开。");
    }

    private void HideOverlayButton_Click(object sender, RoutedEventArgs e)
    {
        if (_overlayWindow is null)
        {
            _log.Info("桌面透明悬浮窗当前未打开。");
            return;
        }

        _overlayWindow.Hide();
        _log.Info("桌面透明悬浮窗已隐藏。");
    }

    private void SimulateCommentButton_Click(object sender, RoutedEventArgs e)
    {
        ApplySettingsFromUi();
        _pipeline.Enqueue(ChatMessage.CreateComment("PitCrew", "普通评论测试，赛车准备入场"));
    }

    private void SimulateSuperChatButton_Click(object sender, RoutedEventArgs e)
    {
        ApplySettingsFromUi();
        _pipeline.Enqueue(ChatMessage.CreateSuperChat("RaceControl", "醒目留言优先进入发车队列", 30));
    }

    private async void BurstButton_Click(object sender, RoutedEventArgs e)
    {
        ApplySettingsFromUi();
        for (var index = 1; index <= 24; index++)
        {
            var message = index % 7 == 0
                ? ChatMessage.CreateSuperChat($"SC{index:00}", $"第 {index:00} 条醒目留言", 30 + index)
                : ChatMessage.CreateComment($"车迷{index:00}", $"第 {index:00} 条连发评论，测试队列减速和出场");

            _pipeline.Enqueue(message);
            await Task.Delay(80);
        }
    }

    private void ClearLogButton_Click(object sender, RoutedEventArgs e)
    {
        _log.Clear();
        LogListBox.Items.Clear();
    }

    private void LogListBox_PreviewMouseRightButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (e.OriginalSource is not DependencyObject source)
        {
            return;
        }

        var item = FindParent<ListBoxItem>(source);
        if (item is null)
        {
            return;
        }

        item.IsSelected = true;
        item.Focus();
    }

    private void CopySelectedLogMenuItem_Click(object sender, RoutedEventArgs e)
    {
        var entries = LogListBox.SelectedItems
            .OfType<string>()
            .ToArray();

        CopyLogEntries(entries.Length > 0 ? entries : _log.Snapshot);
    }

    private void CopyAllLogsMenuItem_Click(object sender, RoutedEventArgs e)
    {
        CopyLogEntries(_log.Snapshot);
    }

    private void ExportLogButton_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog
        {
            Title = "导出 Pitlane Danmaku 日志",
            Filter = "Text Log (*.txt)|*.txt",
            FileName = $"pitlane-danmaku-{DateTime.Now:yyyyMMdd-HHmmss}.txt"
        };

        if (dialog.ShowDialog(this) != true)
        {
            return;
        }

        File.WriteAllLines(dialog.FileName, _log.Snapshot);
        _log.Info($"日志已导出：{dialog.FileName}");
    }

    private void MainWindow_Closing(object? sender, CancelEventArgs e)
    {
        DisconnectBilibiliClient();
        _obsServer.Dispose();
        _pipeline.Dispose();
        _overlayWindow?.Close();
    }

    private void OnBilibiliMessageReceived(ChatMessage message)
    {
        _pipeline.Enqueue(message);
    }

    private void OnMessageReady(ChatMessage message)
    {
        _obsServer.Broadcast(message);
        Dispatcher.BeginInvoke(() =>
        {
            _overlayWindow?.ShowMessage(message);
            StatusText.Text = message.IsSuperChat
                ? $"醒目留言：{message.UserName}"
                : $"普通评论：{message.UserName}";
        });
    }

    private void OnLogEntryAdded(string entry)
    {
        Dispatcher.BeginInvoke(() =>
        {
            LogListBox.Items.Add(entry);
            LogListBox.ScrollIntoView(entry);
        });
    }

    private async Task RestartObsServerAsync()
    {
        try
        {
            await _obsServer.StartAsync(_settings);
            ObsUrlBox.Text = _obsServer.OverlayUrl;
        }
        catch (Exception ex)
        {
            _log.Error($"OBS 服务启动失败：{ex.Message}");
        }
    }

    private void DisconnectBilibiliClient()
    {
        if (_bilibiliClient is null)
        {
            return;
        }

        _bilibiliClient.MessageReceived -= OnBilibiliMessageReceived;
        _bilibiliClient.Dispose();
        _bilibiliClient = null;
    }

    private void ApplySettingsFromUi()
    {
        _settings.RoomInput = RoomInputBox.Text;
        _settings.Cookie = CookieBox.Text;
        _settings.UserAgent = UserAgentBox.Text;
        _settings.Buvid3 = Buvid3Box.Text;
        _settings.ObsPort = ReadInt(ObsPortBox.Text, _settings.ObsPort);
        _settings.LaunchIntervalMs = ReadInt(LaunchIntervalBox.Text, _settings.LaunchIntervalMs);
        _settings.QueueLimit = ReadInt(QueueLimitBox.Text, _settings.QueueLimit);
        _settings.MinVisibleItems = ReadInt(MinVisibleBox.Text, _settings.MinVisibleItems);
        _settings.MaxNicknameLength = ReadInt(NicknameLengthBox.Text, _settings.MaxNicknameLength);
        _settings.MaxMessageLength = ReadInt(MessageLengthBox.Text, _settings.MaxMessageLength);
        _settings.MaxRepeatCharacters = ReadInt(RepeatLimitBox.Text, _settings.MaxRepeatCharacters);
        _settings.MaxStageWidth = ReadInt(MaxStageWidthBox.Text, _settings.MaxStageWidth);
        _settings.Normalize();

        _pipeline.UpdateSettings(_settings);
        _overlayWindow?.ApplySettings(_settings);
        SettingsStore.Save(_settings);
        LoadSettingsToUi();
        ObsUrlBox.Text = $"http://127.0.0.1:{_settings.ObsPort}/overlay";
    }

    private static void CopyLogEntries(IEnumerable<string> entries)
    {
        var text = string.Join(Environment.NewLine, entries);
        if (!string.IsNullOrWhiteSpace(text))
        {
            Clipboard.SetText(text);
        }
    }

    private static T? FindParent<T>(DependencyObject child)
        where T : DependencyObject
    {
        var current = child;
        while (current is not null)
        {
            if (current is T typed)
            {
                return typed;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return null;
    }

    private void LoadSettingsToUi()
    {
        RoomInputBox.Text = _settings.RoomInput;
        CookieBox.Text = _settings.Cookie;
        UserAgentBox.Text = _settings.UserAgent;
        Buvid3Box.Text = _settings.Buvid3;
        ObsPortBox.Text = _settings.ObsPort.ToString();
        LaunchIntervalBox.Text = _settings.LaunchIntervalMs.ToString();
        QueueLimitBox.Text = _settings.QueueLimit.ToString();
        MinVisibleBox.Text = _settings.MinVisibleItems.ToString();
        NicknameLengthBox.Text = _settings.MaxNicknameLength.ToString();
        MessageLengthBox.Text = _settings.MaxMessageLength.ToString();
        RepeatLimitBox.Text = _settings.MaxRepeatCharacters.ToString();
        MaxStageWidthBox.Text = _settings.MaxStageWidth.ToString();
    }

    private static int ReadInt(string value, int fallback)
    {
        return int.TryParse(value, out var parsed) ? parsed : fallback;
    }
}

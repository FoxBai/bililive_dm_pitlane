# PitlaneDanmaku.Windows

这是 `bililive_dm_pitlane` 的 Windows 原生实现，使用 WPF / .NET 8。

```powershell
cd windows-native\PitlaneDanmaku.Windows
dotnet restore
dotnet build -c Debug
dotnet run
```

默认 OBS 浏览器源：

```text
http://127.0.0.1:17333/overlay
```

如果 OBS 服务启动失败，通常是端口被占用。请在控制台里改一个端口并点击“重启 OBS 服务”。

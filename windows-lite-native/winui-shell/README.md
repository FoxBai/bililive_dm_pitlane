# Pitlane Danmaku Lite WinUI/C++ 外壳

这个目录是 lite 版的新 Windows 原生控制台外壳。它不追求“绝对最小”，而是在相对小的体积里换来更现代、更舒服的 WinUI 3 桌面界面，同时继续复用现有素材和交互逻辑。

## 当前状态

WinUI 外壳已经接入 `windows-lite-native/src/` 里的真实 lite 核心，而不是模拟数据层：

- `pitlane::lite::SettingsStore`：读取和保存 `%LOCALAPPDATA%\PitlaneDanmakuLite\settings.ini`
- `pitlane::lite::MessagePipeline`：文字清洗、去重、队列和“只显示 SuperChat/醒目留言”
- `pitlane::lite::LocalObsServer`：本地 OBS HTTP/SSE 浏览器源
- `pitlane::lite::BilibiliClient`：B 站房间解析、WBI、buvid3、WSS/WS/TCP 弹幕连接、zlib/brotli 解包、实时弹幕解析和历史弹幕补偿

也就是说，保存设置、测试弹幕、OBS 浏览器源、真实直播间连接和历史补偿都走同一套 C++ lite 逻辑。真实直播间的长时间稳定性仍然需要最后用直播间实测确认。

## 构建要求

需要 Visual Studio 2022，并安装：

- 使用 C++ 的桌面开发
- Windows App SDK C++ 模板
- C++/WinRT
- Windows 10/11 SDK

真实直播间接收需要 zlib 和 brotli。项目默认从下面目录读取 vcpkg 产物：

```text
windows-lite-native\build-vcpkg\vcpkg_installed\x64-windows
```

如果这个目录还不存在，先构建一次普通 lite 版：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\package-lite.ps1 -Configuration Release
```

## 构建

```powershell
msbuild windows-lite-native\winui-shell\PitlaneDanmaku.Lite.WinUI.sln `
  /restore `
  /p:Configuration=Release `
  /p:Platform=x64
```

输出程序：

```text
windows-lite-native\winui-shell\x64\Release\PitlaneDanmaku.Lite.WinUI\PitlaneDanmaku.Lite.WinUI.exe
```

## 打包

生成不带调试符号的便携目录：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\winui-shell\package-winui-lite.ps1 -Configuration Release
```

默认输出：

```text
windows-lite-native\dist\PitlaneDanmakuLite-WinUI
```

同时生成 zip：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\winui-shell\package-winui-lite.ps1 -Configuration Release -MakeZip
```

## 本地验证

运行解析冒烟测试、Debug/Release 构建、压缩 DLL 检查和 WinUI 启动检查：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\winui-shell\verify-winui-lite.ps1
```

如果只想跑构建和核心检查，不启动窗口：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\winui-shell\verify-winui-lite.ps1 -SkipLaunch
```

## Agent 接手说明

给人看：这里是 WinUI/C++ 控制台外壳，普通 Win32 lite 版仍在 `windows-lite-native\build-vcpkg\pitlane_lite.exe`。当前 WinUI 外壳已经接真实 lite 核心，不再只是静态 UI 试验。

给 agent 看：只改 `windows-lite-native/` 相关内容。不要提交 `build-vcpkg/`、`dist/`、`winui-shell/x64/`、`winui-shell/obj/`、`winui-shell/Generated Files/` 等构建产物。新增注释继续使用中文；如果要碰真实直播间链路，优先补 `src/BilibiliClient.cpp`，不要把网络接收逻辑写进 XAML 事件处理里。

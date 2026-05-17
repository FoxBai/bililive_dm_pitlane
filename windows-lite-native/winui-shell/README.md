# Pitlane Danmaku Lite WinUI/C++ 外壳

这个目录是 lite 版的 WinUI 3 / C++ 外壳试验区。它的目标不是追求极限体积，而是在相对小的 Windows 原生分发基础上，把控制台界面做得更舒服、更像一个可以长期使用的桌面工具。

当前策略：

- 继续复用 `windows-lite-native/src/` 里的弹幕接收、消息管线、OBS 本地服务和设置模型。
- 透明叠加层、OBS 浏览器源和网络接收逻辑仍优先保持原生 C++，避免把显示外壳和直播链路绑死。
- 控制台外壳使用 WinUI 3 / C++/WinRT，换取更现代的控件、排版和窗口观感。
- 分发时优先做 unpackaged 桌面程序，再用普通安装器包装；Windows App SDK 运行时可以作为依赖安装，不强行塞进安装包。

## 当前状态

已经建立第一版 WinUI 外壳骨架：

- 直播间输入。
- Cookie 输入。
- 只显示 SuperChat / 醒目留言开关。
- OBS 端口、同屏显示数、最大舞台宽度设置。
- 连接、断开、保存设置、重启 OBS、添加测试弹幕、添加测试醒目留言按钮。
- 状态提示、运行日志和消息预览区。

这一版用于验证外壳结构和视觉方向，同时已经接入部分现有 lite 核心：

- `pitlane::lite::SettingsStore`
- `pitlane::lite::MessagePipeline`
- `pitlane::lite::LocalObsServer`

也就是说，保存设置、SuperChat 过滤、测试弹幕入队和 OBS 本地 HTTP/SSE 服务已经走真实 C++ 逻辑。

下一步再接入真实直播间接收：

- `pitlane::lite::BilibiliClient`

## 构建要求

需要 Visual Studio 2022，并安装：

- 使用 C++ 的桌面开发
- Windows App SDK C++ 模板
- C++/WinRT
- Windows 10/11 SDK

项目使用 Windows App SDK 1.7 系列作为起步版本，和当前机器上的 VS WinUI/C++ 模板保持一致。

## 构建方式

在 Developer PowerShell 或普通 PowerShell 中运行：

```powershell
msbuild windows-lite-native\winui-shell\PitlaneDanmaku.Lite.WinUI.sln `
  /restore `
  /p:Configuration=Debug `
  /p:Platform=x64
```

如果第一次构建时 NuGet 包还没有缓存，MSBuild 会恢复这些包。恢复完成后，后续构建可直接运行：

```powershell
msbuild windows-lite-native\winui-shell\PitlaneDanmaku.Lite.WinUI.sln `
  /p:Configuration=Debug `
  /p:Platform=x64
```

## Agent 接手说明

人看：这里是新版漂亮控制台，不替代当前已经能跑的 `pitlane_lite.exe`。如果要验证真实直播间，仍先用 `windows-lite-native/build-vcpkg/pitlane_lite.exe`。

Agent 看：这个目录只负责 WinUI 外壳。接入核心库时，优先做一个薄的 `LiteRuntime` 桥接类，不要把网络接收、OBS HTTP 服务和 XAML 事件处理混在一个文件里。新增注释继续使用中文，构建产物不要提交。

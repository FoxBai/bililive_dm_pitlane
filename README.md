# bililive_dm_pitlane

`bililive_dm_pitlane` 是基于 [copyliu/bililive_dm](https://github.com/copyliu/bililive_dm) 创建的 Pitlane 分支，目标是把 B站直播评论包装成赛车 / pit lane 风格的透明叠加画面，并提供 Windows 原生应用。

当前仓库保留 `bililive_dm` 的历史代码和授权文件，在此基础上新增 `windows-native/PitlaneDanmaku.Windows`。Windows 版本使用 WPF / .NET 8，不沿用 Electron，也不直接复用 `FoxBai/pitlane_danmaku` 旧 Windows 实现；赛车和评论框素材来自 Pitlane 项目素材目录。

## 当前能力

- B站直播间 ID / URL 直连。
- 使用网页端弹幕服务器信息，优先走原始 TCP 弹幕通道。
- 鉴权字段使用 `protover: 3`，支持 brotli、zlib / deflate、嵌套包和连续 JSON 消息解析。
- 支持普通评论和 SuperChat / 醒目留言。
- 评论清洗、重复消息去重、等待队列限流、醒目留言优先发车。
- 本地 OBS 浏览器源：`http://127.0.0.1:17333/overlay`。
- Windows 桌面透明置顶悬浮窗。
- 本地普通评论、醒目留言和连发测试按钮。
- 随机赛车素材、横向接续队列、底部透明 overlay 展示。

## Windows 原生应用

项目路径：

```powershell
windows-native\PitlaneDanmaku.Windows
```

本地构建：

```powershell
cd windows-native\PitlaneDanmaku.Windows
dotnet restore
dotnet build -c Debug
dotnet run
```

发布：

```powershell
cd windows-native\PitlaneDanmaku.Windows
dotnet publish -c Release
```

启动后可以在控制台中填写：

- 直播间 ID 或 B站直播间 URL。
- 可选网页 Cookie，用于遇到风控时补充登录态。
- 可选 `buvid3`。
- OBS 端口、发车间隔、队列上限、同屏最少组数、文本长度等显示参数。

OBS 中添加浏览器源并填写：

```text
http://127.0.0.1:17333/overlay
```

浏览器源背景保持透明，适合叠加到直播画面上。

## 素材

素材位于 `assets/`：

- `assets/cars/`：赛车 PNG 和 `cars.json` 清单。
- `assets/comment-box/`：评论框 PNG / SVG。
- `assets/icon.svg`：项目图标素材。

Windows 工程会在构建时把 `assets/` 复制到输出目录的 `Assets/` 下，OBS 本地服务和桌面悬浮窗共用同一份素材。

## 结构

```text
windows-native/PitlaneDanmaku.Windows/
  Models/                 数据模型和配置
  Services/               B站连接、消息管线、OBS 服务、素材加载
  OverlayWindow.xaml      桌面透明悬浮窗
  MainWindow.xaml         Windows 控制台界面
```

关键文件：

- `Services/BilibiliWebRoomClient.cs`：B站直播间直连、弹幕 TCP 协议、压缩包拆包。
- `Services/BilibiliMessageParser.cs`：`DANMU_MSG` 和 `SUPER_CHAT_MESSAGE` 解析。
- `Services/MessagePipeline.cs`：评论清洗、去重、限流和醒目留言优先队列。
- `Services/LocalObsServer.cs`：本地 OBS 浏览器源、SSE 推送和素材服务。
- `OverlayWindow.xaml.cs`：Windows 桌面透明悬浮窗渲染。

## 授权与来源

本仓库基于 `copyliu/bililive_dm` 创建，原项目使用 WTFPL 授权，授权文件保留在 `LICENSE.txt`。新增 Pitlane 分支代码和素材遵守同一仓库授权发布；若后续直接引用第三方实现片段，需要在对应文件中补充来源和授权说明。

更多来源说明见 `NOTICE.md`。

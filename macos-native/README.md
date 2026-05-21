# PitlaneDanmakuMac

这是基于 `windows-native` 迁移出的 macOS 原生版本第一版，使用 Swift / AppKit。

## 运行

```bash
cd macos-native
swift run
```

默认 OBS 浏览器源：

```text
http://127.0.0.1:17333/overlay
```

健康检查：

```text
http://127.0.0.1:17333/health
```

## 打包为 .app

```bash
cd macos-native
bash scripts/build-app.sh
open build/PitlaneDanmaku.app
```

打包脚本会把仓库根目录的 `assets` 复制进 `PitlaneDanmaku.app/Contents/Resources/Assets`，因此分发 `.app` 时不依赖源码目录。

## 图标

```bash
swift macos-native/scripts/generate-icons.swift
```

脚本会生成根目录 `assets/icon.svg`、`assets/icon.png`、`assets/icon.icns` 和 `assets/icon.ico`。macOS 打包使用 `icon.icns`，Windows Native 使用 `icon.ico`。

## 测试

```bash
cd macos-native
swift test
```

## 当前能力

- AppKit 原生控制台：直播间、Cookie/User-Agent/buvid3、显示队列、OBS 端口、日志。
- B 站直播弹幕连接：房间号解析、WBI 签名、WSS/WS 弹幕连接、历史弹幕补偿。
- B 站弹幕连接失败兜底：WSS -> WS -> TCP。
- 消息管线：醒目留言优先、普通评论限流、重复消息去重、文本清洗。
- 输出：桌面透明悬浮窗，以及 OBS 浏览器源 `/overlay` + SSE `/events`。
- 素材：复用仓库根目录 `assets` 中的赛车、评论框和 HarmonyOS Sans SC 字体。

## 迁移说明

macOS 版优先复用 Windows Native 的业务语义和布局常量，但平台层改为原生 AppKit：

- `OverlayWindow.xaml.cs` -> `OverlayWindowController.swift`
- `LocalObsServer.cs` -> `LocalObsServer.swift`
- `BilibiliWebRoomClient.cs` -> `BilibiliWebRoomClient.swift`
- `MessagePipeline.cs` / `TextSanitizer.cs` / `AssetCatalog.cs` -> 同名 Swift 服务

当前客户端优先请求 B 站 `protover=2` 的 zlib 弹幕包，同时也能解析服务器返回的 Brotli 压缩包。

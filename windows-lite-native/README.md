# Pitlane Danmaku Lite

`windows-lite-native` 是 Windows 叠加层应用的小体积重写方向。

现有 WPF 版本使用自包含发布，分发很省心，但会把 .NET Desktop Runtime 一起打进安装包，所以体积偏大。Lite 方向的目标是把应用逻辑迁移到原生 C++，继续复用现有 Pitlane 素材和交互规则，尽量减少运行时依赖和安装包体积。

## 目标

- 控制台界面先使用 Win32 原生窗口实现，后续可再迁移到 WinUI。
- 透明桌面叠加窗口使用原生窗口实现。
- 本地 OBS HTTP/SSE 服务使用原生 C++ 实现。
- B 站直播弹幕客户端使用原生 C++ 实现。
- 继续复用仓库根目录的 `assets/` 素材。
- 不依赖 .NET Runtime。

## 当前范围

当前已经能跑通一个 lite 版闭环，但仍处在迁移阶段。当前已经建立：

- 消息和设置的轻量领域模型。
- 与 WPF 版一致的文字清洗逻辑。
- 与 WPF 版一致的队列优先级、去重和仅显示醒目留言逻辑。
- 可启动的 Win32 原生预览窗口，用于验证 lite 核心库、设置输入、醒目留言过滤和本地叠加层。
- 设置保存和加载，默认写入 `%LOCALAPPDATA%\PitlaneDanmakuLite\settings.ini`。
- 小体积打包脚本和 Inno Setup 安装脚本。
- 后续 WinUI/C++ 实现需要遵循的模块拆分。
- 网络、叠加层和安装器迁移清单。

## 当前可运行状态

现在可以编译出一个带压缩弹幕解码能力的原生窗口程序：

```text
windows-lite-native/build-vcpkg/pitlane_lite.exe
```

这个程序目前用于验证 lite 版的基础运行链路：

- 直播间输入框。
- Cookie 输入框。
- Cookie 输入框会自动清洗 `Cookie:` 前缀，也能从整段请求头中提取 Cookie 行。
- OBS 端口、同屏数量和最大舞台宽度输入框。
- 重启 OBS 服务按钮，用于应用 OBS 相关设置。
- 连接直播间按钮，会用 WinHTTP 访问 B 站真实 API，解析真实房间号、获取弹幕服务器信息并尝试建立 WSS 实时弹幕连接；连接中再次点击可断开。
- 未提供 buvid3 时，会自动请求 B 站指纹接口获取兜底值并用于握手。
- 获取弹幕服务器信息时会使用 WBI 签名参数。
- 仅显示醒目留言开关。
- 显示叠加预览开关。
- 添加测试弹幕。
- 添加醒目留言。
- 消息队列、文字清洗、醒目留言过滤和跨重连同 UID 昵称缓存预览。
- B 站返回脱敏昵称时，同 UID 有缓存会恢复完整名；没有缓存时会跳过该条，避免画面显示星号名。
- B 站 WSS / WS 弹幕节点循环重试；单个节点断开后会尝试下一个节点，全部断开后 5 秒重连。
- 透明、置顶、鼠标穿透的 Win32 叠加层预览。
- OBS 本地服务预览：`http://127.0.0.1:17333/overlay`、`/events`、`/assets/...` 和 `/health`，浏览器源会使用评论框和赛车 PNG 做基础发车动画。
- 从仓库 `assets/` 加载评论框 PNG、赛车 PNG 和 HarmonyOS 中文字体。

Lite 版仍没有做到 WPF 版的全部成熟度。B 站 WebSocket 已有基础连接、WBI 签名、鉴权、心跳、可断开接收循环、未压缩 JSON 弹幕解析、buvid3 兜底、WSS/WS 节点重试和同 UID 脱敏昵称缓存；OBS 本地服务已有基础 HTTP/SSE 链路和浏览器源发车动画预览；更完整的消息解析和 TCP 回退还需要继续补。

压缩弹幕包的原生解压代码已经接入 `zlib` 和 `brotli`。默认构建要求这两个库存在，CMake 找到后会定义 `PITLANE_LITE_HAS_COMPRESSION`、握手请求 `protover:3` 并启用 version 2/3 解包。不要用旧的无依赖 `build-vs` 产物连接真实直播间，否则会遇到“当前构建未启用 zlib 解压依赖”。

## 建议工具链

安装 Visual Studio 2022，并选择：

- 使用 C++ 的桌面开发
- Windows App SDK C++ 模板
- C++/WinRT
- Windows 10/11 SDK

WinUI 3 本身会带来 Windows App SDK 运行时依赖。如果最终目标是尽可能小的安装包，建议控制台界面可以使用 WinUI，但透明叠加窗口和 OBS 服务仍优先保持 Win32 原生实现。

使用 VS 自带 vcpkg 配置：

```powershell
cmake -S windows-lite-native -B windows-lite-native/build-vcpkg -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build windows-lite-native/build-vcpkg --config Debug
```

如果只是做不连接真实直播间的本地 UI 实验，可以显式传入 `-DPITLANE_LITE_REQUIRE_COMPRESSION=OFF`，但这种构建不应用于发布或长时间直播测试。

## 打包和安装器

生成 lite 便携目录：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\package-lite.ps1 -Configuration Release
```

输出目录：

```text
windows-lite-native/dist/PitlaneDanmakuLite
```

生成 Windows 安装器：

```powershell
powershell -ExecutionPolicy Bypass -File windows-lite-native\package-lite.ps1 -Configuration Release -MakeInstaller
```

输出位置：

```text
windows-lite-native/installer/output/PitlaneDanmakuLite-Lite-Setup.exe
```

优先使用 Inno Setup 脚本 `windows-lite-native/installer/PitlaneDanmakuLite.iss`；如果本机没有 Inno Setup，但有 7-Zip SFX 模块，脚本会生成一个自解压安装器。安装器启动后会选择安装目录、询问是否创建桌面快捷方式，并写入当前用户的系统卸载入口。

## 目录结构

```text
windows-lite-native/
  README.md
  MIGRATION.md
  CMakeLists.txt
  src/
    AppSettings.h
    BilibiliClient.cpp
    BilibiliClient.h
    ChatMessage.h
    main.cpp
    MessagePipeline.cpp
    MessagePipeline.h
    SettingsStore.cpp
    SettingsStore.h
    TextSanitizer.cpp
    TextSanitizer.h
  installer/
    install-lite.ps1
    PitlaneDanmakuLite.iss
  package-lite.ps1
```

## Agent 接手说明

这个目录是给“小体积原生版”单独推进的，不是 WPF 版安装包目录。

- 人看：当前它已经能打开一个 Win32 测试窗口，能连 B 站基础 WSS / WS 弹幕，能显示原生透明叠加层，也能启动基础 OBS 本地服务。
- Agent 看：优先保持目录边界清晰，只改 `windows-lite-native/` 和必要的根文档；不要把 `build/`、`build-vs/`、`build-vcpkg/`、`dist/`、`installer/output/`、`BundleArtifacts/` 等构建产物提交；可用 `/health` 快速验证本地 OBS 服务。
- 构建验证：真实直播间测试和发布都使用 `windows-lite-native/build-vcpkg`；`build-vs` 只能作为显式关闭压缩依赖后的本地 UI 实验目录。
- 已知缺口：真实直播间长时间测试、TCP 回退、完整 JSON 解析和最终 OBS 浏览器源视觉细节还要继续打磨。
- 和 WPF 版关系：`windows-native/` 是当前可发布的 WPF/.NET 8 版本；`windows-lite-native/` 是长期减小体积、减少运行时依赖的 C++ 重写方向。

## 下一阶段

1. 创建 WinUI/C++ 应用壳和设置页。
2. 完善 B 站 TCP 回退、历史弹幕补偿和更完整 JSON 解析。
3. 基于真实直播间长时间测试继续打磨本地 OBS HTTP/SSE 服务的最终浏览器源布局。
4. 使用 Win32 layered window 或 Direct2D 渲染叠加层。
5. 增加小体积原生安装器。

# Pitlane Danmaku Lite

`windows-lite-native` 是 Windows 叠加层应用的小体积重写方向。

现有 WPF 版本使用自包含发布，分发很省心，但会把 .NET Desktop Runtime 一起打进安装包，所以体积偏大。Lite 方向的目标是把应用逻辑迁移到原生 C++，继续复用现有 Pitlane 素材和交互规则，尽量减少运行时依赖和安装包体积。

## 目标

- 控制台界面使用 C++ / WinUI。
- 透明桌面叠加窗口使用原生窗口实现。
- 本地 OBS HTTP/SSE 服务使用原生 C++ 实现。
- B 站直播弹幕客户端使用原生 C++ 实现。
- 继续复用仓库根目录的 `assets/` 素材。
- 不依赖 .NET Runtime。

## 当前范围

第一版提交的是迁移骨架，还不是完整应用。当前已经建立：

- 消息和设置的轻量领域模型。
- 与 WPF 版一致的文字清洗逻辑。
- 与 WPF 版一致的队列优先级、去重和仅显示醒目留言逻辑。
- 可启动的 Win32 原生预览窗口，用于验证 lite 核心库、设置输入和醒目留言过滤。
- 后续 WinUI/C++ 实现需要遵循的模块拆分。
- 网络、叠加层和安装器迁移清单。

## 当前可运行状态

现在可以编译出一个最小原生窗口程序：

```text
windows-lite-native/build-vs/pitlane_lite.exe
```

这个程序目前用于验证 lite 版的基础运行链路：

- 直播间输入框。
- Cookie 输入框。
- 连接直播间按钮，会用 WinHTTP 访问 B 站真实 API，解析真实房间号、获取弹幕服务器信息并尝试建立 WSS 实时弹幕连接；连接中再次点击可断开。
- 未提供 buvid3 时，会自动请求 B 站指纹接口获取兜底值并用于握手。
- 获取弹幕服务器信息时会使用 WBI 签名参数。
- 仅显示醒目留言开关。
- 显示叠加预览开关。
- 添加测试弹幕。
- 添加醒目留言。
- 消息队列、文字清洗、醒目留言过滤和同 UID 昵称缓存预览。
- B 站返回脱敏昵称时，同 UID 有缓存会恢复完整名；没有缓存时会跳过该条，避免画面显示星号名。
- 透明、置顶、鼠标穿透的 Win32 叠加层预览。
- OBS 本地服务预览：`http://127.0.0.1:17333/overlay`、`/events` 和 `/assets/...`。
- 从仓库 `assets/` 加载评论框 PNG、赛车 PNG 和 HarmonyOS 中文字体。

它还没有接入赛车发车动画和安装器。B 站 WebSocket 已有基础连接、WBI 签名、鉴权、心跳、可断开接收循环、未压缩 JSON 弹幕解析、buvid3 兜底和同 UID 脱敏昵称缓存；OBS 本地服务已有基础 HTTP/SSE 链路；更完整的消息解析还需要继续补。

压缩弹幕包的原生解压代码已经接入 `zlib` 和 `brotli`，但它是可选编译能力：CMake 找到这两个原生库时会定义 `PITLANE_LITE_HAS_COMPRESSION`、握手请求 `protover:3` 并启用 version 2/3 解包；找不到时会请求 `protover:0`，仍可编译运行。

## 建议工具链

安装 Visual Studio 2022，并选择：

- 使用 C++ 的桌面开发
- Windows App SDK C++ 模板
- C++/WinRT
- Windows 10/11 SDK

WinUI 3 本身会带来 Windows App SDK 运行时依赖。如果最终目标是尽可能小的安装包，建议控制台界面可以使用 WinUI，但透明叠加窗口和 OBS 服务仍优先保持 Win32 原生实现。

如果要启用 B 站压缩弹幕包解压，可以使用 VS 自带 vcpkg 重新配置：

```powershell
cmake -S windows-lite-native -B windows-lite-native/build-vcpkg -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build windows-lite-native/build-vcpkg --config Debug
```

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
    TextSanitizer.cpp
    TextSanitizer.h
```

## Agent 接手说明

这个目录是给“小体积原生版”单独推进的，不是 WPF 版安装包目录。

- 人看：当前它已经能打开一个 Win32 测试窗口，能连 B 站基础 WSS 弹幕，能显示原生透明叠加层，也能启动基础 OBS 本地服务。
- Agent 看：优先保持目录边界清晰，只改 `windows-lite-native/` 和必要的根文档；不要把 `build/`、`build-vs/`、`build-vcpkg/`、`BundleArtifacts/` 等构建产物提交。
- 构建验证：普通构建可以用 `windows-lite-native/build-vs`；压缩包解码需要 vcpkg 能解析 `zlib` 和 `brotli`。
- 已知缺口：赛车发车动画、最终 OBS 浏览器源布局、TCP 回退、完整 JSON 解析、小体积安装器还没完成。
- 和 WPF 版关系：`windows-native/` 是当前可发布的 WPF/.NET 8 版本；`windows-lite-native/` 是长期减小体积、减少运行时依赖的 C++ 重写方向。

## 下一阶段

1. 创建 WinUI/C++ 应用壳和设置页。
2. 完善 B 站 websocket/TCP 弹幕客户端，包括压缩包解压和 TCP 回退。
3. 完善本地 OBS HTTP/SSE 服务的最终浏览器源布局。
4. 使用 Win32 layered window 或 Direct2D 渲染叠加层。
5. 增加小体积原生安装器。

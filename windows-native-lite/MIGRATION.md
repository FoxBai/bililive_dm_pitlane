# Windows Native Lite 迁移说明

## 从 WPF 版保留的行为

- 用户设置：
  - 直播间 ID 或 URL
  - 网页 Cookie
  - User-Agent
  - buvid3
  - OBS 端口
  - 发车间隔
  - 等待队列上限
  - 同屏最少组数
  - 昵称和评论长度限制
  - 重复字符上限
  - 横向最大宽度
  - 仅显示醒目留言模式
- 消息行为：
  - 醒目留言优先显示。
  - 队列满时优先丢弃普通评论。
  - 重复消息 ID 会被抑制。
  - 开启仅显示醒目留言时，普通评论不会入队。
- 文字行为：
  - 去掉首尾空白。
  - 压缩过长的重复字符。
  - 限制展示长度。
  - 昵称为空时回退为 `匿名观众`。

## 原生模块拆分

### UI 壳

如果接受 Windows App SDK 的运行时依赖，控制台可以使用 WinUI/C++。通用设置、消息模型和队列逻辑应保持在 `src/` 下的共享 C++ 层，避免绑定到具体 UI 框架。

### 叠加层

为了体积和可控性，优先使用 Win32 layered、click-through、topmost 窗口。第一版可用 Direct2D 或 GDI+ 渲染；只有动画流畅度确实不够时，再考虑 DirectComposition。

### OBS 服务

实现一个仅监听本机回环地址的 HTTP 服务：

- `GET /overlay`
- `GET /events`
- `GET /assets/...`

Lite 第一阶段可以先省略 `/overlay` 的 HTML 浏览器源，只驱动原生叠加窗口。为了后续 OBS 浏览器源兼容，接口路径仍建议保留。

### B 站弹幕客户端

迁移 C# 版已有行为：

- 解析真实房间号。已完成基础接入。
- 使用 WBI 签名请求 `getDanmuInfo`。已完成基础接入。
- 使用 `x/frontend/finger/spi` 作为 buvid3 兜底。已完成基础接入。
- 清洗 Cookie，并在有 `DedeUserID` 时用于握手。
- 优先 websocket，失败后回退 TCP。已完成基础 WSS 连接。
- 发送心跳。已完成基础接入。
- 支持 zlib/brotli 解压。已完成可选原生依赖接入，CMake 找到 `zlib` 和 `brotli` 时自动启用，并在握手中请求 `protover:3`。
- 抑制脱敏昵称，并使用 UID 昵称缓存。已完成基础缓存，同一 UID 后续出现星号昵称时会优先复用已见过的完整昵称；没有缓存可恢复时会跳过该条弹幕。

### 安装器

小体积安装器建议使用原生安装技术：

- WiX Toolset
- NSIS
- Inno Setup

Lite 应用应尽量只依赖现代 Windows 已经具备的组件。如果使用 WinUI / Windows App SDK，需要先评估 bootstrap/runtime 体积，再决定是否作为最终方向。

## 第一个可构建目标

1. 最小原生控制窗口，带本地测试按钮。已完成。
2. 原生消息队列和文字清洗。已完成。
3. Win32 透明叠加窗口。已完成基础预览。
4. 从仓库加载现有 `assets/comment-box/comment_frame.png`、赛车图片和字体。已完成基础接入。
5. 实现赛车发车动画。
6. 接入 B 站 WebSocket 握手、心跳、解包和消息解析。已完成基础未压缩 JSON 流、心跳、可断开连接和可选压缩包解析。

以上稳定后，再迁移真实 B 站弹幕抓取。

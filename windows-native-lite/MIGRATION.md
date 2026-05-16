# Windows Native Lite Migration

## Preserve From The WPF App

- User settings:
  - room input
  - cookie
  - user agent
  - buvid3
  - OBS port
  - launch interval
  - queue limit
  - visible item count
  - nickname/comment length limits
  - repeated-character limit
  - max stage width
  - only SuperChat mode
- Message behavior:
  - SuperChat has priority.
  - Normal comments are dropped first when the queue is full.
  - Duplicate message IDs are suppressed.
  - Normal comments are ignored when only SuperChat mode is enabled.
- Text behavior:
  - trim whitespace
  - clamp repeated characters
  - clamp display length
  - fallback nickname to `匿名观众`

## Native Modules

### UI Shell

Use WinUI/C++ for the control panel if Windows App SDK packaging is acceptable. Keep a small shared C++ settings layer under `src/`.

### Overlay

Prefer a Win32 layered, click-through, topmost window for size and predictability. Render with Direct2D/GDI+ first; move to DirectComposition only if animation smoothness requires it.

### OBS Server

Implement a loopback HTTP listener with:

- `GET /overlay`
- `GET /events`
- `GET /assets/...`

The first lite milestone can omit `/overlay` HTML and drive only the native overlay. Keep the endpoint for OBS browser-source compatibility later.

### Bilibili Client

Port the C# client behavior:

- room ID resolution
- WBI-signed `getDanmuInfo`
- `x/frontend/finger/spi` buvid3 fallback
- cookie normalization and `DedeUserID` auth
- websocket first, TCP fallback
- heartbeat
- zlib/brotli decompression
- masked-name suppression and UID name cache

### Installer

For a small installer, use native installer technology:

- WiX Toolset
- NSIS
- Inno Setup

The lite app should be runtime-dependent only on Windows components already present on modern Windows. If WinUI/Windows App SDK is used, validate the bootstrap/runtime size before committing to it.

## First Buildable Target

1. Win32 transparent overlay window.
2. Local test buttons in a minimal native control window.
3. Native message queue and text sanitizer.
4. Existing `assets/comment-box/comment_frame.png`, cars, and fonts loaded from the repo.

Once that is stable, port live Bilibili fetching.

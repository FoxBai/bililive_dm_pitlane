# Pitlane Danmaku Lite

`windows-native-lite` is the small-package rewrite track for the Windows overlay app.

The current WPF app is easy to ship because it is self-contained, but that also bundles the .NET desktop runtime and makes the installer large. This track targets a much smaller Windows build by moving the app logic to native C++ and keeping only the existing Pitlane assets and user-facing behavior.

## Target

- C++ / WinUI front end for the control panel.
- Native transparent overlay window.
- Native local OBS HTTP/SSE server.
- Native Bilibili live danmaku client.
- Existing `assets/` folder reused as-is.
- No .NET runtime requirement.

## Current Scope

This first pass is a migration scaffold, not a finished app. It establishes:

- The lightweight domain model for messages and settings.
- Text cleaning and queue behavior matching the WPF version.
- A clear module split for the future WinUI/C++ implementation.
- A migration checklist for the network, overlay, and installer work.

## Suggested Tooling

Install Visual Studio 2022 with:

- Desktop development with C++
- Windows App SDK C++ templates
- C++/WinRT
- Windows 10/11 SDK

WinUI 3 has its own runtime dependency through Windows App SDK. If the absolute smallest installer is the main goal, the overlay window and OBS server should remain native Win32 even if the settings panel uses WinUI.

## Layout

```text
windows-native-lite/
  README.md
  MIGRATION.md
  CMakeLists.txt
  src/
    AppSettings.h
    ChatMessage.h
    MessagePipeline.cpp
    MessagePipeline.h
    TextSanitizer.cpp
    TextSanitizer.h
```

## Next Milestones

1. Create the WinUI/C++ app shell and settings page.
2. Port the Bilibili websocket/TCP client.
3. Port the local OBS HTTP/SSE server.
4. Render the overlay with a layered Win32 window or Direct2D.
5. Add a tiny native installer.

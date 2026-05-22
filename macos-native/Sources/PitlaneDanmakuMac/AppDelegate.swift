import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var mainWindowController: MainWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        AppPaths.ensureRuntimeDirectories()
        configureMenus()

        let controller = MainWindowController()
        mainWindowController = controller
        showMainWindow()
        DispatchQueue.main.async { [weak self] in
            self?.showMainWindow()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
            self?.showMainWindow()
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showMainWindow()
        return true
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ notification: Notification) {
        mainWindowController?.shutdown()
    }

    private func showMainWindow() {
        mainWindowController?.showWindow(nil)
        mainWindowController?.window?.makeKeyAndOrderFront(nil)
        mainWindowController?.window?.orderFrontRegardless()
        NSApp.activate(ignoringOtherApps: true)
    }

    private func configureMenus() {
        let mainMenu = NSMenu()
        mainMenu.addItem(appMenuItem())
        mainMenu.addItem(fileMenuItem())
        mainMenu.addItem(windowMenuItem())
        mainMenu.addItem(helpMenuItem())
        NSApp.mainMenu = mainMenu
    }

    private func appMenuItem() -> NSMenuItem {
        let item = NSMenuItem()
        let menu = NSMenu()
        item.submenu = menu

        menu.addItem(targetedMenuItem(
            title: "关于 \(ApplicationMetadata.name)",
            action: #selector(showAboutPanel),
            keyEquivalent: ""
        ))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "隐藏 \(ApplicationMetadata.name)", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h"))

        let hideOthers = NSMenuItem(title: "隐藏其他", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h")
        hideOthers.keyEquivalentModifierMask = [.command, .option]
        menu.addItem(hideOthers)
        menu.addItem(NSMenuItem(title: "全部显示", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: ""))
        menu.addItem(.separator())
        menu.addItem(NSMenuItem(title: "退出 \(ApplicationMetadata.name)", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        return item
    }

    private func fileMenuItem() -> NSMenuItem {
        let item = NSMenuItem()
        let menu = NSMenu(title: "文件")
        item.submenu = menu
        menu.addItem(targetedMenuItem(title: "打开设置目录", action: #selector(openSettingsDirectory), keyEquivalent: ","))
        return item
    }

    private func windowMenuItem() -> NSMenuItem {
        let item = NSMenuItem()
        let menu = NSMenu(title: "窗口")
        item.submenu = menu
        menu.addItem(NSMenuItem(title: "最小化", action: #selector(NSWindow.miniaturize(_:)), keyEquivalent: "m"))
        menu.addItem(NSMenuItem(title: "缩放", action: #selector(NSWindow.zoom(_:)), keyEquivalent: ""))
        NSApp.windowsMenu = menu
        return item
    }

    private func helpMenuItem() -> NSMenuItem {
        let item = NSMenuItem()
        let menu = NSMenu(title: "帮助")
        item.submenu = menu
        menu.addItem(targetedMenuItem(title: "打开诊断目录", action: #selector(openDiagnosticsDirectory), keyEquivalent: ""))
        menu.addItem(targetedMenuItem(title: "打开 macOS 崩溃日志目录", action: #selector(openCrashReportsDirectory), keyEquivalent: ""))
        NSApp.helpMenu = menu
        return item
    }

    private func targetedMenuItem(title: String, action: Selector, keyEquivalent: String) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: keyEquivalent)
        item.target = self
        return item
    }

    @objc private func showAboutPanel() {
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: ApplicationMetadata.name,
            .applicationVersion: ApplicationMetadata.version,
            .version: "Build \(ApplicationMetadata.build)",
            .credits: NSAttributedString(string: "Bilibili live comments for OBS and desktop pit lane overlays.\n\(ApplicationMetadata.copyright)")
        ])
    }

    @objc private func openSettingsDirectory() {
        AppPaths.openDirectory(AppPaths.supportDirectory)
    }

    @objc private func openDiagnosticsDirectory() {
        AppPaths.openDirectory(AppPaths.diagnosticsDirectory)
    }

    @objc private func openCrashReportsDirectory() {
        AppPaths.openDirectory(AppPaths.crashReportsDirectory)
    }
}

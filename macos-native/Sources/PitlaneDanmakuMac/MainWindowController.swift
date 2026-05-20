import AppKit

final class MainWindowController: NSWindowController, NSWindowDelegate {
    private let log = LogService()
    private var settings = SettingsStore.load()
    private lazy var assets = AssetCatalog(log: log)
    private lazy var pipeline = MessagePipeline(settings: settings, log: log)
    private lazy var obsServer = LocalObsServer(assets: assets, log: log)
    private var bilibiliClient: BilibiliWebRoomClient?
    private var overlayWindowController: OverlayWindowController?

    private let roomInputBox = NSTextField()
    private let cookieTextView = NSTextView()
    private let userAgentBox = NSTextField()
    private let buvid3Box = NSTextField()
    private let obsPortBox = NSTextField()
    private let launchIntervalBox = NSTextField()
    private let queueLimitBox = NSTextField()
    private let minVisibleBox = NSTextField()
    private let nicknameLengthBox = NSTextField()
    private let messageLengthBox = NSTextField()
    private let repeatLimitBox = NSTextField()
    private let maxStageWidthBox = NSTextField()
    private let onlySuperChatBox = NSButton(checkboxWithTitle: "仅显示醒目留言", target: nil, action: nil)
    private let obsURLBox = NSTextField()
    private let statusText = NSTextField(labelWithString: "未连接")
    private let assetText = NSTextField(labelWithString: "")
    private let logTextView = NSTextView()

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1180, height: 760),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Pitlane Danmaku Mac"
        window.minSize = NSSize(width: 1020, height: 680)
        window.center()
        super.init(window: window)
        window.delegate = self

        buildUI()
        wireServices()
        loadSettingsToUI()

        assetText.stringValue = "素材：\(assets.cars.count) 辆赛车，评论框 \(FileManager.default.fileExists(atPath: assets.commentFrameURL.path) ? "已加载" : "缺失")；字体 HarmonyOS Sans SC"
        obsURLBox.stringValue = obsServer.overlayURL
        restartObsServer()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func shutdown() {
        disconnectBilibiliClient()
        obsServer.stop()
        pipeline.stop()
        overlayWindowController?.close()
    }

    func windowWillClose(_ notification: Notification) {
        NSApp.terminate(nil)
    }

    private func wireServices() {
        log.onEntry = { [weak self] entry in
            DispatchQueue.main.async {
                self?.appendLog(entry)
            }
        }

        pipeline.onMessageReady = { [weak self] message in
            guard let self else { return }
            self.obsServer.broadcast(message)
            DispatchQueue.main.async {
                self.overlayWindowController?.showMessage(message)
                self.statusText.stringValue = message.isSuperChat ? "醒目留言：\(message.userName)" : "普通评论：\(message.userName)"
            }
        }
        pipeline.start()
    }

    private func buildUI() {
        guard let contentView = window?.contentView else { return }
        contentView.wantsLayer = true
        contentView.layer?.backgroundColor = NSColor(hex: 0x0D1117).cgColor

        let splitView = NSSplitView()
        splitView.isVertical = true
        splitView.dividerStyle = .thin
        splitView.translatesAutoresizingMaskIntoConstraints = false
        contentView.addSubview(splitView)

        NSLayoutConstraint.activate([
            splitView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 18),
            splitView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -18),
            splitView.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 18),
            splitView.bottomAnchor.constraint(equalTo: contentView.bottomAnchor, constant: -18)
        ])

        let leftScroll = NSScrollView()
        leftScroll.hasVerticalScroller = true
        leftScroll.drawsBackground = false
        let leftStack = verticalStack(spacing: 14)
        leftStack.edgeInsets = NSEdgeInsets(top: 0, left: 0, bottom: 0, right: 12)
        leftScroll.documentView = leftStack
        leftScroll.translatesAutoresizingMaskIntoConstraints = false
        leftStack.translatesAutoresizingMaskIntoConstraints = false
        leftStack.widthAnchor.constraint(equalToConstant: 420).isActive = true

        let headerTitle = NSTextField(labelWithString: "Pitlane Danmaku")
        headerTitle.font = .systemFont(ofSize: 30, weight: .bold)
        headerTitle.textColor = .pitlaneText
        let headerSubtitle = NSTextField(labelWithString: "macOS 原生控制台")
        headerSubtitle.textColor = .pitlaneMuted
        leftStack.addArrangedSubview(headerTitle)
        leftStack.addArrangedSubview(headerSubtitle)
        leftStack.addArrangedSubview(roomGroup())
        leftStack.addArrangedSubview(displayGroup())
        leftStack.addArrangedSubview(outputGroup())

        let rightStack = verticalStack(spacing: 12)
        rightStack.translatesAutoresizingMaskIntoConstraints = false
        rightStack.addArrangedSubview(statusPanel())
        rightStack.addArrangedSubview(logPanel())

        splitView.addArrangedSubview(leftScroll)
        splitView.addArrangedSubview(rightStack)
        leftScroll.widthAnchor.constraint(equalToConstant: 440).isActive = true
    }

    private func roomGroup() -> NSView {
        let stack = verticalStack()
        stack.addArrangedSubview(label("直播间 ID 或 URL"))
        stack.addArrangedSubview(configureTextField(roomInputBox))
        stack.addArrangedSubview(label("网页 Cookie"))
        stack.addArrangedSubview(textViewScroll(cookieTextView, height: 74))
        stack.addArrangedSubview(label("User-Agent"))
        stack.addArrangedSubview(configureTextField(userAgentBox))
        stack.addArrangedSubview(label("buvid3"))
        stack.addArrangedSubview(configureTextField(buvid3Box))

        let buttons = horizontalStack()
        buttons.addArrangedSubview(button("连接", action: #selector(connectButtonClicked)))
        buttons.addArrangedSubview(button("断开", action: #selector(disconnectButtonClicked), tint: NSColor(hex: 0xFCA5A5)))
        stack.addArrangedSubview(buttons)
        return groupBox("B站直播间", stack)
    }

    private func displayGroup() -> NSView {
        let stack = verticalStack()
        stack.addArrangedSubview(twoColumnRow("OBS 端口", obsPortBox, "发车间隔 ms", launchIntervalBox))
        stack.addArrangedSubview(twoColumnRow("等待队列上限", queueLimitBox, "同屏最少组数", minVisibleBox))
        stack.addArrangedSubview(twoColumnRow("昵称长度", nicknameLengthBox, "评论长度", messageLengthBox))
        stack.addArrangedSubview(twoColumnRow("重复字符上限", repeatLimitBox, "横向最大宽度", maxStageWidthBox))
        onlySuperChatBox.target = self
        onlySuperChatBox.action = #selector(settingsChanged)
        onlySuperChatBox.contentTintColor = .pitlaneText
        stack.addArrangedSubview(onlySuperChatBox)
        return groupBox("显示和队列", stack)
    }

    private func outputGroup() -> NSView {
        let stack = verticalStack()
        stack.addArrangedSubview(label("OBS 浏览器源"))
        obsURLBox.isEditable = false
        stack.addArrangedSubview(configureTextField(obsURLBox))

        let buttons = horizontalStack()
        buttons.addArrangedSubview(button("重启 OBS 服务", action: #selector(restartObsButtonClicked)))
        buttons.addArrangedSubview(button("打开悬浮窗", action: #selector(openOverlayButtonClicked)))
        buttons.addArrangedSubview(button("隐藏悬浮窗", action: #selector(hideOverlayButtonClicked), tint: NSColor(hex: 0x94A3B8)))
        stack.addArrangedSubview(buttons)
        return groupBox("输出", stack)
    }

    private func statusPanel() -> NSView {
        let box = NSBox()
        box.boxType = .custom
        box.cornerRadius = 8
        box.fillColor = .pitlanePanel
        box.borderColor = .pitlaneBorder
        box.contentViewMargins = NSSize(width: 16, height: 16)

        let stack = verticalStack(spacing: 10)
        statusText.font = .systemFont(ofSize: 24, weight: .semibold)
        statusText.textColor = .pitlaneText
        assetText.textColor = .pitlaneMuted
        stack.addArrangedSubview(statusText)
        stack.addArrangedSubview(assetText)

        let buttons = horizontalStack()
        buttons.addArrangedSubview(button("普通评论", action: #selector(simulateCommentButtonClicked)))
        buttons.addArrangedSubview(button("醒目留言", action: #selector(simulateSuperChatButtonClicked), tint: NSColor(hex: 0xFDE68A)))
        buttons.addArrangedSubview(button("连发测试", action: #selector(burstButtonClicked), tint: NSColor(hex: 0xC4B5FD)))
        stack.addArrangedSubview(buttons)

        box.contentView = stack
        return box
    }

    private func logPanel() -> NSView {
        let box = NSBox()
        box.boxType = .custom
        box.cornerRadius = 8
        box.fillColor = .pitlanePanel
        box.borderColor = .pitlaneBorder
        box.contentViewMargins = NSSize(width: 14, height: 14)

        let stack = verticalStack(spacing: 10)
        let header = horizontalStack()
        let title = NSTextField(labelWithString: "运行日志")
        title.font = .systemFont(ofSize: 18, weight: .semibold)
        title.textColor = .pitlaneText
        header.addArrangedSubview(title)
        header.addArrangedSubview(spacer())
        header.addArrangedSubview(button("清空日志", action: #selector(clearLogButtonClicked)))
        header.addArrangedSubview(button("导出日志", action: #selector(exportLogButtonClicked)))
        stack.addArrangedSubview(header)

        logTextView.isEditable = false
        logTextView.font = .monospacedSystemFont(ofSize: 13, weight: .regular)
        logTextView.textColor = .pitlaneText
        logTextView.backgroundColor = NSColor(hex: 0x0B1018)
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.documentView = logTextView
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 420).isActive = true
        stack.addArrangedSubview(scroll)

        box.contentView = stack
        return box
    }

    @objc private func connectButtonClicked() {
        applySettingsFromUI()
        guard !settings.roomInput.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            log.warn("请先填写 B站直播间 ID 或 URL。")
            return
        }

        disconnectBilibiliClient()
        let client = BilibiliWebRoomClient(log: log)
        client.onMessageReceived = { [weak self] message in
            self?.pipeline.enqueue(message)
        }
        bilibiliClient = client
        client.start(settings: settings)
        statusText.stringValue = "正在连接 B站直播间"
        log.info("B站直播间连接任务已启动。")
    }

    @objc private func disconnectButtonClicked() {
        disconnectBilibiliClient()
        statusText.stringValue = "已断开"
        log.info("已断开 B站直播间。")
    }

    @objc private func restartObsButtonClicked() {
        applySettingsFromUI()
        restartObsServer()
    }

    @objc private func openOverlayButtonClicked() {
        applySettingsFromUI()
        if let overlayWindowController {
            overlayWindowController.applySettings(settings)
            overlayWindowController.showWindow(nil)
            overlayWindowController.window?.orderFrontRegardless()
            log.info("桌面透明悬浮窗已显示。")
            return
        }

        let controller = OverlayWindowController(assets: assets, settings: settings)
        overlayWindowController = controller
        controller.showWindow(nil)
        controller.window?.orderFrontRegardless()
        log.info("桌面透明悬浮窗已打开。")
    }

    @objc private func hideOverlayButtonClicked() {
        guard let overlayWindowController else {
            log.info("桌面透明悬浮窗当前未打开。")
            return
        }

        overlayWindowController.window?.orderOut(nil)
        log.info("桌面透明悬浮窗已隐藏。")
    }

    @objc private func simulateCommentButtonClicked() {
        applySettingsFromUI()
        pipeline.enqueue(.comment("PitCrew", "普通评论测试，赛车准备入场"))
    }

    @objc private func simulateSuperChatButtonClicked() {
        applySettingsFromUI()
        pipeline.enqueue(.superChat("RaceControl", "醒目留言优先进入发车队列", price: 30))
    }

    @objc private func burstButtonClicked() {
        applySettingsFromUI()
        Task {
            for index in 1...24 {
                let message: ChatMessage = index % 7 == 0
                    ? .superChat(String(format: "SC%02d", index), String(format: "第 %02d 条醒目留言", index), price: Decimal(30 + index))
                    : .comment(String(format: "车迷%02d", index), String(format: "第 %02d 条连发评论，测试队列减速和出场", index))
                pipeline.enqueue(message)
                try? await Task.sleep(nanoseconds: 80_000_000)
            }
        }
    }

    @objc private func clearLogButtonClicked() {
        log.clear()
        logTextView.string = ""
    }

    @objc private func exportLogButtonClicked() {
        let panel = NSSavePanel()
        panel.title = "导出 Pitlane Danmaku 日志"
        panel.allowedContentTypes = [.plainText]
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        panel.nameFieldStringValue = "pitlane-danmaku-\(formatter.string(from: Date())).txt"
        panel.beginSheetModal(for: window!) { [weak self] response in
            guard response == .OK, let url = panel.url, let self else { return }
            do {
                try self.log.snapshot.joined(separator: "\n").write(to: url, atomically: true, encoding: .utf8)
                self.log.info("日志已导出：\(url.path)")
            } catch {
                self.log.error("日志导出失败：\(error.localizedDescription)")
            }
        }
    }

    @objc private func settingsChanged() {
        applySettingsFromUI()
    }

    private func restartObsServer() {
        do {
            try obsServer.start(settings: settings)
            obsURLBox.stringValue = obsServer.overlayURL
        } catch {
            log.error("OBS 服务启动失败：\(error.localizedDescription)")
        }
    }

    private func disconnectBilibiliClient() {
        bilibiliClient?.stop()
        bilibiliClient = nil
    }

    private func applySettingsFromUI() {
        settings.roomInput = roomInputBox.stringValue
        settings.cookie = cookieTextView.string
        settings.userAgent = userAgentBox.stringValue
        settings.buvid3 = buvid3Box.stringValue
        settings.obsPort = intValue(obsPortBox, fallback: settings.obsPort)
        settings.launchIntervalMs = intValue(launchIntervalBox, fallback: settings.launchIntervalMs)
        settings.queueLimit = intValue(queueLimitBox, fallback: settings.queueLimit)
        settings.minVisibleItems = intValue(minVisibleBox, fallback: settings.minVisibleItems)
        settings.maxNicknameLength = intValue(nicknameLengthBox, fallback: settings.maxNicknameLength)
        settings.maxMessageLength = intValue(messageLengthBox, fallback: settings.maxMessageLength)
        settings.maxRepeatCharacters = intValue(repeatLimitBox, fallback: settings.maxRepeatCharacters)
        settings.maxStageWidth = intValue(maxStageWidthBox, fallback: settings.maxStageWidth)
        settings.onlySuperChat = onlySuperChatBox.state == .on
        settings.normalize()

        pipeline.updateSettings(settings)
        overlayWindowController?.applySettings(settings)
        SettingsStore.save(settings)
        loadSettingsToUI()
        obsURLBox.stringValue = "http://127.0.0.1:\(settings.obsPort)/overlay"
    }

    private func loadSettingsToUI() {
        roomInputBox.stringValue = settings.roomInput
        cookieTextView.string = settings.cookie
        userAgentBox.stringValue = settings.userAgent
        buvid3Box.stringValue = settings.buvid3
        obsPortBox.stringValue = String(settings.obsPort)
        launchIntervalBox.stringValue = String(settings.launchIntervalMs)
        queueLimitBox.stringValue = String(settings.queueLimit)
        minVisibleBox.stringValue = String(settings.minVisibleItems)
        nicknameLengthBox.stringValue = String(settings.maxNicknameLength)
        messageLengthBox.stringValue = String(settings.maxMessageLength)
        repeatLimitBox.stringValue = String(settings.maxRepeatCharacters)
        maxStageWidthBox.stringValue = String(settings.maxStageWidth)
        onlySuperChatBox.state = settings.onlySuperChat ? .on : .off
    }

    private func appendLog(_ entry: String) {
        logTextView.string += entry + "\n"
        logTextView.scrollToEndOfDocument(nil)
    }

    private func intValue(_ field: NSTextField, fallback: Int) -> Int {
        Int(field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)) ?? fallback
    }

    private func groupBox(_ title: String, _ view: NSView) -> NSBox {
        let box = NSBox()
        box.title = title
        box.boxType = .custom
        box.cornerRadius = 8
        box.fillColor = .pitlanePanel
        box.borderColor = .pitlaneBorder
        box.titlePosition = .atTop
        box.contentViewMargins = NSSize(width: 14, height: 14)
        box.contentView = view
        return box
    }

    private func label(_ text: String) -> NSTextField {
        let field = NSTextField(labelWithString: text)
        field.textColor = .pitlaneText
        return field
    }

    private func configureTextField(_ field: NSTextField) -> NSTextField {
        field.isBordered = true
        field.isBezeled = true
        field.drawsBackground = true
        field.backgroundColor = NSColor(hex: 0x101722)
        field.textColor = .pitlaneText
        field.translatesAutoresizingMaskIntoConstraints = false
        field.heightAnchor.constraint(greaterThanOrEqualToConstant: 30).isActive = true
        return field
    }

    private func button(_ title: String, action: Selector, tint: NSColor = NSColor(hex: 0x7DD3FC)) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.bezelStyle = .rounded
        button.contentTintColor = tint
        return button
    }

    private func textViewScroll(_ textView: NSTextView, height: CGFloat) -> NSScrollView {
        textView.font = .systemFont(ofSize: 13)
        textView.textColor = .pitlaneText
        textView.backgroundColor = NSColor(hex: 0x101722)
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.documentView = textView
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.heightAnchor.constraint(equalToConstant: height).isActive = true
        return scroll
    }

    private func twoColumnRow(_ leftTitle: String, _ leftField: NSTextField, _ rightTitle: String, _ rightField: NSTextField) -> NSView {
        let row = horizontalStack(spacing: 14)
        let left = verticalStack(spacing: 6)
        left.addArrangedSubview(label(leftTitle))
        left.addArrangedSubview(configureTextField(leftField))
        let right = verticalStack(spacing: 6)
        right.addArrangedSubview(label(rightTitle))
        right.addArrangedSubview(configureTextField(rightField))
        row.addArrangedSubview(left)
        row.addArrangedSubview(right)
        left.widthAnchor.constraint(equalTo: right.widthAnchor).isActive = true
        return row
    }

    private func verticalStack(spacing: CGFloat = 8) -> NSStackView {
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .width
        stack.distribution = .fill
        stack.spacing = spacing
        return stack
    }

    private func horizontalStack(spacing: CGFloat = 8) -> NSStackView {
        let stack = NSStackView()
        stack.orientation = .horizontal
        stack.alignment = .centerY
        stack.distribution = .fill
        stack.spacing = spacing
        return stack
    }

    private func spacer() -> NSView {
        let view = NSView()
        view.setContentHuggingPriority(.defaultLow, for: .horizontal)
        return view
    }
}

extension NSColor {
    static let pitlanePanel = NSColor(hex: 0x18202B)
    static let pitlaneBorder = NSColor(hex: 0x2A3544)
    static let pitlaneText = NSColor(hex: 0xEFF5FF)
    static let pitlaneMuted = NSColor(hex: 0x93A3B8)

    convenience init(hex: Int, alpha: CGFloat = 1) {
        self.init(
            calibratedRed: CGFloat((hex >> 16) & 0xff) / 255,
            green: CGFloat((hex >> 8) & 0xff) / 255,
            blue: CGFloat(hex & 0xff) / 255,
            alpha: alpha
        )
    }
}

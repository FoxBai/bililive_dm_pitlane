import AppKit

final class OverlayWindowController: NSWindowController {
    private let canvasView: OverlayCanvasView

    init(assets: AssetCatalog, settings: AppSettings) {
        canvasView = OverlayCanvasView(assets: assets, settings: settings)
        let screenFrame = NSScreen.main?.visibleFrame ?? NSRect(x: 80, y: 80, width: 1280, height: 240)
        let frame = NSRect(x: screenFrame.minX + 40, y: screenFrame.minY + 40, width: min(1440, screenFrame.width - 80), height: 240)
        let panel = NSPanel(
            contentRect: frame,
            styleMask: [.borderless, .resizable, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        panel.title = "Pitlane Danmaku Overlay"
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .floating
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        panel.contentView = canvasView
        super.init(window: panel)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func applySettings(_ settings: AppSettings) {
        canvasView.applySettings(settings)
    }

    func showMessage(_ message: ChatMessage) {
        canvasView.showMessage(message)
    }
}

private final class OverlayCanvasView: NSView {
    private struct RaceVisual {
        var message: ChatMessage
        var car: CarAsset
        var carImage: NSImage?
        var x: CGFloat
        var targetX: CGFloat
        var leaving: Bool
    }

    private let assets: AssetCatalog
    private var settings: AppSettings
    private var frameImage: NSImage?
    private var items: [RaceVisual] = []
    private var timer: Timer?

    override var isFlipped: Bool {
        true
    }

    init(assets: AssetCatalog, settings: AppSettings) {
        self.assets = assets
        self.settings = settings.normalized()
        self.frameImage = NSImage(contentsOf: assets.commentFrameURL)
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        timer?.invalidate()
    }

    func applySettings(_ settings: AppSettings) {
        self.settings = settings.normalized()
        layoutItems()
    }

    func showMessage(_ message: ChatMessage) {
        do {
            let car = try assets.pickCar()
            let visual = RaceVisual(
                message: message,
                car: car,
                carImage: NSImage(contentsOfFile: car.absolutePath),
                x: -OverlayLayout.baseWidth,
                targetX: 0,
                leaving: false
            )
            items.insert(visual, at: 0)
            layoutItems()
        } catch {
            // Missing assets are already reported by the control window.
        }
    }

    override func mouseDown(with event: NSEvent) {
        window?.performDrag(with: event)
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        layoutItems()
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        NSGraphicsContext.current?.imageInterpolation = .high

        let scale = computeScale()
        let top = max(0, bounds.height - OverlayLayout.baseHeight * scale - 10)

        for item in items.reversed() {
            draw(item, scale: scale, top: top)
        }
    }

    private func draw(_ item: RaceVisual, scale: CGFloat, top: CGFloat) {
        func rect(_ x: CGFloat, _ y: CGFloat, _ width: CGFloat, _ height: CGFloat) -> NSRect {
            NSRect(x: item.x + x * scale, y: top + y * scale, width: width * scale, height: height * scale)
        }

        frameImage?.draw(in: rect(0, 0, OverlayLayout.frameWidth, OverlayLayout.frameHeight), from: .zero, operation: .sourceOver, fraction: 1)

        let carTop = OverlayLayout.baseHeight - item.car.height - OverlayLayout.carBottom
        item.carImage?.draw(
            in: rect(OverlayLayout.carLeft, carTop, item.car.width, item.car.height),
            from: .zero,
            operation: .sourceOver,
            fraction: 1
        )

        let textX = item.x + OverlayLayout.textLeft * scale
        let textY = top + OverlayLayout.textTop * scale
        drawText(for: item.message, x: textX, y: textY, scale: scale)
    }

    private func drawText(for message: ChatMessage, x: CGFloat, y: CGFloat, scale: CGFloat) {
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.55)
        shadow.shadowBlurRadius = 10 * scale
        shadow.shadowOffset = NSSize(width: 0, height: 3 * scale)

        let paragraph = NSMutableParagraphStyle()
        paragraph.lineBreakMode = .byTruncatingTail

        var nameX = x
        let nameY = y
        if message.isSuperChat {
            let badgeRect = NSRect(x: nameX, y: nameY - 5 * scale, width: 78 * scale, height: 46 * scale)
            NSColor(hex: 0xFDE68A).setFill()
            NSBezierPath(roundedRect: badgeRect, xRadius: 10 * scale, yRadius: 10 * scale).fill()
            let badgeAttributes: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: 30 * scale, weight: .black),
                .foregroundColor: NSColor(hex: 0x16120A),
                .paragraphStyle: centeredParagraph()
            ]
            NSAttributedString(string: "SC", attributes: badgeAttributes).draw(in: badgeRect.insetBy(dx: 0, dy: 6 * scale))
            nameX += 90 * scale
        }

        let nameWidth = (message.isSuperChat ? OverlayLayout.superChatNameWidth : OverlayLayout.nameWidth) * scale
        let nameAttributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: OverlayLayout.nameFontSize * scale, weight: .black),
            .foregroundColor: NSColor.white,
            .paragraphStyle: paragraph,
            .shadow: shadow
        ]
        NSAttributedString(string: message.userName, attributes: nameAttributes)
            .draw(in: NSRect(x: nameX, y: nameY, width: nameWidth, height: 82 * scale))

        let messageParagraph = NSMutableParagraphStyle()
        messageParagraph.lineBreakMode = .byWordWrapping
        messageParagraph.minimumLineHeight = OverlayLayout.messageLineHeight * scale
        messageParagraph.maximumLineHeight = OverlayLayout.messageLineHeight * scale
        let messageAttributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: OverlayLayout.messageFontSize * scale, weight: .semibold),
            .foregroundColor: NSColor.white,
            .paragraphStyle: messageParagraph,
            .shadow: shadow
        ]
        NSAttributedString(string: message.text, attributes: messageAttributes)
            .draw(in: NSRect(
                x: x,
                y: y + (OverlayLayout.nameFontSize + OverlayLayout.messageTopMargin) * scale,
                width: OverlayLayout.textWidth * scale,
                height: OverlayLayout.messageMaxHeight * scale
            ))
    }

    private func centeredParagraph() -> NSMutableParagraphStyle {
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = .center
        return paragraph
    }

    private func layoutItems() {
        let scale = computeScale()
        let itemWidth = OverlayLayout.baseWidth * scale
        let gap = OverlayLayout.baseGap * scale
        let visibleWidth = min(bounds.width, CGFloat(settings.maxStageWidth))
        let capacity = max(settings.minVisibleItems, Int(floor(visibleWidth / max(1, itemWidth + gap))))

        while items.filter({ !$0.leaving }).count > capacity {
            guard let index = items.lastIndex(where: { !$0.leaving }) else {
                break
            }
            items[index].leaving = true
            items[index].targetX = bounds.width + itemWidth
        }

        var cursor = bounds.width - itemWidth - 10
        for index in stride(from: items.count - 1, through: 0, by: -1) {
            guard items.indices.contains(index), !items[index].leaving else { continue }
            items[index].targetX = max(-itemWidth, cursor)
            cursor -= itemWidth + gap
        }

        needsDisplay = true
    }

    private func computeScale() -> CGFloat {
        let heightScale = max(0.2, (bounds.height - 10) / OverlayLayout.baseHeight)
        let widthScale = max(
            0.2,
            bounds.width / (CGFloat(settings.minVisibleItems) * OverlayLayout.baseWidth + CGFloat(settings.minVisibleItems - 1) * OverlayLayout.baseGap)
        )
        return min(1, min(heightScale, widthScale))
    }

    private func tick() {
        guard !items.isEmpty else { return }
        let pressure = min(1.0, CGFloat(items.count) / max(1.0, CGFloat(settings.minVisibleItems) + 3.0))

        for index in stride(from: items.count - 1, through: 0, by: -1) {
            guard items.indices.contains(index) else { continue }
            let easing: CGFloat = items[index].leaving ? 0.075 : 0.045 + pressure * 0.035
            items[index].x += (items[index].targetX - items[index].x) * easing
            if items[index].leaving && items[index].x > bounds.width + OverlayLayout.baseWidth {
                items.remove(at: index)
            }
        }

        needsDisplay = true
    }
}

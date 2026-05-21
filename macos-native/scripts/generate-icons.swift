#!/usr/bin/env swift

import AppKit
import Foundation

let scriptURL = URL(fileURLWithPath: #filePath).standardizedFileURL
let macosRoot = scriptURL.deletingLastPathComponent().deletingLastPathComponent()
let repoRoot = macosRoot.deletingLastPathComponent()
let assetsURL = repoRoot.appendingPathComponent("assets", isDirectory: true)
let iconsetURL = URL(fileURLWithPath: NSTemporaryDirectory(), isDirectory: true)
    .appendingPathComponent("pitlane-icon.iconset", isDirectory: true)

let emoji = "🏎️"
let black = NSColor(calibratedRed: 0.015, green: 0.017, blue: 0.022, alpha: 1)

try FileManager.default.createDirectory(at: assetsURL, withIntermediateDirectories: true)
try? FileManager.default.removeItem(at: iconsetURL)
try FileManager.default.createDirectory(at: iconsetURL, withIntermediateDirectories: true)

func renderIcon(size: Int) throws -> Data {
    let rect = NSRect(x: 0, y: 0, width: size, height: size)
    guard let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: size,
        pixelsHigh: size,
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0
    ) else {
        throw NSError(domain: "PitlaneIcon", code: 1, userInfo: [NSLocalizedDescriptionKey: "Unable to allocate bitmap"])
    }

    rep.size = NSSize(width: size, height: size)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

    black.setFill()
    NSBezierPath(rect: rect).fill()

    let fontSize = CGFloat(size) * 0.66
    let font = NSFont(name: "Apple Color Emoji", size: fontSize) ?? NSFont.systemFont(ofSize: fontSize)
    let paragraph = NSMutableParagraphStyle()
    paragraph.alignment = .center

    let attributed = NSAttributedString(string: emoji, attributes: [
        .font: font,
        .paragraphStyle: paragraph
    ])
    let textSize = attributed.size()
    let textRect = NSRect(
        x: 0,
        y: (CGFloat(size) - textSize.height) * 0.5 + CGFloat(size) * 0.18,
        width: CGFloat(size),
        height: textSize.height
    )
    attributed.draw(in: textRect)

    NSGraphicsContext.restoreGraphicsState()

    guard let png = rep.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "PitlaneIcon", code: 2, userInfo: [NSLocalizedDescriptionKey: "Unable to encode PNG"])
    }
    return png
}

func writePNG(size: Int, name: String, to directory: URL) throws {
    let data = try renderIcon(size: size)
    try data.write(to: directory.appendingPathComponent(name), options: .atomic)
}

let iconsetEntries: [(Int, String)] = [
    (16, "icon_16x16.png"),
    (32, "icon_16x16@2x.png"),
    (32, "icon_32x32.png"),
    (64, "icon_32x32@2x.png"),
    (128, "icon_128x128.png"),
    (256, "icon_128x128@2x.png"),
    (256, "icon_256x256.png"),
    (512, "icon_256x256@2x.png"),
    (512, "icon_512x512.png"),
    (1024, "icon_512x512@2x.png")
]

for entry in iconsetEntries {
    try writePNG(size: entry.0, name: entry.1, to: iconsetURL)
}

try renderIcon(size: 1024).write(to: assetsURL.appendingPathComponent("icon.png"), options: .atomic)

let svg = """
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" role="img" aria-label="Pitlane Danmaku">
  <rect width="1024" height="1024" fill="#040406"/>
  <text x="512" y="500" text-anchor="middle" dominant-baseline="middle" font-family="Apple Color Emoji, Segoe UI Emoji, Noto Color Emoji, sans-serif" font-size="660">🏎️</text>
</svg>
""" + "\n"
try svg.write(to: assetsURL.appendingPathComponent("icon.svg"), atomically: true, encoding: .utf8)

let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = [
    "-c", "icns",
    iconsetURL.path,
    "-o", assetsURL.appendingPathComponent("icon.icns").path
]
try iconutil.run()
iconutil.waitUntilExit()
guard iconutil.terminationStatus == 0 else {
    throw NSError(domain: "PitlaneIcon", code: Int(iconutil.terminationStatus), userInfo: [NSLocalizedDescriptionKey: "iconutil failed"])
}

func littleEndian(_ value: UInt16) -> [UInt8] {
    [UInt8(value & 0xff), UInt8((value >> 8) & 0xff)]
}

func littleEndian(_ value: UInt32) -> [UInt8] {
    [
        UInt8(value & 0xff),
        UInt8((value >> 8) & 0xff),
        UInt8((value >> 16) & 0xff),
        UInt8((value >> 24) & 0xff)
    ]
}

let icoSizes = [16, 32, 48, 64, 128, 256]
let icoImages = try icoSizes.map { size in
    (size: size, data: try renderIcon(size: size))
}

var ico = Data()
ico.append(contentsOf: littleEndian(UInt16(0)))
ico.append(contentsOf: littleEndian(UInt16(1)))
ico.append(contentsOf: littleEndian(UInt16(icoImages.count)))

var offset = UInt32(6 + icoImages.count * 16)
for image in icoImages {
    ico.append(UInt8(image.size == 256 ? 0 : image.size))
    ico.append(UInt8(image.size == 256 ? 0 : image.size))
    ico.append(0)
    ico.append(0)
    ico.append(contentsOf: littleEndian(UInt16(1)))
    ico.append(contentsOf: littleEndian(UInt16(32)))
    ico.append(contentsOf: littleEndian(UInt32(image.data.count)))
    ico.append(contentsOf: littleEndian(offset))
    offset += UInt32(image.data.count)
}

for image in icoImages {
    ico.append(image.data)
}
try ico.write(to: assetsURL.appendingPathComponent("icon.ico"), options: .atomic)

print("Generated icons in \(assetsURL.path)")

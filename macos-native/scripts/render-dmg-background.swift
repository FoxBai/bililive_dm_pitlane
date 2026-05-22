#!/usr/bin/env swift

import AppKit
import Foundation

let outputURL = URL(fileURLWithPath: CommandLine.arguments.dropFirst().first ?? "/tmp/pitlane-dmg-background.png")
let version = CommandLine.arguments.dropFirst(2).first ?? "0.1.0"
let size = NSSize(width: 640, height: 420)

guard let rep = NSBitmapImageRep(
    bitmapDataPlanes: nil,
    pixelsWide: Int(size.width),
    pixelsHigh: Int(size.height),
    bitsPerSample: 8,
    samplesPerPixel: 4,
    hasAlpha: true,
    isPlanar: false,
    colorSpaceName: .deviceRGB,
    bytesPerRow: 0,
    bitsPerPixel: 0
) else {
    fatalError("Unable to allocate DMG background bitmap")
}

rep.size = size
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

let rect = NSRect(origin: .zero, size: size)
NSColor(calibratedRed: 0.035, green: 0.045, blue: 0.06, alpha: 1).setFill()
rect.fill()

let gradient = NSGradient(colors: [
    NSColor(calibratedRed: 0.10, green: 0.15, blue: 0.20, alpha: 0.92),
    NSColor(calibratedRed: 0.03, green: 0.035, blue: 0.05, alpha: 1)
])!
gradient.draw(in: rect, angle: -25)

for index in 0..<7 {
    let x = CGFloat(index) * 128 - 160
    let stripe = NSBezierPath()
    stripe.move(to: NSPoint(x: x, y: 420))
    stripe.line(to: NSPoint(x: x + 240, y: 420))
    stripe.line(to: NSPoint(x: x + 550, y: 0))
    stripe.line(to: NSPoint(x: x + 310, y: 0))
    stripe.close()
    NSColor(calibratedRed: 0.16, green: 0.22, blue: 0.28, alpha: index.isMultiple(of: 2) ? 0.30 : 0.14).setFill()
    stripe.fill()
}

let titleAttributes: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 40, weight: .bold),
    .foregroundColor: NSColor.white
]
NSAttributedString(string: "Pitlane Danmaku", attributes: titleAttributes)
    .draw(at: NSPoint(x: 42, y: 42))

let subtitleAttributes: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 15, weight: .medium),
    .foregroundColor: NSColor(calibratedRed: 0.74, green: 0.83, blue: 0.92, alpha: 1)
]
NSAttributedString(string: "macOS \(version)  ·  Drag the app to Applications", attributes: subtitleAttributes)
    .draw(at: NSPoint(x: 44, y: 91))

let hintAttributes: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 13, weight: .regular),
    .foregroundColor: NSColor(calibratedRed: 0.62, green: 0.70, blue: 0.78, alpha: 1)
]
NSAttributedString(string: "OBS source: http://127.0.0.1:17333/overlay", attributes: hintAttributes)
    .draw(at: NSPoint(x: 44, y: 356))

let arrowAttributes: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 52, weight: .light),
    .foregroundColor: NSColor(calibratedRed: 0.80, green: 0.88, blue: 0.95, alpha: 0.78)
]
NSAttributedString(string: "→", attributes: arrowAttributes)
    .draw(at: NSPoint(x: 302, y: 201))

NSGraphicsContext.restoreGraphicsState()

try FileManager.default.createDirectory(at: outputURL.deletingLastPathComponent(), withIntermediateDirectories: true)
guard let data = rep.representation(using: .png, properties: [:]) else {
    fatalError("Unable to encode DMG background PNG")
}
try data.write(to: outputURL, options: .atomic)

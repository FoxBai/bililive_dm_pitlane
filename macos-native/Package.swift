// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "PitlaneDanmakuMac",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(name: "PitlaneDanmakuMac", targets: ["PitlaneDanmakuMac"])
    ],
    targets: [
        .executableTarget(
            name: "PitlaneDanmakuMac",
            path: "Sources/PitlaneDanmakuMac"
        )
    ]
)

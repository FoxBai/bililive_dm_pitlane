import Foundation

final class AssetCatalog {
    private let randomQueue = DispatchQueue(label: "PitlaneDanmaku.AssetCatalog.Random")

    let rootURL: URL
    let cars: [CarAsset]
    let commentFrameURL: URL

    init(log: LogService) {
        rootURL = Self.findAssetsRoot()
        commentFrameURL = rootURL.appendingPathComponent("comment-box/comment_frame.png")
        cars = Self.loadCars(rootURL: rootURL)
        log.info("已加载 \(cars.count) 个赛车素材。")
    }

    func pickCar() throws -> CarAsset {
        guard !cars.isEmpty else {
            throw PitlaneError("没有找到赛车素材。")
        }

        return randomQueue.sync {
            cars[Int.random(in: 0..<cars.count)]
        }
    }

    func webPath(for car: CarAsset) -> String {
        guard let relativePath = URL(fileURLWithPath: car.absolutePath).path
            .droppingPrefix(rootURL.path)
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            .addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) else {
            return "/assets/\(car.fileName)"
        }

        return "/assets/" + relativePath
    }

    func resolveAssetPath(_ requestPath: String) throws -> URL {
        var relative = requestPath
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            .replacingOccurrences(of: "\\", with: "/")

        if relative.lowercased().hasPrefix("assets/") {
            relative = String(relative.dropFirst("assets/".count))
        }

        let resolved = rootURL.appendingPathComponent(relative).standardizedFileURL
        let rootPath = rootURL.standardizedFileURL.path
        guard resolved.path == rootPath || resolved.path.hasPrefix(rootPath + "/") else {
            throw PitlaneError("非法素材路径。")
        }

        return resolved
    }

    private static func findAssetsRoot() -> URL {
        let fileManager = FileManager.default
        let cwd = URL(fileURLWithPath: fileManager.currentDirectoryPath)
        let executable = Bundle.main.executableURL?.deletingLastPathComponent()
        let resource = Bundle.main.resourceURL

        let candidates = [
            resource?.appendingPathComponent("Assets"),
            cwd.appendingPathComponent("Assets"),
            cwd.appendingPathComponent("assets"),
            cwd.appendingPathComponent("../assets"),
            cwd.appendingPathComponent("../../assets"),
            executable?.appendingPathComponent("Assets"),
            executable?.appendingPathComponent("../assets"),
            executable?.appendingPathComponent("../../assets"),
            executable?.appendingPathComponent("../../../assets"),
            executable?.appendingPathComponent("../../../../assets")
        ].compactMap { $0?.standardizedFileURL }

        return candidates.first { url in
            fileManager.fileExists(atPath: url.appendingPathComponent("cars/cars.json").path)
        } ?? cwd.appendingPathComponent("../assets").standardizedFileURL
    }

    private static func loadCars(rootURL: URL) -> [CarAsset] {
        let fileManager = FileManager.default
        let carsDirectory = rootURL.appendingPathComponent("cars")
        let manifestURL = carsDirectory.appendingPathComponent("cars.json")

        if let data = try? Data(contentsOf: manifestURL),
           let raw = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] {
            let cars = raw.compactMap { item -> CarAsset? in
                let id = item["id"] as? String ?? "car"
                let file = (item["file"] as? String ?? "").trimmingCharacters(in: CharacterSet(charactersIn: "/\\"))
                guard !file.isEmpty else { return nil }
                let url = rootURL.appendingPathComponent(file)
                guard fileManager.fileExists(atPath: url.path) else { return nil }
                return CarAsset(
                    id: id,
                    fileName: url.lastPathComponent,
                    absolutePath: url.path,
                    width: CGFloat(item["width"] as? Double ?? 555),
                    height: CGFloat(item["height"] as? Double ?? 215)
                )
            }

            if !cars.isEmpty {
                return cars
            }
        }

        guard let files = try? fileManager.contentsOfDirectory(at: carsDirectory, includingPropertiesForKeys: nil) else {
            return []
        }

        return files
            .filter { $0.pathExtension.lowercased() == "png" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
            .map {
                CarAsset(
                    id: $0.deletingPathExtension().lastPathComponent,
                    fileName: $0.lastPathComponent,
                    absolutePath: $0.path,
                    width: 555,
                    height: 215
                )
            }
    }
}

private extension String {
    func droppingPrefix(_ prefix: String) -> String {
        hasPrefix(prefix) ? String(dropFirst(prefix.count)) : self
    }
}

struct PitlaneError: LocalizedError {
    let message: String

    init(_ message: String) {
        self.message = message
    }

    var errorDescription: String? {
        message
    }
}

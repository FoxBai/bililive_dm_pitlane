import Foundation

final class LogService {
    private let queue = DispatchQueue(label: "PitlaneDanmaku.LogService")
    private var entries: [String] = []

    var onEntry: ((String) -> Void)?

    var snapshot: [String] {
        queue.sync { entries }
    }

    func info(_ message: String) {
        add(level: "INFO", message)
    }

    func warn(_ message: String) {
        add(level: "WARN", message)
    }

    func error(_ message: String) {
        add(level: "ERROR", message)
    }

    func clear() {
        queue.sync {
            entries.removeAll()
        }
    }

    private func add(level: String, _ message: String) {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss"
        let entry = "[\(formatter.string(from: Date()))] \(level.padding(toLength: 5, withPad: " ", startingAt: 0)) \(message)"

        queue.sync {
            entries.append(entry)
            if entries.count > 1500 {
                entries.removeFirst(min(300, entries.count))
            }
        }

        onEntry?(entry)
    }
}

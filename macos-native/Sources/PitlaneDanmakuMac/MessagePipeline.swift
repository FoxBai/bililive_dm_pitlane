import Foundation

final class MessagePipeline {
    private let queue = DispatchQueue(label: "PitlaneDanmaku.MessagePipeline")
    private let log: LogService
    private var settings: AppSettings
    private var normalQueue: [ChatMessage] = []
    private var priorityQueue: [ChatMessage] = []
    private var recentIds = Set<String>()
    private var recentIdOrder: [String] = []
    private var timer: DispatchSourceTimer?

    var onMessageReady: ((ChatMessage) -> Void)?

    init(settings: AppSettings, log: LogService) {
        self.settings = settings.normalized()
        self.log = log
    }

    func updateSettings(_ settings: AppSettings) {
        queue.sync {
            self.settings = settings.normalized()
            if self.settings.onlySuperChat {
                self.normalQueue.removeAll()
            }
        }
    }

    func start() {
        queue.sync {
            guard timer == nil else { return }
            let source = DispatchSource.makeTimerSource(queue: queue)
            source.schedule(deadline: .now() + .milliseconds(settings.launchIntervalMs), repeating: .milliseconds(max(120, settings.launchIntervalMs)))
            source.setEventHandler { [weak self] in
                self?.releaseNext()
            }
            source.resume()
            timer = source
        }
    }

    func enqueue(_ message: ChatMessage) {
        queue.sync {
            let activeSettings = settings
            guard let normalized = TextSanitizer.normalize(message, settings: activeSettings) else {
                return
            }

            if activeSettings.onlySuperChat && !normalized.isSuperChat {
                return
            }

            if !normalized.id.isEmpty {
                guard recentIds.insert(normalized.id).inserted else {
                    return
                }
                rememberId(normalized.id)
            }

            while normalQueue.count + priorityQueue.count >= activeSettings.queueLimit {
                if !normalQueue.isEmpty {
                    normalQueue.removeFirst()
                    continue
                }

                if !normalized.isSuperChat {
                    return
                }

                if !priorityQueue.isEmpty {
                    priorityQueue.removeFirst()
                } else {
                    break
                }
            }

            if normalized.isSuperChat {
                priorityQueue.append(normalized)
            } else {
                normalQueue.append(normalized)
            }
        }
    }

    func stop() {
        queue.sync {
            timer?.cancel()
            timer = nil
            normalQueue.removeAll()
            priorityQueue.removeAll()
        }
        log.info("消息管线已停止。")
    }

    private func releaseNext() {
        if timer?.isCancelled == true {
            return
        }

        if let message = priorityQueue.first {
            priorityQueue.removeFirst()
            onMessageReady?(message)
        } else if let message = normalQueue.first {
            normalQueue.removeFirst()
            onMessageReady?(message)
        }

        timer?.schedule(deadline: .now() + .milliseconds(settings.launchIntervalMs), repeating: .milliseconds(max(120, settings.launchIntervalMs)))
    }

    private func rememberId(_ id: String) {
        guard !id.isEmpty else { return }
        recentIdOrder.append(id)
        while recentIdOrder.count > 500 {
            recentIds.remove(recentIdOrder.removeFirst())
        }
    }
}

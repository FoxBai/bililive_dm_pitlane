import Compression
import CryptoKit
import Foundation
import Network

final class BilibiliWebRoomClient {
    private struct WbiKeys {
        var mixinKey: String
    }

    private static let roomInitEndpoint = URL(string: "https://api.live.bilibili.com/room/v1/Room/room_init")!
    private static let danmuInfoEndpoint = URL(string: "https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo")!
    private static let historyEndpoint = URL(string: "https://api.live.bilibili.com/xlive/web-room/v1/dM/gethistory")!
    private static let fingerprintEndpoint = URL(string: "https://api.bilibili.com/x/frontend/finger/spi")!
    private static let wbiNavEndpoint = URL(string: "https://api.bilibili.com/x/web-interface/nav")!
    private static let defaultHosts = [
        DanmakuHost(host: "broadcastlv.chat.bilibili.com", port: 2243, webSocketPort: 2244, secureWebSocketPort: 443)
    ]
    private static let wbiMixinKeyTable = [
        46, 47, 18, 2, 53, 8, 23, 32,
        15, 50, 10, 31, 58, 3, 45, 35,
        27, 43, 5, 49, 33, 9, 42, 19,
        29, 28, 14, 39, 12, 38, 41, 13,
        37, 48, 7, 16, 24, 55, 40, 61,
        26, 17, 0, 1, 60, 51, 30, 4,
        22, 25, 54, 21, 56, 59, 6, 63,
        57, 62, 11, 36, 20, 34, 44, 52
    ]

    private let log: LogService
    private let stateQueue = DispatchQueue(label: "PitlaneDanmaku.BilibiliWebRoomClient.State")
    private var runTask: Task<Void, Never>?
    private var activeSocket: URLSessionWebSocketTask?
    private var activeConnection: NWConnection?
    private var wbiKeys: WbiKeys?
    private var wbiKeysFetchedAt = Date.distantPast
    private var nameCache: [Int64: String] = [:]
    private var historySeenIds = Set<String>()
    private var historySeenOrder: [String] = []
    private var lastRealtimeMessageAt = Date.distantPast
    private var realtimeMessageCount = 0
    private var historyPollWarned = false
    private var maskedNameWarned = false

    var onMessageReceived: ((ChatMessage) -> Void)?

    init(log: LogService) {
        self.log = log
    }

    func start(settings: AppSettings) {
        stop()
        let activeSettings = settings.normalized()
        runTask = Task { [weak self] in
            guard let self else { return }
            do {
                try await self.run(settings: activeSettings)
            } catch is CancellationError {
                return
            } catch {
                self.log.error("B站直播间连接失败：\(error.localizedDescription)")
            }
        }
    }

    func stop() {
        runTask?.cancel()
        runTask = nil
        stateQueue.sync {
            activeSocket?.cancel(with: .goingAway, reason: nil)
            activeSocket = nil
            activeConnection?.cancel()
            activeConnection = nil
        }
    }

    private func run(settings initialSettings: AppSettings) async throws {
        var settings = initialSettings
        try Task.checkCancellation()
        try await refreshGeneratedBuvid3(settings: &settings)
        if Self.extractCredentialUid(settings.cookie) != nil {
            log.info("已检测到登录 Cookie，将使用登录 uid 进行弹幕握手。")
        }

        let roomId = try await resolveRoomId(settings: settings)
        let historySettings = settings
        let historyTask = Task { [weak self] in
            await self?.historyPollingLoop(settings: historySettings, roomId: roomId)
        }
        defer { historyTask.cancel() }

        let token: String
        let hosts: [DanmakuHost]
        do {
            (token, hosts) = try await getDanmuInfo(settings: settings, roomId: roomId)
        } catch {
            token = ""
            hosts = Self.defaultHosts
            log.warn("获取 B站弹幕服务器列表失败：\(error.localizedDescription)。将尝试默认 WSS 节点；如仍失败，请填写网页 Cookie 后重试。")
        }

        log.info("B站真实房间号：\(roomId)，可用弹幕服务器：\(hosts.count) 个。")

        while !Task.isCancelled {
            for host in hosts {
                try Task.checkCancellation()
                do {
                    try await connectAndRead(settings: settings, roomId: roomId, token: token, host: host)
                } catch is CancellationError {
                    throw CancellationError()
                } catch {
                    log.warn("弹幕服务器 \(host.host) 断开：\(error.localizedDescription)")
                }
            }

            log.warn("全部弹幕服务器都已断开，5 秒后重连。")
            try await Task.sleep(nanoseconds: 5_000_000_000)
        }
    }

    private func resolveRoomId(settings: AppSettings) async throws -> Int {
        let pattern = #"(?:(?:live\.bilibili\.com/)|^)(\d+)"#
        let input = settings.roomInput.trimmingCharacters(in: .whitespacesAndNewlines)
        let regex = try NSRegularExpression(pattern: pattern)
        let range = NSRange(input.startIndex..<input.endIndex, in: input)
        guard let match = regex.firstMatch(in: input, range: range),
              let idRange = Range(match.range(at: 1), in: input),
              let roomId = Int(input[idRange]) else {
            throw PitlaneError("请输入 B站直播间 ID 或直播间 URL。")
        }

        var components = URLComponents(url: Self.roomInitEndpoint, resolvingAgainstBaseURL: false)!
        components.queryItems = [URLQueryItem(name: "id", value: String(roomId))]
        let root = try await requestJSON(components.url!, settings: settings)
        try Self.throwIfBilibiliAPIFailed(root, label: "房间初始化")

        if let data = root["data"] as? [String: Any],
           let realRoomId = Self.asInt(data["room_id"]) {
            return realRoomId
        }

        return roomId
    }

    private func getDanmuInfo(settings: AppSettings, roomId: Int) async throws -> (String, [DanmakuHost]) {
        let query = try await buildWbiSignedQuery(
            settings: settings,
            parameters: [
                "id": String(roomId),
                "type": "0",
                "web_location": "444.8"
            ]
        )

        let url = URL(string: Self.danmuInfoEndpoint.absoluteString + "?" + query)!
        let root = try await requestJSON(url, settings: settings, roomId: roomId)
        try Self.throwIfBilibiliAPIFailed(root, label: "弹幕服务器信息")

        guard let data = root["data"] as? [String: Any] else {
            throw PitlaneError("B站弹幕服务器信息缺少 data 字段。")
        }

        let token = data["token"] as? String ?? ""
        let hosts = (data["host_list"] as? [[String: Any]] ?? []).compactMap { item -> DanmakuHost? in
            guard let host = item["host"] as? String, !host.isEmpty else { return nil }
            return DanmakuHost(
                host: host,
                port: Self.asInt(item["port"]) ?? 2243,
                webSocketPort: Self.asInt(item["ws_port"]) ?? 2244,
                secureWebSocketPort: Self.asInt(item["wss_port"]) ?? 443
            )
        }

        guard !hosts.isEmpty else {
            throw PitlaneError("B站未返回可用弹幕服务器。")
        }

        return (token, hosts)
    }

    private func historyPollingLoop(settings: AppSettings, roomId: Int) async {
        var seeded = false
        while !Task.isCancelled {
            do {
                let messages = try await getHistoryMessages(settings: settings, roomId: roomId)
                let freshMessages = markFreshHistoryMessages(messages)
                if !seeded {
                    seeded = true
                    log.info("历史弹幕补偿轮询已就绪。")
                } else if !freshMessages.isEmpty && Date().timeIntervalSince(lastRealtimeMessageAt) > 6 {
                    var displayedCount = 0
                    for message in freshMessages {
                        let resolved = resolveCachedName(message)
                        if Self.looksMasked(resolved.userName) {
                            continue
                        }
                        onMessageReceived?(resolved)
                        displayedCount += 1
                    }

                    if displayedCount > 0 {
                        log.info("历史弹幕补偿显示 \(displayedCount) 条。")
                    }
                }

                historyPollWarned = false
            } catch is CancellationError {
                return
            } catch {
                if !historyPollWarned {
                    log.warn("历史弹幕补偿轮询失败：\(error.localizedDescription)")
                    historyPollWarned = true
                }
            }

            try? await Task.sleep(nanoseconds: 3_000_000_000)
        }
    }

    private func getHistoryMessages(settings: AppSettings, roomId: Int) async throws -> [ChatMessage] {
        var components = URLComponents(url: Self.historyEndpoint, resolvingAgainstBaseURL: false)!
        components.queryItems = [
            URLQueryItem(name: "roomid", value: String(roomId)),
            URLQueryItem(name: "room_type", value: "0")
        ]
        let root = try await requestJSON(components.url!, settings: settings, roomId: roomId)
        try Self.throwIfBilibiliAPIFailed(root, label: "历史弹幕")
        return BilibiliMessageParser.parseHistoryMessages(root)
    }

    private func refreshGeneratedBuvid3(settings: inout AppSettings) async throws {
        if Self.cookieContains(settings.cookie, name: "buvid3") ||
            (!settings.buvid3.isEmpty && !Self.looksLocallyGeneratedBuvid3(settings.buvid3)) {
            return
        }

        do {
            let root = try await requestJSON(Self.fingerprintEndpoint, settings: settings, referrer: "https://www.bilibili.com/")
            try Self.throwIfBilibiliAPIFailed(root, label: "buvid3")
            if let data = root["data"] as? [String: Any],
               let buvid3 = data["b_3"] as? String,
               !buvid3.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                settings.buvid3 = buvid3.trimmingCharacters(in: .whitespacesAndNewlines)
                log.info("已获取 B站网页 buvid3，用于直播弹幕握手。")
            }
        } catch is CancellationError {
            throw CancellationError()
        } catch {
            log.warn("获取 B站 buvid3 失败：\(error.localizedDescription)。将继续使用当前 buvid3。")
        }
    }

    private func markFreshHistoryMessages(_ messages: [ChatMessage]) -> [ChatMessage] {
        stateQueue.sync {
            var fresh: [ChatMessage] = []
            for message in messages.reversed() where historySeenIds.insert(message.id).inserted {
                fresh.append(message)
                historySeenOrder.append(message.id)
            }

            while historySeenOrder.count > 500 {
                historySeenIds.remove(historySeenOrder.removeFirst())
            }

            return fresh
        }
    }

    private func buildWbiSignedQuery(settings: AppSettings, parameters: [String: String]) async throws -> String {
        let keys = try await getWbiKeys(settings: settings)
        var signed = parameters
        signed["wts"] = String(Int(Date().timeIntervalSince1970))
        let unsignedQuery = Self.buildQueryString(signed)
        let signatureData = Data((unsignedQuery + keys.mixinKey).utf8)
        let signature = Insecure.MD5.hash(data: signatureData).map { String(format: "%02x", $0) }.joined()
        signed["w_rid"] = signature
        return Self.buildQueryString(signed)
    }

    private func getWbiKeys(settings: AppSettings) async throws -> WbiKeys {
        if let cached = stateQueue.sync(execute: { () -> WbiKeys? in
            if let wbiKeys, Date().timeIntervalSince(wbiKeysFetchedAt) < 6 * 60 * 60 {
                return wbiKeys
            }
            return nil
        }) {
            return cached
        }

        let root = try await requestJSON(Self.wbiNavEndpoint, settings: settings, referrer: "https://www.bilibili.com/")
        guard let data = root["data"] as? [String: Any],
              let wbiImg = data["wbi_img"] as? [String: Any] else {
            throw PitlaneError("B站 WBI key 接口缺少 data.wbi_img 字段。")
        }

        let imgKey = try Self.extractWbiKey(wbiImg, name: "img_url")
        let subKey = try Self.extractWbiKey(wbiImg, name: "sub_url")
        let keys = WbiKeys(mixinKey: try Self.buildMixinKey(imgKey + subKey))

        stateQueue.sync {
            wbiKeys = keys
            wbiKeysFetchedAt = Date()
        }
        return keys
    }

    private func connectAndRead(settings: AppSettings, roomId: Int, token: String, host: DanmakuHost) async throws {
        var lastError: Error?

        if host.secureWebSocketPort > 0 {
            do {
                try await connectAndReadWebSocket(settings: settings, roomId: roomId, token: token, host: host, useTLS: true)
                return
            } catch is CancellationError {
                throw CancellationError()
            } catch {
                lastError = error
                log.warn("WSS 弹幕服务器 \(host.host):\(host.secureWebSocketPort) 连接失败：\(error.localizedDescription)")
            }
        }

        if host.webSocketPort > 0 {
            do {
                try await connectAndReadWebSocket(settings: settings, roomId: roomId, token: token, host: host, useTLS: false)
                return
            } catch is CancellationError {
                throw CancellationError()
            } catch {
                lastError = error
                log.warn("WS 弹幕服务器 \(host.host):\(host.webSocketPort) 连接失败：\(error.localizedDescription)")
            }
        }

        if host.port > 0 {
            do {
                try await connectAndReadTCP(settings: settings, roomId: roomId, token: token, host: host)
                return
            } catch is CancellationError {
                throw CancellationError()
            } catch {
                if let lastError {
                    throw PitlaneError("\(lastError.localizedDescription); TCP fallback: \(error.localizedDescription)")
                }

                throw error
            }
        }

        throw lastError ?? PitlaneError("没有可用弹幕服务器。")
    }

    private func connectAndReadWebSocket(settings: AppSettings, roomId: Int, token: String, host: DanmakuHost, useTLS: Bool) async throws {
        let port = useTLS ? host.secureWebSocketPort : host.webSocketPort
        let scheme = useTLS ? "wss" : "ws"
        let url = URL(string: "\(scheme)://\(host.host):\(port)/sub")!
        var request = URLRequest(url: url)
        request.setValue(settings.userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue("https://live.bilibili.com", forHTTPHeaderField: "Origin")
        request.setValue("https://live.bilibili.com/\(roomId)", forHTTPHeaderField: "Referer")
        let cookie = Self.buildCookieHeader(settings)
        if !cookie.isEmpty {
            request.setValue(cookie, forHTTPHeaderField: "Cookie")
        }

        let socket = URLSession.shared.webSocketTask(with: request)
        stateQueue.sync {
            activeSocket = socket
        }

        socket.resume()
        log.info("已连接 \(useTLS ? "WSS" : "WS") 弹幕服务器 \(host.host):\(port)。")
        try await socket.send(.data(Self.buildAuthPacket(settings: settings, roomId: roomId, token: token)))

        let heartbeatTask = Task { [weak self, weak socket] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 30_000_000_000)
                guard !Task.isCancelled, let socket else { return }
                do {
                    try await socket.send(.data(Self.buildPacket(operation: 2, version: 1, payload: Data())))
                } catch {
                    self?.log.warn("弹幕服务器心跳发送失败：\(error.localizedDescription)")
                    return
                }
            }
        }
        defer {
            heartbeatTask.cancel()
            socket.cancel(with: .goingAway, reason: nil)
        }

        while !Task.isCancelled {
            let message = try await socket.receive()
            switch message {
            case let .data(data):
                processWebSocketPayload(data)
            case let .string(text):
                publishMessages(Data(text.utf8))
            @unknown default:
                break
            }
        }
    }

    private func connectAndReadTCP(settings: AppSettings, roomId: Int, token: String, host: DanmakuHost) async throws {
        guard let port = NWEndpoint.Port(rawValue: UInt16(host.port)) else {
            throw PitlaneError("TCP 弹幕服务器端口无效：\(host.port)")
        }

        let connection = NWConnection(host: NWEndpoint.Host(host.host), port: port, using: .tcp)
        stateQueue.sync {
            activeConnection = connection
        }

        defer {
            connection.cancel()
            stateQueue.sync {
                if activeConnection === connection {
                    activeConnection = nil
                }
            }
        }

        try await withTaskCancellationHandler {
            try await Self.startTCPConnection(connection)
            log.info("已连接 TCP 弹幕服务器 \(host.host):\(host.port)。")
            try await Self.sendTCP(Self.buildAuthPacket(settings: settings, roomId: roomId, token: token), connection: connection)

            let heartbeatTask = Task { [weak self, weak connection] in
                while !Task.isCancelled {
                    try? await Task.sleep(nanoseconds: 30_000_000_000)
                    guard !Task.isCancelled, let connection else { return }
                    do {
                        try await Self.sendTCP(Self.buildPacket(operation: 2, version: 1, payload: Data()), connection: connection)
                    } catch {
                        self?.log.warn("TCP 弹幕服务器心跳发送失败：\(error.localizedDescription)")
                        return
                    }
                }
            }
            defer { heartbeatTask.cancel() }

            var reader = TCPPacketReader(connection: connection)
            while !Task.isCancelled {
                let packet = try await reader.readPacket()
                processPacket(version: packet.version, operation: packet.operation, payload: packet.payload)
            }
        } onCancel: {
            connection.cancel()
        }
    }

    private static func startTCPConnection(_ connection: NWConnection) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            let resumeBox = TCPConnectionStartContinuation(continuation)

            connection.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    resumeBox.resume(.success(()))
                case let .failed(error):
                    resumeBox.resume(.failure(error))
                case .cancelled:
                    resumeBox.resume(.failure(CancellationError()))
                default:
                    break
                }
            }
            connection.start(queue: .global(qos: .userInitiated))
        }
    }

    private final class TCPConnectionStartContinuation: @unchecked Sendable {
        private let lock = NSLock()
        private var didResume = false
        private let continuation: CheckedContinuation<Void, Error>

        init(_ continuation: CheckedContinuation<Void, Error>) {
            self.continuation = continuation
        }

        func resume(_ result: Result<Void, Error>) {
            lock.lock()
            defer { lock.unlock() }
            guard !didResume else { return }
            didResume = true

            switch result {
            case .success:
                continuation.resume()
            case let .failure(error):
                continuation.resume(throwing: error)
            }
        }
    }

    private static func sendTCP(_ data: Data, connection: NWConnection) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            connection.send(content: data, completion: .contentProcessed { error in
                if let error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume()
                }
            })
        }
    }

    private struct TCPPacket {
        var version: Int
        var operation: Int
        var payload: Data
    }

    private struct TCPPacketReader {
        let connection: NWConnection
        var buffer = Data()

        mutating func readPacket() async throws -> TCPPacket {
            let header = try await readExact(16)
            guard let packetLength = header.readInt32BE(at: 0),
                  let headerLength = header.readInt16BE(at: 4),
                  let version = header.readInt16BE(at: 6),
                  let operation = header.readInt32BE(at: 8),
                  packetLength >= headerLength,
                  headerLength >= 16,
                  packetLength <= 16 * 1024 * 1024 else {
                throw PitlaneError("非法弹幕包头。")
            }

            if headerLength > 16 {
                _ = try await readExact(headerLength - 16)
            }

            let payload = try await readExact(packetLength - headerLength)
            return TCPPacket(version: version, operation: operation, payload: payload)
        }

        private mutating func readExact(_ length: Int) async throws -> Data {
            while buffer.count < length {
                let chunk = try await Self.receive(connection)
                guard !chunk.isEmpty else {
                    throw PitlaneError("TCP 弹幕服务器已关闭连接。")
                }
                buffer.append(chunk)
            }

            let result = buffer.prefix(length)
            buffer.removeFirst(length)
            return Data(result)
        }

        private static func receive(_ connection: NWConnection) async throws -> Data {
            try await withCheckedThrowingContinuation { continuation in
                connection.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { data, _, isComplete, error in
                    if let error {
                        continuation.resume(throwing: error)
                    } else if let data, !data.isEmpty {
                        continuation.resume(returning: data)
                    } else if isComplete {
                        continuation.resume(throwing: PitlaneError("TCP 弹幕服务器已关闭连接。"))
                    } else {
                        continuation.resume(returning: Data())
                    }
                }
            }
        }
    }

    private func processWebSocketPayload(_ payload: Data) {
        if !tryProcessNestedPackets(payload) {
            publishMessages(payload)
        }
    }

    @discardableResult
    private func tryProcessNestedPackets(_ payload: Data) -> Bool {
        var offset = 0
        var processed = false

        while offset + 16 <= payload.count {
            guard let packetLength = payload.readInt32BE(at: offset),
                  let headerLength = payload.readInt16BE(at: offset + 4),
                  let version = payload.readInt16BE(at: offset + 6),
                  let operation = payload.readInt32BE(at: offset + 8),
                  packetLength >= 16,
                  headerLength >= 16,
                  offset + packetLength <= payload.count else {
                return false
            }

            let start = offset + headerLength
            processPacket(version: version, operation: operation, payload: payload.subdata(in: start..<(offset + packetLength)))
            offset += packetLength
            processed = true
        }

        return processed && offset == payload.count
    }

    private func processPacket(version: Int, operation: Int, payload: Data) {
        if operation == 8 {
            log.info("弹幕服务器认证成功，正在等待新弹幕。")
            return
        }

        guard operation == 5 else { return }

        do {
            switch version {
            case 0, 1:
                publishMessages(payload)
            case 2:
                processDecompressed(try Self.decompressZlib(payload))
            case 3:
                processDecompressed(try Self.decompressBrotli(payload))
            default:
                break
            }
        } catch {
            log.warn("弹幕包解析失败：\(error.localizedDescription)")
        }
    }

    private func processDecompressed(_ payload: Data) {
        if tryProcessNestedPackets(payload) {
            return
        }
        publishMessages(payload)
    }

    private func publishMessages(_ payload: Data) {
        let messages = BilibiliMessageParser.parseJsonMessages(payload)
        guard !messages.isEmpty else { return }

        realtimeMessageCount += messages.count
        if realtimeMessageCount <= 5 || realtimeMessageCount % 20 == 0 {
            log.info("已接收 B站实时弹幕 \(realtimeMessageCount) 条。")
        }

        lastRealtimeMessageAt = Date()
        for message in messages {
            publishWithNameCache(message)
        }
    }

    private func publishWithNameCache(_ message: ChatMessage) {
        let resolved = resolveCachedName(message)
        if Self.looksMasked(resolved.userName) {
            warnMaskedNameSuppressed()
            return
        }
        onMessageReceived?(resolved)
    }

    private func resolveCachedName(_ message: ChatMessage) -> ChatMessage {
        guard let uid = message.userId else {
            return message
        }

        return stateQueue.sync {
            var resolved = message
            if !Self.looksMasked(message.userName) {
                nameCache[uid] = message.userName
            } else if let cached = nameCache[uid] {
                resolved.userName = cached
            }
            return resolved
        }
    }

    private func warnMaskedNameSuppressed() {
        guard !maskedNameWarned else { return }
        maskedNameWarned = true
        log.warn("收到 B站返回的脱敏昵称，已跳过该条弹幕；如仍频繁出现，请填写登录后的网页 Cookie。")
    }

    private func requestJSON(_ url: URL, settings: AppSettings, roomId: Int? = nil, referrer: String? = nil) async throws -> [String: Any] {
        var request = URLRequest(url: url)
        request.setValue(settings.userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("https://live.bilibili.com", forHTTPHeaderField: "Origin")
        request.setValue(referrer ?? (roomId.map { "https://live.bilibili.com/\($0)" } ?? "https://live.bilibili.com/"), forHTTPHeaderField: "Referer")
        let cookie = Self.buildCookieHeader(settings)
        if !cookie.isEmpty {
            request.setValue(cookie, forHTTPHeaderField: "Cookie")
        }

        let (data, response) = try await URLSession.shared.data(for: request)
        if let http = response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
            throw PitlaneError("HTTP \(http.statusCode)")
        }

        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw PitlaneError("接口返回不是 JSON 对象。")
        }

        return root
    }

    private static func buildAuthPacket(settings: AppSettings, roomId: Int, token: String) -> Data {
        let auth: [String: Any] = [
            "uid": extractCredentialUid(settings.cookie) ?? 0,
            "roomid": roomId,
            "protover": 2,
            "platform": "danmuji",
            "type": 2,
            "key": token,
            "buvid": settings.buvid3
        ]
        let payload = (try? JSONSerialization.data(withJSONObject: auth)) ?? Data()
        return buildPacket(operation: 7, version: 1, payload: payload)
    }

    private static func buildPacket(operation: Int, version: Int, payload: Data) -> Data {
        var data = Data()
        data.appendInt32BE(16 + payload.count)
        data.appendInt16BE(16)
        data.appendInt16BE(version)
        data.appendInt32BE(operation)
        data.appendInt32BE(1)
        data.append(payload)
        return data
    }

    private static func buildCookieHeader(_ settings: AppSettings) -> String {
        let cookie = settings.cookie.trimmingCharacters(in: CharacterSet(charactersIn: " ;\n\r\t"))
        if cookieContains(cookie, name: "buvid3") {
            return cookie
        }

        guard !settings.buvid3.isEmpty else {
            return cookie
        }

        let buvidCookie = "buvid3=\(settings.buvid3)"
        return cookie.isEmpty ? buvidCookie : "\(cookie); \(buvidCookie)"
    }

    private static func cookieContains(_ cookie: String, name: String) -> Bool {
        cookie
            .split(separator: ";")
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .contains { $0.lowercased().hasPrefix(name.lowercased() + "=") }
    }

    private static func extractCredentialUid(_ cookie: String) -> Int64? {
        for part in cookie.split(separator: ";") {
            let pieces = part.split(separator: "=", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            if pieces.count == 2,
               pieces[0].caseInsensitiveCompare("DedeUserID") == .orderedSame,
               let uid = Int64(pieces[1]) {
                return uid
            }
        }
        return nil
    }

    private static func buildQueryString(_ parameters: [String: String]) -> String {
        parameters
            .sorted { $0.key < $1.key }
            .map { "\(percentEncode($0.key))=\(percentEncode(filterWbiValue($0.value)))" }
            .joined(separator: "&")
    }

    private static func filterWbiValue(_ value: String) -> String {
        value.filter { !["!", "'", "(", ")", "*"].contains($0) }
    }

    private static func percentEncode(_ value: String) -> String {
        let allowed = CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? ""
    }

    private static func extractWbiKey(_ root: [String: Any], name: String) throws -> String {
        guard let rawURL = root[name] as? String,
              let url = URL(string: rawURL) else {
            throw PitlaneError("B站 WBI key 接口缺少 \(name)。")
        }

        let key = url.deletingPathExtension().lastPathComponent
        guard !key.isEmpty else {
            throw PitlaneError("B站 WBI key \(name) 无法解析。")
        }
        return key
    }

    private static func buildMixinKey(_ rawKey: String) throws -> String {
        let characters = Array(rawKey)
        guard characters.count > (wbiMixinKeyTable.max() ?? 0) else {
            throw PitlaneError("B站 WBI key 长度异常。")
        }
        return String(wbiMixinKeyTable.prefix(32).map { characters[$0] })
    }

    private static func throwIfBilibiliAPIFailed(_ root: [String: Any], label: String) throws {
        guard let code = asInt(root["code"]), code != 0 else {
            return
        }

        let message = root["message"] as? String ?? root["msg"] as? String ?? "未知错误"
        throw PitlaneError("B站\(label)接口返回错误 \(code)：\(message)")
    }

    private static func asInt(_ value: Any?) -> Int? {
        switch value {
        case let value as NSNumber:
            return value.intValue
        case let value as Int:
            return value
        case let value as String:
            return Int(value)
        default:
            return nil
        }
    }

    private static func looksMasked(_ userName: String) -> Bool {
        let name = userName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let firstStar = name.firstIndex(of: "*"), firstStar > name.startIndex else {
            return false
        }
        return name[firstStar...].allSatisfy { $0 == "*" }
    }

    private static func looksLocallyGeneratedBuvid3(_ buvid3: String) -> Bool {
        let pattern = #"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}infoc$"#
        return (try? NSRegularExpression(pattern: pattern).firstMatch(
            in: buvid3,
            range: NSRange(buvid3.startIndex..<buvid3.endIndex, in: buvid3)
        )) != nil
    }

    private static func decompressZlib(_ data: Data) throws -> Data {
        try decompress(data, algorithm: COMPRESSION_ZLIB, label: "zlib")
    }

    private static func decompressBrotli(_ data: Data) throws -> Data {
        try decompress(data, algorithm: COMPRESSION_BROTLI, label: "Brotli")
    }

    private static func decompress(_ data: Data, algorithm: compression_algorithm, label: String) throws -> Data {
        var capacity = max(data.count * 4, 64 * 1024)
        let maxCapacity = 32 * 1024 * 1024

        while capacity <= maxCapacity {
            var output = Data(count: capacity)
            let decoded = output.withUnsafeMutableBytes { outputBuffer -> Int in
                data.withUnsafeBytes { inputBuffer -> Int in
                    guard let outputBase = outputBuffer.bindMemory(to: UInt8.self).baseAddress,
                          let inputBase = inputBuffer.bindMemory(to: UInt8.self).baseAddress else {
                        return 0
                    }

                    return compression_decode_buffer(
                        outputBase,
                        capacity,
                        inputBase,
                        data.count,
                        nil,
                        algorithm
                    )
                }
            }

            if decoded > 0 {
                output.removeSubrange(decoded..<output.count)
                return output
            }

            capacity *= 2
        }

        throw PitlaneError("\(label) 解压失败。")
    }
}

private extension Data {
    func readInt32BE(at offset: Int) -> Int? {
        guard offset >= 0, offset + 4 <= count else { return nil }
        return withUnsafeBytes { raw in
            let bytes = raw.bindMemory(to: UInt8.self)
            return (Int(bytes[offset]) << 24) |
                (Int(bytes[offset + 1]) << 16) |
                (Int(bytes[offset + 2]) << 8) |
                Int(bytes[offset + 3])
        }
    }

    func readInt16BE(at offset: Int) -> Int? {
        guard offset >= 0, offset + 2 <= count else { return nil }
        return withUnsafeBytes { raw in
            let bytes = raw.bindMemory(to: UInt8.self)
            return (Int(bytes[offset]) << 8) | Int(bytes[offset + 1])
        }
    }

    mutating func appendInt32BE(_ value: Int) {
        append(UInt8((value >> 24) & 0xff))
        append(UInt8((value >> 16) & 0xff))
        append(UInt8((value >> 8) & 0xff))
        append(UInt8(value & 0xff))
    }

    mutating func appendInt16BE(_ value: Int) {
        append(UInt8((value >> 8) & 0xff))
        append(UInt8(value & 0xff))
    }
}

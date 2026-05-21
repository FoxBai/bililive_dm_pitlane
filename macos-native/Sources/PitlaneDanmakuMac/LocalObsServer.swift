import Foundation
import Network

final class LocalObsServer {
    private struct RenderableCar: Encodable {
        var id: String
        var url: String
        var width: CGFloat
        var height: CGFloat
    }

    private let queue = DispatchQueue(label: "PitlaneDanmaku.LocalObsServer")
    private let assets: AssetCatalog
    private let log: LogService
    private var listener: NWListener?
    private var clients: [NWConnection] = []
    private var settings = AppSettings().normalized()

    init(assets: AssetCatalog, log: LogService) {
        self.assets = assets
        self.log = log
    }

    var overlayURL: String {
        "http://127.0.0.1:\(settings.obsPort)/overlay"
    }

    func start(settings: AppSettings) throws {
        stop()
        self.settings = settings.normalized()

        guard let port = NWEndpoint.Port(rawValue: UInt16(self.settings.obsPort)),
              let host = IPv4Address("127.0.0.1") else {
            throw PitlaneError("OBS 端口无效。")
        }

        let parameters = NWParameters.tcp
        parameters.allowLocalEndpointReuse = true
        parameters.requiredLocalEndpoint = .hostPort(host: .ipv4(host), port: port)

        let listener = try NWListener(using: parameters)
        listener.newConnectionHandler = { [weak self] connection in
            self?.handle(connection)
        }
        listener.stateUpdateHandler = { [weak self] state in
            if case let .failed(error) = state {
                self?.log.warn("OBS 服务监听失败：\(error.localizedDescription)")
            }
        }
        listener.start(queue: queue)
        self.listener = listener
        log.info("OBS 浏览器源已启动：\(overlayURL)")
    }

    func broadcast(_ message: ChatMessage) {
        let formatter = ISO8601DateFormatter()
        let dto = OverlayMessageDTO(
            id: message.id,
            userName: message.userName,
            text: message.text,
            kind: message.isSuperChat ? "superchat" : "comment",
            price: message.price,
            receivedAt: formatter.string(from: message.receivedAt)
        )

        guard let payload = try? JSONEncoder().encode(dto),
              let json = String(data: payload, encoding: .utf8) else {
            return
        }

        let data = Data("data: \(json)\n\n".utf8)
        queue.async {
            for client in self.clients {
                client.send(content: data, completion: .contentProcessed { [weak self, weak client] error in
                    guard error != nil, let client else { return }
                    self?.removeClient(client)
                })
            }
        }
    }

    func stop() {
        queue.sync {
            listener?.cancel()
            listener = nil
            for client in clients {
                client.cancel()
            }
            clients.removeAll()
        }
    }

    private func handle(_ connection: NWConnection) {
        connection.stateUpdateHandler = { [weak self, weak connection] state in
            guard let self, let connection else { return }
            switch state {
            case .cancelled, .failed:
                self.removeClient(connection)
            default:
                break
            }
        }
        connection.start(queue: queue)
        connection.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self, weak connection] data, _, _, error in
            guard let self, let connection else { return }
            if error != nil {
                connection.cancel()
                return
            }

            guard let data, let request = String(data: data, encoding: .utf8) else {
                connection.cancel()
                return
            }

            self.route(request, connection: connection)
        }
    }

    private func route(_ request: String, connection: NWConnection) {
        let firstLine = request.split(separator: "\r\n", maxSplits: 1).first
            ?? request.split(separator: "\n", maxSplits: 1).first
            ?? ""
        let parts = firstLine.split(separator: " ", maxSplits: 2).map(String.init)
        let rawTarget = parts.count >= 2 ? parts[1] : "/"
        let path = URLComponents(string: rawTarget)?.path.removingPercentEncoding ?? rawTarget

        switch path {
        case "/", "/overlay":
            writeText(buildOverlayHTML(), contentType: "text/html; charset=utf-8", to: connection)
        case "/health":
            writeHealth(to: connection)
        case "/events":
            handleEvents(connection)
        default:
            if path.lowercased().hasPrefix("/assets/") {
                serveAsset(path, to: connection)
            } else if path.lowercased().hasPrefix("/cars/") {
                serveAsset("/assets" + path, to: connection)
            } else {
                writeText("Not Found", contentType: "text/plain; charset=utf-8", to: connection, statusCode: 404, reason: "Not Found")
            }
        }
    }

    private func writeHealth(to connection: NWConnection) {
        let payload: [String: Any] = [
            "status": "ok",
            "overlayUrl": overlayURL,
            "cars": assets.cars.count,
            "events": "/events"
        ]
        let data = (try? JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys])) ?? Data("{}".utf8)
        write(data, contentType: "application/json; charset=utf-8", to: connection)
    }

    private func handleEvents(_ connection: NWConnection) {
        let headers = """
        HTTP/1.1 200 OK\r
        Content-Type: text/event-stream; charset=utf-8\r
        Cache-Control: no-cache\r
        Connection: keep-alive\r
        Access-Control-Allow-Origin: *\r
        \r
        """
        connection.send(content: Data(headers.utf8), completion: .contentProcessed { [weak self, weak connection] error in
            guard let self, let connection else { return }
            if error != nil {
                connection.cancel()
                return
            }
            self.queue.async {
                self.clients.append(connection)
                connection.send(content: Data(": connected\n\n".utf8), completion: .contentProcessed { _ in })
            }
        })
    }

    private func serveAsset(_ path: String, to connection: NWConnection) {
        do {
            let url = try assets.resolveAssetPath(path)
            let data = try Data(contentsOf: url)
            let contentType = Self.contentType(for: url)
            write(data, contentType: contentType, to: connection)
        } catch is PitlaneError {
            writeText("Forbidden", contentType: "text/plain; charset=utf-8", to: connection, statusCode: 403, reason: "Forbidden")
        } catch {
            writeText("Not Found", contentType: "text/plain; charset=utf-8", to: connection, statusCode: 404, reason: "Not Found")
        }
    }

    private func writeText(_ text: String, contentType: String, to connection: NWConnection, statusCode: Int = 200, reason: String = "OK") {
        write(Data(text.utf8), contentType: contentType, to: connection, statusCode: statusCode, reason: reason)
    }

    private func write(_ body: Data, contentType: String, to connection: NWConnection, statusCode: Int = 200, reason: String = "OK") {
        var response = Data()
        response.append(Data("HTTP/1.1 \(statusCode) \(reason)\r\n".utf8))
        response.append(Data("Content-Type: \(contentType)\r\n".utf8))
        response.append(Data("Content-Length: \(body.count)\r\n".utf8))
        response.append(Data("Cache-Control: no-cache\r\n".utf8))
        response.append(Data("Access-Control-Allow-Origin: *\r\n".utf8))
        response.append(Data("Connection: close\r\n\r\n".utf8))
        response.append(body)
        connection.send(content: response, completion: .contentProcessed { [weak connection] _ in
            connection?.cancel()
        })
    }

    private func removeClient(_ connection: NWConnection) {
        queue.async {
            self.clients.removeAll { $0 === connection }
        }
    }

    private static func contentType(for url: URL) -> String {
        switch url.pathExtension.lowercased() {
        case "png":
            return "image/png"
        case "svg":
            return "image/svg+xml"
        case "ttf":
            return "font/ttf"
        case "json":
            return "application/json; charset=utf-8"
        default:
            return "application/octet-stream"
        }
    }

    private func buildOverlayHTML() -> String {
        let cars = assets.cars.map {
            RenderableCar(id: $0.id, url: assets.webPath(for: $0), width: $0.width, height: $0.height)
        }
        let carsJSON = (try? String(data: JSONEncoder().encode(cars), encoding: .utf8)) ?? "[]"

        return """
        <!doctype html>
        <html lang="zh-CN">
        <head>
          <meta charset="utf-8">
          <meta name="viewport" content="width=device-width, initial-scale=1">
          <title>Pitlane Danmaku Overlay</title>
          <style>
            @font-face {
              font-family: "HarmonyOS Sans SC";
              src: url("/assets/fonts/HarmonyOS_Sans_SC_Regular.ttf") format("truetype");
              font-weight: 400 900;
            }
            html, body, #stage {
              width: 100%;
              height: 100%;
              margin: 0;
              overflow: hidden;
              background: transparent;
              font-family: "HarmonyOS Sans SC", "PingFang SC", "Helvetica Neue", sans-serif;
            }
            .item {
              position: absolute;
              width: \(OverlayLayout.baseWidth)px;
              height: \(OverlayLayout.baseHeight)px;
              left: 0;
              bottom: 10px;
              transform-origin: left bottom;
              will-change: transform;
            }
            .frame {
              position: absolute;
              left: 0;
              top: 0;
              width: \(OverlayLayout.frameWidth)px;
              height: \(OverlayLayout.frameHeight)px;
              object-fit: contain;
              transform: rotate(180deg) scaleY(-1);
              transform-origin: center center;
            }
            .car {
              position: absolute;
              object-fit: contain;
              pointer-events: none;
              transform: rotate(180deg) scaleY(-1);
              transform-origin: center center;
            }
            .text {
              position: absolute;
              left: \(OverlayLayout.textLeft)px;
              top: \(OverlayLayout.textTop)px;
              width: \(OverlayLayout.textWidth)px;
              color: white;
              text-shadow: 0 3px 8px rgba(0,0,0,.55);
            }
            .name {
              display: flex;
              gap: 12px;
              align-items: center;
              font-size: \(OverlayLayout.nameFontSize)px;
              line-height: 1;
              font-weight: 900;
              white-space: nowrap;
              overflow: hidden;
              text-overflow: ellipsis;
              max-width: \(OverlayLayout.nameWidth)px;
            }
            .msg {
              margin-top: \(OverlayLayout.messageTopMargin)px;
              font-size: \(OverlayLayout.messageFontSize)px;
              line-height: \(OverlayLayout.messageLineHeight)px;
              font-weight: 650;
              max-height: \(OverlayLayout.messageMaxHeight)px;
              display: -webkit-box;
              -webkit-line-clamp: 3;
              -webkit-box-orient: vertical;
              overflow: hidden;
            }
            .badge {
              display: none;
              padding: 7px 14px 8px;
              border-radius: 10px;
              color: #16120A;
              background: #FDE68A;
              font-size: 30px;
              line-height: 1;
              font-weight: 900;
              flex: 0 0 auto;
            }
            .superchat .badge {
              display: inline-block;
            }
            .superchat .name {
              max-width: \(OverlayLayout.superChatNameWidth)px;
            }
          </style>
        </head>
        <body>
          <div id="stage"></div>
          <script>
            const cars = \(carsJSON);
            const framePath = "/assets/comment-box/comment_frame.png";
            const minVisible = \(settings.minVisibleItems);
            const maxStageWidth = \(settings.maxStageWidth);
            const stage = document.getElementById("stage");
            const items = [];
            const baseWidth = \(OverlayLayout.baseWidth);
            const baseHeight = \(OverlayLayout.baseHeight);
            const baseGap = \(OverlayLayout.baseGap);
            const carLeft = \(OverlayLayout.carLeft);
            const carBottom = \(OverlayLayout.carBottom);

            function pickCar() {
              return cars[Math.floor(Math.random() * cars.length)];
            }

            function addMessage(message) {
              if (!cars.length) return;
              const car = pickCar();
              const carTop = baseHeight - car.height - carBottom;
              const node = document.createElement("div");
              node.className = "item " + (message.kind === "superchat" ? "superchat" : "comment");
              node.innerHTML = `
                <img class="frame" src="${framePath}" alt="">
                <img class="car" src="${car.url}" alt="" style="left:${carLeft}px;top:${carTop}px;width:${car.width}px;height:${car.height}px">
                <div class="text">
                  <div class="name"><span class="badge">SC</span><span>${escapeHtml(message.userName)}</span></div>
                  <div class="msg">${escapeHtml(message.text)}</div>
                </div>`;
              stage.appendChild(node);
              items.unshift({ node, x: -baseWidth, target: 0, leaving: false });
              layout();
            }

            function layout() {
              const scale = computeScale();
              const width = stage.clientWidth;
              const itemWidth = baseWidth * scale;
              const gap = baseGap * scale;
              const capacity = Math.max(minVisible, Math.floor(Math.min(width, maxStageWidth) / Math.max(1, itemWidth + gap)));

              while (items.filter(x => !x.leaving).length > capacity) {
                const item = items.pop();
                if (!item) break;
                item.leaving = true;
                item.target = width + itemWidth;
                items.unshift(item);
              }

              let cursor = width - itemWidth - 10;
              for (let index = items.length - 1; index >= 0; index--) {
                const item = items[index];
                item.node.style.transform = `translateX(${item.x}px) scale(${scale})`;
                if (item.leaving) continue;
                item.target = Math.max(-itemWidth, cursor);
                cursor -= itemWidth + gap;
              }
            }

            function computeScale() {
              const heightScale = Math.max(0.2, (stage.clientHeight - 10) / baseHeight);
              const widthScale = Math.max(0.2, stage.clientWidth / (minVisible * baseWidth + (minVisible - 1) * baseGap));
              return Math.min(1, heightScale, widthScale);
            }

            function tick() {
              const pressure = Math.min(1, items.length / Math.max(1, minVisible + 3));
              for (let index = items.length - 1; index >= 0; index--) {
                const item = items[index];
                const easing = item.leaving ? 0.075 : 0.045 + pressure * 0.035;
                item.x += (item.target - item.x) * easing;
                item.node.style.transform = item.node.style.transform.replace(/translateX\\([^)]*\\)/, `translateX(${item.x}px)`);
                if (item.leaving && item.x > stage.clientWidth + baseWidth) {
                  item.node.remove();
                  items.splice(index, 1);
                }
              }
              requestAnimationFrame(tick);
            }

            function escapeHtml(value) {
              return String(value ?? "").replace(/[&<>"']/g, ch => {
                switch (ch) {
                  case "&": return "&amp;";
                  case "<": return "&lt;";
                  case ">": return "&gt;";
                  case '"': return "&quot;";
                  case "'": return "&#39;";
                  default: return ch;
                }
              });
            }

            addEventListener("resize", layout);
            new EventSource("/events").onmessage = event => addMessage(JSON.parse(event.data));
            requestAnimationFrame(tick);
          </script>
        </body>
        </html>
        """
    }
}

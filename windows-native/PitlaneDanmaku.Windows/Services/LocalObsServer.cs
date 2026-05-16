using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using PitlaneDanmaku.Windows.Models;
using PitlaneDanmaku.Windows.Rendering;

namespace PitlaneDanmaku.Windows.Services;

public sealed class LocalObsServer : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly object _gate = new();
    private readonly AssetCatalog _assets;
    private readonly LogService _log;
    private readonly List<StreamWriter> _clients = [];
    private TcpListener? _listener;
    private CancellationTokenSource? _cancellation;
    private Task? _serverTask;
    private AppSettings _settings = new();

    public LocalObsServer(AssetCatalog assets, LogService log)
    {
        _assets = assets;
        _log = log;
    }

    public string OverlayUrl => $"http://127.0.0.1:{_settings.ObsPort}/overlay";

    public Task StartAsync(AppSettings settings)
    {
        Stop();
        _settings = settings.Clone();
        _settings.Normalize();

        _listener = new TcpListener(IPAddress.Loopback, _settings.ObsPort);
        _listener.Start();

        _cancellation = new CancellationTokenSource();
        _serverTask = Task.Run(() => AcceptLoopAsync(_cancellation.Token));
        _log.Info($"OBS 浏览器源已启动：{OverlayUrl}");
        return Task.CompletedTask;
    }

    public void Broadcast(ChatMessage message)
    {
        var dto = new OverlayMessageDto(
            message.Id,
            message.UserName,
            message.Text,
            message.Kind == ChatMessageKind.SuperChat ? "superchat" : "comment",
            message.Price,
            message.ReceivedAt.ToString("O"));

        var line = "data: " + JsonSerializer.Serialize(dto, JsonOptions) + "\n\n";
        List<StreamWriter> clients;
        lock (_gate)
        {
            clients = _clients.ToList();
        }

        foreach (var client in clients)
        {
            try
            {
                client.Write(line);
                client.Flush();
            }
            catch
            {
                RemoveClient(client);
            }
        }
    }

    public void Stop()
    {
        _cancellation?.Cancel();
        _cancellation?.Dispose();
        _cancellation = null;

        try
        {
            _listener?.Stop();
        }
        catch
        {
            // Stopping the listener also interrupts pending accepts.
        }

        _listener = null;

        lock (_gate)
        {
            foreach (var client in _clients)
            {
                client.Dispose();
            }

            _clients.Clear();
        }
    }

    public void Dispose() => Stop();

    private async Task AcceptLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var listener = _listener;
            if (listener is null)
            {
                return;
            }

            try
            {
                var client = await listener.AcceptTcpClientAsync(cancellationToken);
                _ = Task.Run(() => HandleClientAsync(client, cancellationToken), cancellationToken);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (SocketException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception ex)
            {
                _log.Warn($"OBS 请求处理失败：{ex.Message}");
            }
        }
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken cancellationToken)
    {
        using var ownedClient = client;
        await using var stream = ownedClient.GetStream();

        var request = await ReadRequestAsync(stream, cancellationToken);
        if (request is null)
        {
            return;
        }

        if (request.Path.Equals("/", StringComparison.OrdinalIgnoreCase) ||
            request.Path.Equals("/overlay", StringComparison.OrdinalIgnoreCase))
        {
            await WriteTextAsync(stream, BuildOverlayHtml(), "text/html; charset=utf-8", cancellationToken);
            return;
        }

        if (request.Path.Equals("/events", StringComparison.OrdinalIgnoreCase))
        {
            await HandleEventsAsync(stream, cancellationToken);
            return;
        }

        if (request.Path.StartsWith("/assets/", StringComparison.OrdinalIgnoreCase))
        {
            await ServeAssetAsync(stream, request.Path, cancellationToken);
            return;
        }

        await WriteTextAsync(stream, "Not Found", "text/plain; charset=utf-8", cancellationToken, 404, "Not Found");
    }

    private static async Task<HttpRequest?> ReadRequestAsync(Stream stream, CancellationToken cancellationToken)
    {
        using var reader = new StreamReader(stream, Encoding.ASCII, detectEncodingFromByteOrderMarks: false, bufferSize: 4096, leaveOpen: true);
        var requestLine = await reader.ReadLineAsync(cancellationToken);
        if (string.IsNullOrWhiteSpace(requestLine))
        {
            return null;
        }

        while (true)
        {
            var line = await reader.ReadLineAsync(cancellationToken);
            if (string.IsNullOrEmpty(line))
            {
                break;
            }
        }

        var parts = requestLine.Split(' ', 3, StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length < 2)
        {
            return null;
        }

        var target = parts[1];
        var path = Uri.TryCreate(target, UriKind.Absolute, out var absolute)
            ? absolute.AbsolutePath
            : target.Split('?', 2)[0];

        path = string.IsNullOrWhiteSpace(path) ? "/" : Uri.UnescapeDataString(path);
        return new HttpRequest(path);
    }

    private async Task HandleEventsAsync(Stream stream, CancellationToken cancellationToken)
    {
        var headers =
            "HTTP/1.1 200 OK\r\n" +
            "Content-Type: text/event-stream; charset=utf-8\r\n" +
            "Cache-Control: no-cache\r\n" +
            "Connection: keep-alive\r\n" +
            "Access-Control-Allow-Origin: *\r\n" +
            "\r\n";
        await WriteBytesAsync(stream, Encoding.ASCII.GetBytes(headers), cancellationToken);

        var writer = new StreamWriter(stream, new UTF8Encoding(false), bufferSize: 1024, leaveOpen: false)
        {
            AutoFlush = true
        };

        lock (_gate)
        {
            _clients.Add(writer);
        }

        await writer.WriteAsync(": connected\n\n");

        try
        {
            await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
        }
        catch
        {
            // The client closed the connection or the local server stopped.
        }
        finally
        {
            RemoveClient(writer);
        }
    }

    private async Task ServeAssetAsync(Stream stream, string path, CancellationToken cancellationToken)
    {
        string filePath;
        try
        {
            filePath = _assets.ResolveAssetPath(path);
        }
        catch
        {
            await WriteTextAsync(stream, "Forbidden", "text/plain; charset=utf-8", cancellationToken, 403, "Forbidden");
            return;
        }

        if (!File.Exists(filePath))
        {
            await WriteTextAsync(stream, "Not Found", "text/plain; charset=utf-8", cancellationToken, 404, "Not Found");
            return;
        }

        var contentType = Path.GetExtension(filePath).ToLowerInvariant() switch
        {
            ".png" => "image/png",
            ".svg" => "image/svg+xml",
            ".ttf" => "font/ttf",
            ".json" => "application/json; charset=utf-8",
            _ => "application/octet-stream"
        };

        await using var file = File.OpenRead(filePath);
        var headers =
            "HTTP/1.1 200 OK\r\n" +
            $"Content-Type: {contentType}\r\n" +
            $"Content-Length: {file.Length}\r\n" +
            "Cache-Control: no-cache\r\n" +
            "Access-Control-Allow-Origin: *\r\n" +
            "Connection: close\r\n" +
            "\r\n";
        await WriteBytesAsync(stream, Encoding.ASCII.GetBytes(headers), cancellationToken);
        await file.CopyToAsync(stream, cancellationToken);
    }

    private static async Task WriteTextAsync(
        Stream stream,
        string text,
        string contentType,
        CancellationToken cancellationToken,
        int statusCode = 200,
        string reasonPhrase = "OK")
    {
        var body = Encoding.UTF8.GetBytes(text);
        var headers =
            $"HTTP/1.1 {statusCode} {reasonPhrase}\r\n" +
            $"Content-Type: {contentType}\r\n" +
            $"Content-Length: {body.Length}\r\n" +
            "Cache-Control: no-cache\r\n" +
            "Access-Control-Allow-Origin: *\r\n" +
            "Connection: close\r\n" +
            "\r\n";

        await WriteBytesAsync(stream, Encoding.ASCII.GetBytes(headers), cancellationToken);
        await WriteBytesAsync(stream, body, cancellationToken);
    }

    private static Task WriteBytesAsync(Stream stream, byte[] bytes, CancellationToken cancellationToken)
    {
        return stream.WriteAsync(bytes.AsMemory(0, bytes.Length), cancellationToken).AsTask();
    }

    private void RemoveClient(StreamWriter writer)
    {
        lock (_gate)
        {
            _clients.Remove(writer);
        }

        writer.Dispose();
    }

    private string BuildOverlayHtml()
    {
        var cars = _assets.Cars.Select(car => new
        {
            id = car.Id,
            url = _assets.ToWebPath(car),
            width = car.Width,
            height = car.Height
        });
        var carsJson = JsonSerializer.Serialize(cars, JsonOptions);
        var framePath = "/assets/comment-box/comment_frame.png";
        var minVisible = _settings.MinVisibleItems;
        var maxStageWidth = _settings.MaxStageWidth;

        return $$"""
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Pitlane Danmaku Overlay</title>
  <style>
    {{EmbeddedFonts.HarmonySansScCss}}
    html, body, #stage {
      width: 100%;
      height: 100%;
      margin: 0;
      overflow: hidden;
      background: transparent;
      font-family: "{{EmbeddedFonts.HarmonySansScFamilyName}}", "Microsoft YaHei", "Segoe UI", sans-serif;
    }
    .item {
      position: absolute;
      width: {{OverlayLayout.BaseWidth}}px;
      height: {{OverlayLayout.BaseHeight}}px;
      left: 0;
      bottom: 10px;
      transform-origin: left bottom;
      will-change: transform;
    }
    .frame {
      position: absolute;
      left: 0;
      top: 0;
      width: {{OverlayLayout.FrameWidth}}px;
      height: {{OverlayLayout.FrameHeight}}px;
      object-fit: contain;
    }
    .car {
      position: absolute;
      object-fit: contain;
      pointer-events: none;
    }
    .text {
      position: absolute;
      left: {{OverlayLayout.TextLeft}}px;
      top: {{OverlayLayout.TextTop}}px;
      width: {{OverlayLayout.TextWidth}}px;
      color: white;
      text-shadow: 0 3px 8px rgba(0,0,0,.55);
    }
    .name {
      display: flex;
      gap: 12px;
      align-items: center;
      font-size: {{OverlayLayout.NameFontSize}}px;
      line-height: 1;
      font-weight: 800;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      max-width: {{OverlayLayout.NameWidth}}px;
    }
    .msg {
      margin-top: {{OverlayLayout.MessageTopMargin}}px;
      font-size: {{OverlayLayout.MessageFontSize}}px;
      line-height: {{OverlayLayout.MessageLineHeight}}px;
      font-weight: 650;
      max-height: {{OverlayLayout.MessageMaxHeight}}px;
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
      max-width: {{OverlayLayout.SuperChatNameWidth}}px;
    }
  </style>
</head>
<body>
  <div id="stage"></div>
  <script>
    const cars = {{carsJson}};
    const framePath = "{{framePath}}";
    const minVisible = {{minVisible}};
    const maxStageWidth = {{maxStageWidth}};
    const stage = document.getElementById("stage");
    const items = [];
    const baseWidth = {{OverlayLayout.BaseWidth}};
    const baseHeight = {{OverlayLayout.BaseHeight}};
    const baseGap = {{OverlayLayout.BaseGap}};
    const carLeft = {{OverlayLayout.CarLeft}};
    const carBottom = {{OverlayLayout.CarBottom}};

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
      const capacity = Math.max(minVisible, Math.floor(Math.min(width, maxStageWidth) / (itemWidth + gap)));

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
      for (let index = items.length - 1; index >= 0; index--) {
        const item = items[index];
        const pressure = Math.min(1, items.length / Math.max(1, minVisible + 3));
        const easing = item.leaving ? 0.075 : 0.045 + pressure * 0.035;
        item.x += (item.target - item.x) * easing;
        item.node.style.transform = item.node.style.transform.replace(/translateX\([^)]*\)/, `translateX(${item.x}px)`);
        if (item.leaving && item.x > stage.clientWidth + baseWidth) {
          item.node.remove();
          items.splice(index, 1);
        }
      }
      requestAnimationFrame(tick);
    }

    function escapeHtml(value) {
      return String(value ?? "").replace(/[&<>"']/g, ch => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        "\"": "&quot;",
        "'": "&#39;"
      }[ch]));
    }

    addEventListener("resize", layout);
    new EventSource("/events").onmessage = event => addMessage(JSON.parse(event.data));
    requestAnimationFrame(tick);
  </script>
</body>
</html>
""";
    }

    private sealed record HttpRequest(string Path);
}

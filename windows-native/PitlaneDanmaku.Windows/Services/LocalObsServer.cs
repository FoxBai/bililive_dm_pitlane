using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public sealed class LocalObsServer : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly object _gate = new();
    private readonly AssetCatalog _assets;
    private readonly LogService _log;
    private readonly List<StreamWriter> _clients = [];
    private HttpListener? _listener;
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

        _listener = new HttpListener();
        _listener.Prefixes.Add($"http://127.0.0.1:{_settings.ObsPort}/");
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
            // HttpListener 停止时可能同时打断等待中的请求，可以安全忽略。
        }

        _listener?.Close();
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
        while (!cancellationToken.IsCancellationRequested && _listener is { IsListening: true } listener)
        {
            try
            {
                var context = await listener.GetContextAsync();
                _ = Task.Run(() => HandleRequestAsync(context, cancellationToken), cancellationToken);
            }
            catch when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (HttpListenerException)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (Exception ex)
            {
                _log.Warn($"OBS 请求处理失败：{ex.Message}");
            }
        }
    }

    private async Task HandleRequestAsync(HttpListenerContext context, CancellationToken cancellationToken)
    {
        var path = context.Request.Url?.AbsolutePath ?? "/";
        if (path.Equals("/", StringComparison.OrdinalIgnoreCase) || path.Equals("/overlay", StringComparison.OrdinalIgnoreCase))
        {
            await WriteTextAsync(context.Response, BuildOverlayHtml(), "text/html; charset=utf-8", cancellationToken);
            return;
        }

        if (path.Equals("/events", StringComparison.OrdinalIgnoreCase))
        {
            await HandleEventsAsync(context, cancellationToken);
            return;
        }

        if (path.StartsWith("/assets/", StringComparison.OrdinalIgnoreCase))
        {
            await ServeAssetAsync(context, path, cancellationToken);
            return;
        }

        context.Response.StatusCode = 404;
        context.Response.Close();
    }

    private async Task HandleEventsAsync(HttpListenerContext context, CancellationToken cancellationToken)
    {
        var response = context.Response;
        response.ContentType = "text/event-stream";
        response.Headers["Cache-Control"] = "no-cache";
        response.Headers["Access-Control-Allow-Origin"] = "*";
        response.SendChunked = true;

        var writer = new StreamWriter(response.OutputStream, new UTF8Encoding(false)) { AutoFlush = true };
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
            // 连接关闭或服务停止时统一清理客户端。
        }
        finally
        {
            RemoveClient(writer);
        }
    }

    private async Task ServeAssetAsync(HttpListenerContext context, string path, CancellationToken cancellationToken)
    {
        string filePath;
        try
        {
            filePath = _assets.ResolveAssetPath(path);
        }
        catch
        {
            context.Response.StatusCode = 403;
            context.Response.Close();
            return;
        }

        if (!File.Exists(filePath))
        {
            context.Response.StatusCode = 404;
            context.Response.Close();
            return;
        }

        context.Response.ContentType = Path.GetExtension(filePath).ToLowerInvariant() switch
        {
            ".png" => "image/png",
            ".svg" => "image/svg+xml",
            ".json" => "application/json; charset=utf-8",
            _ => "application/octet-stream"
        };

        await using var file = File.OpenRead(filePath);
        context.Response.ContentLength64 = file.Length;
        await file.CopyToAsync(context.Response.OutputStream, cancellationToken);
        context.Response.Close();
    }

    private async Task WriteTextAsync(HttpListenerResponse response, string text, string contentType, CancellationToken cancellationToken)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        response.ContentType = contentType;
        response.ContentLength64 = bytes.Length;
        await response.OutputStream.WriteAsync(bytes, cancellationToken);
        response.Close();
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
    html, body, #stage {
      width: 100%;
      height: 100%;
      margin: 0;
      overflow: hidden;
      background: transparent;
      font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
    }
    .item {
      position: absolute;
      width: 1040px;
      height: 500px;
      left: 0;
      bottom: 10px;
      transform-origin: left bottom;
      will-change: transform;
    }
    .frame {
      position: absolute;
      left: 0;
      top: 0;
      width: 555px;
      height: 500px;
      object-fit: contain;
    }
    .car {
      position: absolute;
      left: 486px;
      top: 250px;
      width: 555px;
      height: auto;
      object-fit: contain;
    }
    .text {
      position: absolute;
      left: 86px;
      top: 278px;
      width: 360px;
      color: white;
      text-shadow: 0 3px 8px rgba(0,0,0,.55);
    }
    .name {
      display: flex;
      gap: 10px;
      align-items: center;
      font-size: 52px;
      line-height: 1;
      font-weight: 800;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .msg {
      margin-top: 24px;
      font-size: 44px;
      line-height: 1.12;
      font-weight: 650;
      display: -webkit-box;
      -webkit-line-clamp: 2;
      -webkit-box-orient: vertical;
      overflow: hidden;
    }
    .badge {
      display: none;
      padding: 6px 12px;
      border-radius: 10px;
      color: #16120A;
      background: #FDE68A;
      font-size: 30px;
      line-height: 1;
      font-weight: 900;
    }
    .superchat .badge {
      display: inline-block;
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
    const baseWidth = 1040;
    const baseHeight = 500;
    const baseGap = 24;

    function pickCar() {
      return cars[Math.floor(Math.random() * cars.length)];
    }

    function addMessage(message) {
      if (!cars.length) return;
      const car = pickCar();
      const node = document.createElement("div");
      node.className = "item " + (message.kind === "superchat" ? "superchat" : "comment");
      node.innerHTML = `
        <img class="frame" src="${framePath}" alt="">
        <img class="car" src="${car.url}" alt="">
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
}

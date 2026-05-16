using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public sealed partial class BilibiliWebRoomClient : IDisposable
{
    private static readonly Uri RoomInitEndpoint = new("https://api.live.bilibili.com/room/v1/Room/room_init");
    private static readonly Uri DanmuInfoEndpoint = new("https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo");
    private static readonly Uri HistoryEndpoint = new("https://api.live.bilibili.com/xlive/web-room/v1/dM/gethistory");
    private static readonly Uri FingerprintEndpoint = new("https://api.bilibili.com/x/frontend/finger/spi");
    private static readonly Uri WbiNavEndpoint = new("https://api.bilibili.com/x/web-interface/nav");
    private static readonly int[] WbiMixinKeyTable =
    [
        46, 47, 18, 2, 53, 8, 23, 32,
        15, 50, 10, 31, 58, 3, 45, 35,
        27, 43, 5, 49, 33, 9, 42, 19,
        29, 28, 14, 39, 12, 38, 41, 13,
        37, 48, 7, 16, 24, 55, 40, 61,
        26, 17, 0, 1, 60, 51, 30, 4,
        22, 25, 54, 21, 56, 59, 6, 63,
        57, 62, 11, 36, 20, 34, 44, 52
    ];
    private static readonly IReadOnlyList<DanmakuHost> DefaultHosts =
    [
        new("broadcastlv.chat.bilibili.com", 2243, 2244, 443)
    ];

    private readonly HttpClient _httpClient = new();
    private readonly LogService _log;
    private readonly Dictionary<long, string> _nameCache = [];
    private readonly object _historyGate = new();
    private readonly HashSet<string> _historySeenIds = [];
    private WbiKeys? _wbiKeys;
    private DateTimeOffset _wbiKeysFetchedAt;
    private DateTimeOffset _lastRealtimeMessageAt = DateTimeOffset.MinValue;
    private long _realtimeMessageCount;
    private bool _historyPollWarned;
    private bool _maskedNameWarned;
    private CancellationTokenSource? _runCancellation;
    private Task? _runTask;

    public BilibiliWebRoomClient(LogService log)
    {
        _log = log;
    }

    public event Action<ChatMessage>? MessageReceived;

    public Task StartAsync(AppSettings settings, CancellationToken cancellationToken = default)
    {
        Stop();
        settings = settings.Clone();
        settings.Normalize();
        _runCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var runCancellation = _runCancellation;
        _runTask = Task.Run(async () =>
        {
            try
            {
                await RunAsync(settings, runCancellation.Token);
            }
            catch (OperationCanceledException) when (runCancellation.IsCancellationRequested)
            {
                // User requested disconnect.
            }
            catch (Exception ex)
            {
                _log.Error($"B站直播间连接失败：{ex.Message}");
            }
        }, _runCancellation.Token);
        return Task.CompletedTask;
    }

    public void Stop()
    {
        _runCancellation?.Cancel();
        _runCancellation?.Dispose();
        _runCancellation = null;
    }

    public void Dispose()
    {
        Stop();
        _httpClient.Dispose();
    }

    private async Task RunAsync(AppSettings settings, CancellationToken cancellationToken)
    {
        await RefreshGeneratedBuvid3Async(settings, cancellationToken);
        if (ExtractCredentialUid(settings.Cookie) is not null)
        {
            _log.Info("已检测到登录 Cookie，将使用登录 uid 进行弹幕握手。");
        }

        var roomId = await ResolveRoomIdAsync(settings, cancellationToken);
        using var historyCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var historyTask = Task.Run(
            () => HistoryPollingLoopAsync(settings, roomId, historyCancellation.Token),
            historyCancellation.Token);

        try
        {
            string token;
            IReadOnlyList<DanmakuHost> hosts;
            try
            {
                (token, hosts) = await GetDanmuInfoAsync(settings, roomId, cancellationToken);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception ex)
            {
                token = "";
                hosts = DefaultHosts;
                _log.Warn($"获取 B站弹幕服务器列表失败：{ex.Message}。将尝试默认 WSS 节点；如仍失败，请填写网页 Cookie 后重试。");
            }

            _log.Info($"B站真实房间号：{roomId}，可用弹幕服务器：{hosts.Count} 个。");

            while (!cancellationToken.IsCancellationRequested)
            {
                foreach (var host in hosts)
                {
                    try
                    {
                        await ConnectAndReadAsync(settings, roomId, token, host, cancellationToken);
                    }
                    catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                    {
                        return;
                    }
                    catch (Exception ex)
                    {
                        _log.Warn($"弹幕服务器 {host.Host} 断开：{ex.Message}");
                    }

                    if (cancellationToken.IsCancellationRequested)
                    {
                        return;
                    }
                }

                _log.Warn("全部弹幕服务器都已断开，5 秒后重连。");
                await Task.Delay(TimeSpan.FromSeconds(5), cancellationToken);
            }
        }
        finally
        {
            historyCancellation.Cancel();
            try
            {
                await historyTask;
            }
            catch (OperationCanceledException)
            {
            }
        }
    }

    private async Task<int> ResolveRoomIdAsync(AppSettings settings, CancellationToken cancellationToken)
    {
        var input = settings.RoomInput.Trim();
        var match = RoomIdPattern().Match(input);
        if (!match.Success || !int.TryParse(match.Groups["id"].Value, out var roomId))
        {
            throw new InvalidOperationException("请输入 B站直播间 ID 或直播间 URL。");
        }

        var url = new UriBuilder(RoomInitEndpoint)
        {
            Query = $"id={roomId}"
        }.Uri;

        using var request = CreateRequest(url, settings);
        using var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken));
        ThrowIfBilibiliApiFailed(document.RootElement, "房间初始化");

        if (document.RootElement.TryGetProperty("data", out var data) &&
            data.TryGetProperty("room_id", out var realRoomId) &&
            realRoomId.TryGetInt32(out var resolved))
        {
            return resolved;
        }

        return roomId;
    }

    private async Task<(string Token, IReadOnlyList<DanmakuHost> Hosts)> GetDanmuInfoAsync(
        AppSettings settings,
        int roomId,
        CancellationToken cancellationToken)
    {
        var query = await BuildWbiSignedQueryAsync(
            settings,
            new Dictionary<string, string>
            {
                ["id"] = roomId.ToString(CultureInfo.InvariantCulture),
                ["type"] = "0",
                ["web_location"] = "444.8"
            },
            cancellationToken);

        var url = new UriBuilder(DanmuInfoEndpoint)
        {
            Query = query
        }.Uri;

        using var request = CreateRequest(url, settings, roomId);
        using var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken));
        ThrowIfBilibiliApiFailed(document.RootElement, "弹幕服务器信息");

        if (!document.RootElement.TryGetProperty("data", out var data))
        {
            throw new InvalidOperationException("B站弹幕服务器信息缺少 data 字段。");
        }

        var token = data.TryGetProperty("token", out var tokenElement) ? tokenElement.GetString() ?? "" : "";
        var hosts = new List<DanmakuHost>();

        if (data.TryGetProperty("host_list", out var hostList) && hostList.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in hostList.EnumerateArray())
            {
                var host = item.TryGetProperty("host", out var hostElement) ? hostElement.GetString() ?? "" : "";
                var port = item.TryGetProperty("port", out var portElement) && portElement.TryGetInt32(out var p) ? p : 2243;
                var wsPort = item.TryGetProperty("ws_port", out var wsElement) && wsElement.TryGetInt32(out var wp) ? wp : 2244;
                var wssPort = item.TryGetProperty("wss_port", out var wssElement) && wssElement.TryGetInt32(out var wsp) ? wsp : 443;

                if (!string.IsNullOrWhiteSpace(host))
                {
                    hosts.Add(new DanmakuHost(host, port, wsPort, wssPort));
                }
            }
        }

        if (hosts.Count == 0)
        {
            throw new InvalidOperationException("B站未返回可用弹幕服务器。");
        }

        return (token, hosts);
    }

    private async Task HistoryPollingLoopAsync(
        AppSettings settings,
        int roomId,
        CancellationToken cancellationToken)
    {
        var seeded = false;
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var messages = await GetHistoryMessagesAsync(settings, roomId, cancellationToken);
                var freshMessages = MarkFreshHistoryMessages(messages);
                if (!seeded)
                {
                    seeded = true;
                    _log.Info("历史弹幕补偿轮询已就绪。");
                }
                else if (freshMessages.Count > 0 &&
                         DateTimeOffset.UtcNow - _lastRealtimeMessageAt > TimeSpan.FromSeconds(6))
                {
                    var displayedCount = 0;
                    foreach (var message in freshMessages)
                    {
                        var resolvedMessage = ResolveCachedName(message);
                        if (LooksMasked(resolvedMessage.UserName))
                        {
                            continue;
                        }

                        MessageReceived?.Invoke(resolvedMessage);
                        displayedCount++;
                    }

                    if (displayedCount > 0)
                    {
                        _log.Info($"历史弹幕补偿显示 {displayedCount} 条。");
                    }
                }

                _historyPollWarned = false;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception ex)
            {
                if (!_historyPollWarned)
                {
                    _log.Warn($"历史弹幕补偿轮询失败：{ex.Message}");
                    _historyPollWarned = true;
                }
            }

            await Task.Delay(TimeSpan.FromSeconds(3), cancellationToken);
        }
    }

    private async Task<IReadOnlyList<ChatMessage>> GetHistoryMessagesAsync(
        AppSettings settings,
        int roomId,
        CancellationToken cancellationToken)
    {
        var url = new UriBuilder(HistoryEndpoint)
        {
            Query = $"roomid={roomId}&room_type=0"
        }.Uri;

        using var request = CreateRequest(url, settings, roomId);
        using var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken));
        ThrowIfBilibiliApiFailed(document.RootElement, "历史弹幕");
        return BilibiliMessageParser.ParseHistoryMessages(document.RootElement);
    }

    private async Task RefreshGeneratedBuvid3Async(AppSettings settings, CancellationToken cancellationToken)
    {
        if (CookieContains(settings.Cookie.Trim(), "buvid3") ||
            (!string.IsNullOrWhiteSpace(settings.Buvid3) && !LooksLocallyGeneratedBuvid3(settings.Buvid3)))
        {
            return;
        }

        try
        {
            using var request = CreateRequest(FingerprintEndpoint, settings);
            request.Headers.Referrer = new Uri("https://www.bilibili.com/");
            using var response = await _httpClient.SendAsync(request, cancellationToken);
            response.EnsureSuccessStatusCode();
            using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken));
            ThrowIfBilibiliApiFailed(document.RootElement, "buvid3");

            if (document.RootElement.TryGetProperty("data", out var data) &&
                data.TryGetProperty("b_3", out var buvidElement) &&
                buvidElement.ValueKind == JsonValueKind.String)
            {
                var buvid3 = buvidElement.GetString();
                if (!string.IsNullOrWhiteSpace(buvid3))
                {
                    settings.Buvid3 = buvid3.Trim();
                    _log.Info("已获取 B站网页 buvid3，用于直播弹幕握手。");
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception ex)
        {
            _log.Warn($"获取 B站 buvid3 失败：{ex.Message}。将继续使用当前 buvid3。");
        }
    }

    private IReadOnlyList<ChatMessage> MarkFreshHistoryMessages(IReadOnlyList<ChatMessage> messages)
    {
        var freshMessages = new List<ChatMessage>();
        lock (_historyGate)
        {
            foreach (var message in messages.Reverse())
            {
                if (_historySeenIds.Add(message.Id))
                {
                    freshMessages.Add(message);
                }
            }

            if (_historySeenIds.Count > 500)
            {
                _historySeenIds.RemoveWhere(id => _historySeenIds.Count > 400 && !freshMessages.Any(message => message.Id == id));
            }
        }

        return freshMessages;
    }

    private async Task<string> BuildWbiSignedQueryAsync(
        AppSettings settings,
        IReadOnlyDictionary<string, string> parameters,
        CancellationToken cancellationToken)
    {
        var keys = await GetWbiKeysAsync(settings, cancellationToken);
        var signedParameters = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (var parameter in parameters)
        {
            signedParameters[parameter.Key] = parameter.Value;
        }

        signedParameters["wts"] = DateTimeOffset.UtcNow.ToUnixTimeSeconds().ToString(CultureInfo.InvariantCulture);

        var unsignedQuery = BuildQueryString(signedParameters);
        var signature = Convert.ToHexString(MD5.HashData(Encoding.UTF8.GetBytes(unsignedQuery + keys.MixinKey)))
            .ToLowerInvariant();
        signedParameters["w_rid"] = signature;
        return BuildQueryString(signedParameters);
    }

    private async Task<WbiKeys> GetWbiKeysAsync(AppSettings settings, CancellationToken cancellationToken)
    {
        var now = DateTimeOffset.UtcNow;
        if (_wbiKeys is { } cached && now - _wbiKeysFetchedAt < TimeSpan.FromHours(6))
        {
            return cached;
        }

        using var request = CreateRequest(WbiNavEndpoint, settings);
        request.Headers.Referrer = new Uri("https://www.bilibili.com/");
        using var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken));

        if (!document.RootElement.TryGetProperty("data", out var data) ||
            !data.TryGetProperty("wbi_img", out var wbiImg))
        {
            throw new InvalidOperationException("B站 WBI key 接口缺少 data.wbi_img 字段。");
        }

        var imgKey = ExtractWbiKey(wbiImg, "img_url");
        var subKey = ExtractWbiKey(wbiImg, "sub_url");
        var mixinKey = BuildMixinKey(imgKey + subKey);
        _wbiKeys = new WbiKeys(mixinKey);
        _wbiKeysFetchedAt = now;
        return _wbiKeys;
    }

    private static string ExtractWbiKey(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var property) ||
            property.ValueKind != JsonValueKind.String ||
            string.IsNullOrWhiteSpace(property.GetString()))
        {
            throw new InvalidOperationException($"B站 WBI key 接口缺少 {propertyName}。");
        }

        var url = property.GetString()!;
        var fileName = Path.GetFileNameWithoutExtension(new Uri(url).AbsolutePath);
        if (string.IsNullOrWhiteSpace(fileName))
        {
            throw new InvalidOperationException($"B站 WBI key {propertyName} 无法解析。");
        }

        return fileName;
    }

    private static string BuildMixinKey(string rawKey)
    {
        var builder = new StringBuilder(32);
        foreach (var index in WbiMixinKeyTable.Take(32))
        {
            if (index >= rawKey.Length)
            {
                throw new InvalidOperationException("B站 WBI key 长度异常。");
            }

            builder.Append(rawKey[index]);
        }

        return builder.ToString();
    }

    private static string BuildQueryString(IEnumerable<KeyValuePair<string, string>> parameters)
    {
        return string.Join("&", parameters.Select(pair =>
            $"{Uri.EscapeDataString(pair.Key)}={Uri.EscapeDataString(FilterWbiValue(pair.Value))}"));
    }

    private static string FilterWbiValue(string value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return "";
        }

        var builder = new StringBuilder(value.Length);
        foreach (var ch in value)
        {
            if (ch is not ('!' or '\'' or '(' or ')' or '*'))
            {
                builder.Append(ch);
            }
        }

        return builder.ToString();
    }

    private async Task ConnectAndReadAsync(
        AppSettings settings,
        int roomId,
        string token,
        DanmakuHost host,
        CancellationToken cancellationToken)
    {
        Exception? lastError = null;

        if (host.SecureWebSocketPort > 0)
        {
            try
            {
                await ConnectAndReadWebSocketAsync(settings, roomId, token, host, useTls: true, cancellationToken);
                return;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception ex)
            {
                lastError = ex;
                _log.Warn($"WSS 弹幕服务器 {host.Host}:{host.SecureWebSocketPort} 连接失败：{ex.Message}");
            }
        }

        if (host.WebSocketPort > 0)
        {
            try
            {
                await ConnectAndReadWebSocketAsync(settings, roomId, token, host, useTls: false, cancellationToken);
                return;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception ex)
            {
                lastError = ex;
                _log.Warn($"WS 弹幕服务器 {host.Host}:{host.WebSocketPort} 连接失败：{ex.Message}");
            }
        }

        try
        {
            await ConnectAndReadTcpAsync(settings, roomId, token, host, cancellationToken);
        }
        catch (Exception ex) when (lastError is not null && ex is not OperationCanceledException)
        {
            throw new InvalidOperationException($"{lastError.Message}; TCP fallback: {ex.Message}", ex);
        }
    }

    private async Task ConnectAndReadTcpAsync(
        AppSettings settings,
        int roomId,
        string token,
        DanmakuHost host,
        CancellationToken cancellationToken)
    {
        using var client = new TcpClient();
        await client.ConnectAsync(host.Host, host.Port, cancellationToken);
        await using var stream = client.GetStream();

        _log.Info($"已连接 TCP 弹幕服务器 {host.Host}:{host.Port}。");
        await SendAuthAsync(stream, settings, roomId, token, cancellationToken);
        _ = Task.Run(() => HeartbeatLoopAsync(stream, cancellationToken), cancellationToken);
        await ReadLoopAsync(stream, cancellationToken);
    }

    private async Task ConnectAndReadWebSocketAsync(
        AppSettings settings,
        int roomId,
        string token,
        DanmakuHost host,
        bool useTls,
        CancellationToken cancellationToken)
    {
        using var socket = new ClientWebSocket();
        socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
        socket.Options.SetRequestHeader("User-Agent", settings.UserAgent);
        socket.Options.SetRequestHeader("Origin", "https://live.bilibili.com");
        socket.Options.SetRequestHeader("Referer", $"https://live.bilibili.com/{roomId}");
        var cookie = BuildCookieHeader(settings);
        if (!string.IsNullOrWhiteSpace(cookie))
        {
            socket.Options.SetRequestHeader("Cookie", cookie);
        }

        var port = useTls ? host.SecureWebSocketPort : host.WebSocketPort;
        var scheme = useTls ? "wss" : "ws";
        var uri = new Uri($"{scheme}://{host.Host}:{port}/sub");

        await socket.ConnectAsync(uri, cancellationToken);
        _log.Info($"已连接 {(useTls ? "WSS" : "WS")} 弹幕服务器 {host.Host}:{port}。");
        await SendAuthAsync(socket, settings, roomId, token, cancellationToken);
        _ = Task.Run(() => HeartbeatLoopAsync(socket, cancellationToken), cancellationToken);
        await ReadWebSocketLoopAsync(socket, cancellationToken);
    }

    private async Task SendAuthAsync(
        Stream stream,
        AppSettings settings,
        int roomId,
        string token,
        CancellationToken cancellationToken)
    {
        var packet = BuildAuthPacket(settings, roomId, token);
        await stream.WriteAsync(packet, cancellationToken);
    }

    private static Task SendAuthAsync(
        ClientWebSocket socket,
        AppSettings settings,
        int roomId,
        string token,
        CancellationToken cancellationToken)
    {
        var packet = BuildAuthPacket(settings, roomId, token);
        return socket.SendAsync(packet, WebSocketMessageType.Binary, true, cancellationToken);
    }

    private async Task HeartbeatLoopAsync(Stream stream, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(30), cancellationToken);
                var packet = BuildPacket(2, 1, []);
                await stream.WriteAsync(packet, cancellationToken);
            }
            catch
            {
                return;
            }
        }
    }

    private static async Task HeartbeatLoopAsync(ClientWebSocket socket, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(30), cancellationToken);
                var packet = BuildPacket(2, 1, []);
                await socket.SendAsync(packet, WebSocketMessageType.Binary, true, cancellationToken);
            }
            catch
            {
                return;
            }
        }
    }

    private async Task ReadLoopAsync(Stream stream, CancellationToken cancellationToken)
    {
        var header = new byte[16];
        while (!cancellationToken.IsCancellationRequested)
        {
            if (await ReadExactAsync(stream, header, cancellationToken) != header.Length)
            {
                return;
            }

            var packetLength = BinaryPrimitives.ReadInt32BigEndian(header.AsSpan(0, 4));
            var headerLength = BinaryPrimitives.ReadInt16BigEndian(header.AsSpan(4, 2));
            var version = BinaryPrimitives.ReadInt16BigEndian(header.AsSpan(6, 2));
            var operation = BinaryPrimitives.ReadInt32BigEndian(header.AsSpan(8, 4));

            if (packetLength < headerLength || packetLength > 16 * 1024 * 1024)
            {
                throw new InvalidDataException($"非法弹幕包长度：{packetLength}");
            }

            var payload = new byte[packetLength - headerLength];
            if (await ReadExactAsync(stream, payload, cancellationToken) != payload.Length)
            {
                return;
            }

            ProcessPacket(version, operation, payload);
        }
    }

    private async Task ReadWebSocketLoopAsync(ClientWebSocket socket, CancellationToken cancellationToken)
    {
        var buffer = new byte[64 * 1024];
        while (!cancellationToken.IsCancellationRequested && socket.State == WebSocketState.Open)
        {
            using var message = new MemoryStream();
            WebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(buffer, cancellationToken);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    return;
                }

                message.Write(buffer, 0, result.Count);
            }
            while (!result.EndOfMessage);

            var payload = message.ToArray();
            if (payload.Length == 0)
            {
                continue;
            }

            if (result.MessageType == WebSocketMessageType.Binary)
            {
                ProcessWebSocketPayload(payload);
            }
            else
            {
                PublishMessages(payload);
            }
        }
    }

    private void ProcessWebSocketPayload(ReadOnlySpan<byte> payload)
    {
        if (!TryProcessNestedPackets(payload))
        {
            PublishMessages(payload);
        }
    }

    private void ProcessPacket(int version, int operation, ReadOnlySpan<byte> payload)
    {
        if (operation == 8)
        {
            _log.Info("弹幕服务器认证成功，正在等待新弹幕。");
            return;
        }

        if (operation != 5)
        {
            return;
        }

        try
        {
            switch (version)
            {
                case 0:
                case 1:
                    PublishMessages(payload);
                    break;
                case 2:
                    ProcessDecompressed(DecompressZlib(payload.ToArray()));
                    break;
                case 3:
                    ProcessDecompressed(DecompressBrotli(payload.ToArray()));
                    break;
            }
        }
        catch (Exception ex)
        {
            _log.Warn($"弹幕包解析失败：{ex.Message}");
        }
    }

    private void ProcessDecompressed(byte[] payload)
    {
        if (TryProcessNestedPackets(payload))
        {
            return;
        }

        PublishMessages(payload);
    }

    private bool TryProcessNestedPackets(ReadOnlySpan<byte> payload)
    {
        var offset = 0;
        var processed = false;

        while (offset + 16 <= payload.Length)
        {
            var packetLength = BinaryPrimitives.ReadInt32BigEndian(payload.Slice(offset, 4));
            var headerLength = BinaryPrimitives.ReadInt16BigEndian(payload.Slice(offset + 4, 2));
            var version = BinaryPrimitives.ReadInt16BigEndian(payload.Slice(offset + 6, 2));
            var operation = BinaryPrimitives.ReadInt32BigEndian(payload.Slice(offset + 8, 4));

            if (packetLength < 16 || headerLength < 16 || offset + packetLength > payload.Length)
            {
                return false;
            }

            ProcessPacket(version, operation, payload.Slice(offset + headerLength, packetLength - headerLength));
            offset += packetLength;
            processed = true;
        }

        return processed && offset == payload.Length;
    }

    private void PublishMessages(ReadOnlySpan<byte> payload)
    {
        var messages = BilibiliMessageParser.ParseJsonMessages(payload);
        if (messages.Count == 0)
        {
            return;
        }

        var total = Interlocked.Add(ref _realtimeMessageCount, messages.Count);
        if (total <= 5 || total % 20 == 0)
        {
            _log.Info($"已接收 B站实时弹幕 {total} 条。");
        }

        _lastRealtimeMessageAt = DateTimeOffset.UtcNow;
        foreach (var message in messages)
        {
            PublishWithNameCache(message);
        }
    }

    private void PublishWithNameCache(ChatMessage message)
    {
        message = ResolveCachedName(message);
        if (LooksMasked(message.UserName))
        {
            WarnMaskedNameSuppressed();
            return;
        }

        MessageReceived?.Invoke(message);
    }

    private ChatMessage ResolveCachedName(ChatMessage message)
    {
        if (message.UserId is { } uid)
        {
            if (!LooksMasked(message.UserName))
            {
                _nameCache[uid] = message.UserName;
            }
            else if (_nameCache.TryGetValue(uid, out var cachedName))
            {
                message = message with { UserName = cachedName };
            }
        }

        return message;
    }

    private HttpRequestMessage CreateRequest(Uri url, AppSettings settings, int? roomId = null)
    {
        var request = new HttpRequestMessage(HttpMethod.Get, url);
        if (!request.Headers.UserAgent.TryParseAdd(settings.UserAgent))
        {
            request.Headers.UserAgent.ParseAdd(AppSettings.DefaultUserAgent);
        }

        request.Headers.Referrer = new Uri(roomId is null
            ? "https://live.bilibili.com/"
            : $"https://live.bilibili.com/{roomId}");
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        request.Headers.TryAddWithoutValidation("Origin", "https://live.bilibili.com");
        var cookie = BuildCookieHeader(settings);
        if (!string.IsNullOrWhiteSpace(cookie))
        {
            request.Headers.TryAddWithoutValidation("Cookie", cookie);
        }

        return request;
    }

    private static string BuildCookieHeader(AppSettings settings)
    {
        var cookie = settings.Cookie.Trim().TrimEnd(';');
        if (CookieContains(cookie, "buvid3"))
        {
            return cookie;
        }

        if (string.IsNullOrWhiteSpace(settings.Buvid3))
        {
            return cookie;
        }

        var buvidCookie = $"buvid3={settings.Buvid3}";
        return string.IsNullOrWhiteSpace(cookie)
            ? buvidCookie
            : $"{cookie}; {buvidCookie}";
    }

    private static bool CookieContains(string cookie, string name)
    {
        return cookie
            .Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Any(part => part.StartsWith(name + "=", StringComparison.OrdinalIgnoreCase));
    }

    private static long? ExtractCredentialUid(string cookie)
    {
        foreach (var part in cookie.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var pieces = part.Split('=', 2, StringSplitOptions.TrimEntries);
            if (pieces.Length == 2 &&
                pieces[0].Equals("DedeUserID", StringComparison.OrdinalIgnoreCase) &&
                long.TryParse(pieces[1], out var uid))
            {
                return uid;
            }
        }

        return null;
    }

    private static byte[] BuildAuthPacket(AppSettings settings, int roomId, string token)
    {
        var credentialUid = ExtractCredentialUid(settings.Cookie);
        var auth = new
        {
            uid = credentialUid ?? 0,
            roomid = roomId,
            protover = 3,
            platform = "danmuji",
            type = 2,
            key = token,
            buvid = settings.Buvid3
        };
        var payload = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(auth));
        return BuildPacket(7, 1, payload);
    }

    private static byte[] BuildPacket(int operation, int version, byte[] payload)
    {
        var packet = new byte[16 + payload.Length];
        BinaryPrimitives.WriteInt32BigEndian(packet.AsSpan(0, 4), packet.Length);
        BinaryPrimitives.WriteInt16BigEndian(packet.AsSpan(4, 2), 16);
        BinaryPrimitives.WriteInt16BigEndian(packet.AsSpan(6, 2), (short)version);
        BinaryPrimitives.WriteInt32BigEndian(packet.AsSpan(8, 4), operation);
        BinaryPrimitives.WriteInt32BigEndian(packet.AsSpan(12, 4), 1);
        payload.CopyTo(packet.AsSpan(16));
        return packet;
    }

    private static async Task<int> ReadExactAsync(Stream stream, byte[] buffer, CancellationToken cancellationToken)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = await stream.ReadAsync(buffer.AsMemory(offset, buffer.Length - offset), cancellationToken);
            if (read == 0)
            {
                return offset;
            }

            offset += read;
        }

        return offset;
    }

    private static byte[] DecompressBrotli(byte[] payload)
    {
        using var input = new MemoryStream(payload);
        using var brotli = new BrotliStream(input, CompressionMode.Decompress);
        using var output = new MemoryStream();
        brotli.CopyTo(output);
        return output.ToArray();
    }

    private static byte[] DecompressZlib(byte[] payload)
    {
        try
        {
            using var input = new MemoryStream(payload);
            using var zlib = new ZLibStream(input, CompressionMode.Decompress);
            using var output = new MemoryStream();
            zlib.CopyTo(output);
            return output.ToArray();
        }
        catch (InvalidDataException)
        {
            using var input = new MemoryStream(payload);
            using var deflate = new DeflateStream(input, CompressionMode.Decompress);
            using var output = new MemoryStream();
            deflate.CopyTo(output);
            return output.ToArray();
        }
    }

    private static void ThrowIfBilibiliApiFailed(JsonElement root, string label)
    {
        if (!root.TryGetProperty("code", out var codeElement) ||
            !codeElement.TryGetInt32(out var code) ||
            code == 0)
        {
            return;
        }

        var message = root.TryGetProperty("message", out var messageElement) && messageElement.ValueKind == JsonValueKind.String
            ? messageElement.GetString()
            : root.TryGetProperty("msg", out var msgElement) && msgElement.ValueKind == JsonValueKind.String
                ? msgElement.GetString()
                : "未知错误";

        throw new InvalidOperationException($"B站{label}接口返回错误 {code}：{message}");
    }

    private void WarnMaskedNameSuppressed()
    {
        if (_maskedNameWarned)
        {
            return;
        }

        _maskedNameWarned = true;
        _log.Warn("收到 B站返回的脱敏昵称，已跳过该条弹幕；如仍频繁出现，请填写登录后的网页 Cookie。");
    }

    private static bool LooksMasked(string userName)
    {
        userName = userName.Trim();
        var firstStar = userName.IndexOf('*');
        return firstStar > 0 &&
               userName.AsSpan(firstStar).IndexOfAnyExcept('*') < 0;
    }

    private static bool LooksLocallyGeneratedBuvid3(string buvid3)
    {
        return LocalBuvid3Pattern().IsMatch(buvid3.Trim());
    }

    [GeneratedRegex(@"(?:^|live\.bilibili\.com/(?:blanc/)?)(?<id>\d+)", RegexOptions.IgnoreCase)]
    private static partial Regex RoomIdPattern();

    [GeneratedRegex(@"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}infoc$")]
    private static partial Regex LocalBuvid3Pattern();

    private sealed record WbiKeys(string MixinKey);
}

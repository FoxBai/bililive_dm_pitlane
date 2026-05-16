using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public sealed partial class BilibiliWebRoomClient : IDisposable
{
    private static readonly Uri RoomInitEndpoint = new("https://api.live.bilibili.com/room/v1/Room/room_init");
    private static readonly Uri DanmuInfoEndpoint = new("https://api.live.bilibili.com/xlive/web-room/v1/index/getDanmuInfo");

    private readonly HttpClient _httpClient = new();
    private readonly LogService _log;
    private readonly Dictionary<long, string> _nameCache = [];
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
                // 用户主动断开时不作为错误显示。
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
        var roomId = await ResolveRoomIdAsync(settings, cancellationToken);
        var (token, hosts) = await GetDanmuInfoAsync(settings, roomId, cancellationToken);
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
                    _log.Warn($"弹幕服务器 {host.Host}:{host.Port} 断开：{ex.Message}");
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
        var url = new UriBuilder(DanmuInfoEndpoint)
        {
            Query = $"id={roomId}&type=0"
        }.Uri;

        using var request = CreateRequest(url, settings);
        using var response = await _httpClient.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        using var document = JsonDocument.Parse(await response.Content.ReadAsStreamAsync(cancellationToken));

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

    private async Task ConnectAndReadAsync(
        AppSettings settings,
        int roomId,
        string token,
        DanmakuHost host,
        CancellationToken cancellationToken)
    {
        using var client = new TcpClient();
        await client.ConnectAsync(host.Host, host.Port, cancellationToken);
        await using var stream = client.GetStream();

        _log.Info($"已连接弹幕服务器 {host.Host}:{host.Port}。");
        await SendAuthAsync(stream, settings, roomId, token, cancellationToken);
        _ = Task.Run(() => HeartbeatLoopAsync(stream, cancellationToken), cancellationToken);
        await ReadLoopAsync(stream, cancellationToken);
    }

    private async Task SendAuthAsync(
        NetworkStream stream,
        AppSettings settings,
        int roomId,
        string token,
        CancellationToken cancellationToken)
    {
        // protover: 3 请求 brotli 压缩包；platform/type 对齐弹幕姬网页端直连习惯。
        var auth = new
        {
            uid = 0,
            roomid = roomId,
            protover = 3,
            platform = "danmuji",
            type = 2,
            key = token,
            buvid = settings.Buvid3
        };
        var payload = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(auth));
        var packet = BuildPacket(7, 1, payload);
        await stream.WriteAsync(packet, cancellationToken);
    }

    private async Task HeartbeatLoopAsync(NetworkStream stream, CancellationToken cancellationToken)
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

    private async Task ReadLoopAsync(NetworkStream stream, CancellationToken cancellationToken)
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

    private void ProcessPacket(int version, int operation, ReadOnlySpan<byte> payload)
    {
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
        // 压缩包内通常继续嵌套标准 16 字节头，也可能是连续 JSON，二者都兼容。
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
        foreach (var message in BilibiliMessageParser.ParseJsonMessages(payload))
        {
            PublishWithNameCache(message);
        }
    }

    private void PublishWithNameCache(ChatMessage message)
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

        MessageReceived?.Invoke(message);
    }

    private HttpRequestMessage CreateRequest(Uri url, AppSettings settings)
    {
        var request = new HttpRequestMessage(HttpMethod.Get, url);
        request.Headers.UserAgent.ParseAdd(settings.UserAgent);
        request.Headers.Referrer = new Uri("https://live.bilibili.com/");
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        if (!string.IsNullOrWhiteSpace(settings.Cookie))
        {
            request.Headers.TryAddWithoutValidation("Cookie", settings.Cookie);
        }

        return request;
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

    private static async Task<int> ReadExactAsync(NetworkStream stream, byte[] buffer, CancellationToken cancellationToken)
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

    private static bool LooksMasked(string userName)
    {
        return userName.Contains("***", StringComparison.Ordinal) ||
               userName.Contains("****", StringComparison.Ordinal);
    }

    [GeneratedRegex(@"(?:^|live\.bilibili\.com/(?:blanc/)?)(?<id>\d+)", RegexOptions.IgnoreCase)]
    private static partial Regex RoomIdPattern();
}

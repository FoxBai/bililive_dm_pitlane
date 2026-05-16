namespace PitlaneDanmaku.Windows.Models;

public sealed record DanmakuHost(string Host, int Port, int WebSocketPort, int SecureWebSocketPort);

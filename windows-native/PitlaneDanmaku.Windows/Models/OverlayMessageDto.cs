namespace PitlaneDanmaku.Windows.Models;

public sealed record OverlayMessageDto(
    string Id,
    string UserName,
    string Text,
    string Kind,
    decimal? Price,
    string ReceivedAt);

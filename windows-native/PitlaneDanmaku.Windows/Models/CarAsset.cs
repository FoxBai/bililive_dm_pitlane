namespace PitlaneDanmaku.Windows.Models;

public sealed record CarAsset(
    string Id,
    string FileName,
    string AbsolutePath,
    int Width,
    int Height);

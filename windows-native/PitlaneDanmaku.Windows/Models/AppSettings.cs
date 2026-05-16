namespace PitlaneDanmaku.Windows.Models;

public sealed class AppSettings
{
    public const string DefaultUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";

    public string RoomInput { get; set; } = "";
    public string Cookie { get; set; } = "";
    public string UserAgent { get; set; } = DefaultUserAgent;
    public string Buvid3 { get; set; } = "";
    public int ObsPort { get; set; } = 17333;
    public int LaunchIntervalMs { get; set; } = 900;
    public int QueueLimit { get; set; } = 80;
    public int MinVisibleItems { get; set; } = 5;
    public int MaxNicknameLength { get; set; } = 18;
    public int MaxMessageLength { get; set; } = 42;
    public int MaxRepeatCharacters { get; set; } = 4;
    public int MaxStageWidth { get; set; } = 3840;
    public bool OnlySuperChat { get; set; }

    public AppSettings Clone()
    {
        return new AppSettings
        {
            RoomInput = RoomInput,
            Cookie = Cookie,
            UserAgent = UserAgent,
            Buvid3 = Buvid3,
            ObsPort = ObsPort,
            LaunchIntervalMs = LaunchIntervalMs,
            QueueLimit = QueueLimit,
            MinVisibleItems = MinVisibleItems,
            MaxNicknameLength = MaxNicknameLength,
            MaxMessageLength = MaxMessageLength,
            MaxRepeatCharacters = MaxRepeatCharacters,
            MaxStageWidth = MaxStageWidth,
            OnlySuperChat = OnlySuperChat
        };
    }

    public void Normalize()
    {
        UserAgent = string.IsNullOrWhiteSpace(UserAgent) ? DefaultUserAgent : UserAgent.Trim();
        RoomInput = RoomInput.Trim();
        Cookie = NormalizeCookieInput(Cookie);
        Buvid3 = string.IsNullOrWhiteSpace(Buvid3) ? GenerateBuvid3() : Buvid3.Trim();
        ObsPort = Math.Clamp(ObsPort, 1024, 65535);
        LaunchIntervalMs = Math.Clamp(LaunchIntervalMs, 120, 10000);
        QueueLimit = Math.Clamp(QueueLimit, 5, 500);
        MinVisibleItems = Math.Clamp(MinVisibleItems, 1, 12);
        MaxNicknameLength = Math.Clamp(MaxNicknameLength, 4, 64);
        MaxMessageLength = Math.Clamp(MaxMessageLength, 4, 200);
        MaxRepeatCharacters = Math.Clamp(MaxRepeatCharacters, 2, 20);
        MaxStageWidth = Math.Clamp(MaxStageWidth, 960, 7680);
    }

    private static string GenerateBuvid3()
    {
        return Guid.NewGuid().ToString().ToUpperInvariant() + "infoc";
    }

    private static string NormalizeCookieInput(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return "";
        }

        var text = value.Trim();
        var lines = text
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        var cookieLine = lines.FirstOrDefault(line => line.StartsWith("Cookie:", StringComparison.OrdinalIgnoreCase));
        if (cookieLine is not null)
        {
            return CleanCookieValue(cookieLine[("Cookie:".Length)..]);
        }

        var embeddedCookieIndex = text.IndexOf("Cookie:", StringComparison.OrdinalIgnoreCase);
        if (embeddedCookieIndex >= 0)
        {
            var cookieValue = text[(embeddedCookieIndex + "Cookie:".Length)..];
            var lineEnd = cookieValue.IndexOfAny(['\r', '\n']);
            if (lineEnd >= 0)
            {
                cookieValue = cookieValue[..lineEnd];
            }

            return CleanCookieValue(cookieValue);
        }

        return CleanCookieValue(text);
    }

    private static string CleanCookieValue(string value)
    {
        return value
            .Trim()
            .Trim('"', '\'', '`', '\\')
            .Trim()
            .TrimEnd(';');
    }
}

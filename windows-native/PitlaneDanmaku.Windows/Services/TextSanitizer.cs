using System.Text;
using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public static class TextSanitizer
{
    public static ChatMessage? Normalize(ChatMessage message, AppSettings settings)
    {
        var userName = CleanText(message.UserName, settings.MaxNicknameLength, settings.MaxRepeatCharacters);
        var text = CleanText(message.Text, settings.MaxMessageLength, settings.MaxRepeatCharacters);

        if (string.IsNullOrWhiteSpace(userName))
        {
            userName = "匿名观众";
        }

        if (!message.IsSuperChat && string.IsNullOrWhiteSpace(text))
        {
            return null;
        }

        return message with
        {
            UserName = userName,
            Text = string.IsNullOrWhiteSpace(text) ? " " : text
        };
    }

    private static string CleanText(string value, int maxLength, int repeatLimit)
    {
        var builder = new StringBuilder();
        var previous = '\0';
        var repeatCount = 0;

        foreach (var raw in value)
        {
            var ch = char.IsControl(raw) || char.IsWhiteSpace(raw) ? ' ' : raw;
            if (ch == previous)
            {
                repeatCount++;
                if (repeatCount > repeatLimit)
                {
                    continue;
                }
            }
            else
            {
                previous = ch;
                repeatCount = 1;
            }

            if (ch == ' ' && builder.Length > 0 && builder[^1] == ' ')
            {
                continue;
            }

            builder.Append(ch);
            if (builder.Length >= maxLength)
            {
                break;
            }
        }

        return builder.ToString().Trim();
    }
}

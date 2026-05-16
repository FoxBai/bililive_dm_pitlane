using System.Security.Cryptography;
using System.Text.Json;
using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public static class BilibiliMessageParser
{
    public static IReadOnlyList<ChatMessage> ParseJsonMessages(ReadOnlySpan<byte> payload)
    {
        var messages = new List<ChatMessage>();
        var reader = new Utf8JsonReader(payload, new JsonReaderOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = JsonCommentHandling.Skip
        });

        while (reader.Read())
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                continue;
            }

            using var document = JsonDocument.ParseValue(ref reader);
            var message = ParseRoot(document.RootElement, payload);
            if (message is not null)
            {
                messages.Add(message);
            }
        }

        return messages;
    }

    private static ChatMessage? ParseRoot(JsonElement root, ReadOnlySpan<byte> rawPayload)
    {
        var command = TryGetString(root, "cmd");
        if (string.IsNullOrWhiteSpace(command))
        {
            return null;
        }

        command = command.Split(':', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)[0];
        return command switch
        {
            "DANMU_MSG" => ParseDanmuMessage(root, rawPayload),
            "SUPER_CHAT_MESSAGE" => ParseSuperChat(root, rawPayload),
            _ => null
        };
    }

    private static ChatMessage? ParseDanmuMessage(JsonElement root, ReadOnlySpan<byte> rawPayload)
    {
        if (!root.TryGetProperty("info", out var info) || info.ValueKind != JsonValueKind.Array)
        {
            return null;
        }

        var text = GetArrayString(info, 1);
        var userInfo = GetArrayElement(info, 2);
        var uid = GetArrayInt64(userInfo, 0);
        var userName = GetArrayString(userInfo, 1);

        if (string.IsNullOrWhiteSpace(text))
        {
            return null;
        }

        var meta = GetArrayElement(info, 0);
        var timestamp = GetArrayString(meta, 4);
        var messageId = GetArrayString(meta, 5);
        var id = $"{timestamp}:{messageId}:{StableHash(rawPayload)}";
        return ChatMessage.CreateComment(userName, text, uid, id);
    }

    private static ChatMessage? ParseSuperChat(JsonElement root, ReadOnlySpan<byte> rawPayload)
    {
        if (!root.TryGetProperty("data", out var data) || data.ValueKind != JsonValueKind.Object)
        {
            return null;
        }

        var text = TryGetString(data, "message");
        var id = TryGetString(data, "id");
        var uid = TryGetInt64(data, "uid");
        var price = TryGetDecimal(data, "price");
        var userName = "";

        if (data.TryGetProperty("user_info", out var userInfo))
        {
            userName = TryGetString(userInfo, "uname");
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            return null;
        }

        id = string.IsNullOrWhiteSpace(id) ? StableHash(rawPayload) : id;
        return ChatMessage.CreateSuperChat(userName, text, price, uid, "sc:" + id);
    }

    private static JsonElement GetArrayElement(JsonElement element, int index)
    {
        if (element.ValueKind != JsonValueKind.Array || element.GetArrayLength() <= index)
        {
            return default;
        }

        return element[index];
    }

    private static string GetArrayString(JsonElement element, int index)
    {
        var item = GetArrayElement(element, index);
        return item.ValueKind switch
        {
            JsonValueKind.String => item.GetString() ?? "",
            JsonValueKind.Number => item.GetRawText(),
            _ => ""
        };
    }

    private static long? GetArrayInt64(JsonElement element, int index)
    {
        var item = GetArrayElement(element, index);
        return item.ValueKind switch
        {
            JsonValueKind.Number when item.TryGetInt64(out var value) => value,
            JsonValueKind.String when long.TryParse(item.GetString(), out var value) => value,
            _ => null
        };
    }

    private static string TryGetString(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var property))
        {
            return "";
        }

        return property.ValueKind switch
        {
            JsonValueKind.String => property.GetString() ?? "",
            JsonValueKind.Number => property.GetRawText(),
            _ => ""
        };
    }

    private static long? TryGetInt64(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var property))
        {
            return null;
        }

        return property.ValueKind switch
        {
            JsonValueKind.Number when property.TryGetInt64(out var value) => value,
            JsonValueKind.String when long.TryParse(property.GetString(), out var value) => value,
            _ => null
        };
    }

    private static decimal? TryGetDecimal(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var property))
        {
            return null;
        }

        return property.ValueKind switch
        {
            JsonValueKind.Number when property.TryGetDecimal(out var value) => value,
            JsonValueKind.String when decimal.TryParse(property.GetString(), out var value) => value,
            _ => null
        };
    }

    private static string StableHash(ReadOnlySpan<byte> payload)
    {
        var hash = SHA256.HashData(payload);
        return Convert.ToHexString(hash, 0, 8);
    }
}

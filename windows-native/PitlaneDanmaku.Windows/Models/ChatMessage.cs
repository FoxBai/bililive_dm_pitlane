namespace PitlaneDanmaku.Windows.Models;

public enum ChatMessageKind
{
    Comment,
    SuperChat
}

public sealed record ChatMessage(
    string Id,
    string UserName,
    string Text,
    ChatMessageKind Kind,
    DateTimeOffset ReceivedAt,
    long? UserId = null,
    decimal? Price = null)
{
    public bool IsSuperChat => Kind == ChatMessageKind.SuperChat;

    public static ChatMessage CreateComment(string userName, string text, long? userId = null, string? id = null)
    {
        return new ChatMessage(
            id ?? Guid.NewGuid().ToString("N"),
            userName,
            text,
            ChatMessageKind.Comment,
            DateTimeOffset.Now,
            userId);
    }

    public static ChatMessage CreateSuperChat(string userName, string text, decimal? price = null, long? userId = null, string? id = null)
    {
        return new ChatMessage(
            id ?? Guid.NewGuid().ToString("N"),
            userName,
            text,
            ChatMessageKind.SuperChat,
            DateTimeOffset.Now,
            userId,
            price);
    }
}

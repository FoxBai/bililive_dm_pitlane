using System.Collections.ObjectModel;

namespace PitlaneDanmaku.Windows.Services;

public sealed class LogService
{
    private readonly object _gate = new();
    private readonly List<string> _entries = [];

    public event Action<string>? EntryAdded;

    public IReadOnlyList<string> Snapshot
    {
        get
        {
            lock (_gate)
            {
                return new ReadOnlyCollection<string>(_entries.ToList());
            }
        }
    }

    public void Info(string message) => Add("INFO", message);

    public void Warn(string message) => Add("WARN", message);

    public void Error(string message) => Add("ERROR", message);

    public void Add(string level, string message)
    {
        var entry = $"[{DateTimeOffset.Now:HH:mm:ss}] {level,-5} {message}";
        lock (_gate)
        {
            _entries.Add(entry);
            if (_entries.Count > 1500)
            {
                _entries.RemoveRange(0, 300);
            }
        }

        EntryAdded?.Invoke(entry);
    }

    public void Clear()
    {
        lock (_gate)
        {
            _entries.Clear();
        }
    }
}

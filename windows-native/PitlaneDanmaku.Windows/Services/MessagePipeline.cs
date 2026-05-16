using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public sealed class MessagePipeline : IDisposable
{
    private readonly object _gate = new();
    private readonly Queue<ChatMessage> _normalQueue = new();
    private readonly Queue<ChatMessage> _priorityQueue = new();
    private readonly HashSet<string> _recentIds = [];
    private readonly Queue<string> _recentIdOrder = new();
    private readonly LogService _log;
    private AppSettings _settings;
    private CancellationTokenSource? _loopCancellation;
    private Task? _loopTask;

    public MessagePipeline(AppSettings settings, LogService log)
    {
        _settings = settings.Clone();
        _settings.Normalize();
        _log = log;
    }

    public event Action<ChatMessage>? MessageReady;

    public void UpdateSettings(AppSettings settings)
    {
        lock (_gate)
        {
            _settings = settings.Clone();
            _settings.Normalize();
        }
    }

    public void Start()
    {
        if (_loopTask is { IsCompleted: false })
        {
            return;
        }

        _loopCancellation = new CancellationTokenSource();
        _loopTask = Task.Run(() => ReleaseLoopAsync(_loopCancellation.Token));
    }

    public void Enqueue(ChatMessage message)
    {
        ChatMessage? normalized;
        AppSettings settings;

        lock (_gate)
        {
            settings = _settings.Clone();
        }

        normalized = TextSanitizer.Normalize(message, settings);
        if (normalized is null)
        {
            return;
        }

        lock (_gate)
        {
            if (!string.IsNullOrWhiteSpace(normalized.Id) && !_recentIds.Add(normalized.Id))
            {
                return;
            }

            RememberId(normalized.Id);

            // 队列满时优先丢普通评论，醒目留言尽量保留。
            while (_normalQueue.Count + _priorityQueue.Count >= _settings.QueueLimit)
            {
                if (_normalQueue.Count > 0)
                {
                    _normalQueue.Dequeue();
                    continue;
                }

                if (!normalized.IsSuperChat)
                {
                    return;
                }

                _priorityQueue.Dequeue();
            }

            if (normalized.IsSuperChat)
            {
                _priorityQueue.Enqueue(normalized);
            }
            else
            {
                _normalQueue.Enqueue(normalized);
            }
        }
    }

    public void Dispose()
    {
        _loopCancellation?.Cancel();
        _loopCancellation?.Dispose();
    }

    private async Task ReleaseLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            ChatMessage? message = null;
            int delay;

            lock (_gate)
            {
                delay = _settings.LaunchIntervalMs;
                if (_priorityQueue.Count > 0)
                {
                    message = _priorityQueue.Dequeue();
                }
                else if (_normalQueue.Count > 0)
                {
                    message = _normalQueue.Dequeue();
                }
            }

            if (message is not null)
            {
                MessageReady?.Invoke(message);
            }

            try
            {
                await Task.Delay(delay, cancellationToken);
            }
            catch (TaskCanceledException)
            {
                break;
            }
        }

        _log.Info("消息管线已停止。");
    }

    private void RememberId(string id)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            return;
        }

        _recentIdOrder.Enqueue(id);
        while (_recentIdOrder.Count > 500)
        {
            _recentIds.Remove(_recentIdOrder.Dequeue());
        }
    }
}

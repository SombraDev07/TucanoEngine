using System;
using System.Collections.Generic;

namespace EditorCore;

public sealed class EventBus
{
    private readonly Dictionary<Type, List<Delegate>> _subscribers = new();

    public void Subscribe<T>(Action<T> handler) where T : class
    {
        var type = typeof(T);
        if (!_subscribers.TryGetValue(type, out var list))
        {
            list = new List<Delegate>();
            _subscribers[type] = list;
        }
        list.Add(handler);
    }

    public void Unsubscribe<T>(Action<T> handler) where T : class
    {
        if (_subscribers.TryGetValue(typeof(T), out var list))
            list.Remove(handler);
    }

    public void Publish<T>(T message) where T : class
    {
        if (_subscribers.TryGetValue(typeof(T), out var list))
        {
            foreach (var handler in list)
                ((Action<T>)handler)(message);
        }
    }

    public void Clear()
    {
        _subscribers.Clear();
    }
}

public sealed class AssetSelectedEvent
{
    public Guid AssetId { get; init; }
    public string AssetPath { get; init; } = "";
}

public sealed class EntitySelectedEvent
{
    public Guid EntityId { get; init; }
    public string EntityName { get; init; } = "";
}

public sealed class PlayModeChangedEvent
{
    public bool IsPlaying { get; init; }
    public bool IsPaused { get; init; }
}

public sealed class ProjectLoadedEvent
{
    public string ProjectPath { get; init; } = "";
    public string ProjectName { get; init; } = "";
}

using System;
using System.Collections.Generic;

namespace EditorCore;

/// A single reversible editor action. Actions capture the state they need at construction time;
/// nothing here inspects the scene, so replaying is independent of what happened in between.
public sealed class UndoAction
{
    public string Name { get; }
    private readonly Action _undo;
    private readonly Action _redo;

    public UndoAction(string name, Action undo, Action redo)
    {
        Name = name;
        _undo = undo;
        _redo = redo;
    }

    public void Undo() => _undo();
    public void Redo() => _redo();
}

/// Undo/redo history with a bounded depth.
public sealed class UndoStack
{
    private readonly List<UndoAction> _undo = new();
    private readonly List<UndoAction> _redo = new();
    private readonly int _limit;

    public UndoStack(int limit = 100) => _limit = Math.Max(1, limit);

    public bool CanUndo => _undo.Count > 0;
    public bool CanRedo => _redo.Count > 0;
    public string? NextUndoName => CanUndo ? _undo[^1].Name : null;
    public string? NextRedoName => CanRedo ? _redo[^1].Name : null;

    /// Records an action that has already been performed.
    public void Push(UndoAction action)
    {
        _undo.Add(action);
        // A new edit invalidates the redo branch — the future it described no longer exists.
        _redo.Clear();
        if (_undo.Count > _limit) _undo.RemoveAt(0);
    }

    public string? Undo()
    {
        if (!CanUndo) return null;
        var a = _undo[^1];
        _undo.RemoveAt(_undo.Count - 1);
        a.Undo();
        _redo.Add(a);
        return a.Name;
    }

    public string? Redo()
    {
        if (!CanRedo) return null;
        var a = _redo[^1];
        _redo.RemoveAt(_redo.Count - 1);
        a.Redo();
        _undo.Add(a);
        return a.Name;
    }

    public void Clear()
    {
        _undo.Clear();
        _redo.Clear();
    }
}

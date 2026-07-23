using Avalonia.Controls;

using System;
using System.Collections.Generic;
using System.Linq;
using EditorCore;

namespace TucanoEditor.Extensibility;

/// A tool window (Environment, Animation, Profiler, …).
///
/// Panels register themselves and the Tools menu is generated from the registry, so adding a new
/// tool as the runtime grows means writing the panel — not editing the menu, the window, and the
/// open/close bookkeeping in three places.
public interface IEditorPanel
{
    /// Stable id, used to remember which panels were open.
    string Id { get; }

    /// Menu entry text.
    string Title { get; }

    /// Icon key from Styles/Icons.axaml.
    string IconKey { get; }

    /// Creates the window. The host owns it and tracks a single instance per id.
    Window CreateWindow(RuntimeHost runtime);

    /// Called when the scene changes underneath an open panel (load, new scene), so it can re-read
    /// runtime state instead of showing values from the old scene.
    void OnSceneChanged(Window window, RuntimeHost runtime) { }
}

/// Registry of panels and inspector sections.
///
/// Deliberately a plain list rather than reflection-based discovery: registration order is
/// explicit and debuggable, and a broken panel can't take the editor down at startup.
public sealed class EditorExtensions
{
    private readonly List<IEditorPanel> _panels = new();
    private readonly List<IInspectorSection> _sections = new();

    public IReadOnlyList<IEditorPanel> Panels => _panels;

    /// Sections in display order.
    public IReadOnlyList<IInspectorSection> Sections => _sections;

    public void AddPanel(IEditorPanel panel)
    {
        if (_panels.Any(p => p.Id == panel.Id))
        {
            throw new InvalidOperationException($"Duplicate editor panel id '{panel.Id}'");
        }
        _panels.Add(panel);
    }

    public void AddSection(IInspectorSection section)
    {
        _sections.Add(section);
        _sections.Sort((a, b) => a.Order.CompareTo(b.Order));
    }

    public IEditorPanel? FindPanel(string id) => _panels.FirstOrDefault(p => p.Id == id);
}

using Avalonia.Controls;

using System;
using System.Collections.Generic;
using System.Linq;
using EditorCore;

namespace TucanoEditor.Extensibility;

/// Context handed to every inspector section on each refresh.
/// Carries the runtime plus what is currently selected, so sections never reach into the window.
public sealed class InspectorContext
{
    public required RuntimeHost Runtime { get; init; }

    /// Selected mesh object index, or -1.
    public int ObjectIndex { get; init; } = -1;

    /// Selected light index, or -1. Only one of the two is ever set.
    public int LightIndex { get; init; } = -1;

    public bool HasObject => ObjectIndex >= 0;
    public bool HasLight => LightIndex >= 0;

    /// Raised by a section when it changes something the rest of the editor must re-read
    /// (a rename, a new component, a deletion).
    public Action? RequestOutlinerRefresh { get; init; }

    /// Status-line/console message.
    public Action<string>? Log { get; init; }
}

/// One collapsible block in the Inspector.
///
/// This is the seam that keeps the editor cheap to extend: as the runtime grows a new concept
/// (audio source, particle emitter, script), it gets a section here instead of another branch
/// inside the main window. The window only knows about the list.
public interface IInspectorSection
{
    /// Shown in the section header.
    string Title { get; }

    /// Icon key from Styles/Icons.axaml.
    string IconKey { get; }

    /// Lower numbers sort first. Transform is 0, so new sections default to sitting below it.
    int Order => 100;

    /// Whether this section applies to the current selection at all. Returning false hides it.
    bool AppliesTo(InspectorContext context);

    /// Builds the section body once. Called when the section first appears.
    Control CreateContent();

    /// Pushes the current selection's values into the controls. Called whenever the selection or
    /// the underlying data changes. Implementations must not write back to the runtime here.
    void Refresh(InspectorContext context);
}

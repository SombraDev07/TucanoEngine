using System.Linq;
using Avalonia.Controls;
using EditorCore;
using TucanoEditor.Models;
using TucanoEditor.Views;

namespace TucanoEditor.Extensibility;

/// Maps asset kinds to their in-editor document. Materials open as a docked panel hosted by
/// MainWindow — not a floating Window — so this registry only identifies the kind.
public static class AssetEditorRegistry
{
    public static bool CanEdit(AssetKind kind) => kind == AssetKind.Material;
}

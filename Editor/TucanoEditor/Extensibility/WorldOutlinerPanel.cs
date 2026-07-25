using Avalonia.Controls;
using EditorCore;
using TucanoEditor.Views;

namespace TucanoEditor.Extensibility;

public sealed class WorldOutlinerPanel : IEditorPanel
{
    public string Id => "world_outliner";
    public string Title => "World Outliner";
    public string IconKey => "icon_world";

    public Window CreateWindow(RuntimeHost runtime) => new WorldOutlinerWindow(runtime);
}

using Avalonia.Controls;
using EditorCore;
using TucanoEditor.Views;

namespace TucanoEditor.Extensibility;

public sealed class TerrainPanel : IEditorPanel
{
    public string Id => "terrain";
    public string Title => "Terrain";
    public string IconKey => "icon_terrain";

    public Window CreateWindow(RuntimeHost runtime) => new TerrainWindow(runtime);
}

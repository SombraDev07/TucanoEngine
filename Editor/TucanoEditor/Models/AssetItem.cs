using Avalonia.Media;
using EditorCore;

namespace TucanoEditor.Models;

public enum AssetKind
{
    Mesh,
    Material,
    Texture,
    Scene,
}

/// A tile in the content browser.
public sealed class AssetItem
{
    public string Name { get; set; } = "";
    public string Path { get; set; } = "";
    public AssetKind Kind { get; set; }

    /// Set for material assets so the browser can apply them without re-reading the file.
    public MaterialAsset? Material { get; set; }

    public string IconKey => Kind switch
    {
        AssetKind.Material => "IcoMaterial",
        AssetKind.Texture => "IcoTexture",
        AssetKind.Scene => "IcoScene",
        _ => "IcoMesh",
    };

    public IBrush IconBrush => Kind switch
    {
        AssetKind.Material => new SolidColorBrush(Color.Parse("#FFC107")),
        AssetKind.Texture => new SolidColorBrush(Color.Parse("#7ED6A5")),
        AssetKind.Scene => new SolidColorBrush(Color.Parse("#C79BF0")),
        _ => new SolidColorBrush(Color.Parse("#8FB8E8")),
    };
}

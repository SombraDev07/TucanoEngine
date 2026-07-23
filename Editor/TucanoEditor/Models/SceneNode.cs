using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using Avalonia.Media;

namespace TucanoEditor.Models;

public enum SceneNodeKind
{
    Root,       // "Scene"
    Folder,     // user-created group
    LightGroup, // the "Lights" bucket
    Mesh,
    Cube,
    Sphere,
    Plane,
    Light,
}

/// One row in the outliner tree. Holds the runtime index so selection maps straight back to the
/// engine; containers carry -1 because they aren't real scene entries.
/// Implements INotifyPropertyChanged so toggling the eye or renaming updates in place — rebuilding
/// the tree for those would collapse every expander the user opened.
public sealed class SceneNode : INotifyPropertyChanged
{
    private string _name = "";
    private string _detail = "";
    private bool _isVisible = true;

    public string Name
    {
        get => _name;
        set => Set(ref _name, value);
    }

    public string Detail
    {
        get => _detail;
        set => Set(ref _detail, value);
    }

    /// Viewport visibility for objects; for folders, the state applied to everything inside.
    public bool IsVisible
    {
        get => _isVisible;
        set
        {
            if (Set(ref _isVisible, value)) Raise(nameof(EyeIconKey));
        }
    }

    public SceneNodeKind Kind { get; set; }
    public int ObjectIndex { get; set; } = -1;
    public int LightIndex { get; set; } = -1;

    /// Folder path this node lives under ("" = scene root). For folders, their own path.
    public string FolderPath { get; set; } = "";

    public ObservableCollection<SceneNode> Children { get; } = new();

    public bool IsContainer => Kind is SceneNodeKind.Root or SceneNodeKind.Folder or SceneNodeKind.LightGroup;
    public bool IsFolder => Kind == SceneNodeKind.Folder;
    public bool IsObject => Kind is SceneNodeKind.Mesh or SceneNodeKind.Cube or SceneNodeKind.Sphere or SceneNodeKind.Plane;
    public bool CanToggleVisibility => IsObject || IsFolder;

    public string IconKey => Kind switch
    {
        SceneNodeKind.Root => "IcoScene",
        SceneNodeKind.Folder => "IcoFolderOpen",
        SceneNodeKind.LightGroup => "IcoFolderOpen",
        SceneNodeKind.Cube => "IcoCube",
        SceneNodeKind.Sphere => "IcoSphere",
        SceneNodeKind.Plane => "IcoPlane",
        SceneNodeKind.Light => "IcoLight",
        _ => "IcoMesh",
    };

    public string EyeIconKey => IsVisible ? "IcoEye" : "IcoEyeOff";

    public IBrush IconBrush => Kind switch
    {
        SceneNodeKind.Root or SceneNodeKind.Folder or SceneNodeKind.LightGroup =>
            new SolidColorBrush(Color.Parse("#E0A22C")),
        SceneNodeKind.Light => new SolidColorBrush(Color.Parse("#F0D67A")),
        _ => new SolidColorBrush(Color.Parse("#8FB8E8")),
    };

    public event PropertyChangedEventHandler? PropertyChanged;

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (Equals(field, value)) return false;
        field = value;
        Raise(name);
        return true;
    }

    private void Raise(string? name) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

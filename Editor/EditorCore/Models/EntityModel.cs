using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace EditorCore.Models;

public class EntityModel : INotifyPropertyChanged
{
    private Guid _id = Guid.NewGuid();
    private string _name = "Entity";
    private float _posX, _posY, _posZ;
    private float _rotX, _rotY, _rotZ;
    private float _scaleX = 1, _scaleY = 1, _scaleZ = 1;
    private EntityModel? _parent;
    private readonly ObservableCollection<EntityModel> _children = new();
    private readonly ObservableCollection<ComponentModel> _components = new();

    public Guid Id { get => _id; set => Set(ref _id, value); }
    public string Name { get => _name; set => Set(ref _name, value); }
    public float PosX { get => _posX; set => Set(ref _posX, value); }
    public float PosY { get => _posY; set => Set(ref _posY, value); }
    public float PosZ { get => _posZ; set => Set(ref _posZ, value); }
    public float RotX { get => _rotX; set => Set(ref _rotX, value); }
    public float RotY { get => _rotY; set => Set(ref _rotY, value); }
    public float RotZ { get => _rotZ; set => Set(ref _rotZ, value); }
    public float ScaleX { get => _scaleX; set => Set(ref _scaleX, value); }
    public float ScaleY { get => _scaleY; set => Set(ref _scaleY, value); }
    public float ScaleZ { get => _scaleZ; set => Set(ref _scaleZ, value); }
    public EntityModel? Parent { get => _parent; set => Set(ref _parent, value); }
    public ObservableCollection<EntityModel> Children => _children;
    public ObservableCollection<ComponentModel> Components => _components;

    public event PropertyChangedEventHandler? PropertyChanged;

    protected void Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public abstract class ComponentModel : INotifyPropertyChanged
{
    private string _name = "";
    public string Name { get => _name; set => Set(ref _name, value); }
    public abstract string ComponentType { get; }

    public event PropertyChangedEventHandler? PropertyChanged;

    protected void Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

public class TransformComponentModel : ComponentModel
{
    public override string ComponentType => "Transform";
    private float _posX, _posY, _posZ;
    private float _rotX, _rotY, _rotZ;
    private float _scaleX = 1, _scaleY = 1, _scaleZ = 1;

    public float PosX { get => _posX; set => Set(ref _posX, value); }
    public float PosY { get => _posY; set => Set(ref _posY, value); }
    public float PosZ { get => _posZ; set => Set(ref _posZ, value); }
    public float RotX { get => _rotX; set => Set(ref _rotX, value); }
    public float RotY { get => _rotY; set => Set(ref _rotY, value); }
    public float RotZ { get => _rotZ; set => Set(ref _rotZ, value); }
    public float ScaleX { get => _scaleX; set => Set(ref _scaleX, value); }
    public float ScaleY { get => _scaleY; set => Set(ref _scaleY, value); }
    public float ScaleZ { get => _scaleZ; set => Set(ref _scaleZ, value); }
}

public class MeshRendererComponentModel : ComponentModel
{
    public override string ComponentType => "MeshRenderer";
    private string _meshPath = "";
    private bool _castShadows = true;
    private bool _visible = true;

    public string MeshPath { get => _meshPath; set => Set(ref _meshPath, value); }
    public bool CastShadows { get => _castShadows; set => Set(ref _castShadows, value); }
    public bool Visible { get => _visible; set => Set(ref _visible, value); }
}

public class RigidBodyComponentModel : ComponentModel
{
    public override string ComponentType => "RigidBody";
    private float _mass = 1f;
    private bool _kinematic;
    private bool _useGravity = true;

    public float Mass { get => _mass; set => Set(ref _mass, value); }
    public bool Kinematic { get => _kinematic; set => Set(ref _kinematic, value); }
    public bool UseGravity { get => _useGravity; set => Set(ref _useGravity, value); }
}

public class AssetModel : INotifyPropertyChanged
{
    private Guid _id = Guid.NewGuid();
    private string _name = "";
    private string _path = "";
    private string _type = "";

    public Guid Id { get => _id; set => Set(ref _id, value); }
    public string Name { get => _name; set => Set(ref _name, value); }
    public string Path { get => _path; set => Set(ref _path, value); }
    public string Type { get => _type; set => Set(ref _type, value); }

    public event PropertyChangedEventHandler? PropertyChanged;

    protected void Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

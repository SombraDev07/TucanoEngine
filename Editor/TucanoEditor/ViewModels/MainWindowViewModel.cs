using System.Collections.ObjectModel;
using ReactiveUI;
using EditorCore.Models;

namespace TucanoEditor.ViewModels;

public class MainWindowViewModel : ReactiveObject
{
    private string _title = "Tucano Editor";
    private string _statusText = "Ready";
    private string _fpsText = "0 FPS";
    private float _timeOfDay = 0.42f;
    private bool _enableBloom = true;
    private bool _enableAO = true;
    private bool _enableClouds = true;
    private bool _enableAtmosphere = true;
    private EntityModel? _selectedEntity;
    private readonly ObservableCollection<EntityModel> _entities = new();

    public string Title { get => _title; set => this.RaiseAndSetIfChanged(ref _title, value); }
    public string StatusText { get => _statusText; set => this.RaiseAndSetIfChanged(ref _statusText, value); }
    public string FpsText { get => _fpsText; set => this.RaiseAndSetIfChanged(ref _fpsText, value); }
    public float TimeOfDay { get => _timeOfDay; set => this.RaiseAndSetIfChanged(ref _timeOfDay, value); }
    public bool EnableBloom { get => _enableBloom; set => this.RaiseAndSetIfChanged(ref _enableBloom, value); }
    public bool EnableAO { get => _enableAO; set => this.RaiseAndSetIfChanged(ref _enableAO, value); }
    public bool EnableClouds { get => _enableClouds; set => this.RaiseAndSetIfChanged(ref _enableClouds, value); }
    public bool EnableAtmosphere { get => _enableAtmosphere; set => this.RaiseAndSetIfChanged(ref _enableAtmosphere, value); }
    public EntityModel? SelectedEntity { get => _selectedEntity; set => this.RaiseAndSetIfChanged(ref _selectedEntity, value); }
    public ObservableCollection<EntityModel> Entities => _entities;
}

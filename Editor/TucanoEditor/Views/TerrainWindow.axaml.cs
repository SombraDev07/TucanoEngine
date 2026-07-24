using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using EditorCore;
using EditorCore.Interop;

namespace TucanoEditor.Views;

public partial class TerrainWindow : Window
{
    private RuntimeHost? _runtime;

    public TerrainWindow() => InitializeComponent();

    public TerrainWindow(RuntimeHost runtime) : this()
    {
        _runtime = runtime;
        RefreshInfo();
    }

    private void RefreshInfo()
    {
        if (_runtime?.NativePtr is null) return;
        if (TucanoApi.tucano_terrain_get_info(_runtime.NativePtr, out var info))
        {
            TerrainInfo.Text = $"Res: {info.Resolution} | " +
                $"Size: {info.WorldSize:F0}m | " +
                $"H: {info.MinHeight:F0}~{info.MaxHeight:F0}";
            UndoCount.Text = $"{info.UndoCount}/{info.RedoCount}";
        }
        else
        {
            TerrainInfo.Text = "No terrain loaded";
            UndoCount.Text = "0/0";
        }
    }

    private void OnGenerate(object? sender, RoutedEventArgs e)
    {
        if (_runtime?.NativePtr is null) return;
        if (!uint.TryParse(GenRes.Text, out var res)) res = 512;
        if (!float.TryParse(GenSize.Text, out var size)) size = 1024;

        TucanoApi.tucano_terrain_create_procedural(_runtime.NativePtr,
            res, size, 6, 0.5f, 128f, 0f, (uint)Environment.TickCount);
        RefreshInfo();
    }

    private int BrushToolIndex => BrushTool.SelectedIndex;

    private void OnBrushSlider(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        RadiusValue.Text = $"{(int)BrushRadius.Value}";
        StrengthValue.Text = $"{(int)BrushStrength.Value}";
        SyncSculptParams();
    }

    private void OnBrushChanged(object? sender, SelectionChangedEventArgs e)
    {
        SyncSculptParams();
    }

    private void SyncSculptParams()
    {
        _runtime?.TerrainSetSculptParams((float)BrushRadius.Value, (float)BrushStrength.Value, BrushToolIndex);
    }

    private void OnUndo(object? sender, RoutedEventArgs e)
    {
        if (_runtime?.NativePtr is null) return;
        TucanoApi.tucano_terrain_undo(_runtime.NativePtr);
        RefreshInfo();
    }

    private void OnRedo(object? sender, RoutedEventArgs e)
    {
        if (_runtime?.NativePtr is null) return;
        TucanoApi.tucano_terrain_redo(_runtime.NativePtr);
        RefreshInfo();
    }

    private void OnErode(object? sender, RoutedEventArgs e)
    {
        if (_runtime?.NativePtr is null) return;
        TucanoApi.tucano_terrain_erode(_runtime.NativePtr, 50000, 0.3f);
        RefreshInfo();
    }

    private void OnThermalErode(object? sender, RoutedEventArgs e)
    {
        if (_runtime?.NativePtr is null) return;
        TucanoApi.tucano_terrain_thermal_erode(_runtime.NativePtr, 35f, 100);
        RefreshInfo();
    }

    private async void OnExport(object? sender, RoutedEventArgs e)
    {
        if (_runtime?.NativePtr is null) return;

        var top = GetTopLevel(this);
        if (top is null) return;

        var file = await top.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Export heightmap",
            DefaultExtension = ".htmap",
            FileTypeChoices = new[] { new FilePickerFileType("Heightmap") { Patterns = new[] { "*.htmap" } } }
        });

        if (file is not null)
        {
            TucanoApi.tucano_terrain_export(_runtime.NativePtr, file.Path.LocalPath);
        }
    }
}

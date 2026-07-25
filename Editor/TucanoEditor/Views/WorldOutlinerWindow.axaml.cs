using System;
using System.Collections.ObjectModel;
using Avalonia.Controls;
using Avalonia.Interactivity;
using EditorCore;
using EditorCore.Interop;

namespace TucanoEditor.Views;

public partial class WorldOutlinerWindow : Window
{
    private RuntimeHost? _runtime;
    private readonly ObservableCollection<CellItem> _cells = new();

    public class CellItem
    {
        public string Label { get; set; } = "";
    }

    public WorldOutlinerWindow() => InitializeComponent();

    public WorldOutlinerWindow(RuntimeHost runtime) : this()
    {
        _runtime = runtime;
        CellList.ItemsSource = _cells;
    }

    private void SafeRefresh()
    {
        try
        {
            if (_runtime?.IsAlive != true) return;

            if (_runtime.WorldStreamActive)
            {
                var stats = _runtime.WorldStreamGetStats();
                StatsText.Text = $"Resident: {stats.CellsResident} | " +
                    $"Loading: {stats.CellsLoading} | " +
                    $"Layers: {stats.LayersLoaded}\n" +
                    $"Objects: {stats.LiveObjects} | Bodies: {stats.LiveBodies}\n" +
                    $"CPU: {stats.CpuBytes / 1024}KB | GPU: {stats.GpuBytes / 1024}KB\n" +
                    $"Completed: {stats.LoadsCompleted} | Unloaded: {stats.UnloadsIssued}";

                var cells = _runtime.WorldStreamResidentCells(256);
                _cells.Clear();
                foreach (var c in cells)
                {
                    string layers = "";
                    if ((c.LayerMask & 1) != 0) layers += "G";
                    if ((c.LayerMask & 2) != 0) layers += "V";
                    if ((c.LayerMask & 4) != 0) layers += "A";
                    if ((c.LayerMask & 8) != 0) layers += "D";

                    string loading = "";
                    if ((c.LoadingMask & 1) != 0) loading += "G";
                    if ((c.LoadingMask & 2) != 0) loading += "V";
                    if ((c.LoadingMask & 4) != 0) loading += "A";
                    if ((c.LoadingMask & 8) != 0) loading += "D";

                    string status = loading.Length > 0 ? $" [{loading}]" : "";
                    _cells.Add(new CellItem
                    {
                        Label = $"({c.X},{c.Y},{c.Z}) Lv{c.Level} | " +
                                $"LOD:{c.Lod} | {c.Distance:F0}m | {layers}{status}"
                    });
                }
                CellCount.Text = $"{cells.Length} cells";
            }
            else
            {
                StatsText.Text = "Streaming inactive — click Start";
                _cells.Clear();
                CellCount.Text = "0 cells";
            }
        }
        catch (Exception ex)
        {
            StatsText.Text = $"Error: {ex.Message}";
        }
    }

    private void OnStartStream(object? sender, RoutedEventArgs e)
    {
        try
        {
            if (_runtime is null) return;
            int.TryParse(ExtentBox.Text, out var ext);
            float.TryParse(RadiusBox.Text, out var rad);
            bool ok = _runtime.WorldStreamStart(ext > 0 ? ext : 6, rad > 0 ? rad : 200f, 8);
            if (ok)
            {
                StartBtn.IsEnabled = false;
                StopBtn.IsEnabled = true;
                StatsText.Text = "Streaming started. Click Refresh.";
                SafeRefresh();
            }
            else
            {
                StatsText.Text = "FAILED: Could not start streaming.\nCheck if Assets/editor_stream_world exists.";
            }
        }
        catch (Exception ex)
        {
            StatsText.Text = $"ERROR: {ex.Message}";
        }
    }

    private void OnStopStream(object? sender, RoutedEventArgs e)
    {
        try
        {
            _runtime?.WorldStreamStop();
            StartBtn.IsEnabled = true;
            StopBtn.IsEnabled = false;
            SafeRefresh();
        }
        catch (Exception ex)
        {
            StatsText.Text = $"Stop failed: {ex.Message}";
        }
    }

    private void OnRefresh(object? sender, RoutedEventArgs e) => SafeRefresh();

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
    }
}

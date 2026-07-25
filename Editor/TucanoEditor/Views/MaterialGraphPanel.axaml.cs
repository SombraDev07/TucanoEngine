using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using EditorCore;
using TucanoEditor.Models;
using IoPath = System.IO.Path;
using ShapePath = Avalonia.Controls.Shapes.Path;

namespace TucanoEditor.Views;

public sealed class TextureFolderNode
{
    public string Name { get; init; } = "";
    /// Absolute folder path, or empty for "All".
    public string FolderKey { get; init; } = "";
    public string CountLabel { get; set; } = "";
    public ObservableCollection<TextureFolderNode> Children { get; } = new();
}

public sealed class TextureListItem
{
    public string Name { get; init; } = "";
    public string Path { get; init; } = "";
    public string FolderKey { get; init; } = "";
    public string FolderLabel { get; init; } = "";
    public Bitmap? Thumbnail { get; init; }
}

public partial class MaterialGraphPanel : UserControl
{
    private MaterialGraph _graph = MaterialGraph.CreateDefault();
    private MaterialAsset? _asset;
    private Action<MaterialAsset>? _onApplied;
    private Action? _onClosed;

    private MaterialNode? _dragNode;
    private MaterialNode? _selected;
    private Point _dragWorldDelta;
    private bool _isConnecting;
    private string? _connFromNode, _connFromPin;
    private Point _connScreen;
    private int _nextNodeX = 200, _nextNodeY = 100;

    // Infinite canvas view
    private double _panX, _panY;
    private double _zoom = 1.0;
    private bool _isPanning;
    private Point _panStart;
    private double _panOriginX, _panOriginY;
    private bool _spaceDown;

    private readonly Dictionary<string, Border> _nodeCards = new();
    private readonly Dictionary<string, Bitmap?> _thumbCache = new();
    private readonly ObservableCollection<TextureListItem> _allTextures = new();
    private readonly ObservableCollection<TextureListItem> _visibleTextures = new();
    private readonly ObservableCollection<TextureFolderNode> _folderRoots = new();
    private string? _assetsRoot;
    private string _activeFolderKey = "";

    private readonly Stack<string> _undo = new();
    private readonly Stack<string> _redo = new();
    private bool _nodeDragMoved;

    private TextureListItem? _pendingDragItem;
    private Point _pendingDragStart;
    private bool _dragStarted;
    private const double DragThreshold = 6;

    private const double NodeW = 180;
    private const double RowH = 22;
    private const double HdrH = 26;
    private const double PinR = 6.5;
    private const double PreviewH = 56;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 2.5;

    private static readonly IBrush GridMajor = new SolidColorBrush(Color.Parse("#1C1C24"));
    private static readonly IBrush GridMinor = new SolidColorBrush(Color.Parse("#16161C"));
    private static readonly IBrush ConnectLine = new SolidColorBrush(Color.Parse("#CCAA30"));
    private static readonly IBrush SelectBorder = new SolidColorBrush(Color.Parse("#FFC107"));

    private static readonly Dictionary<string, (IBrush header, IBrush title)> CatColors = new()
    {
        ["Constant1"] = (new SolidColorBrush(Color.Parse("#2D4A2D")), new SolidColorBrush(Color.Parse("#6EBD80"))),
        ["Constant3"] = (new SolidColorBrush(Color.Parse("#2D4A2D")), new SolidColorBrush(Color.Parse("#6EBD80"))),
        ["Constant4"] = (new SolidColorBrush(Color.Parse("#2D4A2D")), new SolidColorBrush(Color.Parse("#6EBD80"))),
        ["Multiply"] = (new SolidColorBrush(Color.Parse("#243654")), new SolidColorBrush(Color.Parse("#6EA0D4"))),
        ["Add"] = (new SolidColorBrush(Color.Parse("#243654")), new SolidColorBrush(Color.Parse("#6EA0D4"))),
        ["Lerp"] = (new SolidColorBrush(Color.Parse("#243654")), new SolidColorBrush(Color.Parse("#6EA0D4"))),
        ["TextureSample"] = (new SolidColorBrush(Color.Parse("#44282A")), new SolidColorBrush(Color.Parse("#D4706E"))),
        ["MaterialOutput"] = (new SolidColorBrush(Color.Parse("#3A244E")), new SolidColorBrush(Color.Parse("#B892D4"))),
    };

    private static IBrush PinBrush(MaterialPinType t) => t switch
    {
        MaterialPinType.Float => new SolidColorBrush(Color.Parse("#5EC75E")),
        MaterialPinType.Float2 => new SolidColorBrush(Color.Parse("#5EC7C7")),
        MaterialPinType.Float3 => new SolidColorBrush(Color.Parse("#C7C75E")),
        MaterialPinType.Float4 => new SolidColorBrush(Color.Parse("#C0C0C8")),
        MaterialPinType.Sampler => new SolidColorBrush(Color.Parse("#C75EC7")),
        _ => Brushes.Gray,
    };

    public MaterialGraphPanel()
    {
        InitializeComponent();
        TextureGrid.ItemsSource = _visibleTextures;
        TextureFolderTree.ItemsSource = _folderRoots;
        Focusable = true;
        KeyDown += OnKeyDown;
        KeyUp += OnKeyUp;
        AttachedToVisualTree += (_, _) => { if (IsVisible) RenderGraph(); };
    }

    public bool IsEditing => IsVisible && _asset is not null;

    public bool Undo()
    {
        if (_undo.Count == 0) return false;
        _redo.Push(_graph.CreateSnapshot());
        _graph.RestoreSnapshot(_undo.Pop());
        _selected = null;
        ClearDetails();
        UpdatePreview();
        RenderGraph();
        CompileStatus.Text = "↩ Undo";
        CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#AAAAB4"));
        return true;
    }

    public bool Redo()
    {
        if (_redo.Count == 0) return false;
        _undo.Push(_graph.CreateSnapshot());
        _graph.RestoreSnapshot(_redo.Pop());
        _selected = null;
        ClearDetails();
        UpdatePreview();
        RenderGraph();
        CompileStatus.Text = "↪ Redo";
        CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#AAAAB4"));
        return true;
    }

    private void PushUndo()
    {
        _undo.Push(_graph.CreateSnapshot());
        if (_undo.Count > 80) // cap memory
        {
            var arr = _undo.Reverse().Take(60).Reverse().ToArray();
            _undo.Clear();
            foreach (var s in arr) _undo.Push(s);
        }
        _redo.Clear();
    }

    public void Open(MaterialAsset asset, IEnumerable<string>? texturePaths = null,
        Action<MaterialAsset>? onApplied = null, Action? onClosed = null,
        string? assetsRoot = null)
    {
        _asset = asset;
        _onApplied = onApplied;
        _onClosed = onClosed;
        _assetsRoot = assetsRoot;
        _graph = MaterialGraph.LoadOrCreate(asset);
        _selected = null;
        _undo.Clear();
        _redo.Clear();
        TitleLabel.Text = $"MATERIAL EDITOR — {asset.Name}";
        LoadTextures(texturePaths);
        IsVisible = true;
        ClearDetails();
        UpdatePreview();
        FitView();
        Focus();
    }

    public void CloseEditor()
    {
        IsVisible = false;
        _asset = null;
        _selected = null;
        _onClosed?.Invoke();
    }

    private void OnClose(object? sender, RoutedEventArgs e) => CloseEditor();

    private void LoadTextures(IEnumerable<string>? paths)
    {
        _allTextures.Clear();
        _visibleTextures.Clear();
        _folderRoots.Clear();
        if (paths is null) return;

        var root = _assetsRoot;
        foreach (var path in paths.OrderBy(p => p, StringComparer.OrdinalIgnoreCase))
        {
            if (!File.Exists(path)) continue;
            var ext = IoPath.GetExtension(path).ToLowerInvariant();
            if (ext is not (".png" or ".jpg" or ".jpeg" or ".tga" or ".bmp" or ".webp")) continue;

            var dir = IoPath.GetDirectoryName(path) ?? "";
            var folderKey = dir;
            var folderLabel = dir;
            if (!string.IsNullOrEmpty(root) && dir.StartsWith(root, StringComparison.OrdinalIgnoreCase))
            {
                folderLabel = dir[root.Length..].TrimStart('\\', '/');
                if (string.IsNullOrEmpty(folderLabel)) folderLabel = "(Assets root)";
            }
            else
            {
                folderLabel = IoPath.GetFileName(dir);
                if (string.IsNullOrEmpty(folderLabel)) folderLabel = dir;
            }

            _allTextures.Add(new TextureListItem
            {
                Name = IoPath.GetFileName(path),
                Path = path,
                FolderKey = folderKey,
                FolderLabel = folderLabel.Replace('\\', '/'),
                Thumbnail = TryLoadThumb(path, 128),
            });
        }

        BuildFolderTree(root);
        _activeFolderKey = "";
        ApplyTextureFolderFilter("");
        if (_folderRoots.Count > 0)
            TextureFolderTree.SelectedItem = _folderRoots[0];
    }

    private void BuildFolderTree(string? assetsRoot)
    {
        var all = new TextureFolderNode
        {
            Name = "All",
            FolderKey = "",
            CountLabel = $"({_allTextures.Count})",
        };
        _folderRoots.Add(all);

        // Map absolute dir → node
        var nodes = new Dictionary<string, TextureFolderNode>(StringComparer.OrdinalIgnoreCase);

        foreach (var dir in _allTextures.Select(t => t.FolderKey).Distinct(StringComparer.OrdinalIgnoreCase)
                     .OrderBy(d => d, StringComparer.OrdinalIgnoreCase))
        {
            if (string.IsNullOrEmpty(dir)) continue;

            string relative;
            if (!string.IsNullOrEmpty(assetsRoot) && dir.StartsWith(assetsRoot, StringComparison.OrdinalIgnoreCase))
                relative = dir[assetsRoot.Length..].TrimStart('\\', '/');
            else
                relative = IoPath.GetFileName(dir);

            relative = relative.Replace('\\', '/');
            if (string.IsNullOrEmpty(relative)) relative = IoPath.GetFileName(dir);

            var parts = relative.Split('/', StringSplitOptions.RemoveEmptyEntries);
            TextureFolderNode parent = all;
            var builtAbs = !string.IsNullOrEmpty(assetsRoot) ? assetsRoot.TrimEnd('\\', '/') : "";

            for (int i = 0; i < parts.Length; i++)
            {
                builtAbs = string.IsNullOrEmpty(builtAbs)
                    ? parts[i]
                    : builtAbs + "/" + parts[i];
                // Prefer matching real absolute path for leaf
                var absKey = i == parts.Length - 1 ? dir : FindChildDir(assetsRoot, parts.Take(i + 1));

                if (!nodes.TryGetValue(absKey, out var child))
                {
                    child = new TextureFolderNode
                    {
                        Name = parts[i],
                        FolderKey = absKey,
                    };
                    nodes[absKey] = child;
                    parent.Children.Add(child);
                }
                parent = child;
            }
        }

        // Counts: textures in this folder or any descendant
        void SetCounts(TextureFolderNode n)
        {
            foreach (var c in n.Children) SetCounts(c);
            int count = string.IsNullOrEmpty(n.FolderKey)
                ? _allTextures.Count
                : _allTextures.Count(t =>
                    t.FolderKey.Equals(n.FolderKey, StringComparison.OrdinalIgnoreCase) ||
                    t.FolderKey.StartsWith(n.FolderKey.TrimEnd('\\', '/') + "\\", StringComparison.OrdinalIgnoreCase) ||
                    t.FolderKey.StartsWith(n.FolderKey.TrimEnd('\\', '/') + "/", StringComparison.OrdinalIgnoreCase));
            n.CountLabel = $"({count})";
        }
        SetCounts(all);
    }

    private static string FindChildDir(string? assetsRoot, IEnumerable<string> parts)
    {
        if (string.IsNullOrEmpty(assetsRoot)) return string.Join("/", parts);
        return IoPath.Combine(new[] { assetsRoot }.Concat(parts).ToArray());
    }

    private void OnTextureFolderTreeChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (TextureFolderTree.SelectedItem is TextureFolderNode folder)
        {
            _activeFolderKey = folder.FolderKey;
            ApplyTextureFolderFilter(folder.FolderKey);
        }
    }

    private void ApplyTextureFolderFilter(string folderKey)
    {
        _visibleTextures.Clear();
        IEnumerable<TextureListItem> src;
        if (string.IsNullOrEmpty(folderKey))
        {
            src = _allTextures;
        }
        else
        {
            var prefix = folderKey.TrimEnd('\\', '/');
            src = _allTextures.Where(t =>
                t.FolderKey.Equals(folderKey, StringComparison.OrdinalIgnoreCase) ||
                t.FolderKey.StartsWith(prefix + "\\", StringComparison.OrdinalIgnoreCase) ||
                t.FolderKey.StartsWith(prefix + "/", StringComparison.OrdinalIgnoreCase));
        }
        foreach (var t in src.OrderBy(t => t.Name, StringComparer.OrdinalIgnoreCase))
            _visibleTextures.Add(t);
    }

    private Bitmap? TryLoadThumb(string path, int size)
    {
        if (_thumbCache.TryGetValue(path, out var cached)) return cached;
        try
        {
            using var stream = File.OpenRead(path);
            var bmp = new Bitmap(stream);
            var scaled = bmp.CreateScaledBitmap(new PixelSize(size, size));
            if (!ReferenceEquals(scaled, bmp)) bmp.Dispose();
            _thumbCache[path] = scaled;
            return scaled;
        }
        catch
        {
            _thumbCache[path] = null;
            return null;
        }
    }

    private Point ToScreen(double wx, double wy) => new(wx * _zoom + _panX, wy * _zoom + _panY);
    private Point ToWorld(Point screen) => new((screen.X - _panX) / _zoom, (screen.Y - _panY) / _zoom);

    // ── Render ──────────────────────────────────────────

    private void RenderGraph()
    {
        if (GraphPanel is null) return;
        GraphPanel.Children.Clear();
        _nodeCards.Clear();

        DrawGrid();
        DrawConnections();
        if (_isConnecting && _connFromNode is not null)
            DrawTempConnection();

        foreach (var node in _graph.Nodes)
            RenderNode(node);

        StatsText.Text = $"Nodes: {_graph.Nodes.Count}  ·  Links: {_graph.Connections.Count}";
        ZoomText.Text = $"Zoom {(int)(_zoom * 100)}%";
    }

    private void DrawGrid()
    {
        double w = Math.Max(GraphPanel.Bounds.Width, 1);
        double h = Math.Max(GraphPanel.Bounds.Height, 1);
        if (w < 2 || h < 2) { w = 1600; h = 900; }

        double minor = 24 * _zoom;
        double major = 120 * _zoom;
        if (minor < 6) minor = 6;

        double startX = _panX % minor;
        if (startX > 0) startX -= minor;
        for (double x = startX; x < w; x += minor)
        {
            bool isMajor = Math.Abs((x - _panX) % major) < 0.5 || Math.Abs((x - _panX) % major) > major - 0.5;
            GraphPanel.Children.Add(new Line
            {
                StartPoint = new Point(x, 0), EndPoint = new Point(x, h),
                Stroke = isMajor ? GridMajor : GridMinor,
                StrokeThickness = isMajor ? 1 : 0.5,
                IsHitTestVisible = false, ZIndex = -1
            });
        }

        double startY = _panY % minor;
        if (startY > 0) startY -= minor;
        for (double y = startY; y < h; y += minor)
        {
            bool isMajor = Math.Abs((y - _panY) % major) < 0.5 || Math.Abs((y - _panY) % major) > major - 0.5;
            GraphPanel.Children.Add(new Line
            {
                StartPoint = new Point(0, y), EndPoint = new Point(w, y),
                Stroke = isMajor ? GridMajor : GridMinor,
                StrokeThickness = isMajor ? 1 : 0.5,
                IsHitTestVisible = false, ZIndex = -1
            });
        }
    }

    private void DrawConnections()
    {
        foreach (var c in _graph.Connections)
        {
            var from = _graph.FindNode(c.FromNodeId);
            var to = _graph.FindNode(c.ToNodeId);
            if (from is null || to is null) continue;

            int fi = from.OutputPins.FindIndex(p => p.Id == c.FromPinId);
            int ti = to.InputPins.FindIndex(p => p.Id == c.ToPinId);
            if (fi < 0 || ti < 0) continue;

            var p1 = PinScreen(from, fi, output: true);
            var p2 = PinScreen(to, ti, output: false);
            GraphPanel.Children.Add(MakeBezier(p1, p2, ConnectLine, 2.2 * _zoom));
        }
    }

    private void DrawTempConnection()
    {
        var from = _graph.FindNode(_connFromNode!);
        if (from is null || _connFromPin is null) return;
        int fi = from.OutputPins.FindIndex(p => p.Id == _connFromPin);
        if (fi < 0) return;
        var p1 = PinScreen(from, fi, output: true);
        GraphPanel.Children.Add(MakeBezier(p1, _connScreen,
            new SolidColorBrush(Color.Parse("#FFE066")), 2 * _zoom));
    }

    private Point PinScreen(MaterialNode node, int pinIdx, bool output)
    {
        double localY = HdrH + PreviewOffset(node) + 2 + pinIdx * RowH + RowH / 2;
        // Hang slightly outside the card so bolinhas stay visible and wires land cleanly.
        double localX = output ? NodeW + 2 : -2;
        return ToScreen(node.X + localX, node.Y + localY);
    }

    private static double PreviewOffset(MaterialNode node) =>
        HasPreview(node) ? PreviewH + 4 : 0;

    private static bool HasPreview(MaterialNode node) =>
        node.Type is "Constant1" or "Constant3" or "Constant4" or "TextureSample";

    private static ShapePath MakeBezier(Point a, Point b, IBrush stroke, double thickness)
    {
        var geo = new StreamGeometry();
        using var ctx = geo.Open();
        ctx.BeginFigure(a, false);
        double cx = Math.Max(30, Math.Abs(b.X - a.X) * 0.45);
        ctx.CubicBezierTo(new Point(a.X + cx, a.Y), new Point(b.X - cx, b.Y), b);
        ctx.EndFigure(false);
        return new ShapePath
        {
            Data = geo, Stroke = stroke, StrokeThickness = thickness,
            ZIndex = 0, IsHitTestVisible = false
        };
    }

    private void RenderNode(MaterialNode node)
    {
        int rows = Math.Max(Math.Max(node.InputPins.Count, node.OutputPins.Count), 1);
        bool preview = HasPreview(node);
        double bodyH = HdrH + (preview ? PreviewH + 4 : 0) + rows * RowH + 4;
        double scale = _zoom;
        var (hdrBrush, titleBrush) = CatColors.GetValueOrDefault(node.Type,
            (new SolidColorBrush(Color.Parse("#2A2A35")), new SolidColorBrush(Color.Parse("#AAAAB4"))));

        bool selected = _selected?.Id == node.Id;
        var card = new Border
        {
            Width = NodeW * scale,
            Height = bodyH * scale,
            Background = new SolidColorBrush(Color.Parse("#1E1E28")),
            BorderBrush = selected ? SelectBorder
                : node.Type == "MaterialOutput"
                    ? new SolidColorBrush(Color.Parse("#7A5CA0"))
                    : new SolidColorBrush(Color.Parse("#333340")),
            BorderThickness = new Thickness(selected || node.Type == "MaterialOutput" ? 2 : 1),
            CornerRadius = new CornerRadius(6),
            ZIndex = 10,
            Cursor = new Cursor(StandardCursorType.Hand),
            ClipToBounds = false, // pins hang outside the card
        };
        var screen = ToScreen(node.X, node.Y);
        Canvas.SetLeft(card, screen.X);
        Canvas.SetTop(card, screen.Y);

        // Build unscaled content, then scale the whole card via LayoutTransform
        var inner = new Border
        {
            Width = NodeW,
            Height = bodyH,
            Background = new SolidColorBrush(Color.Parse("#1E1E28")),
            RenderTransformOrigin = new RelativePoint(0, 0, RelativeUnit.Relative),
            RenderTransform = new ScaleTransform(scale, scale),
        };

        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition(HdrH, GridUnitType.Pixel));
        if (preview) grid.RowDefinitions.Add(new RowDefinition(PreviewH + 4, GridUnitType.Pixel));
        grid.RowDefinitions.Add(new RowDefinition(1, GridUnitType.Star));

        var hdr = new Border
        {
            Background = hdrBrush,
            CornerRadius = new CornerRadius(5, 5, 0, 0),
            Child = new TextBlock
            {
                Text = node.Name, FontSize = 11, FontWeight = FontWeight.SemiBold,
                Foreground = titleBrush, Margin = new Thickness(8, 3),
                VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center
            }
        };
        grid.Children.Add(hdr);
        Grid.SetRow(hdr, 0);

        int rowIdx = 1;
        if (preview)
        {
            var previewHost = BuildNodePreview(node);
            grid.Children.Add(previewHost);
            Grid.SetRow(previewHost, rowIdx++);
        }

        var body = new StackPanel { Margin = new Thickness(8, 2, 8, 2), Spacing = 0 };
        for (int i = 0; i < rows; i++)
        {
            var row = new Grid { ColumnDefinitions = new("*,Auto"), Height = RowH };

            // Labels only inside the card — bolinhas are drawn on the canvas so they never clip.
            if (i < node.InputPins.Count)
            {
                var pin = node.InputPins[i];
                var label = new TextBlock
                {
                    Text = "●  " + pin.Name, FontSize = 10,
                    Foreground = PinBrush(pin.Type),
                    VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
                    FontWeight = node.Type == "MaterialOutput" ? FontWeight.SemiBold : FontWeight.Normal,
                };
                row.Children.Add(label);
                Grid.SetColumn(label, 0);
            }

            if (i < node.OutputPins.Count)
            {
                var pin = node.OutputPins[i];
                var label = new TextBlock
                {
                    Text = pin.Name + "  ●", FontSize = 10,
                    Foreground = PinBrush(pin.Type),
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                    VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
                };
                row.Children.Add(label);
                Grid.SetColumn(label, 1);
            }

            body.Children.Add(row);
        }
        grid.Children.Add(body);
        Grid.SetRow(body, rowIdx);
        inner.Child = grid;
        card.Child = inner;

        card.PointerPressed += (_, e) =>
        {
            var pt = e.GetCurrentPoint(card);
            if (pt.Properties.IsLeftButtonPressed && !_spaceDown)
            {
                SelectNode(node);
                PushUndo();
                _nodeDragMoved = false;
                _dragNode = node;
                var world = ToWorld(e.GetPosition(GraphPanel));
                _dragWorldDelta = new Point(node.X - world.X, node.Y - world.Y);
                e.Handled = true;
            }
        };

        GraphPanel.Children.Add(card);
        _nodeCards[node.Id] = card;

        // Canvas-level pins (visible bolinhas that sit on the wire endpoints)
        for (int i = 0; i < node.InputPins.Count; i++)
            AddCanvasPin(node, i, output: false);
        for (int i = 0; i < node.OutputPins.Count; i++)
            AddCanvasPin(node, i, output: true);
    }

    private void AddCanvasPin(MaterialNode node, int pinIdx, bool output)
    {
        var pin = output ? node.OutputPins[pinIdx] : node.InputPins[pinIdx];
        var screen = PinScreen(node, pinIdx, output);
        double r = PinR * _zoom;
        var dot = new Ellipse
        {
            Width = r * 2, Height = r * 2,
            Fill = PinBrush(pin.Type),
            Stroke = Brushes.White,
            StrokeThickness = Math.Max(1.2, 1.4 * _zoom),
            ZIndex = 30,
            Cursor = new Cursor(StandardCursorType.Cross),
        };
        Canvas.SetLeft(dot, screen.X - r);
        Canvas.SetTop(dot, screen.Y - r);

        if (output)
        {
            dot.PointerPressed += (_, e) =>
            {
                _isConnecting = true;
                _connFromNode = node.Id;
                _connFromPin = pin.Id;
                _connScreen = e.GetPosition(GraphPanel);
                e.Handled = true;
            };
        }
        else
        {
            var idx = pinIdx;
            dot.PointerPressed += (_, e) =>
            {
                if (_isConnecting) FinishConnection(node, idx);
                e.Handled = true;
            };
        }

        GraphPanel.Children.Add(dot);
    }

    private Control BuildNodePreview(MaterialNode node)
    {
        var host = new Border
        {
            Margin = new Thickness(6, 2, 6, 2),
            Height = PreviewH,
            CornerRadius = new CornerRadius(4),
            BorderBrush = new SolidColorBrush(Color.Parse("#2A2A32")),
            BorderThickness = new Thickness(1),
            ClipToBounds = true,
            Background = new SolidColorBrush(Color.Parse("#121218")),
        };

        if (node.Type is "Constant3" or "Constant4")
        {
            host.Background = ColorBrushFromValue(node.Properties.GetValueOrDefault("value"));
            host.Child = new TextBlock
            {
                Text = ShortValue(node.Properties.GetValueOrDefault("value")),
                FontSize = 10, Foreground = Brushes.White,
                HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
            };
        }
        else if (node.Type == "Constant1")
        {
            var v = ParseFloat(node.Properties.GetValueOrDefault("value"), 0.5f);
            byte g = (byte)(Math.Clamp(v, 0, 1) * 255);
            host.Background = new SolidColorBrush(Color.FromRgb(g, g, g));
            host.Child = new TextBlock
            {
                Text = v.ToString("0.###", CultureInfo.InvariantCulture),
                FontSize = 12, FontWeight = FontWeight.SemiBold, Foreground = Brushes.White,
                HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
            };
        }
        else if (node.Type == "TextureSample")
        {
            var path = node.Properties.GetValueOrDefault("path") ?? "";
            if (!string.IsNullOrEmpty(path) && File.Exists(path))
            {
                var bmp = TryLoadThumb(path, 128);
                if (bmp is not null)
                {
                    host.Child = new Image { Source = bmp, Stretch = Stretch.UniformToFill };
                }
                else
                {
                    host.Child = new TextBlock
                    {
                        Text = IoPath.GetFileName(path), FontSize = 9, Foreground = new SolidColorBrush(Color.Parse("#888")),
                        HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                        VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
                        TextWrapping = TextWrapping.Wrap, Margin = new Thickness(4),
                    };
                }
            }
            else
            {
                host.Child = new TextBlock
                {
                    Text = "No texture", FontSize = 10, Foreground = new SolidColorBrush(Color.Parse("#666")),
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                    VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
                };
            }
        }

        return host;
    }

    private static IBrush ColorBrushFromValue(string? value)
    {
        var parts = (value ?? "0.5,0.5,0.5").Split(',');
        float r = parts.Length > 0 && float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var rv) ? rv : 0.5f;
        float g = parts.Length > 1 && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var gv) ? gv : 0.5f;
        float b = parts.Length > 2 && float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var bv) ? bv : 0.5f;
        return new SolidColorBrush(Color.FromRgb(
            (byte)(Math.Clamp(r, 0, 1) * 255),
            (byte)(Math.Clamp(g, 0, 1) * 255),
            (byte)(Math.Clamp(b, 0, 1) * 255)));
    }

    private static string ShortValue(string? v)
    {
        if (string.IsNullOrWhiteSpace(v)) return "";
        var parts = v.Split(',');
        if (parts.Length >= 3 &&
            float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var r) &&
            float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var g) &&
            float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var b))
            return $"{r:0.##}, {g:0.##}, {b:0.##}";
        return v;
    }

    private static float ParseFloat(string? s, float fallback) =>
        float.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : fallback;

    // ── Interaction ─────────────────────────────────────

    private void SelectNode(MaterialNode node)
    {
        _selected = node;
        ShowDetails(node);
        RenderGraph();
    }

    private void ClearDetails()
    {
        DetailsHint.IsVisible = true;
        DetailsHint.Text = "Select a node to edit values / texture";
        DetailsTitle.Text = "";
        DetailsSwatch.IsVisible = false;
        DetailsImage.IsVisible = false;
        DetailsValueBox.IsVisible = false;
        BrowseTextureBtn.IsVisible = false;
    }

    private void ShowDetails(MaterialNode node)
    {
        DetailsHint.IsVisible = false;
        DetailsTitle.Text = $"{node.Name}  ({node.Type})";
        DetailsSwatch.IsVisible = HasPreview(node);
        DetailsImage.IsVisible = false;
        DetailsImage.Source = null;
        DetailsValueBox.IsVisible = false;
        BrowseTextureBtn.IsVisible = false;

        if (node.Type is "Constant1" or "Constant3" or "Constant4")
        {
            DetailsValueBox.IsVisible = true;
            DetailsValueBox.Text = node.Properties.GetValueOrDefault("value") ?? "";
            DetailsSwatch.Background = node.Type == "Constant1"
                ? new SolidColorBrush(Color.FromRgb(
                    (byte)(Math.Clamp(ParseFloat(DetailsValueBox.Text, 0.5f), 0, 1) * 255),
                    (byte)(Math.Clamp(ParseFloat(DetailsValueBox.Text, 0.5f), 0, 1) * 255),
                    (byte)(Math.Clamp(ParseFloat(DetailsValueBox.Text, 0.5f), 0, 1) * 255)))
                : ColorBrushFromValue(DetailsValueBox.Text);
            DetailsValueBox.Watermark = node.Type == "Constant1" ? "0.5" : "r,g,b";
        }
        else if (node.Type == "TextureSample")
        {
            BrowseTextureBtn.IsVisible = true;
            var path = node.Properties.GetValueOrDefault("path") ?? "";
            DetailsValueBox.IsVisible = true;
            DetailsValueBox.Text = path;
            DetailsValueBox.Watermark = "texture path";
            if (!string.IsNullOrEmpty(path) && File.Exists(path))
            {
                var bmp = TryLoadThumb(path, 256);
                if (bmp is not null)
                {
                    DetailsImage.Source = bmp;
                    DetailsImage.IsVisible = true;
                    DetailsSwatch.Background = Brushes.Transparent;
                }
            }
            else
            {
                DetailsSwatch.Background = new SolidColorBrush(Color.Parse("#22222A"));
            }
        }
        else
        {
            DetailsSwatch.IsVisible = false;
        }
    }

    private void OnDetailsValueCommit(object? sender, RoutedEventArgs e) => CommitDetailsValue();
    private void OnDetailsValueKey(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) { CommitDetailsValue(); e.Handled = true; }
    }

    private void CommitDetailsValue()
    {
        if (_selected is null || !DetailsValueBox.IsVisible) return;
        PushUndo();
        if (_selected.Type is "Constant1" or "Constant3" or "Constant4")
            _selected.Properties["value"] = DetailsValueBox.Text?.Trim() ?? "";
        else if (_selected.Type == "TextureSample")
            _selected.Properties["path"] = DetailsValueBox.Text?.Trim() ?? "";
        ShowDetails(_selected);
        UpdatePreview();
        RenderGraph();
    }

    private async void OnBrowseTexture(object? sender, RoutedEventArgs e)
    {
        if (_selected?.Type != "TextureSample") return;
        var top = TopLevel.GetTopLevel(this);
        if (top is null) return;
        var files = await top.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Choose texture",
            AllowMultiple = false,
            FileTypeFilter =
            [
                new FilePickerFileType("Images")
                {
                    Patterns = ["*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp", "*.webp"]
                }
            ]
        });
        if (files.Count == 0 || files[0].TryGetLocalPath() is not { } path) return;
        PushUndo();
        _selected.Properties["path"] = path;
        ShowDetails(_selected);
        UpdatePreview();
        RenderGraph();
    }

    private void OnTextureCardPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control card || card.DataContext is not TextureListItem item) return;
        if (!e.GetCurrentPoint(card).Properties.IsLeftButtonPressed) return;
        _pendingDragItem = item;
        _pendingDragStart = e.GetPosition(this);
        _dragStarted = false;
        e.Pointer.Capture(card);
    }

    private async void OnTextureCardMoved(object? sender, PointerEventArgs e)
    {
        if (_pendingDragItem is null || _dragStarted) return;
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
        var pos = e.GetPosition(this);
        if (Math.Abs(pos.X - _pendingDragStart.X) < DragThreshold &&
            Math.Abs(pos.Y - _pendingDragStart.Y) < DragThreshold)
            return;

        _dragStarted = true;
        var item = _pendingDragItem;
        var dragData = new DataObject();
        dragData.Set(DataFormats.Text, item.Path); // fallback
        dragData.Set("tucano/texture-path", item.Path);
        dragData.Set("tucano/texture-name", item.Name);
        try
        {
            await DragDrop.DoDragDrop(e, dragData, DragDropEffects.Copy);
        }
        finally
        {
            _pendingDragItem = null;
            _dragStarted = false;
        }
    }

    private void OnTextureCardReleased(object? sender, PointerReleasedEventArgs e)
    {
        _pendingDragItem = null;
        _dragStarted = false;
        e.Pointer.Capture(null);
    }

    private void OnTextureCardDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: TextureListItem item })
            SpawnTextureNode(item, null, askUser: true);
    }

    private void OnTextureCardCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        if (!_dragStarted)
        {
            _pendingDragItem = null;
        }
    }

    private void OnGraphDragOver(object? sender, DragEventArgs e)
    {
        if (e.Data.Contains("tucano/texture-path") || e.Data.Contains(DataFormats.Text))
        {
            e.DragEffects = DragDropEffects.Copy;
            e.Handled = true;
        }
        else
        {
            e.DragEffects = DragDropEffects.None;
        }
    }

    private async void OnGraphDrop(object? sender, DragEventArgs e)
    {
        var path = e.Data.Get("tucano/texture-path") as string
                   ?? e.Data.Get(DataFormats.Text) as string;
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return;

        var name = e.Data.Get("tucano/texture-name") as string ?? IoPath.GetFileName(path);
        var item = _allTextures.FirstOrDefault(t => t.Path == path)
                   ?? new TextureListItem
                   {
                       Name = name,
                       Path = path,
                       Thumbnail = TryLoadThumb(path, 128),
                       FolderLabel = IoPath.GetFileName(IoPath.GetDirectoryName(path) ?? "")
                   };

        var screen = e.GetPosition(GraphPanel);
        var world = ToWorld(screen);
        e.Handled = true;

        // Dropped directly on a Material Output pin → that pin wins, no menu.
        var hitPin = HitTestOutputPin(world);
        if (hitPin is not null)
        {
            SpawnTextureNode(item, world, hitPin, askUser: false);
            return;
        }

        // Otherwise ask what to do with the texture (Unreal-style choice).
        await AskTextureUseAndSpawn(item, world, e);
    }

    private async Task AskTextureUseAndSpawn(TextureListItem item, Point world, DragEventArgs? dragArgs)
    {
        var menu = new ContextMenu();
        void Add(string header, string? pin)
        {
            var mi = new MenuItem { Header = header };
            mi.Click += (_, _) => SpawnTextureNode(item, world, pin, askUser: false);
            menu.Items.Add(mi);
        }

        Add("Use as Base Color (Albedo)", "albedo");
        Add("Use as Normal", "normal");
        Add("Use as Roughness", "roughness");
        Add("Use as Metallic", "metallic");
        Add("Use as Emissive", "emissive");
        menu.Items.Add(new Separator());
        Add("Add Texture Sample (no wire)", null);

        // Suggested default from filename
        var guess = GuessTexturePin(item.Name);
        if (guess is not null)
        {
            menu.Items.Insert(0, new Separator());
            var suggested = new MenuItem
            {
                Header = $"Suggested: {PinDisplayName(guess)}",
                FontWeight = FontWeight.SemiBold
            };
            suggested.Click += (_, _) => SpawnTextureNode(item, world, guess, askUser: false);
            menu.Items.Insert(0, suggested);
        }

        // Open at drop position relative to the graph panel
        var point = dragArgs?.GetPosition(GraphPanel) ?? ToScreen(world.X, world.Y);
        menu.Placement = PlacementMode.Pointer;
        menu.Open(GraphPanel);
        await Task.CompletedTask;
    }

    private static string PinDisplayName(string pin) => pin switch
    {
        "albedo" => "Base Color",
        "normal" => "Normal",
        "roughness" => "Roughness",
        "metallic" => "Metallic",
        "emissive" => "Emissive",
        _ => pin
    };

    private string? HitTestOutputPin(Point world)
    {
        var output = _graph.FindNode("output") ?? _graph.Nodes.FirstOrDefault(n => n.Type == "MaterialOutput");
        if (output is null) return null;
        for (int i = 0; i < output.InputPins.Count; i++)
        {
            double py = output.Y + HdrH + PreviewOffset(output) + 2 + i * RowH + RowH / 2;
            double px = output.X - 2;
            if (Math.Abs(world.X - px) < 28 && Math.Abs(world.Y - py) < RowH)
                return output.InputPins[i].Id;
        }
        return null;
    }

    private void SpawnTextureNode(TextureListItem item, Point? worldPos, string? forcePin = null,
        bool askUser = false)
    {
        if (askUser)
        {
            // Double-click path: menu without a drag event
            _ = AskTextureUseAndSpawn(item, worldPos ?? ToWorld(new Point(
                Math.Max(GraphPanel.Bounds.Width, 400) / 2,
                Math.Max(GraphPanel.Bounds.Height, 300) / 2)), null);
            return;
        }

        var node = MaterialGraph.CreateNode("TextureSample", IoPath.GetFileNameWithoutExtension(item.Name));
        node.Properties["path"] = item.Path;
        PushUndo();
        if (worldPos is { } w)
        {
            node.X = w.X - NodeW / 2;
            node.Y = w.Y - 40;
        }
        else
        {
            var center = ToWorld(new Point(
                Math.Max(GraphPanel.Bounds.Width, 400) / 2,
                Math.Max(GraphPanel.Bounds.Height, 300) / 2));
            node.X = center.X - NodeW / 2 + (_nextNodeX % 3) * 40;
            node.Y = center.Y - 40 + (_nextNodeY % 3) * 30;
            _nextNodeX += 1;
            _nextNodeY += 1;
        }
        _graph.Nodes.Add(node);

        var output = _graph.FindNode("output") ?? _graph.Nodes.FirstOrDefault(n => n.Type == "MaterialOutput");
        if (output is not null && forcePin is not null)
        {
            _graph.Connections.RemoveAll(c => c.ToNodeId == output.Id && c.ToPinId == forcePin);
            _graph.Connections.Add(new MaterialConnection
            {
                FromNodeId = node.Id, FromPinId = "out",
                ToNodeId = output.Id, ToPinId = forcePin
            });
        }

        SelectNode(node);
        UpdatePreview();
        RenderGraph();
    }

    private static string? GuessTexturePin(string fileName)
    {
        var n = fileName.ToLowerInvariant();
        if (n.Contains("normal") || n.Contains("_nrm") || n.Contains("_norm") || n.Contains("-n.") || n.Contains("_n."))
            return "normal";
        if (n.Contains("rough") || n.Contains("_rgh") || n.Contains("gloss"))
            return "roughness";
        if (n.Contains("metal") || n.Contains("metallic"))
            return "metallic";
        if (n.Contains("emiss") || n.Contains("emit"))
            return "emissive";
        if (n.Contains("albedo") || n.Contains("diffuse") || n.Contains("basecolor") || n.Contains("base_color")
            || n.Contains("_col") || n.Contains("color") || n.Contains("_diff"))
            return "albedo";
        return null;
    }

    private void FinishConnection(MaterialNode target, int inputIdx)
    {
        if (!_isConnecting || _connFromNode is null || _connFromPin is null) return;
        PushUndo();
        _graph.Connections.RemoveAll(c => c.ToNodeId == target.Id && c.ToPinId == target.InputPins[inputIdx].Id);
        _graph.Connections.Add(new MaterialConnection
        {
            FromNodeId = _connFromNode, FromPinId = _connFromPin,
            ToNodeId = target.Id, ToPinId = target.InputPins[inputIdx].Id
        });
        _isConnecting = false;
        _connFromNode = null;
        _connFromPin = null;
        UpdatePreview();
        RenderGraph();
    }

    private void OnCanvasPressed(object? sender, PointerPressedEventArgs e)
    {
        GraphPanel.Focus();
        var pt = e.GetCurrentPoint(GraphPanel);
        if (pt.Properties.IsMiddleButtonPressed || pt.Properties.IsRightButtonPressed ||
            (pt.Properties.IsLeftButtonPressed && _spaceDown))
        {
            _isPanning = true;
            _panStart = pt.Position;
            _panOriginX = _panX;
            _panOriginY = _panY;
            e.Pointer.Capture(GraphPanel);
            e.Handled = true;
            return;
        }

        if (_isConnecting && pt.Properties.IsLeftButtonPressed)
        {
            var world = ToWorld(pt.Position);
            foreach (var node in _graph.Nodes)
            {
                double preview = PreviewOffset(node);
                int pinIdx = (int)((world.Y - node.Y - HdrH - preview) / RowH);
                if (pinIdx >= 0 && pinIdx < node.InputPins.Count &&
                    world.X >= node.X && world.X <= node.X + NodeW)
                {
                    FinishConnection(node, pinIdx);
                    return;
                }
            }
            _isConnecting = false;
            _connFromNode = null;
            _connFromPin = null;
            RenderGraph();
        }

        if (pt.Properties.IsLeftButtonPressed && e.Source == GraphPanel)
        {
            _selected = null;
            ClearDetails();
            RenderGraph();
        }
        _dragNode = null;
    }

    private void OnCanvasMoved(object? sender, PointerEventArgs e)
    {
        var pt = e.GetPosition(GraphPanel);
        if (_isPanning)
        {
            _panX = _panOriginX + (pt.X - _panStart.X);
            _panY = _panOriginY + (pt.Y - _panStart.Y);
            RenderGraph();
            return;
        }

        if (_isConnecting)
        {
            _connScreen = pt;
            RenderGraph();
            return;
        }

        if (_dragNode is not null && e.GetCurrentPoint(GraphPanel).Properties.IsLeftButtonPressed)
        {
            var world = ToWorld(pt);
            _dragNode.X = world.X + _dragWorldDelta.X;
            _dragNode.Y = world.Y + _dragWorldDelta.Y;
            _nodeDragMoved = true;
            RenderGraph();
        }
    }

    private void OnCanvasReleased(object? sender, PointerReleasedEventArgs e)
    {
        // If the node never moved, drop the undo snapshot we pushed on press.
        if (_dragNode is not null && !_nodeDragMoved && _undo.Count > 0)
            _undo.Pop();
        _isPanning = false;
        _dragNode = null;
        _nodeDragMoved = false;
        e.Pointer.Capture(null);
    }

    private void OnCanvasWheel(object? sender, PointerWheelEventArgs e)
    {
        var mouse = e.GetPosition(GraphPanel);
        var before = ToWorld(mouse);
        double factor = e.Delta.Y > 0 ? 1.12 : 1 / 1.12;
        _zoom = Math.Clamp(_zoom * factor, MinZoom, MaxZoom);
        // Keep world point under cursor stable
        _panX = mouse.X - before.X * _zoom;
        _panY = mouse.Y - before.Y * _zoom;
        RenderGraph();
        e.Handled = true;
    }

    private void OnFitView(object? sender, RoutedEventArgs e) => FitView();

    private void FitView()
    {
        if (_graph.Nodes.Count == 0)
        {
            _zoom = 1; _panX = 40; _panY = 40;
            RenderGraph();
            return;
        }

        double minX = _graph.Nodes.Min(n => n.X);
        double minY = _graph.Nodes.Min(n => n.Y);
        double maxX = _graph.Nodes.Max(n => n.X + NodeW);
        double maxY = _graph.Nodes.Max(n => n.Y + 160);
        double gw = Math.Max(maxX - minX, 1);
        double gh = Math.Max(maxY - minY, 1);
        double vw = Math.Max(GraphPanel.Bounds.Width, 800);
        double vh = Math.Max(GraphPanel.Bounds.Height, 500);
        double pad = 80;
        _zoom = Math.Clamp(Math.Min((vw - pad) / gw, (vh - pad) / gh), MinZoom, 1.4);
        _panX = (vw - gw * _zoom) / 2 - minX * _zoom;
        _panY = (vh - gh * _zoom) / 2 - minY * _zoom;
        RenderGraph();
    }

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Space) _spaceDown = true;

        var ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
        if (ctrl && e.Key == Key.Z)
        {
            Undo();
            e.Handled = true;
            return;
        }
        if (ctrl && e.Key == Key.Y)
        {
            Redo();
            e.Handled = true;
            return;
        }

        if (e.Key == Key.Delete && _selected is not null && _selected.Type != "MaterialOutput")
        {
            PushUndo();
            var id = _selected.Id;
            _graph.Connections.RemoveAll(c => c.FromNodeId == id || c.ToNodeId == id);
            _graph.Nodes.RemoveAll(n => n.Id == id);
            _selected = null;
            ClearDetails();
            UpdatePreview();
            RenderGraph();
            e.Handled = true;
        }
        if (e.Key == Key.F) { FitView(); e.Handled = true; }
    }

    private void OnKeyUp(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Space) _spaceDown = false;
    }

    private void AddNode(MaterialNode n)
    {
        PushUndo();
        var center = ToWorld(new Point(
            Math.Max(GraphPanel.Bounds.Width, 400) / 2,
            Math.Max(GraphPanel.Bounds.Height, 300) / 2));
        n.X = center.X - NodeW / 2 + (_nextNodeX % 3) * 40;
        n.Y = center.Y - 40 + (_nextNodeY % 3) * 30;
        _nextNodeX += 1;
        _nextNodeY += 1;
        _graph.Nodes.Add(n);
        RenderGraph();
    }

    private void OnAddConstant3(object? s, RoutedEventArgs e) =>
        AddNode(MaterialGraph.CreateNode("Constant3", "Constant"));
    private void OnAddConstant1(object? s, RoutedEventArgs e) =>
        AddNode(MaterialGraph.CreateNode("Constant1", "Scalar"));
    private void OnAddMultiply(object? s, RoutedEventArgs e) =>
        AddNode(MaterialGraph.CreateNode("Multiply"));
    private void OnAddAdd(object? s, RoutedEventArgs e) =>
        AddNode(MaterialGraph.CreateNode("Add"));
    private void OnAddLerp(object? s, RoutedEventArgs e) =>
        AddNode(MaterialGraph.CreateNode("Lerp"));
    private void OnAddTexture(object? s, RoutedEventArgs e) =>
        AddNode(MaterialGraph.CreateNode("TextureSample", "Texture Sample"));

    private void OnCompile(object? sender, RoutedEventArgs e)
    {
        try
        {
            SyncAssetFromGraph();
            UpdatePreview();
            if (_asset is not null) _onApplied?.Invoke(_asset);
            CompileStatus.Text = "✓ Applied";
            CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#5EC75E"));
        }
        catch (Exception ex)
        {
            CompileStatus.Text = "✗ " + ex.Message;
            CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#E5534B"));
        }
    }

    private void SyncAssetFromGraph()
    {
        if (_asset is null) return;
        var evaluated = _graph.ToMaterialAsset();
        _asset.R = evaluated.R;
        _asset.G = evaluated.G;
        _asset.B = evaluated.B;
        _asset.Metallic = evaluated.Metallic;
        _asset.Roughness = evaluated.Roughness;
        _asset.EmissiveR = evaluated.EmissiveR;
        _asset.EmissiveG = evaluated.EmissiveG;
        _asset.EmissiveB = evaluated.EmissiveB;
        _asset.Name = _graph.Name;
    }

    private void UpdatePreview()
    {
        var mat = _graph.ToMaterialAsset();
        var output = _graph.FindNode("output") ?? _graph.Nodes.FirstOrDefault(n => n.Type == "MaterialOutput");
        string? albedoPath = output is null ? null : _graph.GetConnectedTexturePath(output, "albedo");
        string? normalPath = output is null ? null : _graph.GetConnectedTexturePath(output, "normal");

        PreviewSphere.Source = MaterialPreviewUtil.RenderSphere(mat, albedoPath, normalPath, 248);

        var rough = mat.Roughness.ToString("0.##", CultureInfo.InvariantCulture);
        var metal = mat.Metallic.ToString("0.##", CultureInfo.InvariantCulture);
        var bits = new List<string>();
        if (albedoPath is not null) bits.Add("Albedo✓");
        if (normalPath is not null) bits.Add("Normal✓");
        bits.Add($"M {metal}");
        bits.Add($"R {rough}");
        if (albedoPath is null)
            bits.Insert(0, $"RGB {mat.R:0.##},{mat.G:0.##},{mat.B:0.##}");
        PreviewInfo.Text = string.Join("  ·  ", bits);
    }

    private void OnSave(object? sender, RoutedEventArgs e)
    {
        try
        {
            SyncAssetFromGraph();
            UpdatePreview();
            var path = _graph.AssetPath ?? _asset?.Path;
            if (path is null)
            {
                CompileStatus.Text = "✗ No file path";
                CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#E5534B"));
                return;
            }

            _graph.Save(path);
            if (_asset is not null)
            {
                _asset.Path = path;
                _asset.GraphJson = ExtractGraphFromFile(path);
                _onApplied?.Invoke(_asset);
            }

            CompileStatus.Text = "✓ Saved";
            CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#CCAA30"));
        }
        catch (Exception ex)
        {
            CompileStatus.Text = "✗ " + ex.Message;
            CompileStatus.Foreground = new SolidColorBrush(Color.Parse("#E5534B"));
        }
    }

    private static string? ExtractGraphFromFile(string path)
    {
        try
        {
            var json = File.ReadAllText(path);
            var key = json.IndexOf("\"graph\"", StringComparison.Ordinal);
            if (key < 0) return null;
            var open = json.IndexOf('{', key);
            if (open < 0) return null;
            int depth = 0;
            for (int i = open; i < json.Length; i++)
            {
                if (json[i] == '{') depth++;
                else if (json[i] == '}')
                {
                    depth--;
                    if (depth == 0) return json[open..(i + 1)];
                }
            }
        }
        catch { /* ignore */ }
        return null;
    }
}

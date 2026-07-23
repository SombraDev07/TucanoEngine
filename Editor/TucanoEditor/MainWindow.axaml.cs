using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using EditorCore;
using EditorCore.Interop;
using TucanoEditor.Models;
using TucanoEditor.Views;

namespace TucanoEditor;

public partial class MainWindow : Window
{
    private RuntimeHost? _runtime;
    private int _frameCount;
    private DateTime _lastFpsUpdate = DateTime.UtcNow;
    private readonly ObservableCollection<SceneNode> _outlinerRoots = new();
    private readonly ObservableCollection<AssetItem> _assets = new();
    private readonly ObservableCollection<SceneNode> _folderRoots = new();
    private readonly List<AssetItem> _allAssets = new();
    private MaterialLibrary? _materials;
    private string _outlinerFilter = "";
    private int _selectedLight = -1;
    private readonly UndoStack _undo = new();
    private EnvironmentWindow? _environmentWindow;
    private string? _scenePath;
    private readonly ObservableCollection<string> _console = new();

    /// Status-bar message that also lands in the Console tab, so nothing scrolls away unseen.
    private void Log(string message)
    {
        StatusHint.Text = message;
        _console.Add($"[{DateTime.Now:HH:mm:ss}]  {message}");
        if (_console.Count > 500) _console.RemoveAt(0);
        if (ConsoleList.ItemCount > 0) ConsoleList.ScrollIntoView(_console.Count - 1);
    }

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closing += OnClosing;
        KeyDown += OnKeyDown;
    }

    private void OnLoaded(object? sender, EventArgs e)
    {
        try
        {
            _runtime = new RuntimeHost(enableDebug: true);

            // The runtime owns the fly camera while the viewport has focus; its ImGui HUD stays off
            // so the editor's status bar is the single FPS readout.
            _runtime.CameraNavigation = true;
            _runtime.OverlayVisible = false;

            OutlinerTree.ItemsSource = _outlinerRoots;
            FolderTree.ItemsSource = _folderRoots;
            AssetList.ItemsSource = _assets;
            ConsoleList.ItemsSource = _console;
            _materials = new MaterialLibrary(Path.Combine(ProjectRoot, "Assets", "Materials"));
            RescanAssets();
            RefreshOutliner();
            UpdatePlayUi();

            Title = $"TUCANO EDITOR — Engine v{_runtime.Version}";
            StatusFPS.Text = $"Engine v{_runtime.Version} | Embedding viewport...";

            // Keep the runtime swapchain in sync with the embedded viewport size.
            Viewport.ViewportResized += (w, h) => _runtime?.Resize((uint)w, (uint)h);

            // ViewportControl defers the embed if its container HWND isn't created yet.
            Dispatcher.UIThread.Post(() =>
            {
                var engineHwnd = _runtime.NativeWindowHandle;
                if (engineHwnd != IntPtr.Zero)
                {
                    Viewport.EmbedEngineWindow(engineHwnd);
                    StatusFPS.Text = Viewport.IsAttached
                        ? $"Engine v{_runtime.Version} | Viewport embedded"
                        : $"Engine v{_runtime.Version} | Viewport embed pending...";
                }
            }, DispatcherPriority.Loaded);

            // Render whenever the dispatcher goes idle rather than on a timer. A DispatcherTimer
            // caps the frame rate at its interval *plus* its own scheduling overhead — a 16 ms timer
            // measured ~30 ms per cycle even though the engine frame itself was 4 ms. Re-posting at
            // Background priority lets the engine use all the time the UI isn't using, while input,
            // layout and paint (all higher priority) still pre-empt it, so nothing starves.
            _renderLoopRunning = true;
            QueueRenderPass();

            if (Environment.GetCommandLineArgs().Contains("--selftest")) RunSelfTest();
        }
        catch (Exception ex)
        {
            StatusFPS.Text = $"ERROR: {ex.Message}";
        }
    }

    /// Unattended check of the paths a human would click: opens the Environment window, drives a
    /// preset, keeps rendering, then exits with a status code. Exists because the editor cannot be
    /// clicked from a script, and "it builds" is not evidence that a window opens without hanging.
    private void RunSelfTest()
    {
        var deadline = DateTime.UtcNow.AddSeconds(40);
        var step = 0;
        var logPath = Path.Combine(Path.GetTempPath(), "tucano_selftest.log");
        File.WriteAllText(logPath, $"selftest started {DateTime.Now:HH:mm:ss}{Environment.NewLine}");
        void Record(string m)
        {
            Log(m);
            File.AppendAllText(logPath, m + Environment.NewLine);
        }

        var timer = new DispatcherTimer(TimeSpan.FromSeconds(1.2), DispatcherPriority.Normal, (_, _) => { });
        timer.Tick += (_, _) =>
        {
            try
            {
                switch (step++)
                {
                    case 0:
                        Record("selftest: opening Environment window");
                        OnOpenEnvironment(this, new RoutedEventArgs());
                        break;

                    case 1:
                        Record(_environmentWindow is { IsVisible: true }
                            ? "selftest: environment window open, editor still rendering"
                            : "selftest: FAILED - environment window did not open");
                        _environmentWindow?.ApplyStormPresetForTest();
                        Record("selftest: applied storm preset");
                        break;

                    case 2:
                        // Outliner tree must have been built with the scene/objects/lights groups.
                        // Root holds loose objects plus a Lights group; folders appear as needed.
                        var rootNode0 = _outlinerRoots.FirstOrDefault();
                        Record(rootNode0 is { Kind: SceneNodeKind.Root } && rootNode0.Children.Count > 0
                            ? $"selftest: outliner tree ok ({rootNode0.Children.Count(c => c.IsObject)} loose objects, " +
                              $"{rootNode0.Children.Count(c => c.Kind == SceneNodeKind.LightGroup)} light group)"
                            : "selftest: FAILED - outliner tree not built");
                        break;

                    case 3:
                        OnCreateCube(this, new RoutedEventArgs());
                        Record(SelectedObjectIndex >= 0
                            ? $"selftest: spawned and auto-selected #{SelectedObjectIndex}"
                            : "selftest: FAILED - spawn did not select the new object");
                        break;

                    case 4:
                        // Add-component path, then verify the panel actually became visible.
                        OnAddPhysicsComponent(this, new RoutedEventArgs());
                        Record(CompPhysics.IsVisible &&
                               _runtime!.GetObjectPhysics((uint)SelectedObjectIndex) == TucanoPhysicsKind.Dynamic
                            ? "selftest: physics component added and shown"
                            : "selftest: FAILED - physics component not applied");
                        break;

                    case 5:
                        OnNewMaterial(this, new RoutedEventArgs());
                        var mats = _allAssets.Count(a => a.Kind == AssetKind.Material);
                        Record(mats > 0
                            ? $"selftest: material asset created ({mats} in browser)"
                            : "selftest: FAILED - no material asset appeared");
                        break;

                    case 6:
                        // Apply the material we just made back onto the selection.
                        var matItem = _allAssets.FirstOrDefault(a => a.Kind == AssetKind.Material);
                        if (matItem is not null)
                        {
                            ApplyMaterial(matItem);
                            Record("selftest: applied material to selection");
                        }
                        else
                        {
                            Record("selftest: FAILED - material asset missing");
                        }
                        break;

                    case 7:
                        // Light selection drives a different inspector layout.
                        OnAddPointLight(this, new RoutedEventArgs());
                        var lightNode = FindNode(n => n.Kind == SceneNodeKind.Light);
                        if (lightNode is not null)
                        {
                            OutlinerTree.SelectedItem = lightNode;
                            _selectedLight = lightNode.LightIndex;
                            LoadLightInspector(lightNode.LightIndex);
                            Record(CompLight.IsVisible
                                ? "selftest: light selected, light inspector shown"
                                : "selftest: FAILED - light inspector not shown");
                        }
                        else
                        {
                            Record("selftest: FAILED - no light node in the tree");
                        }
                        break;

                    case 8:
                        // Textures must be hidden by default: one imported model brings hundreds.
                        var shownTextures = _assets.Count(a => a.Kind == AssetKind.Texture);
                        var knownTextures = _allAssets.Count(a => a.Kind == AssetKind.Texture);
                        Record(shownTextures == 0
                            ? $"selftest: texture filter ok ({knownTextures} textures indexed, 0 shown)"
                            : $"selftest: FAILED - {shownTextures} textures polluting the browser");
                        break;

                    case 9:
                        FilterTextures.IsChecked = true;
                        Record(_assets.Any(a => a.Kind == AssetKind.Texture)
                            ? "selftest: textures appear when the chip is enabled"
                            : "selftest: FAILED - texture chip did not reveal textures");
                        FilterTextures.IsChecked = false;
                        break;

                    case 10:
                        // Folder creation + moving an object into it (the drag-drop payload path).
                        _knownFolders.Add("Props");
                        RefreshOutliner();
                        var objNode = FindNode(n => n.IsObject);
                        if (objNode is not null && _runtime is not null)
                        {
                            _runtime.SetObjectFolder((uint)objNode.ObjectIndex, "Props");
                            RefreshOutliner();
                            var moved = FindNode(n => n.IsObject && n.FolderPath == "Props");
                            var folderNode = FindNode(n => n.IsFolder && n.FolderPath == "Props");
                            Record(moved is not null && folderNode is not null &&
                                   folderNode.Children.Any(c => c.ObjectIndex == moved.ObjectIndex)
                                ? $"selftest: folder ok - '{moved.Name}' nested under Props"
                                : "selftest: FAILED - object did not land inside the folder");
                        }
                        else
                        {
                            Record("selftest: FAILED - no object to move");
                        }
                        break;

                    case 11:
                        // Eye toggle must reach the engine, not just the row.
                        var eyeNode = FindNode(n => n.IsObject);
                        if (eyeNode is not null && _runtime is not null)
                        {
                            ToggleVisibility(eyeNode);
                            var hidden = !_runtime.GetObjectVisible((uint)eyeNode.ObjectIndex);
                            ToggleVisibility(eyeNode);
                            var shownAgain = _runtime.GetObjectVisible((uint)eyeNode.ObjectIndex);
                            Record(hidden && shownAgain
                                ? "selftest: eye toggle drives engine visibility both ways"
                                : $"selftest: FAILED - visibility toggle (hid={hidden}, restored={shownAgain})");
                        }
                        break;

                    case 12:
                        // Hiding a folder must cascade to its children.
                        var propsFolder = FindNode(n => n.IsFolder && n.FolderPath == "Props");
                        if (propsFolder is not null && propsFolder.Children.Count > 0 && _runtime is not null)
                        {
                            ToggleVisibility(propsFolder);
                            var childIdx = (uint)propsFolder.Children[0].ObjectIndex;
                            var cascaded = !_runtime.GetObjectVisible(childIdx);
                            ToggleVisibility(propsFolder);
                            Record(cascaded
                                ? "selftest: folder eye cascades to children"
                                : "selftest: FAILED - folder eye did not cascade");
                        }
                        else
                        {
                            Record("selftest: FAILED - Props folder missing for cascade test");
                        }
                        break;

                    case 13:
                        // Folder assignment has to survive a save/load round trip.
                        var tmp = Path.Combine(Path.GetTempPath(), "tucano_folder_test.tscene");
                        var before = FindNode(n => n.IsObject && n.FolderPath == "Props");
                        if (before is not null && _runtime is not null &&
                            _runtime.SaveScene(tmp) && _runtime.LoadScene(tmp))
                        {
                            RefreshOutliner();
                            Record(FindNode(n => n.IsObject && n.FolderPath == "Props") is not null
                                ? "selftest: folders survive save/load"
                                : "selftest: FAILED - folder lost on reload");
                        }
                        else
                        {
                            Record("selftest: FAILED - save/load of folders");
                        }
                        break;

                    case 14:
                        Record($"selftest: still alive, {StatusFPS.Text}");
                        break;

                    default:
                        Record("selftest: PASSED");
                        timer.Stop();
                        Close();
                        break;
                }
            }
            catch (Exception ex)
            {
                Record($"selftest: FAILED - {ex}");
                timer.Stop();
                Close();
            }

            if (DateTime.UtcNow > deadline)
            {
                Record("selftest: FAILED - deadline exceeded");
                timer.Stop();
                Close();
            }
        };
        timer.Start();
    }

    // Render() pumps the Win32 message queue (glfwPollEvents), so the dispatcher can re-enter this
    // handler mid-frame. Re-entering D3D12 command recording corrupts the device, so drop the tick.
    private bool _rendering;
    private bool _renderLoopRunning;

    private void QueueRenderPass() =>
        Dispatcher.UIThread.Post(OnRenderTick, DispatcherPriority.Background);

    private void OnRenderTick()
    {
        if (!_renderLoopRunning) return;
        if (_rendering) { QueueRenderPass(); return; }
        if (_runtime is not { IsAlive: true })
        {
            _renderLoopRunning = false;
            return;
        }

        _rendering = true;
        try
        {
            _runtime.Render();
            _frameCount++;

            // A viewport click can change the selection at any time.
            SyncSelectionFromRuntime();

            var now = DateTime.UtcNow;
            var elapsed = (now - _lastFpsUpdate).TotalSeconds;
            if (elapsed >= 0.5)
            {
                var fps = (int)(_frameCount / elapsed);
                var ms = _runtime.LastFrameMs;
                var dc = _runtime.DrawCalls;
                StatusFPS.Text = $"{fps} FPS | {ms:F1}ms | {dc} draw calls";
                _frameCount = 0;
                _lastFpsUpdate = now;

                // Dragging in the viewport (or a running simulation) moves objects behind the
                // inspector's back; refresh at a human rate rather than every frame.
                if (_lastSelected >= 0) LoadInspector(_lastSelected);
                SyncGizmoToolbar();
            }
        }
        catch (Exception ex)
        {
            StatusFPS.Text = $"Render error: {ex.Message}";
            _renderLoopRunning = false;
        }
        finally
        {
            _rendering = false;
        }

        QueueRenderPass();
    }

    // ── Keyboard shortcuts ────────────────────────────

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        // The viewport has its own W/E/R handling inside the runtime; only editor-level chords land
        // here. Anything typed into a text box must not be hijacked.
        if (FocusManager?.GetFocusedElement() is TextBox or NumericUpDown) return;

        var ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
        switch (e.Key)
        {
            case Key.Delete when !ctrl:
                OnDeleteSelected(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.D when ctrl:
                OnDuplicate(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.Z when ctrl:
                OnUndo(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.Y when ctrl:
                OnRedo(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.S when ctrl:
                OnSaveScene(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.F5:
                OnPlay(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.F7:
                OnPause(sender, new RoutedEventArgs());
                e.Handled = true;
                break;
            case Key.Escape:
                if (_runtime is { PlayState: not TucanoPlayState.Stopped })
                {
                    OnStop(sender, new RoutedEventArgs());
                    e.Handled = true;
                }
                break;
        }
    }

    // ── Scene authoring ───────────────────────────────

    private void OnCreateCube(object? sender, RoutedEventArgs e) => Spawn(TucanoPrimitive.Cube, 1f);
    private void OnCreateSphere(object? sender, RoutedEventArgs e) => Spawn(TucanoPrimitive.Sphere, 1f);
    private void OnCreatePlane(object? sender, RoutedEventArgs e) => Spawn(TucanoPrimitive.Plane, 10f);

    // Places the object a few metres down the view direction so it lands on screen wherever the
    // camera happens to be pointing.
    private void Spawn(TucanoPrimitive prim, float scale)
    {
        if (_runtime is not { IsAlive: true }) return;

        var eye = _runtime.GetCameraPosition();
        var fwd = _runtime.GetCameraForward();
        const float distance = 8f;
        var (x, y, z) = (eye.X + fwd.X * distance, eye.Y + fwd.Y * distance, eye.Z + fwd.Z * distance);
        var index = _runtime.SpawnPrimitive(prim, x, y, z, scale);

        if (index == RuntimeHost.InvalidObject)
        {
            Log($"Failed to spawn {prim}.");
            return;
        }

        // Undo removes the object; redo re-creates it at the same spot. Indices are stable here
        // because the spawn always appends and the undo always removes the tail.
        var rt = _runtime;
        _undo.Push(new UndoAction($"Create {prim}",
            undo: () => rt.RemoveObject(index),
            redo: () => rt.SpawnPrimitive(prim, x, y, z, scale)));

        RefreshOutliner();
        SelectObject((int)index);
        Log($"Spawned {prim} (#{index}) — {_runtime.ObjectCount} objects in scene");
    }

    private void OnDeleteSelected(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        _runtime.RemoveObject((uint)sel);
        // Deleting cannot be undone yet: rebuilding the exact object needs its mesh source, which
        // the ABI doesn't hand back. Clearing the history is honest — a stale undo would be worse.
        _undo.Clear();
        RefreshOutliner();
        Log($"Deleted object #{sel} — {_runtime.ObjectCount} objects in scene (not undoable)");
    }

    private void OnDuplicate(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        var index = _runtime.DuplicateObject((uint)sel, 2f, 0f, 0f);
        if (index == RuntimeHost.InvalidObject) return;

        var rt = _runtime;
        var source = (uint)sel;
        _undo.Push(new UndoAction("Duplicate",
            undo: () => rt.RemoveObject(index),
            redo: () => rt.DuplicateObject(source, 2f, 0f, 0f)));

        RefreshOutliner();
        SelectObject((int)index);
        Log($"Duplicated to #{index}");
    }

    private void OnClearScene(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        _runtime.ClearScene();
        _undo.Clear();
        RefreshOutliner();
        Log("Scene cleared");
    }

    private void OnNewScene(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        StopIfPlaying();
        _runtime.ClearScene();
        _undo.Clear();
        _scenePath = null;
        RefreshOutliner();
        Log("New (empty) scene");
    }

    private void OnExit(object? sender, RoutedEventArgs e) => Close();

    private void OnAbout(object? sender, RoutedEventArgs e) =>
        Log($"Tucano Editor — engine v{_runtime?.Version ?? "?"} · DirectX 12 · Jolt physics");

    // ── Undo / redo ───────────────────────────────────

    private void OnUndo(object? sender, RoutedEventArgs e)
    {
        var name = _undo.Undo();
        RefreshOutliner();
        Log(name is null ? "Nothing to undo" : $"Undid: {name}");
    }

    private void OnRedo(object? sender, RoutedEventArgs e)
    {
        var name = _undo.Redo();
        RefreshOutliner();
        Log(name is null ? "Nothing to redo" : $"Redid: {name}");
    }

    // ── Save / load ───────────────────────────────────

    private async void OnSaveScene(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        if (_scenePath is null)
        {
            OnSaveSceneAs(sender, e);
            return;
        }
        SaveTo(_scenePath);
        await Task.CompletedTask;
    }

    private async void OnSaveSceneAs(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;

        var file = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Save scene",
            SuggestedFileName = "scene.tscene",
            DefaultExtension = "tscene",
            FileTypeChoices = new[] { new FilePickerFileType("Tucano scene") { Patterns = new[] { "*.tscene", "*.json" } } }
        });
        if (file?.TryGetLocalPath() is not { } path) return;
        SaveTo(path);
    }

    private void SaveTo(string path)
    {
        if (_runtime is not { IsAlive: true }) return;
        if (_runtime.SaveScene(path))
        {
            _scenePath = path;
            Title = $"TUCANO EDITOR — {Path.GetFileName(path)}";
            Log($"Saved to {path}");
        }
        else
        {
            Log($"Failed to save to {path}");
        }
    }

    private async void OnOpenScene(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;

        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open scene",
            AllowMultiple = false,
            FileTypeFilter = new[] { new FilePickerFileType("Tucano scene") { Patterns = new[] { "*.tscene", "*.json" } } }
        });
        if (files.Count == 0 || files[0].TryGetLocalPath() is not { } path) return;
        LoadSceneFrom(path);
    }

    /// Shared by the File menu and double-clicking a .tscene in the content browser.
    private void LoadSceneFrom(string path)
    {
        if (_runtime is not { IsAlive: true }) return;

        StopIfPlaying();
        if (_runtime.LoadScene(path))
        {
            _scenePath = path;
            _undo.Clear();
            Title = $"TUCANO EDITOR — {Path.GetFileName(path)}";
            RefreshOutliner();
            _environmentWindow?.LoadFromRuntime(); // the scene carries its own environment
            Log($"Loaded {path} — {_runtime.ObjectCount} objects");
        }
        else
        {
            Log($"Failed to load {path}");
        }
    }

    // ── Assets ────────────────────────────────────────

    private void OnRescanAssets(object? sender, RoutedEventArgs e) => RescanAssets();

    /// Repo root, derived from the running binary. Locates Assets/ without needing a project file.
    private static string ProjectRoot
    {
        get
        {
            var dir = new DirectoryInfo(AppContext.BaseDirectory);
            while (dir is not null && !Directory.Exists(Path.Combine(dir.FullName, "Assets")))
            {
                dir = dir.Parent;
            }
            return dir?.FullName ?? AppContext.BaseDirectory;
        }
    }

    /// Walks the asset folders for meshes, textures, scenes and materials, rebuilding both the
    /// folder tree and the tile grid.
    private void RescanAssets()
    {
        _allAssets.Clear();
        _materials?.Reload();

        var roots = new[]
        {
            Path.Combine(ProjectRoot, "Assets"),
            Path.Combine(ProjectRoot, "EngineAssets"),
        }.Where(Directory.Exists).Distinct().ToList();

        foreach (var root in roots)
        {
            IEnumerable<string> files;
            try
            {
                files = Directory.EnumerateFiles(root, "*.*", SearchOption.AllDirectories);
            }
            catch (Exception)
            {
                continue; // an unreadable folder must not kill the whole scan
            }

            foreach (var f in files)
            {
                var kind = ClassifyAsset(f);
                if (kind is null) continue;
                if (_allAssets.Any(a => a.Path == f)) continue;
                _allAssets.Add(new AssetItem
                {
                    Name = Path.GetFileName(f),
                    Path = f,
                    Kind = kind.Value,
                    Material = kind == AssetKind.Material
                        ? _materials?.Materials.FirstOrDefault(m => m.Path == f)
                        : null,
                });
            }
        }

        RebuildFolderTree(roots);
        ShowAssets(null);
    }

    private static AssetKind? ClassifyAsset(string path)
    {
        return Path.GetExtension(path).ToLowerInvariant() switch
        {
            ".gltf" or ".glb" => AssetKind.Mesh,
            ".tmat" => AssetKind.Material,
            ".png" or ".jpg" or ".jpeg" or ".tga" or ".hdr" or ".dds" => AssetKind.Texture,
            ".tscene" => AssetKind.Scene,
            _ => null,
        };
    }

    private void RebuildFolderTree(List<string> roots)
    {
        _folderRoots.Clear();
        _folderRoots.Add(new SceneNode { Name = "All assets", Kind = SceneNodeKind.Folder });

        foreach (var root in roots)
        {
            var rootNode = new SceneNode
            {
                Name = Path.GetFileName(root),
                Kind = SceneNodeKind.Folder,
                Detail = root,
            };
            // Only folders that actually hold assets, so the tree stays about content.
            foreach (var dir in _allAssets
                         .Where(a => a.Path.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                         .Select(a => Path.GetDirectoryName(a.Path)!)
                         .Distinct()
                         .OrderBy(d => d))
            {
                if (string.Equals(dir, root, StringComparison.OrdinalIgnoreCase)) continue;
                rootNode.Children.Add(new SceneNode
                {
                    Name = Path.GetRelativePath(root, dir),
                    Kind = SceneNodeKind.Folder,
                    Detail = dir,
                });
            }
            _folderRoots.Add(rootNode);
        }
    }

    private string? _assetFolder;

    /// Shows the assets under `folder` (null = everywhere) that pass the type chips.
    private void ShowAssets(string? folder)
    {
        _assetFolder = folder;
        _assets.Clear();

        var items = _allAssets.Where(a => folder is null ||
                (Path.GetDirectoryName(a.Path) ?? "").StartsWith(folder, StringComparison.OrdinalIgnoreCase))
            .Where(PassesTypeFilter)
            .OrderBy(a => a.Kind)
            .ThenBy(a => a.Name)
            .ToList();

        foreach (var a in items) _assets.Add(a);

        // Report what is hidden as well as what is shown, so a filtered-out asset never looks lost.
        var totalHere = _allAssets.Count(a => folder is null ||
            (Path.GetDirectoryName(a.Path) ?? "").StartsWith(folder, StringComparison.OrdinalIgnoreCase));
        var hidden = totalHere - items.Count;
        var meshes = items.Count(a => a.Kind == AssetKind.Mesh);
        var mats = items.Count(a => a.Kind == AssetKind.Material);
        AssetHint.Text = hidden > 0
            ? $"{meshes} meshes | {mats} materials | {hidden} hidden by filter"
            : $"{meshes} meshes | {mats} materials";
    }

    private bool PassesTypeFilter(AssetItem a) => a.Kind switch
    {
        AssetKind.Mesh => FilterMeshes.IsChecked == true,
        AssetKind.Material => FilterMaterials.IsChecked == true,
        AssetKind.Scene => FilterScenes.IsChecked == true,
        AssetKind.Texture => FilterTextures.IsChecked == true,
        _ => true,
    };

    private void OnAssetFilterChanged(object? sender, RoutedEventArgs e)
    {
        // Fired during XAML load, before the collections exist.
        if (_allAssets.Count == 0 && _assets.Count == 0) return;
        ShowAssets(_assetFolder);
    }

    private void OnFolderSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (FolderTree.SelectedItem is not SceneNode node) return;
        ShowAssets(string.IsNullOrEmpty(node.Detail) ? null : node.Detail);
    }

    private void OnAssetDoubleTapped(object? sender, TappedEventArgs e) => UseSelectedAsset();
    private void OnImportSelectedAsset(object? sender, RoutedEventArgs e) => UseSelectedAsset();

    /// Double-click does the obvious thing per asset type: place a mesh, apply a material, open a
    /// scene.
    private void UseSelectedAsset()
    {
        if (AssetList.SelectedItem is not AssetItem asset) return;
        switch (asset.Kind)
        {
            case AssetKind.Mesh:
                ImportMeshAt(asset.Path);
                break;
            case AssetKind.Material:
                ApplyMaterial(asset);
                break;
            case AssetKind.Scene:
                LoadSceneFrom(asset.Path);
                break;
            default:
                Log($"{asset.Name}: textures are applied through materials, not directly");
                break;
        }
    }

    private void OnApplyMaterialToSelection(object? sender, RoutedEventArgs e)
    {
        if (AssetList.SelectedItem is AssetItem { Kind: AssetKind.Material } m) ApplyMaterial(m);
        else Log("Select a material in the browser first");
    }

    private void ApplyMaterial(AssetItem asset)
    {
        if (_runtime is not { IsAlive: true } || asset.Material is null) return;
        var sel = SelectedObjectIndex;
        if (sel < 0)
        {
            Log("Select an object to apply the material to");
            return;
        }

        _runtime.SetObjectMaterial((uint)sel, asset.Material.ToRuntime());
        LoadInspector(sel);
        Log($"Applied material '{asset.Material.Name}' to #{sel}");
    }

    // ── Content browser: folders and asset context menu ──

    /// Folder currently selected in the browser tree, or the materials folder as a sane default.
    private string CurrentAssetFolder =>
        FolderTree.SelectedItem is SceneNode { Detail.Length: > 0 } n ? n.Detail
        : _materials?.RootDirectory ?? Path.Combine(ProjectRoot, "Assets");

    private void OnAssetNewFolder(object? sender, RoutedEventArgs e)
    {
        var parent = CurrentAssetFolder;
        BeginInlineRename("New Folder", name =>
        {
            try
            {
                var path = Path.Combine(parent, name);
                Directory.CreateDirectory(path);
                RescanAssets();
                Log($"Created folder {path}");
            }
            catch (Exception ex)
            {
                Log($"Could not create folder: {ex.Message}");
            }
        });
    }

    private void OnAssetRenameFolder(object? sender, RoutedEventArgs e)
    {
        if (FolderTree.SelectedItem is not SceneNode { Detail.Length: > 0 } node) return;
        var dir = node.Detail;

        BeginInlineRename(Path.GetFileName(dir), name =>
        {
            try
            {
                var target = Path.Combine(Path.GetDirectoryName(dir)!, name);
                Directory.Move(dir, target);
                RescanAssets();
                Log($"Renamed folder to {name}");
            }
            catch (Exception ex)
            {
                Log($"Could not rename folder: {ex.Message}");
            }
        });
    }

    private void OnAssetDeleteFolder(object? sender, RoutedEventArgs e)
    {
        if (FolderTree.SelectedItem is not SceneNode { Detail.Length: > 0 } node) return;

        try
        {
            // Only remove empty folders: deleting assets from a context menu with no confirmation
            // is how people lose work.
            if (Directory.EnumerateFileSystemEntries(node.Detail).Any())
            {
                Log($"'{node.Name}' is not empty — delete its contents first");
                return;
            }
            Directory.Delete(node.Detail);
            RescanAssets();
            Log($"Deleted folder {node.Name}");
        }
        catch (Exception ex)
        {
            Log($"Could not delete folder: {ex.Message}");
        }
    }

    private void OnAssetReveal(object? sender, RoutedEventArgs e)
    {
        if (FolderTree.SelectedItem is SceneNode { Detail.Length: > 0 } node) RevealInExplorer(node.Detail);
    }

    private void OnAssetRevealFile(object? sender, RoutedEventArgs e)
    {
        if (AssetList.SelectedItem is AssetItem a) RevealInExplorer(a.Path);
    }

    private void RevealInExplorer(string path)
    {
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = Directory.Exists(path) ? $"\"{path}\"" : $"/select,\"{path}\"",
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            Log($"Could not open Explorer: {ex.Message}");
        }
    }

    private void OnDuplicateMaterial(object? sender, RoutedEventArgs e)
    {
        if (_materials is null || AssetList.SelectedItem is not AssetItem { Material: { } src }) return;

        var copy = _materials.Create(src.Name + "_copy");
        copy.R = src.R; copy.G = src.G; copy.B = src.B;
        copy.Metallic = src.Metallic;
        copy.Roughness = src.Roughness;
        copy.EmissiveR = src.EmissiveR; copy.EmissiveG = src.EmissiveG; copy.EmissiveB = src.EmissiveB;
        _materials.Save(copy);

        RescanAssets();
        Log($"Duplicated material as '{copy.Name}'");
    }

    private void OnRenameMaterial(object? sender, RoutedEventArgs e)
    {
        if (_materials is null || AssetList.SelectedItem is not AssetItem { Material: { } mat }) return;
        var oldPath = mat.Path;

        BeginInlineRename(mat.Name, name =>
        {
            try
            {
                mat.Name = name;
                var target = Path.Combine(Path.GetDirectoryName(oldPath!)!, name + ".tmat");
                mat.Path = target;
                _materials.Save(mat);
                if (oldPath is not null && oldPath != target && File.Exists(oldPath)) File.Delete(oldPath);
                RescanAssets();
                Log($"Renamed material to '{name}'");
            }
            catch (Exception ex)
            {
                Log($"Could not rename material: {ex.Message}");
            }
        });
    }

    private void OnDeleteMaterial(object? sender, RoutedEventArgs e)
    {
        if (_materials is null || AssetList.SelectedItem is not AssetItem { Material: { } mat }) return;
        try
        {
            _materials.Delete(mat);
            RescanAssets();
            Log($"Deleted material '{mat.Name}'");
        }
        catch (Exception ex)
        {
            Log($"Could not delete material: {ex.Message}");
        }
    }

    private void OnNewMaterial(object? sender, RoutedEventArgs e)
    {
        if (_materials is null || _runtime is not { IsAlive: true }) return;

        var created = _materials.Create("Material");
        // Seed from the selection when there is one, so "new material" captures what you see.
        var sel = SelectedObjectIndex;
        if (sel >= 0)
        {
            var seeded = MaterialAsset.FromRuntime(created.Name, _runtime.GetObjectMaterial((uint)sel));
            seeded.Path = created.Path;
            _materials.Materials.Remove(created);
            _materials.Materials.Add(seeded);
            _materials.Save(seeded);
        }

        RescanAssets();
        Log($"Created material asset '{created.Name}'");
    }

    private void OnSaveMaterialAsset(object? sender, RoutedEventArgs e)
    {
        if (_materials is null || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0)
        {
            Log("Select an object first");
            return;
        }

        var baseName = (_runtime.GetObjectName((uint)sel) ?? "Material") + "_mat";
        var created = _materials.Create(baseName);
        var asset = MaterialAsset.FromRuntime(created.Name, _runtime.GetObjectMaterial((uint)sel));
        asset.Path = created.Path;
        _materials.Materials.Remove(created);
        _materials.Materials.Add(asset);
        _materials.Save(asset);

        RescanAssets();
        Log($"Saved material asset '{asset.Name}'");
    }

    private async void OnImportMesh(object? sender, RoutedEventArgs e)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Import mesh",
            AllowMultiple = false,
            FileTypeFilter = new[] { new FilePickerFileType("glTF mesh") { Patterns = new[] { "*.gltf", "*.glb" } } }
        });
        if (files.Count == 0 || files[0].TryGetLocalPath() is not { } path) return;
        ImportMeshAt(path);
    }

    private void ImportMeshAt(string path)
    {
        if (_runtime is not { IsAlive: true }) return;

        var eye = _runtime.GetCameraPosition();
        var fwd = _runtime.GetCameraForward();
        const float distance = 10f;
        var before = _runtime.ObjectCount;
        var index = _runtime.ImportMesh(path,
            eye.X + fwd.X * distance, eye.Y + fwd.Y * distance, eye.Z + fwd.Z * distance, 1f);

        if (index == RuntimeHost.InvalidObject)
        {
            Log($"Could not import {Path.GetFileName(path)}");
            return;
        }

        // An import can add several objects; undo removes them from the tail inwards.
        var added = _runtime.ObjectCount - before;
        var rt = _runtime;
        _undo.Push(new UndoAction($"Import {Path.GetFileName(path)}",
            undo: () => { for (uint k = 0; k < added; ++k) rt.RemoveObject(rt.ObjectCount - 1); },
            redo: () => rt.ImportMesh(path, eye.X + fwd.X * distance, eye.Y + fwd.Y * distance,
                                      eye.Z + fwd.Z * distance, 1f)));

        RefreshOutliner();
        SelectObject((int)index);
        Log($"Imported {Path.GetFileName(path)} — {added} object(s)");
    }

    // ── Lights ────────────────────────────────────────

    private void OnAddPointLight(object? sender, RoutedEventArgs e) => AddLight(TucanoLightType.Point);
    private void OnAddSpotLight(object? sender, RoutedEventArgs e) => AddLight(TucanoLightType.Spot);
    private void OnAddDirectionalLight(object? sender, RoutedEventArgs e) => AddLight(TucanoLightType.Directional);

    private void AddLight(TucanoLightType type)
    {
        if (_runtime is not { IsAlive: true }) return;

        var eye = _runtime.GetCameraPosition();
        var fwd = _runtime.GetCameraForward();
        var light = new TucanoLight
        {
            Type = (int)type,
            CastShadows = 1,
            Position = new TucanoVec3(eye.X + fwd.X * 6f, eye.Y + fwd.Y * 6f, eye.Z + fwd.Z * 6f),
            Direction = new TucanoVec3(fwd.X, fwd.Y, fwd.Z),
            Color = new TucanoVec3(1f, 0.95f, 0.85f),
            Intensity = type == TucanoLightType.Directional ? 4f : 15f,
            Range = 25f,
            InnerCone = type == TucanoLightType.Spot ? 0.3f : 0f,
            OuterCone = type == TucanoLightType.Spot ? 0.6f : 0f,
        };

        var index = _runtime.AddLight(light);
        if (index == RuntimeHost.InvalidObject) return;

        var rt = _runtime;
        _undo.Push(new UndoAction($"Add {type} light",
            undo: () => rt.RemoveLight(index),
            redo: () => rt.AddLight(light)));

        RefreshOutliner();
        Log($"Added {type} light — {_runtime.LightCount} lights");
    }

    // ── Play mode ─────────────────────────────────────

    private void OnPlay(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        if (!_runtime.PlayStart())
        {
            Log("Could not start play mode (physics failed to initialise)");
            return;
        }
        var failed = _runtime.FailedColliderCount;
        Log(failed > 0
            ? $"Playing — {_runtime.ColliderCount} colliders ({failed} failed to build)"
            : $"Playing — {_runtime.ColliderCount} colliders");
        UpdatePlayUi();
    }

    private void OnPause(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true } || _runtime.PlayState == TucanoPlayState.Stopped) return;
        _runtime.PlayPause(_runtime.PlayState == TucanoPlayState.Running);
        UpdatePlayUi();
    }

    private void OnStop(object? sender, RoutedEventArgs e)
    {
        StopIfPlaying();
        Log("Stopped — transforms restored");
    }

    private void StopIfPlaying()
    {
        if (_runtime is { IsAlive: true, PlayState: not TucanoPlayState.Stopped })
        {
            _runtime.PlayStop();
            UpdatePlayUi();
            RefreshOutliner();
        }
    }

    private void UpdatePlayUi()
    {
        if (_runtime is null) return;
        var state = _runtime.PlayState;
        PlayStatus.Text = state switch
        {
            TucanoPlayState.Running => "● PLAYING",
            TucanoPlayState.Paused => "❚❚ PAUSED",
            _ => ""
        };
        PlayButton.IsEnabled = state != TucanoPlayState.Running;
        PauseButton.IsEnabled = state != TucanoPlayState.Stopped;
        StopButton.IsEnabled = state != TucanoPlayState.Stopped;
    }

    // ── Environment window ────────────────────────────

    private void OnOpenEnvironment(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;

        if (_environmentWindow is { } win && win.IsVisible)
        {
            win.Activate();
            return;
        }

        _environmentWindow = new EnvironmentWindow(_runtime);
        _environmentWindow.Closed += (_, _) => _environmentWindow = null;
        _environmentWindow.Show(this);
    }

    // ── Gizmo ─────────────────────────────────────────

    private void OnGizmoOpChanged(object? sender, RoutedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        _runtime.GizmoOperation =
            GizmoRotate.IsChecked == true ? TucanoGizmoOp.Rotate :
            GizmoScale.IsChecked == true ? TucanoGizmoOp.Scale :
            TucanoGizmoOp.Translate;
    }

    private void OnGizmoSpaceChanged(object? sender, RoutedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        _runtime.GizmoWorldSpace = GizmoWorld.IsChecked == true;
    }

    private void OnGizmoSnapChanged(object? sender, NumericUpDownValueChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        _runtime.GizmoSnap = (float)(GizmoSnap.Value ?? 0m);
    }

    /// The viewport's own W/E/R/X shortcuts change the mode behind the toolbar's back.
    private void SyncGizmoToolbar()
    {
        if (_runtime is not { IsAlive: true }) return;
        var op = _runtime.GizmoOperation;
        var world = _runtime.GizmoWorldSpace;
        if (op == _lastGizmoOp && world == _lastGizmoWorld) return;

        _lastGizmoOp = op;
        _lastGizmoWorld = world;
        _syncingUi = true;
        try
        {
            GizmoMove.IsChecked = op == TucanoGizmoOp.Translate;
            GizmoRotate.IsChecked = op == TucanoGizmoOp.Rotate;
            GizmoScale.IsChecked = op == TucanoGizmoOp.Scale;
            GizmoWorld.IsChecked = world;
        }
        finally
        {
            _syncingUi = false;
        }
    }

    private TucanoGizmoOp _lastGizmoOp = TucanoGizmoOp.Translate;
    private bool _lastGizmoWorld = true;

    // ── Selection ↔ inspector plumbing ────────────────

    // Guards the two-way binding: the runtime owns the selection (the viewport can change it by
    // picking), and writing back into the controls must not re-trigger their edit handlers.
    private bool _syncingUi;
    private int _lastSelected = -1;

    private void OnOutlinerSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        if (OutlinerTree.SelectedItem is not SceneNode node) return;

        if (node.Kind == SceneNodeKind.Light)
        {
            // Lights are a separate list in the engine, so clear the mesh selection to avoid the
            // inspector showing a stale object alongside the light.
            _selectedLight = node.LightIndex;
            _runtime.Selected = -1;
            _lastSelected = -1;
            LoadLightInspector(node.LightIndex);
            return;
        }

        _selectedLight = -1;
        _runtime.Selected = node.ObjectIndex;
        _lastSelected = node.ObjectIndex;
        LoadInspector(node.ObjectIndex);
    }

    /// Object index currently selected in the outliner, or -1.
    private int SelectedObjectIndex =>
        OutlinerTree.SelectedItem is SceneNode { Kind: not SceneNodeKind.Light } n ? n.ObjectIndex : -1;

    /// Selects the tree row that maps to a runtime object index.
    private void SelectObject(int objectIndex)
    {
        var node = FindNode(n => n.ObjectIndex == objectIndex && n.Kind != SceneNodeKind.Light);
        _syncingUi = true;
        try
        {
            OutlinerTree.SelectedItem = node;
        }
        finally
        {
            _syncingUi = false;
        }

        _selectedLight = -1;
        _lastSelected = objectIndex;
        if (_runtime is { IsAlive: true }) _runtime.Selected = objectIndex;
        LoadInspector(objectIndex);
    }

    // Recursive: the tree is Scene ▸ Objects ▸ item, so a two-level walk silently found nothing and
    // every selection-by-index quietly failed.
    private SceneNode? FindNode(Func<SceneNode, bool> predicate)
    {
        static SceneNode? Search(IEnumerable<SceneNode> nodes, Func<SceneNode, bool> match)
        {
            foreach (var node in nodes)
            {
                if (match(node)) return node;
                var hit = Search(node.Children, match);
                if (hit is not null) return hit;
            }
            return null;
        }
        return Search(_outlinerRoots, predicate);
    }

    /// Polls the runtime for a viewport-driven pick and mirrors it into the outliner + inspector.
    private void SyncSelectionFromRuntime()
    {
        if (_runtime is not { IsAlive: true }) return;
        var sel = _runtime.Selected;
        if (sel == _lastSelected) return;

        _lastSelected = sel;
        _selectedLight = -1;
        _syncingUi = true;
        try
        {
            OutlinerTree.SelectedItem = FindNode(n => n.ObjectIndex == sel && n.Kind != SceneNodeKind.Light);
        }
        finally
        {
            _syncingUi = false;
        }
        LoadInspector(sel);
    }

    private void LoadInspector(int index)
    {
        if (_runtime is not { IsAlive: true }) return;

        _syncingUi = true;
        try
        {
            CompLight.IsVisible = false;

            if (index < 0 || index >= (int)_runtime.ObjectCount)
            {
                InspectorNameBox.Text = "";
                InspectorSub.Text = "Select something in the viewport or outliner";
                CompTransform.IsVisible = false;
                CompMaterial.IsVisible = false;
                CompPhysics.IsVisible = false;
                AddComponentButton.IsVisible = false;
                return;
            }

            CompTransform.IsVisible = true;
            CompMaterial.IsVisible = true;
            AddComponentButton.IsVisible = true;

            var i = (uint)index;
            var (pos, euler, scale) = _runtime.GetObjectTransform(i);
            var mat = _runtime.GetObjectMaterial(i);
            var phys = _runtime.GetObjectPhysics(i);
            const double toDeg = 180.0 / Math.PI;

            var name = _runtime.GetObjectName(i) ?? $"Object {index}";
            InspectorNameBox.Text = name;
            InspectorSub.Text = $"index {index}  ·  {KindFromName(name)}";
            InspectorIcon.Data = IconFor(KindFromName(name));

            PosX.Value = (decimal)pos.X; PosY.Value = (decimal)pos.Y; PosZ.Value = (decimal)pos.Z;
            RotX.Value = (decimal)(euler.X * toDeg);
            RotY.Value = (decimal)(euler.Y * toDeg);
            RotZ.Value = (decimal)(euler.Z * toDeg);
            ScaleX.Value = (decimal)scale.X; ScaleY.Value = (decimal)scale.Y; ScaleZ.Value = (decimal)scale.Z;

            ColR.Value = (decimal)mat.BaseColor.X;
            ColG.Value = (decimal)mat.BaseColor.Y;
            ColB.Value = (decimal)mat.BaseColor.Z;
            MatMetallic.Value = mat.Metallic;
            MatRoughness.Value = Math.Clamp(mat.Roughness, 0.02, 1.0);
            MatEmissive.Value = Math.Clamp(mat.Emissive.X, 0, 5);
            UpdateSwatch(mat.BaseColor.X, mat.BaseColor.Y, mat.BaseColor.Z);

            // Physics is an optional component: only shown once the object actually has a body.
            CompPhysics.IsVisible = phys != TucanoPhysicsKind.None;
            PhysKind.SelectedIndex = (int)phys;
            PhysMass.Value = (decimal)_runtime.GetObjectMass(i);
        }
        finally
        {
            _syncingUi = false;
        }
    }

    /// Fills the inspector for a light instead of a mesh.
    private void LoadLightInspector(int lightIndex)
    {
        if (_runtime is not { IsAlive: true } || lightIndex < 0) return;

        _syncingUi = true;
        try
        {
            var l = _runtime.GetLight((uint)lightIndex);
            var type = (TucanoLightType)l.Type;

            InspectorNameBox.Text = $"{type} light {lightIndex}";
            InspectorSub.Text = $"light index {lightIndex}";
            InspectorIcon.Data = IconFor(SceneNodeKind.Light);

            CompTransform.IsVisible = false;
            CompMaterial.IsVisible = false;
            CompPhysics.IsVisible = false;
            AddComponentButton.IsVisible = false;
            CompLight.IsVisible = true;

            LightType.SelectedIndex = Math.Clamp(l.Type, 0, 2);
            LightPosX.Value = (decimal)l.Position.X;
            LightPosY.Value = (decimal)l.Position.Y;
            LightPosZ.Value = (decimal)l.Position.Z;
            LightR.Value = (decimal)Math.Clamp(l.Color.X, 0f, 1f);
            LightG.Value = (decimal)Math.Clamp(l.Color.Y, 0f, 1f);
            LightB.Value = (decimal)Math.Clamp(l.Color.Z, 0f, 1f);
            LightIntensity.Value = Math.Clamp(l.Intensity, 0, 60);
            LightRange.Value = Math.Clamp(l.Range, 0.5, 200);
            LightShadows.IsChecked = l.CastShadows != 0;
        }
        finally
        {
            _syncingUi = false;
        }
    }

    private static Geometry? IconFor(SceneNodeKind kind)
    {
        var key = new SceneNode { Kind = kind }.IconKey;
        return Application.Current?.Resources.TryGetResource(key, null, out var g) == true
            ? g as Geometry
            : null;
    }

    private void UpdateSwatch(float r, float g, float b)
    {
        MatSwatch.Background = new SolidColorBrush(Color.FromRgb(
            (byte)Math.Clamp(r * 255f, 0f, 255f),
            (byte)Math.Clamp(g * 255f, 0f, 255f),
            (byte)Math.Clamp(b * 255f, 0f, 255f)));
    }

    // ── Rename ────────────────────────────────────────

    private void OnRenameKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        CommitRename();
        e.Handled = true;
    }

    private void OnRenameCommit(object? sender, RoutedEventArgs e) => CommitRename();

    private void CommitRename()
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0) return;

        var name = (InspectorNameBox.Text ?? "").Trim();
        if (name.Length == 0 || name == _runtime.GetObjectName((uint)sel)) return;

        _runtime.SetObjectName((uint)sel, name);
        RefreshOutliner();
        Log($"Renamed #{sel} to '{name}'");
    }

    // ── Components ────────────────────────────────────

    private void OnAddComponent(object? sender, RoutedEventArgs e)
    {
        // The button opens its flyout; nothing to do here beyond guarding an empty selection.
        if (SelectedObjectIndex < 0) Log("Select an object first");
    }

    private void OnAddPhysicsComponent(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0) { Log("Select an object first"); return; }

        _runtime.SetObjectPhysics((uint)sel, TucanoPhysicsKind.Dynamic, 1f);
        LoadInspector(sel);
        RefreshOutliner();
        Log($"Added physics body to #{sel}");
    }

    private void OnRemovePhysicsComponent(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0) return;

        _runtime.SetObjectPhysics((uint)sel, TucanoPhysicsKind.None);
        LoadInspector(sel);
        RefreshOutliner();
        Log($"Removed physics body from #{sel}");
    }

    private void OnResetTransform(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0) return;

        _runtime.SetObjectTransform((uint)sel,
            new TucanoVec3(0, 0, 0), new TucanoVec3(0, 0, 0), new TucanoVec3(1, 1, 1));
        LoadInspector(sel);
        Log($"Reset transform of #{sel}");
    }

    // ── Light editing ─────────────────────────────────

    private void OnLightEdited(object? sender, RoutedEventArgs e) => ApplyLight();
    private void OnLightEdited(object? sender, SelectionChangedEventArgs e) => ApplyLight();
    private void OnLightNumberEdited(object? sender, NumericUpDownValueChangedEventArgs e) => ApplyLight();
    private void OnLightEditedSlider(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e) => ApplyLight();

    private void ApplyLight()
    {
        if (_syncingUi || _runtime is not { IsAlive: true } || _selectedLight < 0) return;

        var l = _runtime.GetLight((uint)_selectedLight);
        l.Type = Math.Clamp(LightType.SelectedIndex, 0, 2);
        l.Position = new TucanoVec3((float)(LightPosX.Value ?? 0m), (float)(LightPosY.Value ?? 0m),
                                    (float)(LightPosZ.Value ?? 0m));
        l.Color = new TucanoVec3((float)(LightR.Value ?? 1m), (float)(LightG.Value ?? 1m),
                                 (float)(LightB.Value ?? 1m));
        l.Intensity = (float)LightIntensity.Value;
        l.Range = (float)LightRange.Value;
        l.CastShadows = LightShadows.IsChecked == true ? 1 : 0;
        _runtime.SetLight((uint)_selectedLight, l);
    }

    private void OnDeleteLight(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true } || _selectedLight < 0) return;
        var idx = _selectedLight;
        _runtime.RemoveLight((uint)idx);
        _selectedLight = -1;
        _undo.Clear(); // light indices shift; a stale undo would target the wrong light
        RefreshOutliner();
        Log($"Deleted light {idx}");
    }

    private void OnTransformEdited(object? sender, NumericUpDownValueChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        const double toRad = Math.PI / 180.0;
        static float V(NumericUpDown c, float fallback) => (float)(c.Value ?? (decimal)fallback);

        _runtime.SetObjectTransform(
            (uint)sel,
            new TucanoVec3(V(PosX, 0), V(PosY, 0), V(PosZ, 0)),
            new TucanoVec3((float)(V(RotX, 0) * toRad), (float)(V(RotY, 0) * toRad), (float)(V(RotZ, 0) * toRad)),
            new TucanoVec3(V(ScaleX, 1), V(ScaleY, 1), V(ScaleZ, 1)));
    }

    private void OnColorEdited(object? sender, NumericUpDownValueChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        static float V(NumericUpDown c) => (float)(c.Value ?? 1m);
        _runtime.SetObjectColor((uint)sel, V(ColR), V(ColG), V(ColB));
    }

    private void OnMaterialEdited(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        var mat = _runtime.GetObjectMaterial((uint)sel);
        mat.Metallic = (float)MatMetallic.Value;
        mat.Roughness = (float)MatRoughness.Value;
        _runtime.SetObjectMaterial((uint)sel, mat);
    }

    private void OnPhysicsChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        var kind = (TucanoPhysicsKind)Math.Clamp(PhysKind.SelectedIndex, 0, 2);
        _runtime.SetObjectPhysics((uint)sel, kind, (float)(PhysMass.Value ?? 1m));
        Log($"#{sel} collider: {kind}");
    }

    private void OnPhysicsMassChanged(object? sender, NumericUpDownValueChangedEventArgs e)
    {
        if (_syncingUi || _runtime is not { IsAlive: true }) return;
        var sel = SelectedObjectIndex;
        if (sel < 0 || sel >= (int)_runtime.ObjectCount) return;

        _runtime.SetObjectPhysics((uint)sel,
            (TucanoPhysicsKind)Math.Clamp(PhysKind.SelectedIndex, 0, 2),
            (float)(PhysMass.Value ?? 1m));
    }

    // Folders exist only in the editor: the engine stores a path string per object, and the tree is
    // rebuilt from those. Keeps empty folders alive between rebuilds.
    private readonly HashSet<string> _knownFolders = new();

    // Rebuilds the outliner tree from the scene. Rebuilding drops the tree selection as a side
    // effect, so the runtime's selection is what gets restored afterwards, not the widget's.
    private void RefreshOutliner()
    {
        if (_runtime is null) return;
        var selected = _runtime.Selected;

        _syncingUi = true;
        try
        {
            _outlinerRoots.Clear();

            var root = new SceneNode { Name = "Scene", Kind = SceneNodeKind.Root, FolderPath = "" };
            var folders = new Dictionary<string, SceneNode>(StringComparer.OrdinalIgnoreCase);

            // Materialise every known folder first so empty ones still show and can be dropped into.
            foreach (var path in _knownFolders.OrderBy(f => f))
            {
                EnsureFolder(root, folders, path);
            }

            var count = _runtime.ObjectCount;
            for (uint i = 0; i < count; ++i)
            {
                var name = _runtime.GetObjectName(i) ?? $"Object {i}";
                if (!MatchesFilter(name)) continue;

                var folder = _runtime.GetObjectFolder(i);
                if (!string.IsNullOrEmpty(folder)) _knownFolders.Add(folder);

                var phys = _runtime.GetObjectPhysics(i);
                var node = new SceneNode
                {
                    Name = name,
                    Detail = phys switch
                    {
                        TucanoPhysicsKind.Static => "static",
                        TucanoPhysicsKind.Dynamic => "dynamic",
                        _ => "",
                    },
                    Kind = KindFromName(name),
                    ObjectIndex = (int)i,
                    FolderPath = folder,
                    IsVisible = _runtime.GetObjectVisible(i),
                };

                var parent = string.IsNullOrEmpty(folder) ? root : EnsureFolder(root, folders, folder);
                parent.Children.Add(node);
            }

            var lightCount = _runtime.LightCount;
            if (lightCount > 0)
            {
                var lights = new SceneNode { Name = "Lights", Kind = SceneNodeKind.LightGroup };
                for (uint i = 0; i < lightCount; ++i)
                {
                    var l = _runtime.GetLight(i);
                    var type = (TucanoLightType)l.Type;
                    var name = $"{type} light {i}";
                    if (!MatchesFilter(name)) continue;

                    lights.Children.Add(new SceneNode
                    {
                        Name = name,
                        Detail = $"{l.Intensity:F0}",
                        Kind = SceneNodeKind.Light,
                        LightIndex = (int)i,
                    });
                }
                lights.Detail = $"{lights.Children.Count}";
                root.Children.Add(lights);
            }

            _outlinerRoots.Add(root);
            OutlinerCount.Text = $"{count} obj · {lightCount} lights";
            OutlinerTree.SelectedItem =
                FindNode(n => n.ObjectIndex == selected && n.Kind != SceneNodeKind.Light);
        }
        finally
        {
            _syncingUi = false;
        }

        _lastSelected = selected;
        LoadInspector(selected);
    }

    /// Creates (or finds) the node chain for "A/B/C" so nested folders work.
    private static SceneNode EnsureFolder(SceneNode root, Dictionary<string, SceneNode> cache, string path)
    {
        if (cache.TryGetValue(path, out var hit)) return hit;

        var parent = root;
        var accumulated = "";
        foreach (var part in path.Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            accumulated = accumulated.Length == 0 ? part : accumulated + "/" + part;
            if (!cache.TryGetValue(accumulated, out var node))
            {
                node = new SceneNode { Name = part, Kind = SceneNodeKind.Folder, FolderPath = accumulated };
                cache[accumulated] = node;
                parent.Children.Add(node);
            }
            parent = node;
        }
        return parent;
    }

    private bool MatchesFilter(string name) =>
        _outlinerFilter.Length == 0 ||
        name.Contains(_outlinerFilter, StringComparison.OrdinalIgnoreCase);

    // The engine doesn't report which primitive an object came from, so the spawn naming convention
    // is what drives the icon. Imported meshes fall through to the generic mesh icon.
    private static SceneNodeKind KindFromName(string name) =>
        name.StartsWith("Cube", StringComparison.OrdinalIgnoreCase) ||
        name.StartsWith("Box", StringComparison.OrdinalIgnoreCase) ? SceneNodeKind.Cube :
        name.StartsWith("Sphere", StringComparison.OrdinalIgnoreCase) ||
        name.StartsWith("Marker", StringComparison.OrdinalIgnoreCase) ? SceneNodeKind.Sphere :
        name.StartsWith("Plane", StringComparison.OrdinalIgnoreCase) ||
        name.StartsWith("Ground", StringComparison.OrdinalIgnoreCase) ? SceneNodeKind.Plane :
        SceneNodeKind.Mesh;

    // ── Outliner context menu ─────────────────────────

    private SceneNode? SelectedNode => OutlinerTree.SelectedItem as SceneNode;

    private void OnCtxRename(object? sender, RoutedEventArgs e)
    {
        if (SelectedNode is not { } node) return;

        if (node.IsFolder)
        {
            BeginInlineRename(node.Name, newName =>
            {
                RenameFolder(node.FolderPath, newName);
                RefreshOutliner();
                Log($"Renamed folder to '{newName}'");
            });
            return;
        }

        if (node.ObjectIndex < 0) return;
        // Objects rename through the inspector field, which is already wired up.
        InspectorNameBox.Focus();
        InspectorNameBox.SelectAll();
    }

    private void OnCtxDelete(object? sender, RoutedEventArgs e)
    {
        if (SelectedNode is not { } node) return;

        if (node.IsFolder)
        {
            // Deleting a folder keeps the objects: they move up to the scene root. Losing geometry
            // to a mis-click on a container would be far worse than an extra step.
            if (_runtime is { IsAlive: true })
            {
                for (uint i = 0; i < _runtime.ObjectCount; ++i)
                {
                    var f = _runtime.GetObjectFolder(i);
                    if (f == node.FolderPath || f.StartsWith(node.FolderPath + "/", StringComparison.OrdinalIgnoreCase))
                    {
                        _runtime.SetObjectFolder(i, "");
                    }
                }
            }
            _knownFolders.RemoveWhere(f =>
                f == node.FolderPath || f.StartsWith(node.FolderPath + "/", StringComparison.OrdinalIgnoreCase));
            RefreshOutliner();
            Log($"Removed folder '{node.Name}' — its objects moved to the scene root");
            return;
        }

        if (node.Kind == SceneNodeKind.Light)
        {
            _selectedLight = node.LightIndex;
            OnDeleteLight(sender, e);
            return;
        }

        OnDeleteSelected(sender, e);
    }

    private void OnCtxToggleVisibility(object? sender, RoutedEventArgs e)
    {
        if (SelectedNode is { } node) ToggleVisibility(node);
    }

    private void OnToggleNodeVisibility(object? sender, RoutedEventArgs e)
    {
        if (sender is Button { DataContext: SceneNode node }) ToggleVisibility(node);
    }

    private void ToggleVisibility(SceneNode node)
    {
        if (_runtime is not { IsAlive: true }) return;

        var target = !node.IsVisible;
        if (node.IsFolder)
        {
            // A folder's eye drives everything under it, recursively.
            ApplyVisibilityRecursive(node, target);
            node.IsVisible = target;
            Log($"{(target ? "Showed" : "Hid")} folder '{node.Name}'");
            return;
        }

        if (node.ObjectIndex < 0) return;
        _runtime.SetObjectVisible((uint)node.ObjectIndex, target);
        node.IsVisible = target;
        Log($"{(target ? "Showed" : "Hid")} {node.Name}");
    }

    private void ApplyVisibilityRecursive(SceneNode node, bool visible)
    {
        foreach (var child in node.Children)
        {
            if (child.ObjectIndex >= 0 && _runtime is { IsAlive: true })
            {
                _runtime.SetObjectVisible((uint)child.ObjectIndex, visible);
                child.IsVisible = visible;
            }
            ApplyVisibilityRecursive(child, visible);
        }
    }

    private void OnCtxNewFolder(object? sender, RoutedEventArgs e)
    {
        // Nest inside the selected folder when there is one.
        var parent = SelectedNode is { IsFolder: true } f ? f.FolderPath : "";
        BeginInlineRename("New Folder", name =>
        {
            var path = string.IsNullOrEmpty(parent) ? name : parent + "/" + name;
            _knownFolders.Add(path);
            RefreshOutliner();
            Log($"Created folder '{path}'");
        });
    }

    private void OnCtxMoveToRoot(object? sender, RoutedEventArgs e)
    {
        if (SelectedNode is not { ObjectIndex: >= 0 } node || _runtime is not { IsAlive: true }) return;
        _runtime.SetObjectFolder((uint)node.ObjectIndex, "");
        RefreshOutliner();
        Log($"Moved {node.Name} to the scene root");
    }

    private void OnCtxFocus(object? sender, RoutedEventArgs e)
    {
        if (SelectedNode is not { ObjectIndex: >= 0 } node || _runtime is not { IsAlive: true }) return;

        // Frame the object from a fixed offset — enough to see it and its footing.
        var (pos, _, scale) = _runtime.GetObjectTransform((uint)node.ObjectIndex);
        var radius = MathF.Max(MathF.Max(scale.X, scale.Y), scale.Z);
        var dist = MathF.Max(3f, radius * 3.5f);
        _runtime.SetCameraPosition(pos.X + dist * 0.6f, pos.Y + dist * 0.45f, pos.Z + dist);
        _runtime.CameraLookAt(pos.X, pos.Y, pos.Z);
        Log($"Focused {node.Name}");
    }

    private void RenameFolder(string oldPath, string newName)
    {
        if (_runtime is not { IsAlive: true } || string.IsNullOrWhiteSpace(newName)) return;

        var parentPath = oldPath.Contains('/') ? oldPath[..oldPath.LastIndexOf('/')] : "";
        var newPath = string.IsNullOrEmpty(parentPath) ? newName : parentPath + "/" + newName;

        for (uint i = 0; i < _runtime.ObjectCount; ++i)
        {
            var f = _runtime.GetObjectFolder(i);
            if (f == oldPath)
            {
                _runtime.SetObjectFolder(i, newPath);
            }
            else if (f.StartsWith(oldPath + "/", StringComparison.OrdinalIgnoreCase))
            {
                _runtime.SetObjectFolder(i, newPath + f[oldPath.Length..]);
            }
        }

        foreach (var f in _knownFolders.Where(f =>
                     f == oldPath || f.StartsWith(oldPath + "/", StringComparison.OrdinalIgnoreCase)).ToList())
        {
            _knownFolders.Remove(f);
            _knownFolders.Add(f == oldPath ? newPath : newPath + f[oldPath.Length..]);
        }
    }

    /// Simple inline prompt reusing the outliner filter box — avoids a modal dialog for a one-line
    /// answer, and Escape cancels.
    private Action<string>? _pendingRename;

    private void BeginInlineRename(string seed, Action<string> commit)
    {
        _pendingRename = commit;
        OutlinerFilter.Text = seed;
        OutlinerFilter.Watermark = "Type a name, press Enter";
        OutlinerFilter.Focus();
        OutlinerFilter.SelectAll();
    }

    private void EndInlineRename(bool commit)
    {
        var action = _pendingRename;
        var text = (OutlinerFilter.Text ?? "").Trim();
        _pendingRename = null;
        OutlinerFilter.Watermark = "Filter…";
        OutlinerFilter.Text = "";
        _outlinerFilter = "";
        if (commit && action is not null && text.Length > 0) action(text.Replace("/", "-"));
        else RefreshOutliner();
    }

    private void OnOutlinerFilterKeyDown(object? sender, KeyEventArgs e)
    {
        if (_pendingRename is null) return;
        if (e.Key == Key.Enter) { EndInlineRename(true); e.Handled = true; }
        else if (e.Key == Key.Escape) { EndInlineRename(false); e.Handled = true; }
    }

    // ── Drag and drop ─────────────────────────────────

    private SceneNode? _dragNode;

    private void OnOutlinerPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        // Remember what was under the cursor; the drag only starts once it actually moves.
        if (e.GetCurrentPoint(OutlinerTree).Properties.IsLeftButtonPressed)
        {
            _dragNode = (e.Source as Control)?.DataContext as SceneNode;
        }
    }

    private async void OnOutlinerPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_dragNode is null || !e.GetCurrentPoint(OutlinerTree).Properties.IsLeftButtonPressed) return;
        if (_dragNode.Kind is SceneNodeKind.Root or SceneNodeKind.LightGroup) return;

        var node = _dragNode;
        _dragNode = null; // one drag per press

        var data = new DataObject();
        data.Set("tucano/node", node);
        await DragDrop.DoDragDrop(e, data, DragDropEffects.Move);
    }

    private void OnOutlinerDragOver(object? sender, DragEventArgs e)
    {
        var target = (e.Source as Control)?.DataContext as SceneNode;
        var dragged = e.Data.Get("tucano/node") as SceneNode;

        // Only containers accept drops, and a folder can't be dropped inside itself.
        var ok = dragged is not null && target is not null && target.IsContainer &&
                 !(dragged.IsFolder && (target.FolderPath == dragged.FolderPath ||
                                        target.FolderPath.StartsWith(dragged.FolderPath + "/", StringComparison.OrdinalIgnoreCase)));

        e.DragEffects = ok ? DragDropEffects.Move : DragDropEffects.None;
        e.Handled = true;
    }

    private void OnOutlinerDrop(object? sender, DragEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;
        if (e.Data.Get("tucano/node") is not SceneNode dragged) return;
        if ((e.Source as Control)?.DataContext is not SceneNode target || !target.IsContainer) return;

        var destination = target.Kind == SceneNodeKind.Root ? "" : target.FolderPath;

        if (dragged.IsFolder)
        {
            if (destination.StartsWith(dragged.FolderPath, StringComparison.OrdinalIgnoreCase)) return;
            var name = dragged.Name;
            var newPath = string.IsNullOrEmpty(destination) ? name : destination + "/" + name;
            RenameFolder(dragged.FolderPath, name);
            // RenameFolder keeps the original parent; re-target explicitly for a move.
            MoveFolder(dragged.FolderPath, newPath);
            Log($"Moved folder '{name}' into '{(destination.Length == 0 ? "Scene" : destination)}'");
        }
        else if (dragged.ObjectIndex >= 0)
        {
            _runtime.SetObjectFolder((uint)dragged.ObjectIndex, destination);
            Log($"Moved {dragged.Name} into '{(destination.Length == 0 ? "Scene" : destination)}'");
        }

        RefreshOutliner();
        e.Handled = true;
    }

    private void MoveFolder(string oldPath, string newPath)
    {
        if (_runtime is not { IsAlive: true } || oldPath == newPath) return;

        for (uint i = 0; i < _runtime.ObjectCount; ++i)
        {
            var f = _runtime.GetObjectFolder(i);
            if (f == oldPath) _runtime.SetObjectFolder(i, newPath);
            else if (f.StartsWith(oldPath + "/", StringComparison.OrdinalIgnoreCase))
                _runtime.SetObjectFolder(i, newPath + f[oldPath.Length..]);
        }

        foreach (var f in _knownFolders.Where(f =>
                     f == oldPath || f.StartsWith(oldPath + "/", StringComparison.OrdinalIgnoreCase)).ToList())
        {
            _knownFolders.Remove(f);
            _knownFolders.Add(f == oldPath ? newPath : newPath + f[oldPath.Length..]);
        }
    }

    private void OnOutlinerFilterChanged(object? sender, TextChangedEventArgs e)
    {
        if (_pendingRename is not null) return; // the box is acting as a name prompt right now
        _outlinerFilter = OutlinerFilter.Text ?? "";
        RefreshOutliner();
    }

    private void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        _renderLoopRunning = false;
        _environmentWindow?.Close();
        _runtime?.Dispose();
        _runtime = null;
    }
}

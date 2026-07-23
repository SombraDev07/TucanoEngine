using Avalonia.Controls;
using EditorCore;
using TucanoEditor.Views;

namespace TucanoEditor.Extensibility;

/// Registers the Environment window as a tool panel.
///
/// The panel itself is an ordinary window; this adapter is all it takes to put it in the Tools
/// menu and have its lifetime managed. Future tools (Profiler, Audio Mixer, Animation Graph)
/// follow the same three lines.
public sealed class EnvironmentPanel : IEditorPanel
{
    public string Id => "environment";
    public string Title => "Environment";
    public string IconKey => "IcoSun";

    public Window CreateWindow(RuntimeHost runtime) => new EnvironmentWindow(runtime);

    public void OnSceneChanged(Window window, RuntimeHost runtime)
    {
        // A loaded scene carries its own environment; re-read it rather than showing the old one.
        if (window is EnvironmentWindow env) env.LoadFromRuntime();
    }
}

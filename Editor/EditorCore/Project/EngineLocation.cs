using System;
using System.IO;

namespace EditorCore;

/// Where the engine's own files live, as opposed to the user's project.
///
/// Two layouts have to work. In a development tree the editor binary sits several levels under the
/// engine source root, so the root is found by walking up. In an installed engine the editor ships
/// beside its content, so the root is the binary's own folder. Checking the installed layout first
/// keeps a developer's source tree from being picked up by an installed copy that happens to be
/// running inside one.
///
/// Known gap: the native side still bakes `TUCANO_ENGINE_ASSETS_DIR` at compile time from
/// `CMAKE_SOURCE_DIR` (src/CMakeLists.txt), so an installed engine would resolve engine assets to
/// a path that only exists on the build machine. Fixing that belongs with the install target, not
/// here — this class is the managed half of the same separation.
public static class EngineLocation
{
    /// Directory that contains the engine's content folders, or null if neither layout matched.
    public static string? RootDirectory { get; } = FindRoot();

    /// Engine-shipped content (IBL probes, built-in meshes). Read-only from the editor's side.
    public static string? AssetsDirectory => Combine("EngineAssets");

    /// Compiled and source shaders that ship with the engine.
    public static string? ShaderDirectory => Combine("Shaders");

    /// True when the engine was found beside the running binary rather than up a source tree.
    public static bool IsInstalledLayout { get; private set; }

    private static string? Combine(string folder)
    {
        if (RootDirectory is null) return null;
        var path = Path.Combine(RootDirectory, folder);
        return Directory.Exists(path) ? path : null;
    }

    private static string? FindRoot()
    {
        var baseDir = AppContext.BaseDirectory;

        // Installed layout: content sits next to the executable.
        if (Directory.Exists(Path.Combine(baseDir, "EngineAssets")))
        {
            IsInstalledLayout = true;
            return Path.GetFullPath(baseDir);
        }

        // Development layout: walk up to the source root. `EngineAssets` is the marker rather than
        // `Assets`, because `Assets` is the thing that now belongs to a project and would match the
        // wrong folder.
        var dir = new DirectoryInfo(baseDir);
        while (dir is not null)
        {
            if (Directory.Exists(Path.Combine(dir.FullName, "EngineAssets")))
            {
                IsInstalledLayout = false;
                return dir.FullName;
            }
            dir = dir.Parent;
        }

        return null;
    }
}

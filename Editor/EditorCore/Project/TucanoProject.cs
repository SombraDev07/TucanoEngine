using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;

namespace EditorCore;

/// A user project: the directory that owns the content someone authors, kept separate from the
/// engine that opens it.
///
/// Before this existed the editor located content by walking up from its own executable until it
/// found a folder named `Assets`, which silently assumed the editor was running from inside the
/// engine's source tree. That works for exactly one project — the engine's own — and is why every
/// sample had to be a target inside the engine's CMake build. Anything the engine ships with is
/// reached through <see cref="EngineLocation"/> instead; anything the user made is reached through
/// a project.
public sealed class TucanoProject
{
    public const string Extension = ".tuproject";

    /// Bumped when the on-disk shape changes in a way older editors can't read. Distinct from
    /// <see cref="EngineApiVersion"/>, which tracks the native ABI.
    public const int CurrentFormatVersion = 1;

    private TucanoProject(string filePath, string name, int formatVersion)
    {
        FilePath = Path.GetFullPath(filePath);
        RootDirectory = Path.GetDirectoryName(FilePath)
                        ?? throw new ArgumentException($"Project path has no directory: {filePath}");
        Name = name;
        FormatVersion = formatVersion;
    }

    /// Absolute path of the `.tuproject` file.
    public string FilePath { get; }

    /// Directory containing the project file. Every project path is resolved from here, so a
    /// project folder can be moved or renamed without editing anything inside it.
    public string RootDirectory { get; }

    public string Name { get; set; }

    /// Format version as read from disk. A project written by a newer editor is refused rather
    /// than half-read.
    public int FormatVersion { get; }

    /// Native ABI version this project was last opened with, or 0 when never recorded. Compared
    /// against the running engine on open so a mismatch is reported instead of crashing somewhere
    /// deeper. See <see cref="CompatibilityWith"/>.
    public int EngineApiVersion { get; set; }

    /// Scene to load when the project opens, relative to <see cref="RootDirectory"/>. Null means
    /// open empty.
    public string? StartupScene { get; set; }

    // ── Well-known directories ────────────────────────────
    // Derived rather than stored: a project whose file says its assets live somewhere the folder
    // doesn't have is a broken state that can't be repaired from inside the editor.

    public string AssetsDirectory => Path.Combine(RootDirectory, "Assets");
    public string MaterialsDirectory => Path.Combine(AssetsDirectory, "Materials");
    public string ScenesDirectory => Path.Combine(AssetsDirectory, "Scenes");
    public string ImportedDirectory => Path.Combine(AssetsDirectory, "Imported");

    /// Content roots to scan, project first. The engine's own assets are included so built-in
    /// materials and meshes stay reachable, but they are not part of the project and are never
    /// written to.
    public IEnumerable<string> ContentRoots
    {
        get
        {
            yield return AssetsDirectory;
            var engineAssets = EngineLocation.AssetsDirectory;
            if (engineAssets is not null &&
                !PathsEqual(engineAssets, AssetsDirectory))
            {
                yield return engineAssets;
            }
        }
    }

    /// True when <paramref name="path"/> is inside the project rather than inside the engine.
    /// Used to keep the editor from offering to save over engine-shipped content.
    public bool OwnsPath(string path)
    {
        var full = Path.GetFullPath(path);
        var root = Path.GetFullPath(RootDirectory);
        return full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
               || PathsEqual(full, root);
    }

    /// Path relative to the project root, or the absolute path unchanged when it lies outside.
    /// Stored references use this so a project stays portable.
    public string ToProjectRelative(string absolutePath)
    {
        if (!OwnsPath(absolutePath)) return absolutePath;
        return Path.GetRelativePath(RootDirectory, absolutePath).Replace('\\', '/');
    }

    public string ToAbsolute(string projectRelativePath) =>
        Path.IsPathRooted(projectRelativePath)
            ? projectRelativePath
            : Path.GetFullPath(Path.Combine(RootDirectory, projectRelativePath));

    // ── Compatibility ─────────────────────────────────────

    public enum Compatibility
    {
        /// Project and engine agree, or the project has never been opened.
        Match,
        /// Project was authored against an older ABI. Loading is expected to work.
        ProjectOlder,
        /// Project was authored against a newer ABI than the running engine. Loading may fail.
        ProjectNewer,
    }

    public Compatibility CompatibilityWith(int runningApiVersion)
    {
        if (EngineApiVersion == 0 || EngineApiVersion == runningApiVersion) return Compatibility.Match;
        return EngineApiVersion < runningApiVersion ? Compatibility.ProjectOlder : Compatibility.ProjectNewer;
    }

    // ── Create / open / save ──────────────────────────────

    /// Creates the folder layout and writes the project file. Fails if a project already exists
    /// there rather than overwriting someone's work.
    public static TucanoProject Create(string directory, string name, int engineApiVersion)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("Project name is required", nameof(name));

        var root = Path.GetFullPath(directory);
        var file = Path.Combine(root, SanitizeFileName(name) + Extension);
        if (File.Exists(file)) throw new IOException($"A project already exists at {file}");

        var project = new TucanoProject(file, name, CurrentFormatVersion) { EngineApiVersion = engineApiVersion };

        Directory.CreateDirectory(project.AssetsDirectory);
        Directory.CreateDirectory(project.MaterialsDirectory);
        Directory.CreateDirectory(project.ScenesDirectory);
        Directory.CreateDirectory(project.ImportedDirectory);
        project.Save();
        return project;
    }

    /// Opens an existing project file, or the single project file inside a directory.
    public static TucanoProject Open(string path)
    {
        var file = ResolveProjectFile(path)
                   ?? throw new FileNotFoundException($"No {Extension} found at {path}");

        using var doc = JsonDocument.Parse(File.ReadAllText(file));
        var root = doc.RootElement;

        var formatVersion = GetInt(root, "formatVersion", 0);
        if (formatVersion > CurrentFormatVersion)
        {
            throw new NotSupportedException(
                $"Project format v{formatVersion} is newer than this editor supports (v{CurrentFormatVersion}). Update the editor.");
        }

        var name = GetString(root, "name") ?? Path.GetFileNameWithoutExtension(file);
        var project = new TucanoProject(file, name, formatVersion)
        {
            EngineApiVersion = GetInt(root, "engineApiVersion", 0),
            StartupScene = GetString(root, "startupScene"),
        };

        // A project folder missing its content dirs is recoverable — recreate rather than refuse.
        Directory.CreateDirectory(project.AssetsDirectory);
        return project;
    }

    /// Finds the project file for a path that may be the file itself or the folder holding it.
    /// Returns null when there is none, or when a folder holds more than one and the choice is
    /// ambiguous.
    public static string? ResolveProjectFile(string path)
    {
        if (File.Exists(path) &&
            string.Equals(Path.GetExtension(path), Extension, StringComparison.OrdinalIgnoreCase))
        {
            return Path.GetFullPath(path);
        }

        if (!Directory.Exists(path)) return null;

        var candidates = Directory.GetFiles(path, "*" + Extension, SearchOption.TopDirectoryOnly);
        return candidates.Length == 1 ? Path.GetFullPath(candidates[0]) : null;
    }

    public void Save()
    {
        var sb = new StringBuilder();
        sb.AppendLine("{");
        sb.AppendLine($"  \"formatVersion\": {CurrentFormatVersion},");
        sb.AppendLine($"  \"name\": {Quote(Name)},");
        sb.AppendLine($"  \"engineApiVersion\": {EngineApiVersion.ToString(CultureInfo.InvariantCulture)},");
        sb.AppendLine($"  \"startupScene\": {(StartupScene is null ? "null" : Quote(StartupScene))}");
        sb.AppendLine("}");

        Directory.CreateDirectory(RootDirectory);
        File.WriteAllText(FilePath, sb.ToString());
    }

    // ── Helpers ───────────────────────────────────────────

    private static bool PathsEqual(string a, string b) =>
        string.Equals(Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar),
                      Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar),
                      StringComparison.OrdinalIgnoreCase);

    private static string SanitizeFileName(string name)
    {
        var sb = new StringBuilder(name.Length);
        foreach (var c in name)
        {
            sb.Append(Array.IndexOf(Path.GetInvalidFileNameChars(), c) >= 0 ? '_' : c);
        }
        var cleaned = sb.ToString().Trim();
        return cleaned.Length == 0 ? "Project" : cleaned;
    }

    private static string Quote(string value)
    {
        var sb = new StringBuilder(value.Length + 2);
        sb.Append('"');
        foreach (var c in value)
        {
            switch (c)
            {
                case '"': sb.Append("\\\""); break;
                case '\\': sb.Append("\\\\"); break;
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                default: sb.Append(c); break;
            }
        }
        sb.Append('"');
        return sb.ToString();
    }

    private static string? GetString(JsonElement root, string property) =>
        root.TryGetProperty(property, out var el) && el.ValueKind == JsonValueKind.String
            ? el.GetString()
            : null;

    private static int GetInt(JsonElement root, string property, int fallback) =>
        root.TryGetProperty(property, out var el) && el.ValueKind == JsonValueKind.Number &&
        el.TryGetInt32(out var value)
            ? value
            : fallback;
}

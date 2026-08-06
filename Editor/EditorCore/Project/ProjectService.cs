using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EditorCore;

/// Decides which project the editor opens, and remembers the ones opened before.
///
/// Kept separate from <see cref="TucanoProject"/> so the project model stays a plain description of
/// a folder: it can be created and tested without touching the user's machine-wide state.
public static class ProjectService
{
    private const int MaxRecents = 10;

    /// Per-user state, outside any project and outside the engine tree — a recents list that lived
    /// in either one would be lost on reinstall or committed by accident.
    private static string RecentsFile => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "TucanoEngine", "recent-projects.txt");

    /// Resolves the project to open, in priority order:
    /// 1. `--project &lt;path&gt;` on the command line — how automation and the self-test pin a project;
    /// 2. the most recent project that still exists on disk;
    /// 3. the engine's bundled project, created on first run so an engine checkout opens as-is.
    ///
    /// <paramref name="note"/> receives a one-line explanation of which branch was taken, for the
    /// editor log. Returns null only when no project could be opened or created at all.
    public static TucanoProject? Resolve(IReadOnlyList<string> args, int engineApiVersion, out string note)
    {
        var explicitPath = ReadFlag(args, "--project");
        if (explicitPath is not null)
        {
            try
            {
                var project = TucanoProject.Open(explicitPath);
                Remember(project);
                note = $"Opened project from --project: {project.Name}";
                return project;
            }
            catch (Exception ex)
            {
                // An explicit path that fails must not silently fall through to a different
                // project — that would have the editor quietly edit content the user didn't ask for.
                note = $"--project '{explicitPath}' could not be opened: {ex.Message}";
                return null;
            }
        }

        foreach (var path in Recents())
        {
            try
            {
                var project = TucanoProject.Open(path);
                Remember(project);
                note = $"Reopened last project: {project.Name}";
                return project;
            }
            catch (Exception)
            {
                // A recent entry that no longer opens is stale, not fatal: skip it and try the next.
            }
        }

        try
        {
            var bundled = OpenOrCreateBundled(engineApiVersion, out var created);
            if (bundled is not null)
            {
                Remember(bundled);
                note = created
                    ? $"Created bundled project '{bundled.Name}' at {bundled.RootDirectory}"
                    : $"Opened bundled project: {bundled.Name}";
                return bundled;
            }
        }
        catch (Exception ex)
        {
            note = $"Could not open a bundled project: {ex.Message}";
            return null;
        }

        note = "No project found and none could be created.";
        return null;
    }

    /// The project that ships with the engine tree. Adopts the existing `Assets` folder rather than
    /// creating a second one, so a checkout that predates project files keeps its content.
    public static TucanoProject? OpenOrCreateBundled(int engineApiVersion, out bool created)
    {
        created = false;
        var root = EngineLocation.RootDirectory;
        if (root is null) return null;

        var existing = TucanoProject.ResolveProjectFile(root);
        if (existing is not null) return TucanoProject.Open(existing);

        var project = TucanoProject.Create(root, "Tucano", engineApiVersion);
        created = true;
        return project;
    }

    // ── Recents ───────────────────────────────────────────

    /// Recently opened project files, newest first, filtered to those that still exist.
    public static IReadOnlyList<string> Recents()
    {
        try
        {
            if (!File.Exists(RecentsFile)) return Array.Empty<string>();
            return File.ReadAllLines(RecentsFile)
                       .Select(l => l.Trim())
                       .Where(l => l.Length > 0 && File.Exists(l))
                       .Distinct(StringComparer.OrdinalIgnoreCase)
                       .Take(MaxRecents)
                       .ToList();
        }
        catch (Exception)
        {
            // Recents are a convenience; an unreadable list must never stop the editor starting.
            return Array.Empty<string>();
        }
    }

    public static void Remember(TucanoProject project)
    {
        try
        {
            var entries = new List<string> { project.FilePath };
            entries.AddRange(Recents().Where(p =>
                !string.Equals(p, project.FilePath, StringComparison.OrdinalIgnoreCase)));

            var dir = Path.GetDirectoryName(RecentsFile);
            if (dir is not null) Directory.CreateDirectory(dir);
            File.WriteAllLines(RecentsFile, entries.Take(MaxRecents));
        }
        catch (Exception)
        {
            // Same reasoning as Recents(): losing the list is not worth failing an open over.
        }
    }

    /// Reads `--flag value`, returning null when absent or when the flag is the last argument.
    private static string? ReadFlag(IReadOnlyList<string> args, string flag)
    {
        for (var i = 0; i < args.Count - 1; i++)
        {
            if (string.Equals(args[i], flag, StringComparison.OrdinalIgnoreCase))
            {
                return args[i + 1];
            }
        }
        return null;
    }
}

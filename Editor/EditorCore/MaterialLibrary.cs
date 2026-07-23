using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using EditorCore.Interop;

namespace EditorCore;

/// A named material saved as a small JSON file. Materials live as project assets so they can be
/// created once and reused across objects, rather than only existing inline on a mesh.
public sealed class MaterialAsset
{
    public string Name { get; set; } = "New Material";
    public string? Path { get; set; }

    public float R { get; set; } = 0.72f;
    public float G { get; set; } = 0.72f;
    public float B { get; set; } = 0.75f;
    public float Metallic { get; set; }
    public float Roughness { get; set; } = 0.55f;
    public float EmissiveR { get; set; }
    public float EmissiveG { get; set; }
    public float EmissiveB { get; set; }

    public TucanoMaterial ToRuntime() => new()
    {
        BaseColor = new TucanoVec3(R, G, B),
        Emissive = new TucanoVec3(EmissiveR, EmissiveG, EmissiveB),
        Metallic = Metallic,
        Roughness = Roughness,
        Alpha = 1f,
    };

    public static MaterialAsset FromRuntime(string name, TucanoMaterial m) => new()
    {
        Name = name,
        R = m.BaseColor.X, G = m.BaseColor.Y, B = m.BaseColor.Z,
        EmissiveR = m.Emissive.X, EmissiveG = m.Emissive.Y, EmissiveB = m.Emissive.Z,
        Metallic = m.Metallic,
        Roughness = m.Roughness,
    };
}

/// Loads and saves .tmat files under a materials folder. Deliberately hand-rolled JSON: the format
/// is six numbers and a name, and this avoids taking a serializer dependency on the editor.
public sealed class MaterialLibrary
{
    public string RootDirectory { get; }
    public List<MaterialAsset> Materials { get; } = new();

    public MaterialLibrary(string rootDirectory)
    {
        RootDirectory = rootDirectory;
        Directory.CreateDirectory(RootDirectory);
    }

    public void Reload()
    {
        Materials.Clear();
        foreach (var file in Directory.EnumerateFiles(RootDirectory, "*.tmat", SearchOption.AllDirectories)
                                      .OrderBy(f => f))
        {
            if (TryLoad(file, out var mat)) Materials.Add(mat);
        }
    }

    public MaterialAsset Create(string name)
    {
        // Never silently overwrite an existing asset — suffix until the name is free.
        var baseName = SanitiseFileName(name);
        var path = System.IO.Path.Combine(RootDirectory, baseName + ".tmat");
        var n = 1;
        while (File.Exists(path))
        {
            path = System.IO.Path.Combine(RootDirectory, $"{baseName}_{n++}.tmat");
        }

        var mat = new MaterialAsset { Name = System.IO.Path.GetFileNameWithoutExtension(path), Path = path };
        Save(mat);
        Materials.Add(mat);
        return mat;
    }

    public void Save(MaterialAsset m)
    {
        m.Path ??= System.IO.Path.Combine(RootDirectory, SanitiseFileName(m.Name) + ".tmat");
        var inv = CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\n");
        sb.Append($"  \"name\": \"{Escape(m.Name)}\",\n");
        sb.Append($"  \"baseColor\": [{m.R.ToString("G6", inv)}, {m.G.ToString("G6", inv)}, {m.B.ToString("G6", inv)}],\n");
        sb.Append($"  \"emissive\": [{m.EmissiveR.ToString("G6", inv)}, {m.EmissiveG.ToString("G6", inv)}, {m.EmissiveB.ToString("G6", inv)}],\n");
        sb.Append($"  \"metallic\": {m.Metallic.ToString("G6", inv)},\n");
        sb.Append($"  \"roughness\": {m.Roughness.ToString("G6", inv)}\n");
        sb.Append("}\n");
        File.WriteAllText(m.Path, sb.ToString());
    }

    public void Delete(MaterialAsset m)
    {
        if (m.Path is not null && File.Exists(m.Path)) File.Delete(m.Path);
        Materials.Remove(m);
    }

    private static bool TryLoad(string path, out MaterialAsset mat)
    {
        mat = new MaterialAsset { Path = path, Name = System.IO.Path.GetFileNameWithoutExtension(path) };
        try
        {
            var text = File.ReadAllText(path);
            mat.Name = ReadString(text, "name") ?? mat.Name;
            var basec = ReadArray(text, "baseColor");
            if (basec.Length >= 3) { mat.R = basec[0]; mat.G = basec[1]; mat.B = basec[2]; }
            var em = ReadArray(text, "emissive");
            if (em.Length >= 3) { mat.EmissiveR = em[0]; mat.EmissiveG = em[1]; mat.EmissiveB = em[2]; }
            mat.Metallic = ReadNumber(text, "metallic") ?? mat.Metallic;
            mat.Roughness = ReadNumber(text, "roughness") ?? mat.Roughness;
            return true;
        }
        catch (Exception)
        {
            return false; // a corrupt .tmat shouldn't break the whole browser
        }
    }

    // ── Tiny readers (the schema is fixed and flat) ──

    private static string? ReadString(string json, string key)
    {
        var i = json.IndexOf($"\"{key}\"", StringComparison.Ordinal);
        if (i < 0) return null;
        var q1 = json.IndexOf('"', json.IndexOf(':', i) + 1);
        if (q1 < 0) return null;
        var q2 = json.IndexOf('"', q1 + 1);
        return q2 < 0 ? null : json[(q1 + 1)..q2];
    }

    private static float? ReadNumber(string json, string key)
    {
        var i = json.IndexOf($"\"{key}\"", StringComparison.Ordinal);
        if (i < 0) return null;
        var start = json.IndexOf(':', i) + 1;
        var end = json.IndexOfAny(new[] { ',', '\n', '}' }, start);
        if (end < 0) return null;
        return float.TryParse(json[start..end].Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var v)
            ? v : null;
    }

    private static float[] ReadArray(string json, string key)
    {
        var i = json.IndexOf($"\"{key}\"", StringComparison.Ordinal);
        if (i < 0) return Array.Empty<float>();
        var open = json.IndexOf('[', i);
        var close = json.IndexOf(']', open + 1);
        if (open < 0 || close < 0) return Array.Empty<float>();
        return json[(open + 1)..close]
            .Split(',')
            .Select(s => float.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : 0f)
            .ToArray();
    }

    private static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");

    private static string SanitiseFileName(string name)
    {
        var invalid = System.IO.Path.GetInvalidFileNameChars();
        var clean = new string(name.Select(c => invalid.Contains(c) ? '_' : c).ToArray()).Trim();
        return string.IsNullOrEmpty(clean) ? "Material" : clean;
    }
}

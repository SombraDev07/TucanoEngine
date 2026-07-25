using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using EditorCore;

namespace TucanoEditor.Models;

public enum MaterialPinType
{
    Float,
    Float2,
    Float3,
    Float4,
    Sampler,
}

public class MaterialPin
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public MaterialPinType Type { get; set; }
    public bool IsInput { get; set; }
    public string? DefaultValue { get; set; }
}

public class MaterialNode
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N")[..8];
    public string Type { get; set; } = "";
    public string Name { get; set; } = "";
    public double X { get; set; }
    public double Y { get; set; }
    public List<MaterialPin> InputPins { get; set; } = new();
    public List<MaterialPin> OutputPins { get; set; } = new();
    public Dictionary<string, string> Properties { get; set; } = new();
}

public class MaterialConnection
{
    public string FromNodeId { get; set; } = "";
    public string FromPinId { get; set; } = "";
    public string ToNodeId { get; set; } = "";
    public string ToPinId { get; set; } = "";
}

public class MaterialGraph
{
    public string Name { get; set; } = "New Material";
    public string? AssetPath { get; set; }
    public List<MaterialNode> Nodes { get; set; } = new();
    public List<MaterialConnection> Connections { get; set; } = new();

    public MaterialNode? FindNode(string id) => Nodes.FirstOrDefault(n => n.Id == id);

    public MaterialPin? FindPin(string nodeId, string pinId)
    {
        var node = FindNode(nodeId);
        return node?.InputPins.FirstOrDefault(p => p.Id == pinId)
            ?? node?.OutputPins.FirstOrDefault(p => p.Id == pinId);
    }

    public static MaterialGraph CreateDefault(MaterialAsset? seed = null)
    {
        var name = seed?.Name ?? "New Material";
        var albedo = seed is null
            ? "0.72,0.72,0.75"
            : string.Create(CultureInfo.InvariantCulture, $"{seed.R:G6},{seed.G:G6},{seed.B:G6}");
        var roughness = (seed?.Roughness ?? 0.55f).ToString("G6", CultureInfo.InvariantCulture);
        var metallic = (seed?.Metallic ?? 0f).ToString("G6", CultureInfo.InvariantCulture);
        var emissive = seed is null
            ? "0,0,0"
            : string.Create(CultureInfo.InvariantCulture,
                $"{seed.EmissiveR:G6},{seed.EmissiveG:G6},{seed.EmissiveB:G6}");

        var graph = new MaterialGraph { Name = name, AssetPath = seed?.Path };
        var output = CreateNode("MaterialOutput", "Material Output", "output", 600, 160);
        var baseColor = CreateNode("Constant3", "Base Color", "basecolor", 100, 80);
        baseColor.Properties["value"] = albedo;
        var rough = CreateNode("Constant1", "Roughness", "roughness", 100, 200);
        rough.Properties["value"] = roughness;
        var metal = CreateNode("Constant1", "Metallic", "metallic", 100, 280);
        metal.Properties["value"] = metallic;
        var emis = CreateNode("Constant3", "Emissive", "emissive", 100, 360);
        emis.Properties["value"] = emissive;

        graph.Nodes.Add(output);
        graph.Nodes.Add(baseColor);
        graph.Nodes.Add(rough);
        graph.Nodes.Add(metal);
        graph.Nodes.Add(emis);
        graph.Connections.Add(new() { FromNodeId = "basecolor", FromPinId = "out", ToNodeId = "output", ToPinId = "albedo" });
        graph.Connections.Add(new() { FromNodeId = "roughness", FromPinId = "out", ToNodeId = "output", ToPinId = "roughness" });
        graph.Connections.Add(new() { FromNodeId = "metallic", FromPinId = "out", ToNodeId = "output", ToPinId = "metallic" });
        graph.Connections.Add(new() { FromNodeId = "emissive", FromPinId = "out", ToNodeId = "output", ToPinId = "emissive" });
        return graph;
    }

    public static MaterialNode CreateNode(string type, string? name = null, string? id = null,
        double x = 0, double y = 0)
    {
        var node = new MaterialNode
        {
            Id = id ?? Guid.NewGuid().ToString("N")[..8],
            Type = type,
            Name = name ?? type,
            X = x,
            Y = y,
        };

        switch (type)
        {
            case "MaterialOutput":
                node.Name = name ?? "Material Output";
                node.InputPins =
                [
                    new() { Id = "albedo", Name = "Base Color", Type = MaterialPinType.Float3, IsInput = true },
                    new() { Id = "normal", Name = "Normal", Type = MaterialPinType.Float3, IsInput = true },
                    new() { Id = "roughness", Name = "Roughness", Type = MaterialPinType.Float, IsInput = true, DefaultValue = "0.5" },
                    new() { Id = "metallic", Name = "Metallic", Type = MaterialPinType.Float, IsInput = true, DefaultValue = "0.0" },
                    new() { Id = "emissive", Name = "Emissive", Type = MaterialPinType.Float3, IsInput = true, DefaultValue = "0,0,0" },
                ];
                break;
            case "Constant1":
                node.Properties["value"] = "0.5";
                node.OutputPins = [new() { Id = "out", Name = "X", Type = MaterialPinType.Float }];
                break;
            case "Constant3":
                node.Properties["value"] = "0.5,0.5,0.5";
                node.OutputPins = [new() { Id = "out", Name = "RGB", Type = MaterialPinType.Float3 }];
                break;
            case "Constant4":
                node.Properties["value"] = "1,1,1,1";
                node.OutputPins = [new() { Id = "out", Name = "RGBA", Type = MaterialPinType.Float4 }];
                break;
            case "Multiply":
                node.InputPins =
                [
                    new() { Id = "a", Name = "A", Type = MaterialPinType.Float4, IsInput = true, DefaultValue = "1" },
                    new() { Id = "b", Name = "B", Type = MaterialPinType.Float4, IsInput = true, DefaultValue = "1" },
                ];
                node.OutputPins = [new() { Id = "out", Name = "", Type = MaterialPinType.Float4 }];
                break;
            case "Add":
                node.InputPins =
                [
                    new() { Id = "a", Name = "A", Type = MaterialPinType.Float4, IsInput = true },
                    new() { Id = "b", Name = "B", Type = MaterialPinType.Float4, IsInput = true },
                ];
                node.OutputPins = [new() { Id = "out", Name = "", Type = MaterialPinType.Float4 }];
                break;
            case "Lerp":
                node.InputPins =
                [
                    new() { Id = "a", Name = "A", Type = MaterialPinType.Float4, IsInput = true },
                    new() { Id = "b", Name = "B", Type = MaterialPinType.Float4, IsInput = true },
                    new() { Id = "alpha", Name = "Alpha", Type = MaterialPinType.Float, IsInput = true, DefaultValue = "0.5" },
                ];
                node.OutputPins = [new() { Id = "out", Name = "", Type = MaterialPinType.Float4 }];
                break;
            case "TextureSample":
                node.Properties["path"] = "";
                node.OutputPins =
                [
                    new() { Id = "out", Name = "RGB", Type = MaterialPinType.Float3 },
                    new() { Id = "r", Name = "R", Type = MaterialPinType.Float },
                    new() { Id = "g", Name = "G", Type = MaterialPinType.Float },
                    new() { Id = "b", Name = "B", Type = MaterialPinType.Float },
                    new() { Id = "a", Name = "A", Type = MaterialPinType.Float },
                ];
                break;
        }

        return node;
    }

    public static MaterialGraph LoadOrCreate(MaterialAsset asset)
    {
        if (asset.Path is not null && File.Exists(asset.Path))
        {
            try
            {
                var graph = Load(asset.Path);
                graph.AssetPath = asset.Path;
                if (string.IsNullOrWhiteSpace(graph.Name))
                    graph.Name = asset.Name;
                return graph;
            }
            catch
            {
                // Fall through to a seeded default if the file is corrupt or graph-less.
            }
        }

        return CreateDefault(asset);
    }

    public static MaterialGraph Load(string path)
    {
        var text = File.ReadAllText(path);
        using var doc = JsonDocument.Parse(text);
        var root = doc.RootElement;

        var graph = new MaterialGraph
        {
            Name = root.TryGetProperty("name", out var n) ? n.GetString() ?? "Material" : "Material",
            AssetPath = path,
        };

        if (root.TryGetProperty("graph", out var g) && g.ValueKind == JsonValueKind.Object)
        {
            LoadGraphObject(graph, g);
            return graph;
        }

        // Legacy flat .tmat — build a usable default graph from the scalar fields.
        var seed = new MaterialAsset { Name = graph.Name, Path = path };
        if (root.TryGetProperty("baseColor", out var bc) && bc.ValueKind == JsonValueKind.Array)
        {
            var a = bc.EnumerateArray().Select(e => e.GetSingle()).ToArray();
            if (a.Length >= 3) { seed.R = a[0]; seed.G = a[1]; seed.B = a[2]; }
        }
        if (root.TryGetProperty("emissive", out var em) && em.ValueKind == JsonValueKind.Array)
        {
            var a = em.EnumerateArray().Select(e => e.GetSingle()).ToArray();
            if (a.Length >= 3) { seed.EmissiveR = a[0]; seed.EmissiveG = a[1]; seed.EmissiveB = a[2]; }
        }
        if (root.TryGetProperty("metallic", out var met) && met.TryGetSingle(out var mv)) seed.Metallic = mv;
        if (root.TryGetProperty("roughness", out var rg) && rg.TryGetSingle(out var rv)) seed.Roughness = rv;
        return CreateDefault(seed);
    }

    private static void LoadGraphObject(MaterialGraph graph, JsonElement g)
    {
        if (g.TryGetProperty("nodes", out var nodes) && nodes.ValueKind == JsonValueKind.Array)
        {
            foreach (var el in nodes.EnumerateArray())
            {
                var type = el.TryGetProperty("type", out var t) ? t.GetString() ?? "Constant1" : "Constant1";
                var id = el.TryGetProperty("id", out var idEl) ? idEl.GetString() : null;
                var name = el.TryGetProperty("name", out var nameEl) ? nameEl.GetString() : null;
                var x = el.TryGetProperty("x", out var xEl) ? xEl.GetDouble() : 0;
                var y = el.TryGetProperty("y", out var yEl) ? yEl.GetDouble() : 0;
                var node = CreateNode(type, name, id, x, y);
                if (el.TryGetProperty("props", out var props) && props.ValueKind == JsonValueKind.Object)
                {
                    foreach (var p in props.EnumerateObject())
                        node.Properties[p.Name] = p.Value.GetString() ?? "";
                }
                graph.Nodes.Add(node);
            }
        }

        if (g.TryGetProperty("connections", out var conns) && conns.ValueKind == JsonValueKind.Array)
        {
            foreach (var el in conns.EnumerateArray())
            {
                var from = el.TryGetProperty("from", out var f) ? f.GetString() ?? "" : "";
                var to = el.TryGetProperty("to", out var t) ? t.GetString() ?? "" : "";
                var fi = from.IndexOf('.');
                var ti = to.IndexOf('.');
                if (fi <= 0 || ti <= 0) continue;
                graph.Connections.Add(new MaterialConnection
                {
                    FromNodeId = from[..fi],
                    FromPinId = from[(fi + 1)..],
                    ToNodeId = to[..ti],
                    ToPinId = to[(ti + 1)..],
                });
            }
        }

        if (graph.FindNode("output") is null && graph.Nodes.All(n => n.Type != "MaterialOutput"))
        {
            var seeded = CreateDefault();
            graph.Nodes = seeded.Nodes;
            graph.Connections = seeded.Connections;
        }
    }

    /// Evaluates simple constant / texture chains into a MaterialAsset for runtime / flat .tmat fields.
    public MaterialAsset ToMaterialAsset()
    {
        var asset = new MaterialAsset { Name = Name, Path = AssetPath };
        var output = FindNode("output") ?? Nodes.FirstOrDefault(n => n.Type == "MaterialOutput");
        if (output is null) return asset;

        ApplyFloat3(GetPinValue(output, "albedo"), (r, g, b) => { asset.R = r; asset.G = g; asset.B = b; });
        ApplyFloat3(GetPinValue(output, "emissive"), (r, g, b) =>
        {
            asset.EmissiveR = r; asset.EmissiveG = g; asset.EmissiveB = b;
        });
        if (float.TryParse(GetPinValue(output, "roughness"), NumberStyles.Float, CultureInfo.InvariantCulture, out var rough))
            asset.Roughness = rough;
        if (float.TryParse(GetPinValue(output, "metallic"), NumberStyles.Float, CultureInfo.InvariantCulture, out var metal))
            asset.Metallic = metal;
        return asset;
    }

    public MaterialNode? GetConnectedSource(MaterialNode node, string pinId)
    {
        var conn = Connections.FirstOrDefault(c => c.ToNodeId == node.Id && c.ToPinId == pinId);
        return conn is null ? null : FindNode(conn.FromNodeId);
    }

    public string? GetConnectedTexturePath(MaterialNode node, string pinId)
    {
        var src = GetConnectedSource(node, pinId);
        if (src?.Type != "TextureSample") return null;
        var path = src.Properties.GetValueOrDefault("path");
        return string.IsNullOrWhiteSpace(path) ? null : path;
    }

    public string? GetPinValue(MaterialNode node, string pinId)
    {
        var conn = Connections.FirstOrDefault(c => c.ToNodeId == node.Id && c.ToPinId == pinId);
        if (conn is not null)
        {
            var src = FindNode(conn.FromNodeId);
            if (src is null) return node.InputPins.FirstOrDefault(p => p.Id == pinId)?.DefaultValue;

            if (src.Type == "TextureSample")
            {
                var path = src.Properties.GetValueOrDefault("path");
                if (!string.IsNullOrWhiteSpace(path) && File.Exists(path))
                {
                    var avg = AverageTextureRgb(path);
                    if (avg is not null)
                        return string.Create(CultureInfo.InvariantCulture, $"{avg.Value.r:G6},{avg.Value.g:G6},{avg.Value.b:G6}");
                }
                return "0.5,0.5,0.5";
            }

            if (src.Properties.TryGetValue("value", out var v)) return v;
        }
        return node.InputPins.FirstOrDefault(p => p.Id == pinId)?.DefaultValue;
    }

    /// Cheap downsample average so flat runtime materials still pick up a sensible base colour.
    private static (float r, float g, float b)? AverageTextureRgb(string path)
    {
        try
        {
            using var bmp = SkiaSharp.SKBitmap.Decode(path);
            if (bmp is null) return null;
            long r = 0, g = 0, b = 0, n = 0;
            int step = Math.Max(1, Math.Min(bmp.Width, bmp.Height) / 32);
            for (int y = 0; y < bmp.Height; y += step)
            for (int x = 0; x < bmp.Width; x += step)
            {
                var c = bmp.GetPixel(x, y);
                r += c.Red; g += c.Green; b += c.Blue; n++;
            }
            if (n == 0) return null;
            return (r / (255f * n), g / (255f * n), b / (255f * n));
        }
        catch
        {
            return null;
        }
    }

    public void Save(string? path = null)
    {
        path ??= AssetPath ?? throw new InvalidOperationException("No asset path to save.");
        AssetPath = path;
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);

        var mat = ToMaterialAsset();
        var inv = CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.AppendLine("{");
        sb.AppendLine($"  \"name\": \"{Escape(Name)}\",");
        sb.AppendLine($"  \"baseColor\": [{Fmt(mat.R)}, {Fmt(mat.G)}, {Fmt(mat.B)}],");
        sb.AppendLine($"  \"emissive\": [{Fmt(mat.EmissiveR)}, {Fmt(mat.EmissiveG)}, {Fmt(mat.EmissiveB)}],");
        sb.AppendLine($"  \"metallic\": {Fmt(mat.Metallic)},");
        sb.AppendLine($"  \"roughness\": {Fmt(mat.Roughness)},");
        sb.AppendLine("  \"graph\": {");
        sb.AppendLine("    \"nodes\": [");
        for (int i = 0; i < Nodes.Count; i++)
        {
            var n = Nodes[i];
            sb.Append("      {");
            sb.Append($"\"id\":\"{Escape(n.Id)}\",\"type\":\"{Escape(n.Type)}\",\"name\":\"{Escape(n.Name)}\",");
            sb.Append($"\"x\":{n.X.ToString(inv)},\"y\":{n.Y.ToString(inv)}");
            if (n.Properties.Count > 0)
            {
                sb.Append(",\"props\":{");
                var first = true;
                foreach (var (k, v) in n.Properties)
                {
                    if (!first) sb.Append(',');
                    first = false;
                    sb.Append($"\"{Escape(k)}\":\"{Escape(v)}\"");
                }
                sb.Append('}');
            }
            sb.Append('}');
            sb.AppendLine(i < Nodes.Count - 1 ? "," : "");
        }
        sb.AppendLine("    ],");
        sb.AppendLine("    \"connections\": [");
        for (int i = 0; i < Connections.Count; i++)
        {
            var c = Connections[i];
            sb.Append($"      {{\"from\":\"{Escape(c.FromNodeId)}.{Escape(c.FromPinId)}\",\"to\":\"{Escape(c.ToNodeId)}.{Escape(c.ToPinId)}\"}}");
            sb.AppendLine(i < Connections.Count - 1 ? "," : "");
        }
        sb.AppendLine("    ]");
        sb.AppendLine("  }");
        sb.AppendLine("}");
        File.WriteAllText(path, sb.ToString());

        static string Fmt(float v) => v.ToString("G6", CultureInfo.InvariantCulture);
    }

    /// Compact graph-only JSON for undo/redo (nodes + connections).
    public string CreateSnapshot()
    {
        var inv = CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.Append("{\"nodes\":[");
        for (int i = 0; i < Nodes.Count; i++)
        {
            var n = Nodes[i];
            if (i > 0) sb.Append(',');
            sb.Append('{');
            sb.Append($"\"id\":\"{Escape(n.Id)}\",\"type\":\"{Escape(n.Type)}\",\"name\":\"{Escape(n.Name)}\",");
            sb.Append($"\"x\":{n.X.ToString(inv)},\"y\":{n.Y.ToString(inv)}");
            if (n.Properties.Count > 0)
            {
                sb.Append(",\"props\":{");
                var first = true;
                foreach (var (k, v) in n.Properties)
                {
                    if (!first) sb.Append(',');
                    first = false;
                    sb.Append($"\"{Escape(k)}\":\"{Escape(v)}\"");
                }
                sb.Append('}');
            }
            sb.Append('}');
        }
        sb.Append("],\"connections\":[");
        for (int i = 0; i < Connections.Count; i++)
        {
            var c = Connections[i];
            if (i > 0) sb.Append(',');
            sb.Append($"{{\"from\":\"{Escape(c.FromNodeId)}.{Escape(c.FromPinId)}\",\"to\":\"{Escape(c.ToNodeId)}.{Escape(c.ToPinId)}\"}}");
        }
        sb.Append("]}");
        return sb.ToString();
    }

    public void RestoreSnapshot(string json)
    {
        Nodes.Clear();
        Connections.Clear();
        using var doc = JsonDocument.Parse(json);
        LoadGraphObject(this, doc.RootElement);
    }

    private static void ApplyFloat3(string? text, Action<float, float, float> apply)
    {
        if (string.IsNullOrWhiteSpace(text)) return;
        var parts = text.Split(',');
        if (parts.Length < 3) return;
        if (float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var r) &&
            float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var g) &&
            float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var b))
            apply(r, g, b);
    }

    private static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
}

using System;
using System.Collections.Generic;
using System.IO;

namespace EditorCore.Testing;

/// Writes a minimal but valid skinned+animated glTF 2.0 file so the editor's automated runs can
/// exercise the import → rig → Animation-section path without shipping a binary asset.
///
/// Mirrors Tools/AnimationTest/gltf_fixture.h: two joints in a chain, the child rotating 90 degrees
/// about Z over one second, plus a single skinned triangle.
public static class SkinnedGltfFixture
{
    /// Writes `skinned.gltf` and `skinned.bin` into `directory`. Returns the .gltf path.
    public static string Write(string directory)
    {
        Directory.CreateDirectory(directory);

        var bin = new List<byte>();

        void Floats(params float[] v)
        {
            foreach (var f in v) bin.AddRange(BitConverter.GetBytes(f));
        }

        void UShorts(params ushort[] v)
        {
            foreach (var u in v) bin.AddRange(BitConverter.GetBytes(u));
        }

        // Inverse bind matrices: identity for the root, translate(0,-2,0) for the child, so the
        // child's bind pose sits two units up.
        var ibmOffset = bin.Count;
        Floats(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
               1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1);

        var timeOffset = bin.Count;
        Floats(0.0f, 0.5f, 1.0f);

        // Rotation about Z: 0, 45, 90 degrees, stored xyzw as glTF requires.
        var rotOffset = bin.Count;
        Floats(0, 0, 0, 1,
               0, 0, 0.38268343f, 0.92387953f,
               0, 0, 0.70710678f, 0.70710678f);

        var posOffset = bin.Count;
        Floats(0, 0, 0, 1, 0, 0, 0, 2, 0);

        // First two vertices ride the root, the tip rides the child.
        var jointOffset = bin.Count;
        UShorts(0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0);
        while (bin.Count % 4 != 0) bin.Add(0); // keep the next accessor 4-byte aligned

        var weightOffset = bin.Count;
        Floats(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0);

        var idxOffset = bin.Count;
        UShorts(0, 1, 2);

        var binPath = Path.Combine(directory, "skinned.bin");
        var gltfPath = Path.Combine(directory, "skinned.gltf");
        File.WriteAllBytes(binPath, bin.ToArray());

        // Raw interpolated string with a `$$` prefix: holes are {{...}}, so every literal JSON brace
        // stays a single brace and needs no escaping.
        var json = $$"""
{
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 3] } ],
  "nodes": [
    { "name": "Root",  "children": [1], "translation": [0, 0, 0] },
    { "name": "Child", "translation": [0, 2, 0] },
    { "name": "Unused" },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "skins": [ { "joints": [0, 1], "inverseBindMatrices": 0, "skeleton": 0 } ],
  "meshes": [ {
    "primitives": [ {
      "attributes": { "POSITION": 3, "JOINTS_0": 4, "WEIGHTS_0": 5 },
      "indices": 6
    } ]
  } ],
  "animations": [ {
    "name": "Bend",
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ],
    "samplers": [ { "input": 1, "output": 2, "interpolation": "LINEAR" } ]
  } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,2,0] },
    { "bufferView": 4, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 6, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": {{ibmOffset}}, "byteLength": 128 },
    { "buffer": 0, "byteOffset": {{timeOffset}}, "byteLength": 12 },
    { "buffer": 0, "byteOffset": {{rotOffset}}, "byteLength": 48 },
    { "buffer": 0, "byteOffset": {{posOffset}}, "byteLength": 36 },
    { "buffer": 0, "byteOffset": {{jointOffset}}, "byteLength": 24 },
    { "buffer": 0, "byteOffset": {{weightOffset}}, "byteLength": 48 },
    { "buffer": 0, "byteOffset": {{idxOffset}}, "byteLength": 6 }
  ],
  "buffers": [ { "uri": "skinned.bin", "byteLength": {{bin.Count}} } ]
}
""";

        File.WriteAllText(gltfPath, json);
        return gltfPath;
    }
}

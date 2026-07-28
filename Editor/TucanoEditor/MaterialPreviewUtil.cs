using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Media.Imaging;
using EditorCore;
using SkiaSharp;
using TucanoEditor.Models;

namespace TucanoEditor;

/// Shared lit-sphere preview used by the Material Editor and Content Browser tiles.
public static class MaterialPreviewUtil
{
    private static readonly Dictionary<string, (long stamp, Bitmap bmp)> Cache = new();

    public static Bitmap? GetOrCreateThumbnail(MaterialAsset? mat, int size = 64)
    {
        if (mat?.Path is null || !File.Exists(mat.Path)) return null;
        try
        {
            var stamp = File.GetLastWriteTimeUtc(mat.Path).Ticks;
            if (Cache.TryGetValue(mat.Path, out var hit) && hit.stamp == stamp)
                return hit.bmp;

            var evaluated = mat;
            try { evaluated = MaterialGraph.LoadOrCreate(mat).ToMaterialAsset(); }
            catch { /* keep flat */ }

            var bmp = RenderSphere(evaluated, null, null, size);
            Cache[mat.Path] = (stamp, bmp);
            return bmp;
        }
        catch
        {
            return null;
        }
    }

    public static void Invalidate(string? path)
    {
        if (path is null) return;
        Cache.Remove(path);
    }

    public static Bitmap RenderSphere(MaterialAsset mat, string? albedoPath, string? normalPath, int size)
    {
        using var surface = SKSurface.Create(new SKImageInfo(size, size, SKColorType.Bgra8888, SKAlphaType.Premul));
        var canvas = surface.Canvas;
        canvas.Clear(new SKColor(18, 18, 24));

        using (var light = new SKPaint { Color = new SKColor(28, 28, 34) })
        using (var dark = new SKPaint { Color = new SKColor(20, 20, 26) })
        {
            int cell = Math.Max(4, size / 10);
            for (int y = 0; y < size; y += cell)
            for (int x = 0; x < size; x += cell)
                canvas.DrawRect(x, y, cell, cell, ((x / cell) + (y / cell)) % 2 == 0 ? light : dark);
        }

        using var albedoBmp = LoadSkia(albedoPath, size);
        using var normalBmp = LoadSkia(normalPath, size);

        float ar = mat.R, ag = mat.G, ab = mat.B;
        float er = mat.EmissiveR, eg = mat.EmissiveG, eb = mat.EmissiveB;
        float metallic = Math.Clamp(mat.Metallic, 0f, 1f);
        float roughness = Math.Clamp(mat.Roughness, 0.04f, 1f);

        float cx = (size - 1) * 0.5f;
        float cy = (size - 1) * 0.5f;
        float radius = size * 0.42f;
        float lx = -0.45f, ly = -0.55f, lz = 0.70f;
        Normalize(ref lx, ref ly, ref lz);
        float vx = 0, vy = 0, vz = 1;
        float gloss = MathF.Pow(1f - roughness, 2.8f) * 64f + 6f;
        float specScale = 0.12f + metallic * 0.85f;

        using var bmp = new SKBitmap(size, size, SKColorType.Bgra8888, SKAlphaType.Premul);
        var pixels = bmp.Pixels;

        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                float nx = (x - cx) / radius;
                float ny = (y - cy) / radius;
                float r2 = nx * nx + ny * ny;
                int idx = y * size + x;
                if (r2 > 1f)
                {
                    pixels[idx] = SKColors.Transparent;
                    continue;
                }

                float nz = MathF.Sqrt(MathF.Max(0f, 1f - r2));
                float nnx = nx, nny = ny, nnz = nz;
                if (normalBmp is not null)
                {
                    SampleRgb(normalBmp, nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, out float nr, out float ng, out float nb);
                    float tx = nr * 2f - 1f;
                    float ty = ng * 2f - 1f;
                    float tz = MathF.Max(0.05f, nb * 2f - 1f);
                    nnx = Math.Clamp(nx + tx * 0.55f, -1f, 1f);
                    nny = Math.Clamp(ny + ty * 0.55f, -1f, 1f);
                    nnz = MathF.Max(0.05f, nz * tz + (1f - MathF.Abs(tx)) * 0.15f);
                    Normalize(ref nnx, ref nny, ref nnz);
                }

                float br = ar, bgc = ag, bb = ab;
                if (albedoBmp is not null)
                    SampleRgb(albedoBmp, nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, out br, out bgc, out bb);

                float ndotl = Math.Clamp(nnx * lx + nny * ly + nnz * lz, 0f, 1f);
                float diff = 0.16f + ndotl * (0.84f - metallic * 0.55f);

                float hx = lx + vx, hy = ly + vy, hz = lz + vz;
                Normalize(ref hx, ref hy, ref hz);
                float ndoth = Math.Clamp(nnx * hx + nny * hy + nnz * hz, 0f, 1f);
                float spec = MathF.Pow(ndoth, gloss) * specScale * ndotl;

                float fr = br * diff + spec * (metallic > 0.15f ? br : 1f) + er;
                float fg = bgc * diff + spec * (metallic > 0.15f ? bgc : 1f) + eg;
                float fb = bb * diff + spec * (metallic > 0.15f ? bb : 1f) + eb;
                float rim = MathF.Pow(1f - nnz, 2.4f) * 0.10f;
                fr += rim; fg += rim; fb += rim;

                pixels[idx] = new SKColor(
                    (byte)(Math.Clamp(fr, 0f, 1f) * 255),
                    (byte)(Math.Clamp(fg, 0f, 1f) * 255),
                    (byte)(Math.Clamp(fb, 0f, 1f) * 255),
                    255);
            }
        }
        bmp.Pixels = pixels;
        canvas.DrawBitmap(bmp, 0, 0);

        using var image = surface.Snapshot();
        using var data = image.Encode(SKEncodedImageFormat.Png, 90);
        using var stream = data.AsStream();
        return new Bitmap(stream);
    }

    private static SKBitmap? LoadSkia(string? path, int size)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return null;
        try
        {
            using var src = SKBitmap.Decode(path);
            if (src is null) return null;
            return src.Resize(new SKImageInfo(size, size), SKFilterQuality.Medium);
        }
        catch { return null; }
    }

    private static void SampleRgb(SKBitmap bmp, float u, float v, out float r, out float g, out float b)
    {
        int x = Math.Clamp((int)(u * (bmp.Width - 1)), 0, bmp.Width - 1);
        int y = Math.Clamp((int)(v * (bmp.Height - 1)), 0, bmp.Height - 1);
        var c = bmp.GetPixel(x, y);
        r = c.Red / 255f; g = c.Green / 255f; b = c.Blue / 255f;
    }

    private static void Normalize(ref float x, ref float y, ref float z)
    {
        float len = MathF.Sqrt(x * x + y * y + z * z);
        if (len < 1e-5f) { x = 0; y = 0; z = 1; return; }
        x /= len; y /= len; z /= len;
    }
}

using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using EditorCore;
using EditorCore.Interop;

namespace TucanoEditor.Views;

/// Live editor for everything the renderer exposes as "weather and lighting": sun, atmosphere, fog,
/// clouds, rain, post and shadows/GI. Every control writes straight through to the runtime, so the
/// viewport updates as you drag.
public partial class EnvironmentWindow : Window
{
    private RuntimeHost? _runtime;

    // Controls are populated from the runtime on open; that must not echo back as user edits.
    private bool _loading;

    public EnvironmentWindow()
    {
        InitializeComponent();
    }

    public EnvironmentWindow(RuntimeHost runtime) : this()
    {
        _runtime = runtime;
        LoadFromRuntime();
    }

    // No hand-written InitializeComponent here: Avalonia's generated one both loads the XAML and
    // assigns the x:Name fields. A manual AvaloniaXamlLoader.Load() only does the former, leaving
    // every named control null — which is what crashed this window on open.

    /// Re-reads the runtime state into the controls (used on open and after loading a scene).
    public void LoadFromRuntime()
    {
        if (_runtime is not { IsAlive: true }) return;

        _loading = true;
        try
        {
            var e = _runtime.GetEnvironment();
            EnvAtmosphere.IsChecked = e.EnableAtmosphere != 0;
            EnvBruneton.IsChecked = e.UseBrunetonAtmosphere != 0;
            EnvSunFromTime.IsChecked = e.AtmosphereDrivesSun != 0;
            EnvTimeOfDay.Value = e.TimeOfDay;
            EnvTurbidity.Value = e.Turbidity;
            EnvFogDensity.Value = e.FogDensity;
            EnvFogHeight.Value = e.FogHeight;

            EnvClouds.IsChecked = e.EnableClouds != 0;
            EnvCloudShadows.IsChecked = e.EnableCloudShadows != 0;
            EnvCloudGodRays.IsChecked = e.EnableCloudGodRays != 0;
            EnvCloudsDriveRain.IsChecked = e.CloudsDriveRain != 0;
            EnvCloudCoverage.Value = e.CloudCoverage;
            EnvCloudDensity.Value = e.CloudDensity;
            EnvCloudStorminess.Value = e.CloudStorminess;
            EnvCloudAltitude.Value = e.CloudAltitude;
            EnvCloudThickness.Value = e.CloudThickness;

            EnvBloom.IsChecked = e.EnableBloom != 0;
            EnvAO.IsChecked = e.EnableAO != 0;
            EnvTonemap.IsChecked = e.EnableTonemap != 0;
            EnvAutoExposure.IsChecked = e.EnableAutoExposure != 0;
            EnvBloomStrength.Value = e.BloomStrength;
            EnvExposureTarget.Value = e.ExposureTarget;

            EnvShadows.IsChecked = e.EnableShadows != 0;
            EnvPCSS.IsChecked = e.EnablePCSS != 0;
            EnvContactShadows.IsChecked = e.EnableContactShadows != 0;
            EnvIBL.IsChecked = e.EnableIBL != 0;
            EnvSSR.IsChecked = e.EnableSSR != 0;
            EnvRTShadows.IsChecked = e.EnableRTShadows != 0;
            EnvRTReflections.IsChecked = e.EnableRTReflections != 0;
            EnvVoxelGI.IsChecked = e.EnableVoxelGI != 0;
            EnvGiTier.SelectedIndex = Math.Clamp(e.GiTier, 0, 3);

            var r = _runtime.GetRain();
            RainEnabled.IsChecked = r.Enabled != 0;
            RainSceneRain.IsChecked = r.EnableSceneRain != 0;
            RainSplashes.IsChecked = r.EnableWorldSplashes != 0;
            RainAmount.Value = r.Amount;
            RainStreaks.Value = r.StreakIntensity;
            RainPuddles.Value = r.PuddlesAmount;
            RainMist.Value = r.MistAmount;
            RainDrops.Value = r.RainDropsAmount;

            EnvMoon.IsChecked = e.EnableMoon != 0;
            EnvStars.IsChecked = e.EnableStars != 0;
            EnvMoonIntensity.Value = Math.Clamp(e.MoonIntensity, 0, 0.3);
            EnvMoonDisc.Value = Math.Clamp(e.MoonDiscBrightness, 0, 8);
            EnvMoonSize.Value = Math.Clamp(e.MoonAngularRadiusDeg, 0.05, 5);
            EnvStarIntensity.Value = Math.Clamp(e.StarIntensity, 0, 4);
            EnvStarTwinkle.Value = Math.Clamp(e.StarTwinkle, 0, 1);
            EnvPurkinje.Value = Math.Clamp(e.PurkinjeStrength, 0, 1);
            EnvLatitude.Value = Math.Clamp(e.LatitudeDeg, -90, 90);
            EnvDayOfYear.Value = Math.Clamp(e.DayOfYear, 0, 365);
            UpdateMoonReadout();

            EnvSkyboxPath.Text = _runtime.GetSkyboxTexture();
            EnvSkyboxBrightness.Value = Math.Clamp(_runtime.SkyboxBrightness, 0, 4);

            UpdateTimeLabel(e.TimeOfDay);
        }
        finally
        {
            _loading = false;
        }
    }

    // ── Writing back ─────────────────────────────────

    private static int B(CheckBox c) => c.IsChecked == true ? 1 : 0;

    private void ApplyEnvironment()
    {
        if (_loading || _runtime is not { IsAlive: true }) return;

        // Read-modify-write: the struct carries fields with no UI (exposure range, AO radius,
        // shadow map size), and those must survive an edit.
        var e = _runtime.GetEnvironment();
        e.EnableAtmosphere = B(EnvAtmosphere);
        e.UseBrunetonAtmosphere = B(EnvBruneton);
        e.AtmosphereDrivesSun = B(EnvSunFromTime);
        e.TimeOfDay = (float)EnvTimeOfDay.Value;
        e.Turbidity = (float)EnvTurbidity.Value;
        e.FogDensity = (float)EnvFogDensity.Value;
        e.FogHeight = (float)EnvFogHeight.Value;

        e.EnableClouds = B(EnvClouds);
        e.EnableCloudShadows = B(EnvCloudShadows);
        e.EnableCloudGodRays = B(EnvCloudGodRays);
        e.CloudsDriveRain = B(EnvCloudsDriveRain);
        e.CloudCoverage = (float)EnvCloudCoverage.Value;
        e.CloudDensity = (float)EnvCloudDensity.Value;
        e.CloudStorminess = (float)EnvCloudStorminess.Value;
        e.CloudAltitude = (float)EnvCloudAltitude.Value;
        e.CloudThickness = (float)EnvCloudThickness.Value;

        e.EnableBloom = B(EnvBloom);
        e.EnableAO = B(EnvAO);
        e.EnableTonemap = B(EnvTonemap);
        e.EnableAutoExposure = B(EnvAutoExposure);
        e.BloomStrength = (float)EnvBloomStrength.Value;
        e.ExposureTarget = (float)EnvExposureTarget.Value;

        // Skybox brightness lives outside the environment struct — it belongs to the sky renderer.
        _runtime.SkyboxBrightness = (float)EnvSkyboxBrightness.Value;

        e.EnableMoon = B(EnvMoon);
        e.EnableStars = B(EnvStars);
        e.MoonIntensity = (float)EnvMoonIntensity.Value;
        e.MoonDiscBrightness = (float)EnvMoonDisc.Value;
        e.MoonAngularRadiusDeg = (float)EnvMoonSize.Value;
        e.StarIntensity = (float)EnvStarIntensity.Value;
        e.StarTwinkle = (float)EnvStarTwinkle.Value;
        e.PurkinjeStrength = (float)EnvPurkinje.Value;
        e.LatitudeDeg = (float)EnvLatitude.Value;
        e.DayOfYear = (float)EnvDayOfYear.Value;

        e.EnableShadows = B(EnvShadows);
        e.EnablePCSS = B(EnvPCSS);
        e.EnableContactShadows = B(EnvContactShadows);
        e.EnableIBL = B(EnvIBL);
        e.EnableSSR = B(EnvSSR);
        e.EnableRTShadows = B(EnvRTShadows);
        e.EnableRTReflections = B(EnvRTReflections);
        e.EnableVoxelGI = B(EnvVoxelGI);
        e.GiTier = EnvGiTier.SelectedIndex < 0 ? 0 : EnvGiTier.SelectedIndex;

        _runtime.SetEnvironment(e);
        UpdateTimeLabel(e.TimeOfDay);
        UpdateMoonReadout();
    }

    /// Reads back where the runtime actually put the moon. Phase is not a dial the user sets — it
    /// follows from the date, because phase IS the moon's elongation from the sun. Showing the
    /// result makes that relationship visible instead of mysterious.
    private void UpdateMoonReadout()
    {
        if (_runtime is not { IsAlive: true }) return;

        var dir = _runtime.MoonDirection;
        var altitude = Math.Asin(Math.Clamp(-dir.Y, -1.0, 1.0)) * 180.0 / Math.PI;
        var illum = _runtime.MoonIllumination;
        var phase = _runtime.MoonPhase;

        var name = phase switch
        {
            < 0.03f or > 0.97f => "new",
            < 0.22f => "waxing crescent",
            < 0.28f => "first quarter",
            < 0.47f => "waxing gibbous",
            < 0.53f => "full",
            < 0.72f => "waning gibbous",
            < 0.78f => "last quarter",
            _ => "waning crescent",
        };

        MoonReadout.Text = altitude >= 0
            ? $"Moon: {name}, {illum * 100:F0}% lit, {altitude:F0}° above the horizon"
            : $"Moon: {name}, {illum * 100:F0}% lit — below the horizon ({altitude:F0}°)";
    }

    private void ApplyRain()
    {
        if (_loading || _runtime is not { IsAlive: true }) return;

        var r = _runtime.GetRain();
        r.Enabled = B(RainEnabled);
        r.EnableSceneRain = B(RainSceneRain);
        r.EnableWorldSplashes = B(RainSplashes);
        r.Amount = (float)RainAmount.Value;
        r.StreakIntensity = (float)RainStreaks.Value;
        r.PuddlesAmount = (float)RainPuddles.Value;
        r.MistAmount = (float)RainMist.Value;
        r.RainDropsAmount = (float)RainDrops.Value;
        _runtime.SetRain(r);
    }

    private void UpdateTimeLabel(float tod)
    {
        // 0 = midnight, 0.5 = noon — show it as a clock so the slider means something.
        var totalMinutes = (int)Math.Round(tod * 24.0 * 60.0) % (24 * 60);
        TimeOfDayLabel.Text = $"Time of day — {totalMinutes / 60:00}:{totalMinutes % 60:00}";
    }

    // ── Skybox ───────────────────────────────────────

    private async void OnBrowseSkybox(object? sender, RoutedEventArgs e)
    {
        if (_runtime is not { IsAlive: true }) return;

        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Choose an environment map",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Environment map")
                {
                    Patterns = new[] { "*.hdr", "*.dds", "*.exr", "*.png", "*.jpg" }
                }
            }
        });
        if (files.Count == 0 || files[0].TryGetLocalPath() is not { } path) return;

        SetSkybox(path);
    }

    private void OnClearSkybox(object? sender, RoutedEventArgs e) => SetSkybox("");

    private void SetSkybox(string path)
    {
        if (_runtime is not { IsAlive: true }) return;

        if (_runtime.SetSkyboxTexture(path))
        {
            // Read it back: the runtime resolves and normalises the path, and an empty string means
            // it fell back to the procedural sky.
            EnvSkyboxPath.Text = _runtime.GetSkyboxTexture();
        }
        else
        {
            EnvSkyboxPath.Text = "(failed to load)";
        }
    }

    private void OnEnvChanged(object? sender, RoutedEventArgs e) => ApplyEnvironment();
    private void OnEnvSliderChanged(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e) => ApplyEnvironment();
    private void OnGiTierChanged(object? sender, SelectionChangedEventArgs e) => ApplyEnvironment();
    private void OnRainChanged(object? sender, RoutedEventArgs e) => ApplyRain();
    private void OnRainSliderChanged(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e) => ApplyRain();

    // ── Presets ──────────────────────────────────────

    private void OnPresetClearDay(object? sender, RoutedEventArgs e) =>
        ApplyPreset(tod: 0.42f, turbidity: 2.4f, coverage: 0.35f, density: 1.0f, storm: 0.15f,
                    fog: 0.006f, rain: false, rainAmount: 0f);

    private void OnPresetOvercast(object? sender, RoutedEventArgs e) =>
        ApplyPreset(tod: 0.45f, turbidity: 4.5f, coverage: 0.85f, density: 1.6f, storm: 0.45f,
                    fog: 0.018f, rain: false, rainAmount: 0f);

    /// Hook for the editor's --selftest run; exercises the same path as the Storm button.
    public void ApplyStormPresetForTest() => OnPresetStorm(this, new RoutedEventArgs());

    private void OnPresetStorm(object? sender, RoutedEventArgs e) =>
        ApplyPreset(tod: 0.40f, turbidity: 6.5f, coverage: 0.95f, density: 2.2f, storm: 0.9f,
                    fog: 0.03f, rain: true, rainAmount: 0.9f);

    private void OnPresetNight(object? sender, RoutedEventArgs e) =>
        ApplyPreset(tod: 0.02f, turbidity: 2.0f, coverage: 0.4f, density: 1.0f, storm: 0.2f,
                    fog: 0.01f, rain: false, rainAmount: 0f);

    private void ApplyPreset(float tod, float turbidity, float coverage, float density, float storm,
                             float fog, bool rain, float rainAmount)
    {
        if (_runtime is not { IsAlive: true }) return;

        // Set the controls with events suppressed, then push once — otherwise each assignment
        // would fire its own round-trip through the ABI.
        _loading = true;
        try
        {
            EnvTimeOfDay.Value = tod;
            EnvTurbidity.Value = turbidity;
            EnvCloudCoverage.Value = coverage;
            EnvCloudDensity.Value = density;
            EnvCloudStorminess.Value = storm;
            EnvFogDensity.Value = fog;
            EnvClouds.IsChecked = true;
            EnvAtmosphere.IsChecked = true;
            RainEnabled.IsChecked = rain;
            RainAmount.Value = rainAmount;
        }
        finally
        {
            _loading = false;
        }

        ApplyEnvironment();
        ApplyRain();
    }
}

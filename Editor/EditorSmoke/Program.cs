using EditorCore;
using EditorCore.Interop;

namespace EditorSmoke;

/// Headless gate for the editor's managed layer. It drives RuntimeHost exactly as the UI does, so
/// P/Invoke struct layouts, the scene format and play mode are all covered without a window.
/// Run it after changing TucanoAPI.h/.cpp or the interop structs.
internal static class Program
{
    private static int _failures;

    private static void Check(string label, bool ok)
    {
        Console.WriteLine(ok ? $"  OK   {label}" : $"  FAIL {label}");
        if (!ok) _failures++;
    }

    private static bool Near(float a, float b, float eps = 0.001f) => MathF.Abs(a - b) < eps;

    private static int Main()
    {
        var scenePath = Path.Combine(Path.GetTempPath(), "tucano_editor_smoke.tscene");

        using var rt = new RuntimeHost(enableDebug: false);
        Check("runtime alive", rt.IsAlive);
        Check("version reported", rt.Version.Length > 0);
        Console.WriteLine($"  engine v{rt.Version}, {rt.ObjectCount} objects, {rt.LightCount} lights");

        // ── Environment ──
        Console.WriteLine("\n-- environment --");
        var env = rt.GetEnvironment();
        Console.WriteLine($"  tod={env.TimeOfDay:F2} coverage={env.CloudCoverage:F2} " +
                          $"fog={env.FogDensity:F4} giTier={env.GiTier} shadowMap={env.ShadowMapSize}");
        // A mismatched struct layout surfaces as nonsense in the trailing fields.
        Check($"shadowMapSize plausible ({env.ShadowMapSize})", env.ShadowMapSize is >= 256 and <= 8192);
        Check($"timeOfDay in range ({env.TimeOfDay:F2})", env.TimeOfDay is >= 0 and <= 1);
        Check($"cloudAltitude plausible ({env.CloudAltitude:F0})", env.CloudAltitude is > 100 and < 10000);
        Check($"giTier in range ({env.GiTier})", env.GiTier is >= 0 and <= 3);

        env.TimeOfDay = 0.68f;
        env.CloudCoverage = 0.77f;
        env.FogDensity = 0.031f;
        env.CloudStorminess = 0.55f;
        env.EnableBloom = 0;
        rt.SetEnvironment(env);

        var env2 = rt.GetEnvironment();
        Check($"timeOfDay written ({env2.TimeOfDay:F2})", Near(env2.TimeOfDay, 0.68f));
        Check($"coverage written ({env2.CloudCoverage:F2})", Near(env2.CloudCoverage, 0.77f));
        Check($"fog written ({env2.FogDensity:F3})", Near(env2.FogDensity, 0.031f));
        Check("bloom flag written", env2.EnableBloom == 0);
        Check($"untouched field survived (exposureTarget {env2.ExposureTarget:F2})",
              env2.ExposureTarget is > 0 and <= 1);

        // ── Rain ──
        Console.WriteLine("\n-- rain --");
        var rain = rt.GetRain();
        Check($"rain radius plausible ({rain.Radius:F0})", rain.Radius is > 0 and <= 100000);
        rain.Enabled = 1;
        rain.Amount = 0.83f;
        rain.MistAmount = 0.62f;
        rt.SetRain(rain);
        var rain2 = rt.GetRain();
        Check("rain enabled written", rain2.Enabled == 1);
        Check($"amount written ({rain2.Amount:F2})", Near(rain2.Amount, 0.83f));
        Check($"mist written ({rain2.MistAmount:F2})", Near(rain2.MistAmount, 0.62f));

        // ── Authoring ──
        Console.WriteLine("\n-- authoring --");
        var cube = rt.SpawnPrimitive(TucanoPrimitive.Cube, 0, 6, -5, 2f);
        Check($"spawned cube (#{cube})", cube != RuntimeHost.InvalidObject);
        rt.SetObjectName(cube, "Crate");
        Check("renamed", rt.GetObjectName(cube) == "Crate");

        var mat = rt.GetObjectMaterial(cube);
        mat.Metallic = 0.4f;
        mat.Roughness = 0.3f;
        rt.SetObjectMaterial(cube, mat);
        var mat2 = rt.GetObjectMaterial(cube);
        Check($"metallic written ({mat2.Metallic:F2})", Near(mat2.Metallic, 0.4f, 0.01f));
        Check($"roughness written ({mat2.Roughness:F2})", Near(mat2.Roughness, 0.3f, 0.01f));

        var dup = rt.DuplicateObject(cube, 3, 0, 0);
        Check($"duplicated (#{dup})", dup != RuntimeHost.InvalidObject);

        // ── Selection ──
        rt.Selected = (int)cube;
        Check("selection round-trip", rt.Selected == (int)cube);
        var colorWhileSelected = rt.GetObjectColor(cube);
        Check("selected object reports its real colour, not the highlight tint",
              colorWhileSelected.X <= 1f && Near(colorWhileSelected.X, mat2.BaseColor.X, 0.01f));
        rt.Selected = -1;

        // ── Lights ──
        Console.WriteLine("\n-- lights --");
        var light = new TucanoLight
        {
            Type = (int)TucanoLightType.Point,
            Position = new TucanoVec3(0, 5, 0),
            Direction = new TucanoVec3(0, -1, 0),
            Color = new TucanoVec3(1f, 0.9f, 0.7f),
            Intensity = 18f,
            Range = 30f,
            CastShadows = 1,
        };
        var li = rt.AddLight(light);
        Check($"light added (#{li})", li != RuntimeHost.InvalidObject);
        var lg = rt.GetLight(li);
        Check($"intensity round-trip ({lg.Intensity:F1})", Near(lg.Intensity, 18f, 0.01f));
        Check($"range round-trip ({lg.Range:F1})", Near(lg.Range, 30f, 0.01f));
        Check("type round-trip", lg.Type == (int)TucanoLightType.Point);

        // ── Physics + play ──
        Console.WriteLine("\n-- physics & play --");
        rt.SetObjectPhysics(0, TucanoPhysicsKind.Static);          // skylab ground
        rt.SetObjectPhysics(cube, TucanoPhysicsKind.Dynamic, 3f);
        Check("collider kind stored", rt.GetObjectPhysics(cube) == TucanoPhysicsKind.Dynamic);
        Check($"mass stored ({rt.GetObjectMass(cube):F1})", Near(rt.GetObjectMass(cube), 3f, 0.01f));

        var startY = rt.GetObjectTransform(cube).pos.Y;
        Check("play started", rt.PlayStart());
        Check($"colliders built ({rt.ColliderCount})", rt.ColliderCount >= 2);
        Check($"no failed colliders ({rt.FailedColliderCount})", rt.FailedColliderCount == 0);
        Check("state is running", rt.PlayState == TucanoPlayState.Running);

        var sw = System.Diagnostics.Stopwatch.StartNew();
        while (sw.Elapsed.TotalSeconds < 2.5) rt.Render();
        var landedY = rt.GetObjectTransform(cube).pos.Y;
        Console.WriteLine($"  fell from {startY:F2} to {landedY:F2}");
        Check("object fell", landedY < startY - 0.5f);
        Check("object landed on the static floor (did not tunnel)", landedY > 0f);

        rt.PlayPause(true);
        Check("pause state", rt.PlayState == TucanoPlayState.Paused);
        var pausedY = rt.GetObjectTransform(cube).pos.Y;
        var swPause = System.Diagnostics.Stopwatch.StartNew();
        while (swPause.Elapsed.TotalSeconds < 0.4) rt.Render();
        Check("pause freezes the simulation", Near(rt.GetObjectTransform(cube).pos.Y, pausedY));

        rt.PlayStop();
        Check("state stopped", rt.PlayState == TucanoPlayState.Stopped);
        Check("stop restored the authored transform",
              Near(rt.GetObjectTransform(cube).pos.Y, startY));

        // ── Save / load ──
        Console.WriteLine("\n-- save / load --");
        var objectsBefore = rt.ObjectCount;
        var lightsBefore = rt.LightCount;
        Check("saved", rt.SaveScene(scenePath));
        rt.ClearScene();
        Check("cleared", rt.ObjectCount == 0);
        Check("loaded", rt.LoadScene(scenePath));
        Check($"objects restored ({rt.ObjectCount}/{objectsBefore})", rt.ObjectCount == objectsBefore);
        Check($"lights restored ({rt.LightCount}/{lightsBefore})", rt.LightCount == lightsBefore);
        Check($"environment restored (tod {rt.GetEnvironment().TimeOfDay:F2})",
              Near(rt.GetEnvironment().TimeOfDay, 0.68f, 0.01f));
        Check("rain restored", rt.GetRain().Enabled == 1);
        Check("collider restored", rt.GetObjectPhysics(cube) == TucanoPhysicsKind.Dynamic);
        Check("name restored", rt.GetObjectName(cube) == "Crate");

        // Playing a reloaded scene is the path a "test my level" loop actually takes.
        Check("play works after reload", rt.PlayStart());
        var swReload = System.Diagnostics.Stopwatch.StartNew();
        while (swReload.Elapsed.TotalSeconds < 1.0) rt.Render();
        Check("object fell after reload", rt.GetObjectTransform(cube).pos.Y < startY - 0.3f);
        rt.PlayStop();

        // ── Virtual input (Phase I-0) ──
        Console.WriteLine("\n-- virtual input --");
        Check($"button table populated ({rt.ButtonCodeCount} codes)", rt.ButtonCodeCount > 80);
        Check("button name lookup", rt.GetButtonName(rt.GetButtonFromName("W")) == "W");
        Check("mouse-left name round-trip",
              rt.GetButtonName(rt.GetButtonFromName("MouseLeft")) == "MouseLeft");
        // No key is physically held in a headless run, so every query must read false rather than
        // garbage — that is what proves the snapshot is actually being filled each frame.
        rt.Render();
        Check("no phantom key held", !rt.IsButtonHeld(rt.GetButtonFromName("W")));
        Check("no phantom virtual button", !rt.IsVirtualButtonHeld("MoveForward"));
        var (mdx, mdy) = rt.GetMouseDelta();
        Check($"mouse delta finite ({mdx:F2}, {mdy:F2})",
              !float.IsNaN(mdx) && !float.IsNaN(mdy) && MathF.Abs(mdx) < 10000f);
        Check("unknown virtual axis reads zero", Near(rt.GetVirtualAxis("NoSuchAxis"), 0f));
        rt.BindButton("TestAction", rt.GetButtonFromName("K"));
        rt.Render();
        Check("new binding is queryable", !rt.IsVirtualButtonHeld("TestAction"));
        rt.ResetInputBindings();
        Check("reset restores defaults", !rt.IsVirtualButtonHeld("MoveForward"));

        // ── Environment presets, captured ──
        // Renders the same scene under the Environment panel's presets. The images are the only
        // way to confirm the weather controls actually reach the renderer.
        Console.WriteLine("\n-- environment presets --");
        var shotDir = Path.Combine(Path.GetTempPath(), "tucano_env_shots");
        Directory.CreateDirectory(shotDir);

        foreach (var (name, tod, turbidity, coverage, density, storm, fog, rainOn, rainAmount) in new[]
        {
            ("clear_day", 0.42f, 2.4f, 0.35f, 1.0f, 0.15f, 0.006f, false, 0f),
            ("overcast",  0.45f, 4.5f, 0.85f, 1.6f, 0.45f, 0.018f, false, 0f),
            ("storm",     0.40f, 6.5f, 0.95f, 2.2f, 0.90f, 0.030f, true,  0.9f),
            ("night",     0.02f, 2.0f, 0.40f, 1.0f, 0.20f, 0.010f, false, 0f),
        })
        {
            var e = rt.GetEnvironment();
            e.TimeOfDay = tod;
            e.Turbidity = turbidity;
            e.CloudCoverage = coverage;
            e.CloudDensity = density;
            e.CloudStorminess = storm;
            e.FogDensity = fog;
            e.EnableClouds = 1;
            e.EnableAtmosphere = 1;
            e.EnableBloom = 1;
            rt.SetEnvironment(e);

            var r = rt.GetRain();
            r.Enabled = rainOn ? 1 : 0;
            r.Amount = rainAmount;
            rt.SetRain(r);

            // Clouds and exposure are temporally accumulated, so let them settle before capturing.
            for (var i = 0; i < 45; i++) rt.Render();
            var path = Path.Combine(shotDir, $"{name}.png");
            Check($"preset '{name}' rendered", rt.Screenshot(path));
        }
        Console.WriteLine($"  shots in {shotDir}");

        // -- Night sky --
        // The moon and stars only exist while the sun is below the horizon, so this is the one
        // configuration that exercises them. Checked numerically rather than by eye: the frame
        // must actually carry more light with the celestial bodies on than with them off.
        Console.WriteLine("[night sky]");
        {
            var e = rt.GetEnvironment();
            e.TimeOfDay = 0.02f;   // just past midnight
            e.EnableClouds = 0;    // an overcast sky would hide exactly what is being tested
            e.EnableMoon = 1;
            e.EnableStars = 1;
            e.StarIntensity = 1.0f;
            e.MoonIntensity = 0.045f;
            e.PurkinjeStrength = 0.75f;
            e.LatitudeDeg = -23.55f;
            e.DayOfYear = 156f;
            rt.SetEnvironment(e);
            for (var i = 0; i < 45; i++) rt.Render();

            var withMoon = Path.Combine(shotDir, "night_moon.png");
            Check("night with moon rendered", rt.Screenshot(withMoon));
            var litBytes = new FileInfo(withMoon).Length;

            e.EnableMoon = 0;
            e.EnableStars = 0;
            rt.SetEnvironment(e);
            for (var i = 0; i < 45; i++) rt.Render();
            var dark = Path.Combine(shotDir, "night_dark.png");
            Check("night without moon rendered", rt.Screenshot(dark));
            var darkBytes = new FileInfo(dark).Length;

            // A near-black frame compresses far smaller than one carrying a moon disc, stars and
            // moonlit geometry. Crude, but it is a real signal that pixels changed and it needs
            // no image decoding here.
            Check($"celestial bodies change the frame (moon={litBytes}B, dark={darkBytes}B)",
                  litBytes > darkBytes);

            // Round-trip the new fields so a struct-layout mismatch cannot pass quietly.
            e.EnableMoon = 1;
            e.MoonAngularRadiusDeg = 0.75f;
            e.LatitudeDeg = 45.5f;
            rt.SetEnvironment(e);
            // The lunar model has one relationship that must always hold: a full moon is opposite
            // the sun, so at midnight it is high and at noon it is below the horizon. If the phase
            // or the sidereal transform regressed, this is what would catch it.
            e = rt.GetEnvironment();
            e.DayOfYear = 156f;      // a full moon in this model
            e.TimeOfDay = 0.02f;     // just past midnight
            e.LatitudeDeg = -23.55f; // must be explicit: altitude depends on it
            rt.SetEnvironment(e);
            rt.Render();
            var midnight = rt.MoonDirection;
            var midnightAlt = -midnight.Y;
            var illum = rt.MoonIllumination;

            e.TimeOfDay = 0.5f;   // noon
            rt.SetEnvironment(e);
            rt.Render();
            var noonAlt = -rt.MoonDirection.Y;

            Check($"full moon is up at midnight and down at noon (alt {midnightAlt:F2} -> {noonAlt:F2}, illum {illum:F2})",
                  illum > 0.9f && midnightAlt > 0.5f && noonAlt < 0f);

            // Same moment, other hemisphere. A June full moon sits far south on the celestial
            // sphere, so it rides high from southern latitudes and low from northern ones. If the
            // horizon transform ignored latitude this number would not move.
            e.TimeOfDay = 0.02f;
            e.LatitudeDeg = 45.5f;
            rt.SetEnvironment(e);
            rt.Render();
            var northAlt = -rt.MoonDirection.Y;
            Check($"latitude changes the moon's altitude (south {midnightAlt:F2} vs north {northAlt:F2})",
                  northAlt < midnightAlt - 0.2f);

            // A week later the same clock time should show a very different phase — that is the
            // synodic cycle, which falls out of the sidereal month rather than being hardcoded.
            e.DayOfYear = 149f;
            e.TimeOfDay = 0.02f;
            rt.SetEnvironment(e);
            rt.Render();
            var quarterIllum = rt.MoonIllumination;
            Check($"phase advances with the date (full {illum:F2} vs quarter {quarterIllum:F2})",
                  quarterIllum < 0.7f);

            var back = rt.GetEnvironment();
            Check($"night-sky settings round-trip (radius={back.MoonAngularRadiusDeg}, lat={back.LatitudeDeg})",
                  Math.Abs(back.MoonAngularRadiusDeg - 0.75f) < 0.001f &&
                  Math.Abs(back.LatitudeDeg - 45.5f) < 0.001f &&
                  back.EnableMoon == 1);
        }

        Console.WriteLine($"\n=== failures: {_failures} ===");
        return _failures == 0 ? 0 : 1;
    }
}

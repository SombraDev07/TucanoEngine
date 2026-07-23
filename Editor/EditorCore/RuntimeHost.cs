using System;
using System.IO;
using System.Runtime.InteropServices;
using EditorCore.Interop;

namespace EditorCore;

public sealed class RuntimeHost : IDisposable
{
    private IntPtr _handle;
    private IntPtr _scene;
    private bool _disposed;

    public IntPtr Handle => _handle;
    public IntPtr Scene => _scene;
    public bool IsAlive => _handle != IntPtr.Zero && TucanoApi.tucano_runtime_alive(_handle);

    public RuntimeHost(string? assetsDir = null, string? shaderDir = null, bool enableDebug = true)
    {
        var desc = new TucanoInitDesc
        {
            Width = 1280,
            Height = 720,
            Title = Marshal.StringToHGlobalAnsi("Tucano Engine"),
            EnableDebugLayer = enableDebug,
            Borderless = true
        };

        if (assetsDir != null)
            desc.AssetsDir = Marshal.StringToHGlobalAnsi(assetsDir);
        if (shaderDir != null)
            desc.ShaderDir = Marshal.StringToHGlobalAnsi(shaderDir);

        try
        {
            _handle = TucanoApi.tucano_runtime_init(ref desc);
            if (_handle == IntPtr.Zero)
                throw new InvalidOperationException("Failed to initialize TucanoRuntime.");

            _scene = TucanoApi.tucano_scene_create(_handle);
            TucanoApi.tucano_scene_load_skylab(_scene);
        }
        finally
        {
            Marshal.FreeHGlobal(desc.Title);
            if (desc.AssetsDir != IntPtr.Zero) Marshal.FreeHGlobal(desc.AssetsDir);
            if (desc.ShaderDir != IntPtr.Zero) Marshal.FreeHGlobal(desc.ShaderDir);
        }
    }

    public string Version
    {
        get
        {
            var ptr = TucanoApi.tucano_runtime_version();
            return Marshal.PtrToStringAnsi(ptr) ?? "unknown";
        }
    }

    public IntPtr NativeWindowHandle =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_runtime_native_window(_handle) : IntPtr.Zero;

    public IntPtr ViewportHandle =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_runtime_viewport_handle(_handle) : IntPtr.Zero;

    public void Resize(uint width, uint height)
    {
        if (_handle != IntPtr.Zero)
            TucanoApi.tucano_runtime_resize(_handle, width, height);
    }

    public bool Render()
    {
        if (_handle == IntPtr.Zero) return false;
        return TucanoApi.tucano_runtime_render(_handle);
    }

    public float LastFrameMs =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_runtime_last_frame_ms(_handle) : 0f;

    public uint DrawCalls =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_runtime_draw_calls(_handle) : 0;

    public uint MeshletsDrawn =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_runtime_meshlets_drawn(_handle) : 0;

    public float TimeOfDay
    {
        get => _handle != IntPtr.Zero ? TucanoApi.tucano_settings_get_time_of_day(_handle) : 0f;
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_settings_set_time_of_day(_handle, value); }
    }

    public float CloudCoverage
    {
        get => _handle != IntPtr.Zero ? TucanoApi.tucano_settings_get_cloud_coverage(_handle) : 0f;
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_settings_set_cloud_coverage(_handle, value); }
    }

    public bool EnableBloom
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_settings_get_enable_bloom(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_settings_set_enable_bloom(_handle, value); }
    }

    public bool EnableAO
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_settings_get_enable_ao(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_settings_set_enable_ao(_handle, value); }
    }

    public bool EnableClouds
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_settings_get_enable_clouds(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_settings_set_enable_clouds(_handle, value); }
    }

    public bool EnableAtmosphere
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_settings_get_enable_atmosphere(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_settings_set_enable_atmosphere(_handle, value); }
    }

    public void SetCameraPosition(float x, float y, float z)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_camera_set_position(_scene, new TucanoVec3(x, y, z));
    }

    public TucanoVec3 GetCameraPosition()
    {
        if (_scene == IntPtr.Zero) return default;
        TucanoApi.tucano_camera_get_position(_scene, out var pos);
        return pos;
    }

    public TucanoVec3 GetCameraForward()
    {
        if (_scene == IntPtr.Zero) return new TucanoVec3(0f, 0f, -1f);
        TucanoApi.tucano_camera_get_forward(_scene, out var dir);
        return dir;
    }

    public void CameraLookAt(float x, float y, float z)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_camera_look_at(_scene, new TucanoVec3(x, y, z));
    }

    public void CameraSetFov(float fovYRad)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_camera_set_fov_y_rad(_scene, fovYRad);
    }

    public void CameraFly(float dt, float moveX, float moveY, float moveZ, float yawDelta, float pitchDelta)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_camera_fly(_scene, dt, new TucanoVec3(moveX, moveY, moveZ), yawDelta, pitchDelta);
    }

    public uint ObjectCount =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_scene_object_count(_scene) : 0;

    public (float x, float y, float z) GetObjectPosition(uint index)
    {
        var pos = TucanoApi.tucano_scene_object_position(_scene, index);
        return (pos.X, pos.Y, pos.Z);
    }

    public string? GetObjectName(uint index)
    {
        if (_scene == IntPtr.Zero) return null;
        var ptr = TucanoApi.tucano_scene_object_name(_scene, index);
        return Marshal.PtrToStringAnsi(ptr);
    }

    // ── Viewport behaviour ──

    /// Runtime-side fly camera inside the embedded viewport (RMB looks, WASD flies).
    public bool CameraNavigation
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_runtime_get_camera_navigation(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_runtime_set_camera_navigation(_handle, value); }
    }

    /// The engine's own ImGui perf HUD. Off inside the editor — the status bar already shows FPS.
    public bool OverlayVisible
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_runtime_get_overlay_visible(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_runtime_set_overlay_visible(_handle, value); }
    }

    // -- Celestial bodies --
    // Derived from TimeOfDay / DayOfYear / LatitudeDeg in the environment. Directions point FROM
    // the body toward the scene, so negate one to look at it.

    public TucanoVec3 SunDirection =>
        _handle == IntPtr.Zero ? default : TucanoApi.tucano_sky_sun_direction(_handle);

    public TucanoVec3 MoonDirection =>
        _handle == IntPtr.Zero ? default : TucanoApi.tucano_sky_moon_direction(_handle);

    /// 0 = new, 0.5 = full.
    public float MoonPhase =>
        _handle == IntPtr.Zero ? 0f : TucanoApi.tucano_sky_moon_phase(_handle);

    /// Fraction of the lunar disc that is lit.
    public float MoonIllumination =>
        _handle == IntPtr.Zero ? 0f : TucanoApi.tucano_sky_moon_illumination(_handle);

    // ── World Machine (WM-4) ──────────────────────────
    /// Runs the GPU cell-cull parity self-test. Returns the number of CPU/GPU disagreements
    /// (0 = the compute shader matches the reference); negative values are harness errors.
    public int WorldCullSelfTest() =>
        _handle == IntPtr.Zero ? -1 : TucanoApi.tucano_world_cull_selftest(_handle);

    /// Visible-cell count the reference finds in the self-test scene.
    public int WorldCullVisibleCount() =>
        _handle == IntPtr.Zero ? -1 : TucanoApi.tucano_world_cull_selftest_visible_count(_handle);

    public bool Screenshot(string pngPath) =>
        _handle != IntPtr.Zero && TucanoApi.tucano_runtime_screenshot(_handle, pngPath);

    // ── Scene authoring ──

    public const uint InvalidObject = 0xFFFFFFFFu;

    /// Spawns a primitive; returns its object index or <see cref="InvalidObject"/>.
    public uint SpawnPrimitive(TucanoPrimitive prim, float x, float y, float z, float scale = 1f)
    {
        if (_scene == IntPtr.Zero) return InvalidObject;
        return TucanoApi.tucano_scene_spawn_primitive(_scene, prim, new TucanoVec3(x, y, z), scale);
    }

    public void SetObjectPosition(uint index, float x, float y, float z)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_scene_set_object_position(_scene, index, new TucanoVec3(x, y, z));
    }

    public void SetObjectColor(uint index, float r, float g, float b)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_scene_set_object_color(_scene, index, new TucanoVec3(r, g, b));
    }

    public void RemoveObject(uint index)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_scene_remove_object(_scene, index);
    }

    public void ClearScene()
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_scene_clear(_scene);
    }

    /// Object picked in the viewport, or -1. Settable to drive the pick from the Outliner.
    public int Selected
    {
        get => _scene != IntPtr.Zero ? TucanoApi.tucano_scene_get_selected(_scene) : -1;
        set { if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_selected(_scene, value); }
    }

    /// Euler angles are radians, XYZ order.
    public (TucanoVec3 pos, TucanoVec3 euler, TucanoVec3 scale) GetObjectTransform(uint index)
    {
        if (_scene == IntPtr.Zero) return (default, default, new TucanoVec3(1, 1, 1));
        TucanoApi.tucano_scene_get_object_transform(_scene, index, out var p, out var e, out var s);
        return (p, e, s);
    }

    public void SetObjectTransform(uint index, TucanoVec3 pos, TucanoVec3 eulerRad, TucanoVec3 scale)
    {
        if (_scene != IntPtr.Zero)
            TucanoApi.tucano_scene_set_object_transform(_scene, index, pos, eulerRad, scale);
    }

    // ── Transform gizmo ──

    public TucanoGizmoOp GizmoOperation
    {
        get => _handle != IntPtr.Zero ? TucanoApi.tucano_gizmo_get_operation(_handle) : TucanoGizmoOp.Translate;
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_gizmo_set_operation(_handle, value); }
    }

    public bool GizmoEnabled
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_gizmo_get_enabled(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_gizmo_set_enabled(_handle, value); }
    }

    public bool GizmoWorldSpace
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_gizmo_get_world_space(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_gizmo_set_world_space(_handle, value); }
    }

    /// Snap step: metres (translate), degrees (rotate), factor (scale). 0 disables.
    public float GizmoSnap
    {
        get => _handle != IntPtr.Zero ? TucanoApi.tucano_gizmo_get_snap(_handle) : 0f;
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_gizmo_set_snap(_handle, value); }
    }

    public TucanoVec3 GetObjectColor(uint index)
    {
        if (_scene == IntPtr.Zero) return new TucanoVec3(1, 1, 1);
        TucanoApi.tucano_scene_get_object_color(_scene, index, out var rgb);
        return rgb;
    }

    // ── Input ──

    public bool IsButtonHeld(int buttonCode) =>
        _handle != IntPtr.Zero && TucanoApi.tucano_input_is_button_held(_handle, buttonCode);

    public (float dx, float dy) GetMouseDelta()
    {
        if (_handle == IntPtr.Zero) return (0f, 0f);
        TucanoApi.tucano_input_get_mouse_delta(_handle, out var dx, out var dy);
        return (dx, dy);
    }

    public bool IsVirtualButtonHeld(string name) =>
        _handle != IntPtr.Zero && TucanoApi.tucano_input_is_virtual_button_held(_handle, name);

    public float GetVirtualAxis(string name) =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_input_get_virtual_axis(_handle, name) : 0f;

    public void BindButton(string name, int buttonCode, int modifiers = 0, bool repeatable = false)
    {
        if (_handle != IntPtr.Zero)
            TucanoApi.tucano_input_bind_button(_handle, name, buttonCode, modifiers, repeatable);
    }

    public void ResetInputBindings()
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_input_reset_bindings(_handle);
    }

    public string GetButtonName(int buttonCode) =>
        Marshal.PtrToStringAnsi(TucanoApi.tucano_input_button_name(buttonCode)) ?? "Unassigned";

    public int GetButtonFromName(string name) => TucanoApi.tucano_input_button_from_name(name);

    public int ButtonCodeCount => TucanoApi.tucano_input_button_count();

    // ── Animation ──

    /// Imports a mesh together with its rig and clips when the file has them.
    public uint ImportAnimatedMesh(string path, float x, float y, float z, float scale = 1f)
    {
        if (_scene == IntPtr.Zero) return InvalidObject;
        return TucanoApi.tucano_scene_import_animated_mesh(_scene, path, new TucanoVec3(x, y, z), scale);
    }

    public uint GetClipCount(uint obj) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_anim_clip_count(_scene, obj) : 0;

    public string GetClipName(uint obj, uint clip) =>
        Marshal.PtrToStringAnsi(TucanoApi.tucano_anim_clip_name(_scene, obj, clip)) ?? "";

    public float GetClipDuration(uint obj, uint clip) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_anim_clip_duration(_scene, obj, clip) : 0f;

    public uint GetBoneCount(uint obj) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_anim_bone_count(_scene, obj) : 0;

    public void PlayClip(uint obj, uint clip, bool loop = true)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_anim_play(_scene, obj, clip, loop);
    }

    public void StopClip(uint obj)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_anim_stop(_scene, obj);
    }

    public void PauseClip(uint obj, bool paused)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_anim_pause(_scene, obj, paused);
    }

    public bool IsClipPlaying(uint obj) =>
        _scene != IntPtr.Zero && TucanoApi.tucano_anim_is_playing(_scene, obj);

    public int GetCurrentClip(uint obj) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_anim_current_clip(_scene, obj) : -1;

    public float GetClipTime(uint obj) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_anim_get_time(_scene, obj) : 0f;

    public void SetClipTime(uint obj, float time)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_anim_set_time(_scene, obj, time);
    }

    public float GetClipSpeed(uint obj) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_anim_get_speed(_scene, obj) : 1f;

    public void SetClipSpeed(uint obj, float speed)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_anim_set_speed(_scene, obj, speed);
    }

    // ── Skybox ──

    /// Re-cooks image-based lighting from an .hdr. False means the file could not be used and the
    /// previous environment is still in place.
    public bool SetSkyboxTexture(string hdriPath) =>
        _handle != IntPtr.Zero && TucanoApi.tucano_skybox_set_texture(_handle, hdriPath);

    public string GetSkyboxTexture() =>
        _handle == IntPtr.Zero ? "" : Marshal.PtrToStringAnsi(TucanoApi.tucano_skybox_get_texture(_handle)) ?? "";

    public float SkyboxBrightness
    {
        get => _handle != IntPtr.Zero ? TucanoApi.tucano_skybox_get_brightness(_handle) : 0f;
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_skybox_set_brightness(_handle, value); }
    }

    // ── Environment ──

    public TucanoEnvironment GetEnvironment()
    {
        TucanoApi.tucano_env_get(_handle, out var env);
        return env;
    }

    public void SetEnvironment(TucanoEnvironment env)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_env_set(_handle, ref env);
    }

    public TucanoRain GetRain()
    {
        TucanoApi.tucano_rain_get(_handle, out var rain);
        return rain;
    }

    public void SetRain(TucanoRain rain)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_rain_set(_handle, ref rain);
    }

    // ── Lights ──

    public uint LightCount => _scene != IntPtr.Zero ? TucanoApi.tucano_scene_light_count(_scene) : 0;

    public uint AddLight(TucanoLight light)
    {
        if (_scene == IntPtr.Zero) return InvalidObject;
        return TucanoApi.tucano_scene_add_light(_scene, ref light);
    }

    public TucanoLight GetLight(uint index)
    {
        TucanoApi.tucano_scene_get_light(_scene, index, out var l);
        return l;
    }

    public void SetLight(uint index, TucanoLight light)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_light(_scene, index, ref light);
    }

    public void RemoveLight(uint index)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_remove_light(_scene, index);
    }

    // ── Materials / objects ──

    public TucanoMaterial GetObjectMaterial(uint index)
    {
        TucanoApi.tucano_scene_get_object_material(_scene, index, out var m);
        return m;
    }

    public void SetObjectMaterial(uint index, TucanoMaterial mat)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_object_material(_scene, index, ref mat);
    }

    public uint DuplicateObject(uint index, float dx, float dy, float dz)
    {
        if (_scene == IntPtr.Zero) return InvalidObject;
        return TucanoApi.tucano_scene_duplicate_object(_scene, index, new TucanoVec3(dx, dy, dz));
    }

    public void SetObjectName(uint index, string name)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_object_name(_scene, index, name);
    }

    public void SetObjectVisible(uint index, bool visible)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_object_visible(_scene, index, visible);
    }

    public bool GetObjectVisible(uint index) =>
        _scene == IntPtr.Zero || TucanoApi.tucano_scene_get_object_visible(_scene, index);

    /// Outliner group path, e.g. "Props/Crates". Empty means the scene root.
    public void SetObjectFolder(uint index, string folder)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_object_folder(_scene, index, folder ?? "");
    }

    public string GetObjectFolder(uint index)
    {
        if (_scene == IntPtr.Zero) return "";
        return Marshal.PtrToStringAnsi(TucanoApi.tucano_scene_get_object_folder(_scene, index)) ?? "";
    }

    /// Imports a .gltf/.glb; returns the first new object index or <see cref="InvalidObject"/>.
    public uint ImportMesh(string path, float x, float y, float z, float scale = 1f)
    {
        if (_scene == IntPtr.Zero) return InvalidObject;
        return TucanoApi.tucano_scene_import_mesh(_scene, path, new TucanoVec3(x, y, z), scale);
    }

    // ── Save / load ──

    public bool SaveScene(string path) =>
        _scene != IntPtr.Zero && TucanoApi.tucano_scene_save(_scene, path);

    public bool LoadScene(string path) =>
        _scene != IntPtr.Zero && TucanoApi.tucano_scene_load(_scene, path);

    // ── Physics & play ──

    public void SetObjectPhysics(uint index, TucanoPhysicsKind kind, float mass = 1f)
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_scene_set_object_physics(_scene, index, kind, mass);
    }

    public TucanoPhysicsKind GetObjectPhysics(uint index) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_scene_get_object_physics(_scene, index) : TucanoPhysicsKind.None;

    public float GetObjectMass(uint index) =>
        _scene != IntPtr.Zero ? TucanoApi.tucano_scene_get_object_mass(_scene, index) : 1f;

    public bool PlayStart() => _scene != IntPtr.Zero && TucanoApi.tucano_play_start(_scene);

    public void PlayPause(bool paused)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_play_pause(_handle, paused);
    }

    public void PlayStop()
    {
        if (_scene != IntPtr.Zero) TucanoApi.tucano_play_stop(_scene);
    }

    public TucanoPlayState PlayState =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_play_state(_handle) : TucanoPlayState.Stopped;

    public uint ColliderCount =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_play_collider_count(_handle) : 0;

    public uint FailedColliderCount =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_play_failed_collider_count(_handle) : 0;

    // ── Audio (Phase I-2) ──

    public bool AudioInit() =>
        _handle != IntPtr.Zero && TucanoApi.tucano_audio_init(_handle);

    public void AudioShutdown()
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_shutdown(_handle);
    }

    public bool AudioInitialized =>
        _handle != IntPtr.Zero && TucanoApi.tucano_audio_is_initialized(_handle);

    public float MasterVolume
    {
        get => _handle != IntPtr.Zero ? TucanoApi.tucano_audio_get_master_volume(_handle) : 0f;
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_set_master_volume(_handle, value); }
    }

    public bool AudioPaused
    {
        get => _handle != IntPtr.Zero && TucanoApi.tucano_audio_is_paused(_handle);
        set { if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_set_paused(_handle, value); }
    }

    public int AudioLoadClip(string path) =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_audio_load_clip(_handle, path) : -1;

    public void AudioUnloadClip(int clipId)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_unload_clip(_handle, clipId);
    }

    public float AudioClipDuration(int clipId) =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_audio_clip_duration(_handle, clipId) : 0f;

    public int AudioCreateSource() =>
        _handle != IntPtr.Zero ? TucanoApi.tucano_audio_create_source(_handle) : -1;

    public void AudioDestroySource(int sourceId)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_destroy_source(_handle, sourceId);
    }

    public void AudioSourcePlay(int sourceId, int clipId, float volume, bool loop)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_play(_handle, sourceId, clipId, volume, loop);
    }

    public void AudioSourceStop(int sourceId)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_stop(_handle, sourceId);
    }

    public void AudioSourcePause(int sourceId)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_pause(_handle, sourceId);
    }

    public void AudioSourceResume(int sourceId)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_resume(_handle, sourceId);
    }

    public bool AudioSourceIsPlaying(int sourceId) =>
        _handle != IntPtr.Zero && TucanoApi.tucano_audio_source_is_playing(_handle, sourceId);

    public void AudioSourceSetPosition(int sourceId, float x, float y, float z)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_set_position(_handle, sourceId, new TucanoVec3(x, y, z));
    }

    public void AudioSourceSetVolume(int sourceId, float volume)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_set_volume(_handle, sourceId, volume);
    }

    public void AudioSourceSetLooping(int sourceId, bool loop)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_source_set_looping(_handle, sourceId, loop);
    }

    public void AudioListenerSetPosition(float x, float y, float z)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_listener_set_position(_handle, new TucanoVec3(x, y, z));
    }

    public void AudioListenerSetOrientation(float fx, float fy, float fz, float ux, float uy, float uz)
    {
        if (_handle != IntPtr.Zero) TucanoApi.tucano_audio_listener_set_orientation(_handle,
            new TucanoVec3(fx, fy, fz), new TucanoVec3(ux, uy, uz));
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        AudioShutdown();

        if (_scene != IntPtr.Zero)
        {
            TucanoApi.tucano_scene_destroy(_scene);
            _scene = IntPtr.Zero;
        }
        if (_handle != IntPtr.Zero)
        {
            TucanoApi.tucano_runtime_shutdown(_handle);
            _handle = IntPtr.Zero;
        }
    }
}

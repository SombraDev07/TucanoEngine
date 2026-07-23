using System;
using System.Runtime.InteropServices;

namespace EditorCore.Interop;

[StructLayout(LayoutKind.Sequential)]
public struct TucanoVec3
{
    public float X, Y, Z;

    public TucanoVec3(float x, float y, float z)
    {
        X = x; Y = y; Z = z;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct TucanoInitDesc
{
    public uint Width;
    public uint Height;
    public IntPtr Title;
    public IntPtr AssetsDir;
    public IntPtr ShaderDir;
    [MarshalAs(UnmanagedType.I1)]
    public bool EnableDebugLayer;
    [MarshalAs(UnmanagedType.I1)]
    public bool Borderless;
}

public enum TucanoResult
{
    Success = 0,
    Error = -1,
}

public enum TucanoPrimitive
{
    Cube = 0,
    Sphere = 1,
    Plane = 2,
}

public enum TucanoGizmoOp
{
    Translate = 0,
    Rotate = 1,
    Scale = 2,
}

public enum TucanoLightType
{
    Directional = 0,
    Point = 1,
    Spot = 2,
}

public enum TucanoPhysicsKind
{
    None = 0,
    Static = 1,
    Dynamic = 2,
}

public enum TucanoPlayState
{
    Stopped = 0,
    Running = 1,
    Paused = 2,
}

// Booleans are int32 on the native side so these structs stay blittable — no marshalling
// attributes, no layout surprises.
[StructLayout(LayoutKind.Sequential)]
public struct TucanoEnvironment
{
    public int EnableAtmosphere, UseBrunetonAtmosphere, AtmosphereDrivesSun;
    public float TimeOfDay, Turbidity, FogDensity, FogHeight;
    public TucanoVec3 Wind;

    public int EnableClouds, EnableCloudShadows, EnableCloudGodRays, CloudsDriveRain;
    public float CloudCoverage, CloudDensity, CloudAltitude, CloudThickness;
    public float CloudShadowStrength, CloudGodRayStrength, CloudStorminess;

    public int EnableBloom, EnableAO, EnableTonemap, EnableAutoExposure;
    public float BloomStrength, AoIntensity, AoRadius;
    public float ExposureMin, ExposureMax, ExposureAdapt, ExposureTarget;

    public int EnableShadows, EnablePCSS, EnableContactShadows, EnableSSR;
    public int EnableRTShadows, EnableRTReflections, EnableVoxelGI, EnableIBL;
    public int GiTier, ShadowMapSize;
    public float PcssLightSize;

    // Night sky — appended to match the C struct, which grows only at the end.
    public int EnableMoon, EnableStars;
    public float MoonIntensity, MoonDiscBrightness, MoonAngularRadiusDeg;
    public float StarIntensity, StarTwinkle, StarSizeDeg;
    public float PurkinjeStrength;
    public float LatitudeDeg, DayOfYear;
}

[StructLayout(LayoutKind.Sequential)]
public struct TucanoRain
{
    public int Enabled, EnableSceneRain, EnableWorldSplashes;
    public float Amount, MaxViewDist, DiffuseDarkening;
    public float PuddlesAmount, PuddlesMask, PuddlesRipple, PuddlesSSR;
    public float SplashesAmount, RainDropsAmount, RainDropsSpeed, RainDropsLighting;
    public float StreakIntensity, StreakSpeed, StreakLayers, MistAmount, GlossBoost;
    public float SceneRainIntensity, Radius;
    public TucanoVec3 Color;
    public TucanoVec3 Wind;
}

[StructLayout(LayoutKind.Sequential)]
public struct TucanoLight
{
    public int Type, CastShadows;
    public TucanoVec3 Position, Direction, Color;
    public float Intensity, Range, InnerCone, OuterCone;
}

[StructLayout(LayoutKind.Sequential)]
public struct TucanoMaterial
{
    public TucanoVec3 BaseColor, Emissive;
    public float Metallic, Roughness, Alpha;
}

public static class TucanoApi
{
    private const string DllName = "TucanoEditorAPI.dll";

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_runtime_version();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_runtime_init(ref TucanoInitDesc desc);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_runtime_shutdown(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_runtime_alive(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_runtime_native_window(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_runtime_viewport_handle(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_runtime_resize(IntPtr rt, uint w, uint h);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_runtime_render(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_scene_create(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_destroy(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_load_skylab(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_set_position(IntPtr scene, TucanoVec3 pos);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_get_position(IntPtr scene, out TucanoVec3 pos);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_get_forward(IntPtr scene, out TucanoVec3 dir);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_look_at(IntPtr scene, TucanoVec3 target);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_set_fov_y_rad(IntPtr scene, float fovYRad);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_fly(IntPtr scene, float dt, TucanoVec3 move, float yawDelta, float pitchDelta);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_camera_set_perspective(IntPtr scene, float fovYRad, float aspect, float zNear, float zFar);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_object_count(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern TucanoVec3 tucano_scene_object_position(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_scene_object_name(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_runtime_screenshot(IntPtr rt, [MarshalAs(UnmanagedType.LPStr)] string pngPath);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_runtime_set_camera_navigation(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool enabled);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_runtime_get_camera_navigation(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_runtime_set_overlay_visible(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool visible);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_runtime_get_overlay_visible(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_spawn_primitive(IntPtr scene, TucanoPrimitive prim, TucanoVec3 pos, float scale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_position(IntPtr scene, uint index, TucanoVec3 pos);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_color(IntPtr scene, uint index, TucanoVec3 rgb);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_remove_object(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_clear(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int tucano_scene_get_selected(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_selected(IntPtr scene, int index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_get_object_transform(
        IntPtr scene, uint index, out TucanoVec3 position, out TucanoVec3 eulerRad, out TucanoVec3 scale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_transform(
        IntPtr scene, uint index, TucanoVec3 position, TucanoVec3 eulerRad, TucanoVec3 scale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_get_object_color(IntPtr scene, uint index, out TucanoVec3 rgb);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_gizmo_set_operation(IntPtr rt, TucanoGizmoOp op);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern TucanoGizmoOp tucano_gizmo_get_operation(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_gizmo_set_enabled(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool enabled);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_gizmo_get_enabled(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_gizmo_set_world_space(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool worldSpace);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_gizmo_get_world_space(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_gizmo_set_snap(IntPtr rt, float snap);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_gizmo_get_snap(IntPtr rt);

    // ── Input (virtual input, Phase I-0) ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_input_is_button_held(IntPtr rt, int buttonCode);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_input_get_mouse_delta(IntPtr rt, out float dx, out float dy);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_input_is_virtual_button_held(IntPtr rt, [MarshalAs(UnmanagedType.LPStr)] string name);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_input_get_virtual_axis(IntPtr rt, [MarshalAs(UnmanagedType.LPStr)] string name);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_input_bind_button(IntPtr rt, [MarshalAs(UnmanagedType.LPStr)] string name, int buttonCode, int modifiers, [MarshalAs(UnmanagedType.I1)] bool repeatable);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_input_reset_bindings(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_input_button_name(int buttonCode);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int tucano_input_button_from_name([MarshalAs(UnmanagedType.LPStr)] string name);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int tucano_input_button_count();

    // ── Animation ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_import_animated_mesh(IntPtr scene, [MarshalAs(UnmanagedType.LPStr)] string path, TucanoVec3 position, float scale);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_anim_clip_count(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_anim_clip_name(IntPtr scene, uint obj, uint clip);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_anim_clip_duration(IntPtr scene, uint obj, uint clip);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_anim_bone_count(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_anim_play(IntPtr scene, uint obj, uint clip, [MarshalAs(UnmanagedType.I1)] bool loop);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_anim_stop(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_anim_pause(IntPtr scene, uint obj, [MarshalAs(UnmanagedType.I1)] bool paused);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_anim_is_playing(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int tucano_anim_current_clip(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_anim_get_time(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_anim_set_time(IntPtr scene, uint obj, float time);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_anim_get_speed(IntPtr scene, uint obj);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_anim_set_speed(IntPtr scene, uint obj, float speed);

    // ── Celestial bodies ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern TucanoVec3 tucano_sky_sun_direction(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern TucanoVec3 tucano_sky_moon_direction(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_sky_moon_phase(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_sky_moon_illumination(IntPtr rt);

    // ── Skybox ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_skybox_set_texture(IntPtr rt, [MarshalAs(UnmanagedType.LPStr)] string hdriPath);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_skybox_get_texture(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_skybox_set_brightness(IntPtr rt, float brightness);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_skybox_get_brightness(IntPtr rt);

    // ── Environment ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_env_get(IntPtr rt, out TucanoEnvironment env);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_env_set(IntPtr rt, ref TucanoEnvironment env);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_rain_get(IntPtr rt, out TucanoRain rain);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_rain_set(IntPtr rt, ref TucanoRain rain);

    // ── Lights ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_light_count(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_add_light(IntPtr scene, ref TucanoLight light);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_get_light(IntPtr scene, uint index, out TucanoLight light);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_light(IntPtr scene, uint index, ref TucanoLight light);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_remove_light(IntPtr scene, uint index);

    // ── Materials, duplicate, rename ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_get_object_material(IntPtr scene, uint index, out TucanoMaterial mat);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_material(IntPtr scene, uint index, ref TucanoMaterial mat);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_duplicate_object(IntPtr scene, uint index, TucanoVec3 offset);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_name(IntPtr scene, uint index, [MarshalAs(UnmanagedType.LPStr)] string name);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_visible(IntPtr scene, uint index, [MarshalAs(UnmanagedType.I1)] bool visible);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_scene_get_object_visible(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_folder(IntPtr scene, uint index, [MarshalAs(UnmanagedType.LPStr)] string folder);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tucano_scene_get_object_folder(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_scene_import_mesh(IntPtr scene, [MarshalAs(UnmanagedType.LPStr)] string path, TucanoVec3 position, float scale);

    // ── Save / load ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_scene_save(IntPtr scene, [MarshalAs(UnmanagedType.LPStr)] string path);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_scene_load(IntPtr scene, [MarshalAs(UnmanagedType.LPStr)] string path);

    // ── Physics & play ──

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_scene_set_object_physics(IntPtr scene, uint index, TucanoPhysicsKind kind, float mass);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern TucanoPhysicsKind tucano_scene_get_object_physics(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_scene_get_object_mass(IntPtr scene, uint index);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_play_start(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_play_pause(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool paused);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_play_stop(IntPtr scene);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern TucanoPlayState tucano_play_state(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_play_collider_count(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_play_failed_collider_count(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_runtime_last_frame_ms(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_runtime_draw_calls(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint tucano_runtime_meshlets_drawn(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_settings_set_time_of_day(IntPtr rt, float tod);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_settings_get_time_of_day(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_settings_set_cloud_coverage(IntPtr rt, float c);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern float tucano_settings_get_cloud_coverage(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_settings_set_enable_bloom(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool b);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_settings_get_enable_bloom(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_settings_set_enable_ao(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool b);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_settings_get_enable_ao(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_settings_set_enable_clouds(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool b);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_settings_get_enable_clouds(IntPtr rt);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tucano_settings_set_enable_atmosphere(IntPtr rt, [MarshalAs(UnmanagedType.I1)] bool b);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool tucano_settings_get_enable_atmosphere(IntPtr rt);
}

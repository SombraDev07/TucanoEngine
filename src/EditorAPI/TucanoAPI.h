#pragma once

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef TUCANO_API_EXPORTS
#define TUCANO_API __declspec(dllexport)
#else
#define TUCANO_API __declspec(dllimport)
#endif
#else
#define TUCANO_API __attribute__((visibility("default")))
#endif

typedef struct TucanoRuntime TucanoRuntime;
typedef struct TucanoScene TucanoScene;

typedef struct {
  uint32_t width;
  uint32_t height;
  const char* title;
  const char* assetsDir;
  const char* shaderDir;
  bool enableDebugLayer;
  bool borderless;
} TucanoInitDesc;

typedef struct {
  float x, y, z;
} TucanoVec3;

// ── Lifecycle ────────────────────────────────────────

TUCANO_API const char* tucano_runtime_version(void);

TUCANO_API TucanoRuntime* tucano_runtime_init(const TucanoInitDesc* desc);

TUCANO_API void tucano_runtime_shutdown(TucanoRuntime* rt);

// True while the window is open and no errors occurred.
TUCANO_API bool tucano_runtime_alive(TucanoRuntime* rt);

// ── Viewport / Window ────────────────────────────────

// Returns the native window handle (HWND on Win32) — embed into Avalonia
TUCANO_API void* tucano_runtime_native_window(TucanoRuntime* rt);

// Returns DX12 swapchain backbuffer HWND or nullptr.  After a render cycle
// the swapchain texture contains the latest frame (Present state).
TUCANO_API void* tucano_runtime_viewport_handle(TucanoRuntime* rt);

TUCANO_API void tucano_runtime_resize(TucanoRuntime* rt, uint32_t w, uint32_t h);

// Render one frame.  Returns false on error.
TUCANO_API bool tucano_runtime_render(TucanoRuntime* rt);

// Render one frame and save the viewport backbuffer as a PNG. Captures only the engine's own
// output (never the desktop). Also the basis for asset thumbnails later. Returns false on error.
TUCANO_API bool tucano_runtime_screenshot(TucanoRuntime* rt, const char* pngPath);

// Editor-style viewport navigation handled inside the runtime (the embedded child window owns
// the mouse/keyboard while hovered): RMB+drag looks, WASD flies, QE up/down, Shift boosts,
// scroll dollies. On by default. Turn off to drive the camera purely from the editor.
TUCANO_API void tucano_runtime_set_camera_navigation(TucanoRuntime* rt, bool enabled);
TUCANO_API bool tucano_runtime_get_camera_navigation(TucanoRuntime* rt);

// The runtime's own ImGui perf overlay. Off makes sense inside the editor, which has its own
// status bar (otherwise two different FPS counters show up).
TUCANO_API void tucano_runtime_set_overlay_visible(TucanoRuntime* rt, bool visible);
TUCANO_API bool tucano_runtime_get_overlay_visible(TucanoRuntime* rt);

// ── Scene ────────────────────────────────────────────

TUCANO_API TucanoScene* tucano_scene_create(TucanoRuntime* rt);

TUCANO_API void tucano_scene_destroy(TucanoScene* scene);

TUCANO_API void tucano_scene_load_skylab(TucanoScene* scene);

// ── Camera ───────────────────────────────────────────

TUCANO_API void tucano_camera_set_position(TucanoScene* scene, TucanoVec3 pos);

TUCANO_API void tucano_camera_get_position(TucanoScene* scene, TucanoVec3* pos);

// Unit view direction — use it to place new objects in front of the camera.
TUCANO_API void tucano_camera_get_forward(TucanoScene* scene, TucanoVec3* dir);

TUCANO_API void tucano_camera_look_at(TucanoScene* scene, TucanoVec3 target);

TUCANO_API void tucano_camera_set_fov_y_rad(TucanoScene* scene, float fovYRad);

TUCANO_API void tucano_camera_fly(TucanoScene* scene,
                                   float dt,
                                   TucanoVec3 move,
                                   float yawDelta,
                                   float pitchDelta);

TUCANO_API void tucano_camera_set_perspective(TucanoScene* scene,
                                               float fovYRad,
                                               float aspect,
                                               float zNear,
                                               float zFar);

// ── Scene objects ────────────────────────────────────

TUCANO_API uint32_t tucano_scene_object_count(TucanoScene* scene);

TUCANO_API TucanoVec3 tucano_scene_object_position(TucanoScene* scene, uint32_t index);

TUCANO_API const char* tucano_scene_object_name(TucanoScene* scene, uint32_t index);

// ── Scene authoring (build a level in the viewport) ───

typedef enum {
  TUCANO_PRIM_CUBE = 0,
  TUCANO_PRIM_SPHERE = 1,
  TUCANO_PRIM_PLANE = 2,
} TucanoPrimitive;

// Spawns a primitive at `pos`. Returns its object index, or 0xFFFFFFFF on failure.
TUCANO_API uint32_t tucano_scene_spawn_primitive(TucanoScene* scene, TucanoPrimitive prim, TucanoVec3 pos,
                                                 float scale);

// ── Animation (Phase I-1) ────────────────────────────

// Imports a .gltf/.glb *with* its skeleton and animation clips, if it has them. Falls back to a
// plain mesh import when the file has no skin. Returns the first object index or 0xFFFFFFFF.
TUCANO_API uint32_t tucano_scene_import_animated_mesh(TucanoScene* scene, const char* path,
                                                      TucanoVec3 position, float scale);

// Clips available on an object (0 when it has no animation).
TUCANO_API uint32_t tucano_anim_clip_count(TucanoScene* scene, uint32_t object);
TUCANO_API const char* tucano_anim_clip_name(TucanoScene* scene, uint32_t object, uint32_t clip);
TUCANO_API float tucano_anim_clip_duration(TucanoScene* scene, uint32_t object, uint32_t clip);
TUCANO_API uint32_t tucano_anim_bone_count(TucanoScene* scene, uint32_t object);

// Playback. The runtime advances time and rebuilds the skinning palette every frame.
TUCANO_API void tucano_anim_play(TucanoScene* scene, uint32_t object, uint32_t clip, bool loop);
TUCANO_API void tucano_anim_stop(TucanoScene* scene, uint32_t object);
TUCANO_API void tucano_anim_pause(TucanoScene* scene, uint32_t object, bool paused);
TUCANO_API bool tucano_anim_is_playing(TucanoScene* scene, uint32_t object);
TUCANO_API int32_t tucano_anim_current_clip(TucanoScene* scene, uint32_t object);
TUCANO_API float tucano_anim_get_time(TucanoScene* scene, uint32_t object);
TUCANO_API void tucano_anim_set_time(TucanoScene* scene, uint32_t object, float time);
TUCANO_API float tucano_anim_get_speed(TucanoScene* scene, uint32_t object);
TUCANO_API void tucano_anim_set_speed(TucanoScene* scene, uint32_t object, float speed);

// ── Skinning (Phase I-1) ─────────────────────────────

// Uploads this object's skinning palette: `count` matrices of 16 floats each, in the order the
// vertices' bone indices expect (world x inverseBindPose per bone). Passing count 0 makes the
// object rigid again. The renderer picks these up on the next frame.
TUCANO_API void tucano_scene_set_skinning_matrices(TucanoScene* scene, uint32_t index,
                                                   const float* matrices, uint32_t count);
TUCANO_API uint32_t tucano_scene_get_skinning_bone_count(TucanoScene* scene, uint32_t index);

// Replaces an object's mesh with a skinned test rig: a tall box split into `segments` rings, each
// ring weighted to one bone in a straight chain. Used to verify GPU skinning end to end without a
// character asset.
TUCANO_API uint32_t tucano_scene_spawn_skinned_test(TucanoScene* scene, TucanoVec3 pos,
                                                    uint32_t segments);

// Moves an existing object (index from spawn / object_count).
TUCANO_API void tucano_scene_set_object_position(TucanoScene* scene, uint32_t index, TucanoVec3 pos);

// Sets base color (0..1 RGB) of the object's first material.
TUCANO_API void tucano_scene_set_object_color(TucanoScene* scene, uint32_t index, TucanoVec3 rgb);

// Removes an object. Indices after it shift down by one.
TUCANO_API void tucano_scene_remove_object(TucanoScene* scene, uint32_t index);

// Clears every object (empty level).
TUCANO_API void tucano_scene_clear(TucanoScene* scene);

// ── Selection & transforms ───────────────────────────

// Index of the object selected in the viewport, or -1. Left-click picks; left-drag moves the pick
// across the camera-facing plane. Both are handled by the runtime while camera navigation is on.
TUCANO_API int32_t tucano_scene_get_selected(TucanoScene* scene);
TUCANO_API void tucano_scene_set_selected(TucanoScene* scene, int32_t index);

// Euler angles are in radians (XYZ order), scale is per-axis.
TUCANO_API void tucano_scene_get_object_transform(TucanoScene* scene, uint32_t index,
                                                  TucanoVec3* position, TucanoVec3* eulerRad,
                                                  TucanoVec3* scale);
TUCANO_API void tucano_scene_set_object_transform(TucanoScene* scene, uint32_t index,
                                                  TucanoVec3 position, TucanoVec3 eulerRad,
                                                  TucanoVec3 scale);

// Base color of the object's first material (0..1 RGB).
TUCANO_API void tucano_scene_get_object_color(TucanoScene* scene, uint32_t index, TucanoVec3* rgb);

// ── Transform gizmo ──────────────────────────────────

typedef enum {
  TUCANO_GIZMO_TRANSLATE = 0,
  TUCANO_GIZMO_ROTATE = 1,
  TUCANO_GIZMO_SCALE = 2,
} TucanoGizmoOp;

// The gizmo is drawn over the selected object. W/E/R switch mode inside the viewport, X toggles
// world/local — these mirror that so the editor UI can drive it too.
TUCANO_API void tucano_gizmo_set_operation(TucanoRuntime* rt, TucanoGizmoOp op);
TUCANO_API TucanoGizmoOp tucano_gizmo_get_operation(TucanoRuntime* rt);
TUCANO_API void tucano_gizmo_set_enabled(TucanoRuntime* rt, bool enabled);
TUCANO_API bool tucano_gizmo_get_enabled(TucanoRuntime* rt);
TUCANO_API void tucano_gizmo_set_world_space(TucanoRuntime* rt, bool worldSpace);
TUCANO_API bool tucano_gizmo_get_world_space(TucanoRuntime* rt);
// Snap increment: metres for translate, degrees for rotate, factor for scale. <= 0 disables.
TUCANO_API void tucano_gizmo_set_snap(TucanoRuntime* rt, float snap);
TUCANO_API float tucano_gizmo_get_snap(TucanoRuntime* rt);

// ── Stats ────────────────────────────────────────────

TUCANO_API float tucano_runtime_last_frame_ms(TucanoRuntime* rt);

TUCANO_API uint32_t tucano_runtime_draw_calls(TucanoRuntime* rt);

TUCANO_API uint32_t tucano_runtime_meshlets_drawn(TucanoRuntime* rt);

// ── Lights ───────────────────────────────────────────

typedef enum {
  TUCANO_LIGHT_DIRECTIONAL = 0,
  TUCANO_LIGHT_POINT = 1,
  TUCANO_LIGHT_SPOT = 2,
} TucanoLightType;

typedef struct {
  int32_t type;
  int32_t castShadows;
  TucanoVec3 position;
  TucanoVec3 direction;
  TucanoVec3 color;
  float intensity;
  float range;
  float innerCone; // radians
  float outerCone; // radians
} TucanoLight;

TUCANO_API uint32_t tucano_scene_light_count(TucanoScene* scene);
TUCANO_API uint32_t tucano_scene_add_light(TucanoScene* scene, const TucanoLight* light);
TUCANO_API void tucano_scene_get_light(TucanoScene* scene, uint32_t index, TucanoLight* out);
TUCANO_API void tucano_scene_set_light(TucanoScene* scene, uint32_t index, const TucanoLight* light);
TUCANO_API void tucano_scene_remove_light(TucanoScene* scene, uint32_t index);

// ── Materials ────────────────────────────────────────

typedef struct {
  TucanoVec3 baseColor;
  TucanoVec3 emissive;
  float metallic;
  float roughness;
  float alpha;
} TucanoMaterial;

TUCANO_API void tucano_scene_get_object_material(TucanoScene* scene, uint32_t index,
                                                 TucanoMaterial* out);
TUCANO_API void tucano_scene_set_object_material(TucanoScene* scene, uint32_t index,
                                                 const TucanoMaterial* mat);

// Copies the object (mesh shared, material cloned) offset by `offset`. Returns the new index.
TUCANO_API uint32_t tucano_scene_duplicate_object(TucanoScene* scene, uint32_t index,
                                                  TucanoVec3 offset);

// Renames an object (shown in the outliner).
TUCANO_API void tucano_scene_set_object_name(TucanoScene* scene, uint32_t index, const char* name);

// Viewport visibility. Hidden objects are skipped by every pass — g-buffer, shadows, ray tracing
// and GI — and are not pickable. Saved with the scene.
TUCANO_API void tucano_scene_set_object_visible(TucanoScene* scene, uint32_t index, bool visible);
TUCANO_API bool tucano_scene_get_object_visible(TucanoScene* scene, uint32_t index);

// Outliner group this object belongs to, as a path like "Props/Crates". Empty means scene root.
// Purely organisational — the engine ignores it — but it is saved with the scene.
TUCANO_API void tucano_scene_set_object_folder(TucanoScene* scene, uint32_t index, const char* folder);
TUCANO_API const char* tucano_scene_get_object_folder(TucanoScene* scene, uint32_t index);

// ── Asset import ─────────────────────────────────────

// Imports a .gltf/.glb, appending every mesh in it as scene objects placed at `position`.
// Returns the index of the first object added, or 0xFFFFFFFF on failure.
TUCANO_API uint32_t tucano_scene_import_mesh(TucanoScene* scene, const char* path,
                                             TucanoVec3 position, float scale);

// ── Physics & Play mode ──────────────────────────────

typedef enum {
  TUCANO_PHYSICS_NONE = 0,   // no collider; ignored while playing
  TUCANO_PHYSICS_STATIC = 1, // immovable collider (floors, walls)
  TUCANO_PHYSICS_DYNAMIC = 2 // simulated rigid body
} TucanoPhysicsKind;

// Collider assignment is per object and is saved with the scene.
TUCANO_API void tucano_scene_set_object_physics(TucanoScene* scene, uint32_t index,
                                                TucanoPhysicsKind kind, float mass);
TUCANO_API TucanoPhysicsKind tucano_scene_get_object_physics(TucanoScene* scene, uint32_t index);
TUCANO_API float tucano_scene_get_object_mass(TucanoScene* scene, uint32_t index);

typedef enum {
  TUCANO_PLAY_STOPPED = 0,
  TUCANO_PLAY_RUNNING = 1,
  TUCANO_PLAY_PAUSED = 2
} TucanoPlayState;

// Play builds rigid bodies from the colliders and starts stepping physics; Stop tears them down and
// restores every edit-time transform, so playing never damages the level being authored.
TUCANO_API bool tucano_play_start(TucanoScene* scene);
TUCANO_API void tucano_play_pause(TucanoRuntime* rt, bool paused);
TUCANO_API void tucano_play_stop(TucanoScene* scene);
TUCANO_API TucanoPlayState tucano_play_state(TucanoRuntime* rt);

// Colliders actually built by the last Play. A non-zero failed count means some object had a
// collider assigned that Jolt refused (usually degenerate bounds).
TUCANO_API uint32_t tucano_play_collider_count(TucanoRuntime* rt);
TUCANO_API uint32_t tucano_play_failed_collider_count(TucanoRuntime* rt);

// ── Save / load ──────────────────────────────────────

// Writes the scene (objects, lights, camera, environment) as JSON. Returns false on I/O error.
TUCANO_API bool tucano_scene_save(TucanoScene* scene, const char* path);

// Replaces the current scene with the one in `path`. Returns false if it can't be read or parsed.
TUCANO_API bool tucano_scene_load(TucanoScene* scene, const char* path);

// ── Input (Phase I-0: virtual input) ─────────────────
// Physical codes are engine-stable (see src/Input/InputFwd.h ButtonCode) — not GLFW values — so
// saved bindings keep working if the windowing backend changes.

TUCANO_API bool tucano_input_is_button_down(TucanoRuntime* rt, int buttonCode);
TUCANO_API bool tucano_input_is_button_held(TucanoRuntime* rt, int buttonCode);
TUCANO_API void tucano_input_get_mouse_delta(TucanoRuntime* rt, float* dx, float* dy);
TUCANO_API float tucano_input_get_scroll(TucanoRuntime* rt);

// Virtual buttons and axes are addressed by name, so rebinding never touches gameplay code.
TUCANO_API bool tucano_input_is_virtual_button_down(TucanoRuntime* rt, const char* name);
TUCANO_API bool tucano_input_is_virtual_button_held(TucanoRuntime* rt, const char* name);
TUCANO_API bool tucano_input_is_virtual_button_up(TucanoRuntime* rt, const char* name);
TUCANO_API float tucano_input_get_virtual_axis(TucanoRuntime* rt, const char* name);

// Binding management. `modifiers` is a bitmask: 1=Shift, 2=Ctrl, 4=Alt.
TUCANO_API void tucano_input_bind_button(TucanoRuntime* rt, const char* name, int buttonCode,
                                         int modifiers, bool repeatable);
TUCANO_API void tucano_input_unbind_button(TucanoRuntime* rt, const char* name);
TUCANO_API void tucano_input_bind_axis(TucanoRuntime* rt, const char* name, int axis,
                                       float deadZone, float sensitivity, bool invert,
                                       bool normalize);
TUCANO_API void tucano_input_reset_bindings(TucanoRuntime* rt);

// Name of a physical button, for binding UI. Never null.
TUCANO_API const char* tucano_input_button_name(int buttonCode);
TUCANO_API int tucano_input_button_from_name(const char* name);
TUCANO_API int tucano_input_button_count(void);

// ── Skybox / environment lighting (Phase I-1) ────────

// Re-cooks image-based lighting from an .hdr equirectangular map: diffuse irradiance plus the
// filtered specular chain. Path is resolved against the engine assets folder first, then as given.
// Returns false and keeps the current environment when the file can't be used.
TUCANO_API bool tucano_skybox_set_texture(TucanoRuntime* rt, const char* hdriPath);
TUCANO_API const char* tucano_skybox_get_texture(TucanoRuntime* rt);

// Brightness multiplier applied to the environment lighting.
TUCANO_API void tucano_skybox_set_brightness(TucanoRuntime* rt, float brightness);
TUCANO_API float tucano_skybox_get_brightness(TucanoRuntime* rt);

// ── Celestial bodies ─────────────────────────────────
// Read-only: these are derived from timeOfDay, dayOfYear and latitude in the environment struct.
// Directions point FROM the body TOWARD the scene, matching how light directions are stored, so
// negate one to get the direction you look to see the body.

TUCANO_API TucanoVec3 tucano_sky_sun_direction(TucanoRuntime* rt);
TUCANO_API TucanoVec3 tucano_sky_moon_direction(TucanoRuntime* rt);
/// 0 = new, 0.5 = full, 1 = new again.
TUCANO_API float tucano_sky_moon_phase(TucanoRuntime* rt);
/// Fraction of the lunar disc that is lit, 0..1.
TUCANO_API float tucano_sky_moon_illumination(TucanoRuntime* rt);

// ── Environment ──────────────────────────────────────
// One flat struct instead of a setter per field. Booleans are int32_t so the layout is blittable
// from C# with no marshalling attributes.

typedef struct {
  // Sun & atmosphere
  int32_t enableAtmosphere;
  int32_t useBrunetonAtmosphere;
  int32_t atmosphereDrivesSun;
  float timeOfDay; // 0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset
  float turbidity;
  float fogDensity;
  float fogHeight;
  TucanoVec3 wind;

  // Clouds
  int32_t enableClouds;
  int32_t enableCloudShadows;
  int32_t enableCloudGodRays;
  int32_t cloudsDriveRain;
  float cloudCoverage;
  float cloudDensity;
  float cloudAltitude;
  float cloudThickness;
  float cloudShadowStrength;
  float cloudGodRayStrength;
  float cloudStorminess;

  // Post processing
  int32_t enableBloom;
  int32_t enableAO;
  int32_t enableTonemap;
  int32_t enableAutoExposure;
  float bloomStrength;
  float aoIntensity;
  float aoRadius;
  float exposureMin;
  float exposureMax;
  float exposureAdapt;
  float exposureTarget;

  // Shadows, reflections, GI
  int32_t enableShadows;
  int32_t enablePCSS;
  int32_t enableContactShadows;
  int32_t enableSSR;
  int32_t enableRTShadows;
  int32_t enableRTReflections;
  int32_t enableVoxelGI;
  int32_t enableIBL;
  int32_t giTier; // 0=Off 1=Low 2=Medium 3=High (GITier)
  int32_t shadowMapSize;
  float pcssLightSize;

  // Night sky. Appended at the end so older layouts stay binary-compatible.
  int32_t enableMoon;
  int32_t enableStars;
  float moonIntensity;         // moonlight on the scene
  float moonDiscBrightness;    // the disc itself
  float moonAngularRadiusDeg;  // real moon is 0.26; larger reads better on screen
  float starIntensity;
  float starTwinkle;
  float starSizeDeg;
  float purkinjeStrength;      // night-vision desaturation in the tonemapper
  float latitudeDeg;           // observer position: decides which constellations are up
  float dayOfYear;             // 0..365; with latitude it sets the sidereal rotation
} TucanoEnvironment;

typedef struct {
  int32_t enabled;
  int32_t enableSceneRain;
  int32_t enableWorldSplashes;
  float amount;
  float maxViewDist;
  float diffuseDarkening;
  float puddlesAmount;
  float puddlesMask;
  float puddlesRipple;
  float puddlesSSR;
  float splashesAmount;
  float rainDropsAmount;
  float rainDropsSpeed;
  float rainDropsLighting;
  float streakIntensity;
  float streakSpeed;
  float streakLayers;
  float mistAmount;
  float glossBoost;
  float sceneRainIntensity;
  float radius;
  TucanoVec3 color;
  TucanoVec3 wind;
} TucanoRain;

TUCANO_API void tucano_env_get(TucanoRuntime* rt, TucanoEnvironment* out);
TUCANO_API void tucano_env_set(TucanoRuntime* rt, const TucanoEnvironment* env);
TUCANO_API void tucano_rain_get(TucanoRuntime* rt, TucanoRain* out);
TUCANO_API void tucano_rain_set(TucanoRuntime* rt, const TucanoRain* rain);

// ── Renderer settings ────────────────────────────────

TUCANO_API void tucano_settings_set_time_of_day(TucanoRuntime* rt, float tod);
TUCANO_API float tucano_settings_get_time_of_day(TucanoRuntime* rt);
TUCANO_API void tucano_settings_set_cloud_coverage(TucanoRuntime* rt, float c);
TUCANO_API float tucano_settings_get_cloud_coverage(TucanoRuntime* rt);
TUCANO_API void tucano_settings_set_enable_bloom(TucanoRuntime* rt, bool b);
TUCANO_API bool tucano_settings_get_enable_bloom(TucanoRuntime* rt);
TUCANO_API void tucano_settings_set_enable_ao(TucanoRuntime* rt, bool b);
TUCANO_API bool tucano_settings_get_enable_ao(TucanoRuntime* rt);
TUCANO_API void tucano_settings_set_enable_clouds(TucanoRuntime* rt, bool b);
TUCANO_API bool tucano_settings_get_enable_clouds(TucanoRuntime* rt);
TUCANO_API void tucano_settings_set_enable_atmosphere(TucanoRuntime* rt, bool b);
TUCANO_API bool tucano_settings_get_enable_atmosphere(TucanoRuntime* rt);

#ifdef __cplusplus
}
#endif

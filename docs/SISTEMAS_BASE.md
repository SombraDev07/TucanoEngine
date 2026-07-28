# TucanoEngine — Sistemas Base

> Catálogo de todas as primitivas disponíveis para criação de jogos via código.
> Atualizado: 2026-07-27

---

## Engine Core (C++)

### RHI — Render Hardware Interface
| Sistema | Arquivo | Status |
|---------|---------|--------|
| DX12 Device | `src/RHI/DX12/DX12Device.cpp` | ✅ Production |
| CommandList | `src/RHI/DX12/DX12CommandList.cpp` | ✅ Production |
| SwapChain | `src/RHI/DX12/DX12SwapChain.cpp` | ✅ Production |
| BarrierBatcher | `src/RHI/DX12/BarrierBatcher.cpp` | ✅ Production |
| BindlessManager | `src/RHI/DX12/BindlessManager.cpp` | ✅ Production |
| PipelineCache | `src/RHI/DX12/PipelineCache.cpp` | ✅ Production |
| GPU Crash Recovery | `src/RHI/DX12/GpuCrashRecovery.cpp` | ✅ Production |

### Renderer — Pipeline de Renderização
| Sistema | Arquivo | Status |
|---------|---------|--------|
| Deferred PBR | `src/Renderer/Deferred/` | ✅ Production |
| Shadows (Toroidal, Octa, VSM) | `src/Renderer/Shadows/` | ✅ Production |
| PostFX (Tonemap, Bloom, AO, Exposure) | `src/Renderer/PostFX/` | ✅ Production |
| GI (IBL, DDGI, VoxelGI, WorldSDF) | `src/Renderer/GI/` | ✅ Production |
| Atmosphere (Bruneton) | `src/Renderer/GI/BrunetonAtmosphere.cpp` | ✅ Production |
| Weather (Rain, Clouds) | `src/Renderer/Weather/` | ✅ Production |
| Ray Tracing (DXR) | `src/Renderer/RayTracing/` | ✅ Production |
| RenderGraph | `src/Renderer/RenderGraph/` | ✅ Production |
| Camera | `src/Renderer/Camera.h` | ✅ Production |
| Camera::screenToWorldRay | `src/Renderer/Camera.cpp` | ✅ Production |
| CameraShake | `src/Renderer/CameraShake.h` | ✅ Novo |

### ECS — Entity Component System
| Sistema | Arquivo | Status |
|---------|---------|--------|
| EntityManager (Archetype SoA) | `src/ECS/EntityManager.cpp` | ✅ Production |
| ComponentRegistry | `src/ECS/ComponentTypes.cpp` | ✅ Production |
| Components (Transform, PhysicsBody, RenderObject) | `src/ECS/Components.cpp` | ✅ Production |
| QueryManager (Bloom filter, MT) | `src/ECS/QueryManager.cpp` | ✅ Production |
| EventManager (Deferred FIFO) | `src/ECS/EventManager.cpp` | ✅ Production |
| TemplateManager (JSON) | `src/ECS/TemplateManager.cpp` | ✅ Production |
| World (fachada ECS completa) | `src/ECS/World.cpp` | ✅ Production |

### Physics — Jolt Physics
| Sistema | Arquivo | Status |
|---------|---------|--------|
| PhysicsWorld | `src/Physics/PhysicsWorld.cpp` | ✅ Production |
| Raycast | `src/Physics/PhysicsWorld.h` | ✅ Production |
| Character Controller | `src/Physics/PhysicsWorld.h` | ✅ Production |
| Rigid Bodies (Static, Dynamic) | `src/Physics/PhysicsWorld.h` | ✅ Production |
| TriggerVolume (Box, Sphere) | `src/Physics/TriggerVolume.h` | ✅ Novo |

### Audio — miniaudio
| Sistema | Arquivo | Status |
|---------|---------|--------|
| Audio Engine | `src/Audio/Audio.cpp` | ✅ Production |
| AudioClip (WAV, OGG) | `src/Audio/AudioClip.h` | ✅ Production |
| AudioSource (2D, 3D) | `src/Audio/AudioSource.h` | ✅ Production |
| AudioListener | `src/Audio/Audio.h` | ✅ Production |
| SoundEvents (Cues) | `src/Audio/SoundEvents.h` | ✅ Novo |

### Animation — Esquelética
| Sistema | Arquivo | Status |
|---------|---------|--------|
| AnimationClip | `src/Animation/AnimationClip.h` | ✅ Production |
| AnimationCurve (Hermite, Catmull-Rom) | `src/Animation/AnimationCurve.h` | ✅ Production |
| Skeleton | `src/Animation/Skeleton.h` | ✅ Production |
| AnimationPlayer | `src/Animation/AnimationClip.h` | ✅ Production |

### Core — Sistemas Base
| Sistema | Arquivo | Status |
|---------|---------|--------|
| JobSystem (Thread Pool) | `src/Core/JobSystem.cpp` | ✅ Production |
| TaskScheduler | `src/Core/TaskScheduler.cpp` | ✅ Production |
| JSON Parser | `src/Core/Json.cpp` | ✅ Production |
| StateStorage (Key-Value flags) | `src/Core/StateStorage.h` | ✅ Novo |
| TweenSystem (Easing functions) | `src/Core/Tween.h` | ✅ Novo |
| ObjectPool (Entity pools) | `src/Core/ObjectPool.h` | ✅ Novo |
| SplinePath (Catmull-Rom) | `src/Core/Spline.h` | ✅ Novo |
| EventBus (Pub/Sub) | `src/Core/EventBus.h` | ✅ Novo |

### Platform — Abstração de SO
| Sistema | Arquivo | Status |
|---------|---------|--------|
| Window (GLFW) | `src/Platform/Window.cpp` | ✅ Production |
| Input (Keyboard, Mouse) | `src/Platform/Input.cpp` | ✅ Production |
| FileSystem | `src/Platform/FileSystem.cpp` | ✅ Production |

### Input — Mapeamento de Input
| Sistema | Arquivo | Status |
|---------|---------|--------|
| ButtonCode enum | `src/Input/InputFwd.h` | ✅ Production |
| VirtualInput | `src/Input/VirtualInput.cpp` | ✅ Production |
| InputConfiguration | `src/Input/InputConfiguration.cpp` | ✅ Production |

### Asset Pipeline
| Sistema | Arquivo | Status |
|---------|---------|--------|
| GLTFLoader | `src/AssetPipeline/GLTFLoader.cpp` | ✅ Production |
| DdsLoader (BC4/BC5) | `src/AssetPipeline/DdsLoader.cpp` | ✅ Production |
| FBXLoader | `src/AssetPipeline/FBXLoader.cpp` | ✅ Production |
| AssetCooker | `src/AssetPipeline/AssetCooker.cpp` | ✅ Production |
| AssetPack (.tcpkg) | `src/AssetPipeline/AssetPack.cpp` | ✅ Production |
| DracoMesh (compressão) | `src/AssetPipeline/DracoMesh.cpp` | ✅ Production |

### World — Streaming
| Sistema | Arquivo | Status |
|---------|---------|--------|
| WorldGrid (Morton order) | `src/World/WorldGrid.cpp` | ✅ Production |
| StreamingScheduler | `src/World/StreamingScheduler.cpp` | ✅ Production |
| FrustumCull | `src/World/FrustumCull.cpp` | ✅ Production |
| InstanceCloud | `src/World/InstanceCloud.cpp` | ✅ Production |

### Terrain — Terreno
| Sistema | Arquivo | Status |
|---------|---------|--------|
| Heightmap | `src/Terrain/Heightmap.cpp` | ✅ Production |
| ClipmapTerrain | `src/Terrain/ClipmapTerrain.cpp` | ✅ Production |
| TerrainRenderer | `src/Terrain/TerrainRenderer.cpp` | ✅ Production |
| MaterialAtlas | `src/Terrain/MaterialAtlas.cpp` | ✅ Production |
| ErosionSimulation | `src/Terrain/ErosionSimulation.cpp` | ✅ Production |

### Lua — Scripting Runtime
| Sistema | Arquivo | Status |
|---------|---------|--------|
| LuaVM | `src/Lua/LuaVM.cpp` | ✅ Novo |
| FileWatcher (Hot Reload) | `src/Lua/LuaVM.cpp` | ✅ Novo |
| LuaBindings (Core + Gameplay) | `src/Lua/LuaBindings.cpp` | ✅ Novo |

### AI — Agente de IA
| Sistema | Arquivo | Status |
|---------|---------|--------|
| LLMClient (Anthropic, OpenAI, Local) | `src/AI/LLMClient.cpp` | ✅ Novo |
| AIAgent (System prompt, Context builder) | `src/AI/AIAgent.cpp` | ✅ Novo |

### Network — Networking (UDP/Winsock2)
| Sistema | Arquivo | Status |
|---------|---------|--------|
| NetworkManager (Server/Client UDP) | `src/Network/NetworkManager.h` | ✅ Novo |
| ReplicationSystem (ECS auto-sync) | `src/Network/ReplicationSystem.h` | ✅ Novo |
| RPCSystem (Remote Procedure Calls) | `src/Network/RPCSystem.h` | ✅ Novo |

### Animation — Extended
| Sistema | Arquivo | Status |
|---------|---------|--------|
| AnimationController (State Machine) | `src/Animation/AnimationController.h` | ✅ Novo |
| BlendSpace1D (idle→walk→run) | `src/Animation/BlendSpace.h` | ✅ Novo |
| BlendSpace2D (Speed+Direction) | `src/Animation/BlendSpace.h` | ✅ Novo |
| AnimationLayers (Bone Mask + Additive) | `src/Animation/AnimationLayers.h` | ✅ Novo |
| AnimationEvents (Callbacks) | `src/Animation/AnimationEvents.h` | ✅ Novo |

### Pathfinding
| Sistema | Arquivo | Status |
|---------|---------|--------|
| NavMesh (A* on node graph) | `src/Physics/NavMesh.h` | ✅ Novo |

### Core — Extended
| Sistema | Arquivo | Status |
|---------|---------|--------|
| CoroutineSystem (Wait/Yield) | `src/Core/Coroutine.h` | ✅ Novo |

---

## APIs Lua (para IA e desenvolvedores)

### `ecs.*` — Entidades e Componentes
```lua
local e = ecs.create({"Transform", "Health"})
ecs.destroy(e)
ecs.alive(e)
ecs.has(e, "Health")
local t = ecs.get(e, "Transform")        -- retorna tabela Lua
ecs.set(e, "Transform", {position=..., rotation=..., scale=...})
ecs.add(e, "ComponentName")
ecs.remove(e, "ComponentName")
local x, y, z = ecs.get_position(e)
ecs.set_position(e, x, y, z)
ecs.set_rotation(e, pitch, yaw, roll)
local entities = ecs.query({"Transform", "Health"}) -- array de entity IDs
local e = ecs.instantiate("TemplateName")
ecs.load_templates("path/to/templates.json")
```

### `vec3.*` / `quat.*` — Matemática 3D
```lua
vec3.new(x, y, z)              vec3.add(a, b)
vec3.sub(a, b)                 vec3.mul(v, scalar)
vec3.length(v)                 vec3.normalize(v)
vec3.dot(a, b)                 vec3.cross(a, b)
vec3.lerp(a, b, t)             vec3.distance(a, b)
quat.new(x, y, z, w)           quat.from_euler(pitch, yaw, roll)
quat.mul(a, b)                 quat.slerp(a, b, t)
```

### `input.*` — Input (Teclado e Mouse)
```lua
input.key_down("Space")        input.key_held("W")
input.mouse_down(0)            -- 0=left, 1=right, 2=middle
input.mouse_held(0)
local mx, my = input.mouse_position()
local dx, dy = input.mouse_delta()
input.scroll()
```

### `physics.*` — Física
```lua
local hit, dist, normal, point = physics.raycast(origin, dir, maxDist)
local rayOrigin, rayDir = physics.screen_ray(mx, my, viewportW, viewportH)
```

### `camera.*` — Câmera
```lua
local x, y, z = camera.get_position()
camera.set_position(x, y, z)
local fx, fy, fz = camera.get_forward()
camera.look_at(x, y, z)
camera.shake(intensity, duration, frequency, roughness)
camera.shake_stop()
local sx, sy, sz = camera.shake_offset()
```

### `tween.*` — Animação Procedural
```lua
tween.position(entity, x, y, z, duration, "easeOutQuad")
tween.rotation(entity, w, x, y, z, duration, "easeOutQuad")
tween.scale(entity, x, y, z, duration, "easeOutBounce")
tween.kill(entity)             tween.kill_all()
-- Easings: linear, easeInQuad, easeOutQuad, easeInOutQuad,
--          easeInCubic, easeOutCubic, easeInOutCubic,
--          easeInElastic, easeOutElastic, easeOutBounce
```

### `state.*` — Estado Global (Save/Load)
```lua
state.set_bool("has_key", true)
state.set_int("score", 100)
state.set_float("timer", 5.0)
state.set_string("player_name", "Hero")
local v = state.get_bool("has_key", false)
local v = state.get_int("score", 0)
state.has("has_key")
state.save("savegame.txt")     state.load("savegame.txt")
state.clear()
```

### `audio.*` — Áudio
```lua
local srcId = audio.play_2d("sound.wav", volume, loop)
local srcId = audio.play_3d("sound.wav", position, volume, loop)
audio.stop(srcId)
audio.set_volume(srcId, volume)
audio.set_position(srcId, position)
audio.set_master_volume(volume)
audio.play_cue("explosion")           -- fire-and-forget
audio.play_cue_3d("explosion", pos)   -- 3D fire-and-forget
audio.register_cue("explosion", "explosion.wav")
```

### `events.*` — EventBus (Pub/Sub)
```lua
events.on("entity_damaged", function(event, payload)
    log.print("Entity was damaged!")
end)
events.emit("entity_damaged")
events.off("entity_damaged")
events.clear()
```

### `pool.*` — Object Pooling
```lua
pool.prewarm("Bullet", 100)
local bullet = pool.acquire("Bullet")
pool.release("Bullet", bullet)
local count = pool.available("Bullet")
pool.clear()
```

### `trigger.*` — Volumes de Trigger
```lua
local id = trigger.create_box(center, halfExtent)
local id = trigger.create_sphere(center, radius)
trigger.remove(id)
```

### `spline.*` — Spline Path
```lua
local spline = spline.new({{x=0,y=0,z=0}, {x=5,y=2,z=0}, {x=10,y=0,z=0}})
local pos = spline.evaluate(spline, 0.5)   -- t in [0, 1]
local dir = spline.tangent(spline, 0.5)
local len = spline.length(spline)
local t = spline.closest_param(spline, {x=3,y=0,z=0})
spline.destroy(spline)
```

### `coroutine.*` — Wait/Yield
```lua
coroutine.wait(2.0, function()
    log.print("Fired after 2 seconds!")
end)
coroutine.wait_frames(30, function()
    log.print("Fired after 30 frames!")
end)
```

### `net.*` — Networking
```lua
net.start_server(port, maxClients)
net.connect("192.168.1.5", port)
net.stop()
net.is_running()      net.is_server()      net.is_client()
net.client_count()
net.latency(clientId)
```

### `rpc.*` — Remote Procedure Calls
```lua
rpc.call_server(rpcId, "data_string")
rpc.call_client(targetClientId, rpcId, "data_string")
rpc.broadcast(rpcId, "data_string")
```

### `replication.*` — ECS Replication
```lua
replication.mark_component("Transform")
replication.mark_component("Health")
replication.spawn(entityId)
replication.despawn(entityId)
```

### `anim.*` — Animação Básica
```lua
anim.play(entity, "clipName", loop)
anim.stop(entity)
anim.set_speed(entity, speed)
```

### `anim_ctrl.*` — Animation State Machine
```lua
local ctrl = anim_ctrl.new({
    {name="idle", clip="idle", speed=1.0, loop=true},
    {name="walk", clip="walk", speed=1.5, loop=true},
    {name="attack", clip="attack", speed=1.0, loop=false},
}, {
    {from="idle", to="walk", condition="Speed > 0.1"},
    {from="walk", to="idle", condition="Speed < 0.1"},
    {from="idle", to="attack", condition="Attacking == true"},
})
anim_ctrl.update(ctrl)
anim_ctrl.set_float(ctrl, "Speed", 5.0)
anim_ctrl.set_bool(ctrl, "Attacking", true)
anim_ctrl.trigger(ctrl, "Jump")
anim_ctrl.force_state(ctrl, "death", 0.1)
local clip = anim_ctrl.current_clip(ctrl)
anim_ctrl.destroy(ctrl)
```

### `blendspace.*` — Blend Spaces 1D e 2D
```lua
-- 1D: idle → walk → run baseado em Speed
local bs = blendspace.new_1d("Speed", 0, 10)
blendspace.add_sample_1d(bs, 0, idleClip, 1.0)
blendspace.add_sample_1d(bs, 5, walkClip, 1.2)
blendspace.add_sample_1d(bs, 10, runClip, 1.5)
blendspace.destroy_1d(bs)

-- 2D: Speed + Direction
local bs2 = blendspace.new_2d()
blendspace.add_sample_2d(bs2, 0, 0, idleClip)
blendspace.add_sample_2d(bs2, 5, 0, walkFwdClip)
blendspace.add_sample_2d(bs2, -3, 0, walkBackClip)
blendspace.add_sample_2d(bs2, 3, 3, strafeRightClip)
blendspace.destroy_2d(bs2)
```

### `anim_layers.*` — Animation Layers com Bone Mask
```lua
local layers = anim_layers.new()

local base = anim_layers.add_layer(layers, "base")
anim_layers.play(base, walkClip, true, 1.0)
anim_layers.set_weight(base, 1.0)

local upper = anim_layers.add_layer(layers, "upper_body")
anim_layers.set_mask_upper(upper)          -- só afeta torso/braços/cabeça
anim_layers.play(upper, aimClip, true, 1.0)
anim_layers.set_additive(upper)            -- blending aditivo
anim_layers.set_weight(upper, 0.5)

anim_layers.include_bone(upper, "spine2")  -- ou bone específico
anim_layers.set_mask_full(upper)           -- volta pro corpo todo

anim_layers.update(layers, dt)
anim_layers.stop(layer, 0.3)               -- fade out em 0.3s
anim_layers.remove_layer(layers, "upper_body")
anim_layers.destroy(layers)
```

### `anim_events.*` — Animation Events (callbacks)
```lua
anim_events.add("walk", 0.3, "footstep_left")
anim_events.add("walk", 0.8, "footstep_right")
anim_events.add("attack", 0.5, "hit_detection", "damage=25")

anim_events.set_callback(function(name, param)
    if name == "footstep_left" then
        audio.play_cue_3d("footstep", getFootPos("left"))
    elseif name == "hit_detection" then
        events.emit("deal_damage")
    end
end)

-- Chamado a cada frame pelo AnimationPlayer:
anim_events.process("walk", currentTime, lastTime)
anim_events.clear()
```

### `navmesh.*` — Pathfinding (A*)
```lua
local nav = navmesh.new(
    {{x=0,y=0,z=0}, {x=5,y=0,z=0}, {x=10,y=0,z=5}},
    {{1, 2}, {2, 3}}  -- edges between nodes
)
local node = navmesh.closest_node(nav, {x=2,y=0,z=0})
local path = navmesh.find_path(nav, {x=0,y=0,z=0}, {x=10,y=0,z=5})
-- path = array of vec3 positions
navmesh.destroy(nav)
```

### `render.*` — Render 2D
```lua
render.draw_sprite("icon.png", x, y, w, h)
render.draw_rect(x, y, w, h, r, g, b, a)
render.font.text("Hello World", x, y, fontSize, {r=255,g=255,b=255,a=255})
```

### `log.*` — Logging
```lua
log.print("message")
log.warn("warning")
log.error("error")
```

### `time.*` — Tempo
```lua
local dt = time.delta()
local elapsed = time.elapsed()
```

---

## Como criar um jogo (workflow da IA)

### 1. Estrutura do projeto
```
meu_jogo/
├── scripts/
│   ├── main.lua          -- entry point (setup + update)
│   ├── player.lua        -- player controller
│   ├── enemy.lua         -- enemy AI
│   └── ...
├── templates/
│   └── entities.json     -- ECS templates
├── assets/               -- modelos, texturas, sons
```

### 2. main.lua (entry point)
```lua
function setup()
    ecs.load_templates("templates/entities.json")
    pool.prewarm("Bullet", 100)
    audio.register_cue("gunshot", "assets/gunshot.wav")
end

function update(dt)
    -- engine chama update() todo frame
    local player = ecs.query({"Player", "Transform"})[1]
    -- logic...
end
```

### 3. Fluxo de iteração
```
Usuário descreve → IA gera .lua + .json → Hot reload → Preview → Refina
```

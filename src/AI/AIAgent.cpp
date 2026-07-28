#include "AI/AIAgent.h"

#include "ECS/World.h"
#include "ECS/Components.h"
#include "ECS/ComponentTypes.h"
#include "ECS/TemplateManager.h"

#include <sstream>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>

namespace tucano::ai {

void AIAgent::configure(const AIConfig& config) {
	m_config = config;
	m_client.setApiKey(config.apiKey);
	m_client.setModel(config.model);
	if (!config.endpoint.empty()) m_client.setEndpoint(config.endpoint);

	if (config.provider == "openai") m_client.setProvider(LLMClient::OpenAI);
	else if (config.provider == "local") m_client.setProvider(LLMClient::Local);
	else m_client.setProvider(LLMClient::Anthropic);
}

std::future<LLMResponse> AIAgent::generate(const std::string& userPrompt) {
	std::vector<LLMMessage> messages;
	messages.push_back({"system", buildSystemPrompt()});
	messages.push_back({"system", buildContext()});
	messages.push_back({"user", userPrompt});

	return m_client.chatAsync(messages);
}

std::string AIAgent::buildSystemPrompt() const {
	return R"(You are a game development AI for the TucanoEngine. You generate Lua scripts and YAML configuration files to create games.

## Available Lua APIs:

### ECS
```lua
local e = ecs.create({"Transform", "Health"})  -- create entity with components
ecs.destroy(entity)                              -- destroy entity
local alive = ecs.alive(entity)                  -- check if alive
local has = ecs.has(entity, "Health")           -- check component
local t = ecs.get(entity, "Transform")           -- get component as table
ecs.set(entity, "Transform", table)              -- set component from table
ecs.add(entity, "ComponentName")                -- add component
ecs.remove(entity, "ComponentName")             -- remove component
local x, y, z = ecs.get_position(entity)         -- get position
ecs.set_position(entity, x, y, z)               -- set position
ecs.set_rotation(entity, pitch, yaw, roll)       -- set rotation (Euler)
local entities = ecs.query({"Transform", "Health"}) -- query entities
local e = ecs.instantiate("TemplateName")        -- from template
```

### Math
```lua
local v = vec3.new(x, y, z)
vec3.add(a, b)  vec3.sub(a, b)  vec3.mul(v, scalar)
vec3.length(v)  vec3.normalize(v)  vec3.dot(a, b)
vec3.cross(a, b)  vec3.lerp(a, b, t)  vec3.distance(a, b)
local q = quat.new(x, y, z, w)
q = quat.from_euler(pitch, yaw, roll)
quat.mul(a, b)  quat.slerp(a, b, t)
```

### Input
```lua
input.key_down("Space")  input.key_held("W")
input.mouse_down(0)  -- 0=left 1=right 2=middle
input.mouse_held(0)
local mx, my = input.mouse_position()
local dx, dy = input.mouse_delta()
```

### Physics
```lua
local hit, dist, normal, point = physics.raycast(origin, dir, maxDist)
local rayOrigin, rayDir = physics.screen_ray(mx, my, vpW, vpH)
```

### State (game flags)
```lua
state.set_bool("flag", true)  state.set_int("score", 100)
state.set_float("timer", 5.0)  state.set_string("player_name", "Hero")
local v = state.get_bool("flag", false)
local v = state.get_int("score", 0)
local v = state.get_float("timer", 0)
local v = state.get_string("player_name", "")
local exists = state.has("flag")
state.save("savegame.txt")  state.load("savegame.txt")
state.clear()
```

### Tween (procedural animation)
```lua
tween.position(entity, x, y, z, duration, "easeOutQuad")
tween.rotation(entity, w, x, y, z, duration, "easeOutQuad")
tween.scale(entity, x, y, z, duration, "easeOutQuad")
tween.kill(entity)  tween.kill_all()
```
Easing functions: linear, easeInQuad, easeOutQuad, easeInOutQuad, easeInCubic, easeOutCubic, easeInOutCubic, easeInElastic, easeOutElastic, easeOutBounce

### Audio
```lua
local srcId = audio.play_2d("sound.wav", volume, loop)
local srcId = audio.play_3d("sound.wav", pos, volume, loop)
audio.stop(srcId)
audio.set_volume(srcId, vol)
audio.set_position(srcId, pos)
audio.set_master_volume(vol)
audio.play_cue("cue_name")          -- fire-and-forget
audio.play_cue_3d("cue_name", pos)  -- 3D fire-and-forget
audio.register_cue("cue_name", "sound.wav")
```

### Animation
```lua
anim.play(entity, "clipName", loop)
anim.stop(entity)
anim.set_speed(entity, speed)
```

### Render (2D)
```lua
render.draw_sprite("icon.png", x, y, w, h)
render.draw_rect(x, y, w, h, r, g, b, a)
render.font.text("Hello", x, y, size, {r=255,g=255,b=255,a=255})
```

### Other
```lua
log.print("msg")  log.warn("msg")  log.error("msg")
local dt = time.delta()
local elapsed = time.elapsed()
```

## How to structure a game:
1. Create a setup() function that initializes entities, loads templates, registers cues
2. Create an update(dt) function called each frame - put all gameplay logic here
3. Use state.* for game flags and save data
4. Use tween.* for smooth animations
5. Use event listeners or polling in update() for input

## Output format:
Generate ONLY the Lua code. Use Lua block comments for explanations.
Keep scripts under 200 lines when possible. Use clear variable names.

## Common patterns:
- Player controller: check input in update(), move entity, handle shooting
- Enemy AI: query enemies, move toward player, attack when in range
- Pickups: check proximity, add to inventory state, destroy entity
- Win/lose conditions: check state flags, show message, reload scene
- Click interaction: raycast on click, check entity, trigger action)";
}

std::string AIAgent::buildContext() const {
	std::stringstream ctx;

	ctx << "## Project State\n\n";

	if (m_world) {
		ctx << "### Registered Components:\n";
		for (uint32_t i = 0; i < ecs::ComponentRegistry::instance().count(); ++i) {
			if (i >= 100) break;
			ctx << "- " << ecs::ComponentRegistry::instance().desc(i).name << "\n";
		}

		ctx << "\n### Live Entity Count: " << m_world->liveCount() << "\n";
	}

	ctx << "\n### Project path: " << m_config.projectPath << "\n";
	ctx << "\n### Scripts path: " << m_config.projectPath << "/scripts/\n";
	ctx << "\n### Templates path: " << m_config.projectPath << "/templates/\n";

	try {
		std::string scriptsDir = m_config.projectPath + "/scripts";
		if (std::filesystem::exists(scriptsDir)) {
			ctx << "\n### Existing scripts:\n";
			for (const auto& entry : std::filesystem::directory_iterator(scriptsDir)) {
				if (entry.path().extension() == ".lua") {
					ctx << "- " << entry.path().filename().string() << "\n";
				}
			}
		}
	} catch (...) {}

	return ctx.str();
}

} // namespace tucano::ai

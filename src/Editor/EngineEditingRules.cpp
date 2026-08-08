#include "Editor/EngineEditingRules.h"

#include "Editor/TypeEditingRules.h"
#include "Generated/Reflection.g.h"

namespace tucano::editor {

void registerEngineEditingRules() {
	// ── Material ──
	// Three parameters that the shader reads only when their gate is open. Hidden rather than
	// locked: each is a single row, so nothing jumps, and a greyed-out "Alpha cutoff" still invites
	// the question of what it would do.
	defineRules<Material>()
	    .hideUnless("alphaCutoff", [](const Material& m) { return m.alphaMask; })
	    .hideUnless("clearcoatRoughness", [](const Material& m) { return m.clearcoat > 0.0f; })
	    .hideUnless("fuzzColor", [](const Material& m) { return m.fuzz > 0.0f; });

	// ── Weather ──
	// The master switches gate everything below them. Locked rather than hidden: turning `enabled`
	// off would otherwise collapse 28 rows and move the checkbox out from under the cursor, and the
	// values are still worth reading while the pass is off.
	defineRules<WaterParams>().lockAllUnless([](const WaterParams& w) { return w.enabled; },
	                                         {"enabled"});
	defineRules<FogParams>().lockAllUnless([](const FogParams& f) { return f.enabled; }, {"enabled"});

	// Volumetric fog has its own subtree. Its settings are inert in the analytic path, so they lock
	// with it rather than pretending to do something.
	defineRules<FogParams>().lockUnless("noiseStrength",
	                                    [](const FogParams& f) { return f.enabled && f.volumetric; });
	defineRules<FogParams>().lockUnless("noiseScale",
	                                    [](const FogParams& f) { return f.enabled && f.volumetric; });
	defineRules<FogParams>().lockUnless("noiseSpeed",
	                                    [](const FogParams& f) { return f.enabled && f.volumetric; });
}

} // namespace tucano::editor

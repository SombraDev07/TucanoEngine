#pragma once

namespace tucano::editor {

// Registers the editing rules for the engine's own types. Called once, lazily, from
// TypeEditingRules::find — not from a static initialiser, and not from any main().
//
// The rules could have been static initialisers inside EngineEditingRules.cpp, which reads better
// and is a trap: a translation unit in a static library whose only content is static initialisers
// has nothing to pull it in, so the linker is free to drop it and the rules vanish with no error
// anywhere. Calling this explicitly from the one place that reads the registry means the rules
// exist whenever anyone asks for them.
void registerEngineEditingRules();

} // namespace tucano::editor

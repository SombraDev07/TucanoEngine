#pragma once

// UI gallery — every editor widget on one screen.
//
// It exists to make the UI layer reviewable. A widget library whose only exercise is the editor
// itself gets tested by accident: a control nobody happens to use right now silently rots, and a
// style change is judged from whatever panel is open. Here the whole surface is visible at once, so
// a regression shows up next to a working neighbour.
//
// Also the verification target for the roadmap's P1 tasks — `--ui-gallery` screenshots this.

namespace tucano::editor::ui {

// Draws the gallery window. `open` is the caller's visibility flag (the window's close button
// clears it); pass nullptr for a window with no close button.
void drawGallery(bool* open);

} // namespace tucano::editor::ui

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// DialogManager — in-editor modals, queued so only one is ever up.
//
// Derived from Esoterica (MIT) — Code/EngineTools/Core/DialogManager.{h,cpp}
//
// Every panel that hand-rolls a modal gets the details slightly wrong: a different position, a
// different button order, Escape doing something else, two of them stacking on top of each other.
// Routing them through one place makes those decisions once.
//
// Requests are queued rather than shown immediately. Code deep in a save path can ask a question
// without knowing whether another question is already on screen:
//
//   dialogs.confirm("Delete", "Delete 12 objects?", [&](bool yes) { if (yes) deleteSelection(); });
//
// The callback runs when the user answers, which may be several frames later — so it must own what
// it needs rather than capture references to anything that can go away.

namespace tucano::editor {

class DialogManager {
public:
	// Three-way answer, for "you have unsaved work" shapes.
	enum class Choice : unsigned char { Primary, Secondary, Cancel };

	DialogManager();
	~DialogManager();
	DialogManager(const DialogManager&) = delete;
	DialogManager& operator=(const DialogManager&) = delete;

	// Message with a single OK.
	void message(std::string title, std::string text);

	// Yes/no. `onResult(true)` on confirm, `onResult(false)` on cancel or Escape.
	void confirm(std::string title, std::string text, std::function<void(bool)> onResult,
	             std::string confirmLabel = "OK", std::string cancelLabel = "Cancel");

	// Three buttons — Save / Discard / Cancel and friends. Escape always answers Cancel: the safe
	// answer is the one that loses nothing.
	void choice(std::string title, std::string text, std::string primaryLabel, std::string secondaryLabel,
	            std::function<void(Choice)> onResult);

	// Arbitrary body. Return true from `body` to close the dialog.
	void custom(std::string title, std::function<bool()> body, float width = 0.0f, float height = 0.0f);

	// Draws the front of the queue. Call once per frame, after the panels.
	void draw();

	bool hasActiveDialog() const;
	size_t queued() const;
	// Drops everything, answering nothing. For teardown.
	void clear();

private:
	struct Request;

	std::vector<std::unique_ptr<Request>> m_queue;
	bool m_openPending = false;
};

} // namespace tucano::editor

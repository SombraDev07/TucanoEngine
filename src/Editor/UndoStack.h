#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Undo/redo — the thing that turns editing from "irreversible change" into editing.
//
// Derived from Esoterica (MIT) — Code/EngineTools/Core/UndoStack.{h,cpp}
//
// Two additions over the original, both of which decide whether undo is usable at all:
//
//   * **Coalescing.** Dragging a slider emits an edit per frame. Without merging, Ctrl+Z undoes one
//     pixel of movement and the user presses it forty times. Consecutive edits to the same target
//     fold into one step until breakMerge() says the gesture ended.
//   * **A depth limit.** An unbounded history holds every intermediate value of every drag alive
//     for the whole session.
//
// Each tool owns its own stack: undoing in the material editor must not reach into the scene.
//
//   // the common case
//   stack.pushValue("Roughness", &material.roughness, oldValue, newValue);
//
//   // several changes as one step
//   { UndoStack::Compound c(stack, "Delete selection"); for (...) stack.push(...); }

namespace tucano::editor {

class UndoAction {
public:
	virtual ~UndoAction() = default;
	virtual void undo() = 0;
	virtual void redo() = 0;
	// Shown in the Edit menu ("Undo Roughness"), so it names what the user did, not what the code did.
	virtual const std::string& name() const = 0;

	// Folds `next` into this action, for gestures that emit a stream of edits. Returning true means
	// `next` is discarded after being absorbed.
	virtual bool mergeWith(const UndoAction& next) {
		(void)next;
		return false;
	}
};

// Edit of a single value, held by pointer. Covers nearly every property-grid change.
//
// The pointer must outlive the stack entry — true for anything a tool owns, false for a temporary,
// which is why this is created through UndoStack::pushValue rather than by hand.
template <typename T>
class ValueAction final : public UndoAction {
public:
	ValueAction(std::string name, T* target, T before, T after)
	    : m_name(std::move(name)), m_target(target), m_before(before), m_after(after) {}

	void undo() override {
		if (m_target != nullptr) *m_target = m_before;
	}
	void redo() override {
		if (m_target != nullptr) *m_target = m_after;
	}
	const std::string& name() const override { return m_name; }

	bool mergeWith(const UndoAction& next) override {
		const auto* other = dynamic_cast<const ValueAction<T>*>(&next);
		// Same field of the same object: keep the original "before" and take the latest "after", so
		// the whole drag reads as one edit from where it started to where it ended.
		if (other == nullptr || other->m_target != m_target || other->m_name != m_name) {
			return false;
		}
		m_after = other->m_after;
		return true;
	}

private:
	std::string m_name;
	T* m_target;
	T m_before;
	T m_after;
};

// Action defined by two lambdas, for changes that are not a single value (reparenting, deletion).
class LambdaAction final : public UndoAction {
public:
	LambdaAction(std::string name, std::function<void()> undoFn, std::function<void()> redoFn)
	    : m_name(std::move(name)), m_undo(std::move(undoFn)), m_redo(std::move(redoFn)) {}

	void undo() override {
		if (m_undo) m_undo();
	}
	void redo() override {
		if (m_redo) m_redo();
	}
	const std::string& name() const override { return m_name; }

private:
	std::string m_name;
	std::function<void()> m_undo;
	std::function<void()> m_redo;
};

class UndoStack {
public:
	UndoStack();
	~UndoStack();
	UndoStack(const UndoStack&) = delete;
	UndoStack& operator=(const UndoStack&) = delete;

	// Records an action. Anything that was undone is discarded — editing after undoing forks the
	// history, and the branch the user walked away from is gone.
	void push(std::unique_ptr<UndoAction> action);

	// Convenience for the common case; returns false when before == after (nothing happened).
	template <typename T>
	bool pushValue(std::string name, T* target, T before, T after) {
		if (target == nullptr || before == after) return false;
		push(std::make_unique<ValueAction<T>>(std::move(name), target, before, after));
		return true;
	}

	bool undo();
	bool redo();

	bool canUndo() const;
	bool canRedo() const;
	// Empty when there is nothing to undo/redo. For labelling the Edit menu.
	const std::string& undoName() const;
	const std::string& redoName() const;

	// Ends the current gesture, so the next push starts a new step instead of merging. Call when a
	// widget stops being edited — ImGui's IsItemDeactivatedAfterEdit is exactly this moment.
	void breakMerge() { m_canMerge = false; }

	// Everything pushed until endCompound() becomes a single step under `name`.
	void beginCompound(std::string name);
	void endCompound();
	bool inCompound() const { return m_compound != nullptr; }

	// RAII form; safe on an early return, which the manual pair is not.
	class Compound {
	public:
		Compound(UndoStack& stack, std::string name);
		~Compound();
		Compound(const Compound&) = delete;
		Compound& operator=(const Compound&) = delete;

	private:
		UndoStack& m_stack;
	};

	void clear();
	// Oldest steps are dropped past this. 0 means unbounded.
	void setMaxDepth(size_t depth);

	size_t undoDepth() const { return m_undoStack.size(); }
	size_t redoDepth() const { return m_redoStack.size(); }

private:
	std::vector<std::unique_ptr<UndoAction>> m_undoStack;
	std::vector<std::unique_ptr<UndoAction>> m_redoStack;
	std::unique_ptr<UndoAction> m_compound; // built up between begin/endCompound
	bool m_canMerge = false;
	size_t m_maxDepth = 256;
};

} // namespace tucano::editor

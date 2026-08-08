#include "Editor/UndoStack.h"

#include <algorithm>

namespace tucano::editor {
namespace {

const std::string kEmpty;

// Groups several actions into one step. Undo runs them in reverse: the last change made is the
// first undone, or a delete-then-reparent pair unwinds into a state that never existed.
class CompoundAction final : public UndoAction {
public:
	explicit CompoundAction(std::string name) : m_name(std::move(name)) {}

	void add(std::unique_ptr<UndoAction> action) {
		if (action) m_actions.push_back(std::move(action));
	}
	bool empty() const { return m_actions.empty(); }

	void undo() override {
		for (size_t i = m_actions.size(); i-- > 0;) {
			m_actions[i]->undo();
		}
	}
	void redo() override {
		for (const std::unique_ptr<UndoAction>& a : m_actions) {
			a->redo();
		}
	}
	const std::string& name() const override { return m_name; }

private:
	std::string m_name;
	std::vector<std::unique_ptr<UndoAction>> m_actions;
};

} // namespace

UndoStack::UndoStack() = default;
UndoStack::~UndoStack() = default;

void UndoStack::push(std::unique_ptr<UndoAction> action) {
	if (!action) return;

	// Inside a compound, everything accumulates into the one step being built.
	if (m_compound != nullptr) {
		static_cast<CompoundAction*>(m_compound.get())->add(std::move(action));
		return;
	}

	// Editing after undoing forks the history; the branch the user walked away from is discarded.
	m_redoStack.clear();

	// Fold into the previous step when it is the same ongoing gesture.
	if (m_canMerge && !m_undoStack.empty() && m_undoStack.back()->mergeWith(*action)) {
		return;
	}

	m_undoStack.push_back(std::move(action));
	m_canMerge = true;

	if (m_maxDepth > 0 && m_undoStack.size() > m_maxDepth) {
		// Drop from the front: the oldest step is the one the user is least likely to reach for, and
		// keeping every intermediate value of every drag alive is what an unbounded history costs.
		m_undoStack.erase(m_undoStack.begin(),
		                  m_undoStack.begin() + static_cast<ptrdiff_t>(m_undoStack.size() - m_maxDepth));
	}
}

bool UndoStack::undo() {
	if (m_undoStack.empty()) return false;
	std::unique_ptr<UndoAction> action = std::move(m_undoStack.back());
	m_undoStack.pop_back();
	action->undo();
	m_redoStack.push_back(std::move(action));
	// A gesture cannot continue across an undo, or the next edit would merge into a step the user
	// just took back.
	m_canMerge = false;
	return true;
}

bool UndoStack::redo() {
	if (m_redoStack.empty()) return false;
	std::unique_ptr<UndoAction> action = std::move(m_redoStack.back());
	m_redoStack.pop_back();
	action->redo();
	m_undoStack.push_back(std::move(action));
	m_canMerge = false;
	return true;
}

bool UndoStack::canUndo() const { return !m_undoStack.empty(); }
bool UndoStack::canRedo() const { return !m_redoStack.empty(); }

const std::string& UndoStack::undoName() const {
	return m_undoStack.empty() ? kEmpty : m_undoStack.back()->name();
}

const std::string& UndoStack::redoName() const {
	return m_redoStack.empty() ? kEmpty : m_redoStack.back()->name();
}

void UndoStack::beginCompound(std::string name) {
	// Nested compounds keep the outermost name: that is the operation the user asked for; the inner
	// ones are implementation detail.
	if (m_compound != nullptr) return;
	m_compound = std::make_unique<CompoundAction>(std::move(name));
}

void UndoStack::endCompound() {
	if (m_compound == nullptr) return;
	std::unique_ptr<UndoAction> compound = std::move(m_compound);
	m_compound = nullptr;

	// An empty compound is not a step: it would be an Edit menu entry that undoes nothing.
	if (static_cast<CompoundAction*>(compound.get())->empty()) return;

	// A compound is always its own step, never merged into the previous one.
	m_canMerge = false;
	push(std::move(compound));
	m_canMerge = false;
}

UndoStack::Compound::Compound(UndoStack& stack, std::string name) : m_stack(stack) {
	m_stack.beginCompound(std::move(name));
}
UndoStack::Compound::~Compound() { m_stack.endCompound(); }

void UndoStack::clear() {
	m_undoStack.clear();
	m_redoStack.clear();
	m_compound.reset();
	m_canMerge = false;
}

void UndoStack::setMaxDepth(size_t depth) {
	m_maxDepth = depth;
	if (m_maxDepth > 0 && m_undoStack.size() > m_maxDepth) {
		m_undoStack.erase(m_undoStack.begin(),
		                  m_undoStack.begin() + static_cast<ptrdiff_t>(m_undoStack.size() - m_maxDepth));
	}
}

} // namespace tucano::editor

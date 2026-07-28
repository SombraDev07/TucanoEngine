#pragma once

#include <functional>
#include <vector>
#include <string>

namespace tucano {

struct Coroutine {
	std::string name;
	float waitTime = 0;
	float elapsed = 0;
	bool done = false;
	std::function<void()> onComplete;
};

class CoroutineSystem {
public:
	static CoroutineSystem& instance() { static CoroutineSystem cs; return cs; }

	uint32_t start(std::function<void()> fn, float delay = 0) {
		Coroutine co;
		co.name = "co_" + std::to_string(m_nextId++);
		co.waitTime = delay;
		co.elapsed = 0;
		co.onComplete = std::move(fn);
		m_coroutines.push_back(std::move(co));
		return m_nextId - 1;
	}

	uint32_t wait(float seconds, std::function<void()> fn) {
		return start(std::move(fn), seconds);
	}

	uint32_t waitFrames(uint32_t frames, std::function<void()> fn) {
		return start(std::move(fn), float(frames) / 60.0f);
	}

	void update(float dt) {
		for (auto it = m_coroutines.begin(); it != m_coroutines.end(); ) {
			it->elapsed += dt;
			if (it->elapsed >= it->waitTime) {
				if (it->onComplete) it->onComplete();
				it = m_coroutines.erase(it);
			} else {
				++it;
			}
		}
	}

	void cancel(uint32_t) {
		// TODO: implement coroutine cancellation by ID
	}

	void clear() { m_coroutines.clear(); }

	size_t activeCount() const { return m_coroutines.size(); }

private:
	std::vector<Coroutine> m_coroutines;
	uint32_t m_nextId = 1;
};

} // namespace tucano

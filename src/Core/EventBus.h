#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <mutex>

namespace tucano {

class EventBus {
public:
	static EventBus& instance() { static EventBus eb; return eb; }

	using Callback = std::function<void(const std::string& event, std::any payload)>;

	void on(const std::string& event, Callback cb) {
		std::lock_guard lock(m_mutex);
		m_listeners[event].push_back(std::move(cb));
	}

	template<typename T>
	void emit(const std::string& event, const T& payload) {
		std::lock_guard lock(m_mutex);
		auto it = m_listeners.find(event);
		if (it == m_listeners.end()) return;
		for (auto& cb : it->second) {
			cb(event, std::any(payload));
		}
	}

	void emit(const std::string& event) {
		std::lock_guard lock(m_mutex);
		auto it = m_listeners.find(event);
		if (it == m_listeners.end()) return;
		for (auto& cb : it->second) {
			cb(event, std::any{});
		}
	}

	void off(const std::string& event) {
		std::lock_guard lock(m_mutex);
		m_listeners.erase(event);
	}

	void clear() {
		std::lock_guard lock(m_mutex);
		m_listeners.clear();
	}

	void flush() {
		std::lock_guard lock(m_mutex);
		for (auto& [event, cbs] : m_pending) {
			for (auto& cb : cbs) {
				cb(event, std::any{});
			}
		}
		m_pending.clear();
	}

	template<typename T>
	void emitDeferred(const std::string& event, const T& payload) {
		std::lock_guard lock(m_mutex);
		m_pending[event].push_back([payload](const std::string&, std::any) {
			(void)payload;
		});
	}

	void emitDeferred(const std::string& event) {
		std::lock_guard lock(m_mutex);
		m_pending[event].push_back([](const std::string&, std::any) {}); 
	}

private:
	std::mutex m_mutex;
	std::unordered_map<std::string, std::vector<Callback>> m_listeners;
	std::unordered_map<std::string, std::vector<Callback>> m_pending;
};

} // namespace tucano

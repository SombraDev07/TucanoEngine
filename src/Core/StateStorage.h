#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <fstream>
#include <sstream>
#include <iostream>

namespace tucano {

class StateStorage {
public:
	static StateStorage& instance() {
		static StateStorage s;
		return s;
	}

	void set(const std::string& key, bool val) { m_bools[key] = val; }
	void set(const std::string& key, int val) { m_ints[key] = val; }
	void set(const std::string& key, float val) { m_floats[key] = val; }
	void set(const std::string& key, const std::string& val) { m_strings[key] = val; }

	bool getBool(const std::string& key, bool def = false) const {
		auto it = m_bools.find(key);
		return it != m_bools.end() ? it->second : def;
	}
	int getInt(const std::string& key, int def = 0) const {
		auto it = m_ints.find(key);
		return it != m_ints.end() ? it->second : def;
	}
	float getFloat(const std::string& key, float def = 0.0f) const {
		auto it = m_floats.find(key);
		return it != m_floats.end() ? it->second : def;
	}
	std::string getString(const std::string& key, const std::string& def = "") const {
		auto it = m_strings.find(key);
		return it != m_strings.end() ? it->second : def;
	}

	bool has(const std::string& key) const {
		return m_bools.count(key) || m_ints.count(key) || m_floats.count(key) || m_strings.count(key);
	}

	void clear() {
		m_bools.clear();
		m_ints.clear();
		m_floats.clear();
		m_strings.clear();
	}

	bool save(const std::string& path) const {
		std::ofstream f(path);
		if (!f) return false;
		for (auto& [k, v] : m_bools) f << "b:" << k << "=" << (v ? "1" : "0") << "\n";
		for (auto& [k, v] : m_ints) f << "i:" << k << "=" << v << "\n";
		for (auto& [k, v] : m_floats) f << "f:" << k << "=" << v << "\n";
		for (auto& [k, v] : m_strings) f << "s:" << k << "=" << v << "\n";
		return true;
	}

	bool load(const std::string& path) {
		std::ifstream f(path);
		if (!f) return false;
		clear();
		std::string line;
		while (std::getline(f, line)) {
			if (line.size() < 4) continue;
			char type = line[0];
			if (line[1] != ':') continue;
			auto eq = line.find('=', 2);
			if (eq == std::string::npos) continue;
			std::string key = line.substr(2, eq - 2);
			std::string val = line.substr(eq + 1);
			switch (type) {
				case 'b': m_bools[key] = (val == "1"); break;
				case 'i': m_ints[key] = std::stoi(val); break;
				case 'f': m_floats[key] = std::stof(val); break;
				case 's': m_strings[key] = val; break;
			}
		}
		return true;
	}

private:
	std::unordered_map<std::string, bool> m_bools;
	std::unordered_map<std::string, int> m_ints;
	std::unordered_map<std::string, float> m_floats;
	std::unordered_map<std::string, std::string> m_strings;
};

} // namespace tucano

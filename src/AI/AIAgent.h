#pragma once

#include "AI/LLMClient.h"

#include <string>
#include <vector>
#include <functional>
#include <future>
#include <unordered_map>

namespace tucano {

namespace ecs { class World; }

namespace ai {

struct AIConfig {
	std::string provider = "anthropic";
	std::string apiKey;
	std::string model = "claude-sonnet-4-20250514";
	std::string endpoint;
	std::string projectPath;
};

class AIAgent {
public:
	static AIAgent& instance() { static AIAgent a; return a; }

	void configure(const AIConfig& config);
	const AIConfig& config() const { return m_config; }

	void setWorld(ecs::World* world) { m_world = world; }

	std::future<LLMResponse> generate(const std::string& userPrompt);

	std::string buildSystemPrompt() const;
	std::string buildContext() const;

private:
	AIConfig m_config;
	LLMClient m_client;
	ecs::World* m_world = nullptr;
};

} // namespace ai
} // namespace tucano

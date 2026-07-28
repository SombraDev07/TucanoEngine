#pragma once

#include <string>
#include <functional>
#include <future>
#include <vector>

namespace tucano::ai {

struct LLMMessage {
	std::string role;
	std::string content;
};

struct LLMResponse {
	std::string content;
	bool success = false;
	std::string error;
};

class LLMClient {
public:
	enum Provider { Anthropic, OpenAI, Local };

	void setProvider(Provider p) { m_provider = p; }
	void setApiKey(const std::string& key) { m_apiKey = key; }
	void setModel(const std::string& model) { m_model = model; }
	void setEndpoint(const std::string& url) { m_endpoint = url; }

	LLMResponse chat(const std::vector<LLMMessage>& messages);
	std::future<LLMResponse> chatAsync(const std::vector<LLMMessage>& messages);

	Provider provider() const { return m_provider; }
	const std::string& model() const { return m_model; }

private:
	LLMResponse callAnthropic(const std::vector<LLMMessage>& messages);
	LLMResponse callOpenAI(const std::vector<LLMMessage>& messages);
	LLMResponse callLocal(const std::vector<LLMMessage>& messages);

	static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output);

	Provider m_provider = Anthropic;
	std::string m_apiKey;
	std::string m_model = "claude-sonnet-4-20250514";
	std::string m_endpoint;
};

} // namespace tucano::ai

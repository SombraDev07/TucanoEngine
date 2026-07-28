#include "AI/LLMClient.h"

#include <curl/curl.h>

#include <sstream>
#include <thread>
#include <iostream>

namespace tucano::ai {

size_t LLMClient::writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
	size_t total = size * nmemb;
	output->append(static_cast<char*>(contents), total);
	return total;
}

LLMResponse LLMClient::chat(const std::vector<LLMMessage>& messages) {
	switch (m_provider) {
		case Anthropic: return callAnthropic(messages);
		case OpenAI:    return callOpenAI(messages);
		case Local:     return callLocal(messages);
	}
	return {.success = false, .error = "Unknown provider"};
}

std::future<LLMResponse> LLMClient::chatAsync(const std::vector<LLMMessage>& messages) {
	return std::async(std::launch::async, [this, messages]() {
		return chat(messages);
	});
}

LLMResponse LLMClient::callAnthropic(const std::vector<LLMMessage>& messages) {
	LLMResponse resp;

	CURL* curl = curl_easy_init();
	if (!curl) {
		resp.error = "Failed to init curl";
		return resp;
	}

	std::string body;
	body = "{\"model\":\"" + m_model + "\",\"max_tokens\":4096,\"messages\":[";
	for (size_t i = 0; i < messages.size(); ++i) {
		if (i > 0) body += ",";
		body += "{\"role\":\"" + messages[i].role + "\",\"content\":\"";

		std::string escaped;
		for (char c : messages[i].content) {
			if (c == '"') escaped += "\\\"";
			else if (c == '\\') escaped += "\\\\";
			else if (c == '\n') escaped += "\\n";
			else if (c == '\r') escaped += "\\r";
			else if (c == '\t') escaped += "\\t";
			else escaped += c;
		}
		body += escaped;
		body += "\"}";
	}
	body += "]}";

	std::string response;
	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, ("x-api-key: " + m_apiKey).c_str());
	headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

	curl_easy_setopt(curl, CURLOPT_URL, m_endpoint.empty() ?
		"https://api.anthropic.com/v1/messages" : m_endpoint.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		long httpCode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

		if (httpCode == 200) {
			auto contentStart = response.find("\"text\":\"");
			if (contentStart != std::string::npos) {
				contentStart += 8;
				auto contentEnd = response.find("\"", contentStart);
				resp.content = response.substr(contentStart, contentEnd - contentStart);
			} else {
				resp.content = response;
			}
			resp.success = true;
		} else {
			resp.error = "HTTP " + std::to_string(httpCode) + ": " + response;
		}
	} else {
		resp.error = "CURL error: " + std::string(curl_easy_strerror(res));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return resp;
}

LLMResponse LLMClient::callOpenAI(const std::vector<LLMMessage>& messages) {
	LLMResponse resp;

	CURL* curl = curl_easy_init();
	if (!curl) {
		resp.error = "Failed to init curl";
		return resp;
	}

	std::string body = "{\"model\":\"" + m_model + "\",\"messages\":[";
	for (size_t i = 0; i < messages.size(); ++i) {
		if (i > 0) body += ",";
		body += "{\"role\":\"" + messages[i].role + "\",\"content\":\"";

		std::string escaped;
		for (char c : messages[i].content) {
			if (c == '"') escaped += "\\\"";
			else if (c == '\\') escaped += "\\\\";
			else if (c == '\n') escaped += "\\n";
			else if (c == '\r') escaped += "\\r";
			else if (c == '\t') escaped += "\\t";
			else escaped += c;
		}
		body += escaped;
		body += "\"}";
	}
	body += "]}";

	std::string response;
	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, ("Authorization: Bearer " + m_apiKey).c_str());

	curl_easy_setopt(curl, CURLOPT_URL, m_endpoint.empty() ?
		"https://api.openai.com/v1/chat/completions" : m_endpoint.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		long httpCode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

		if (httpCode == 200) {
			auto contentStart = response.find("\"content\":\"");
			if (contentStart != std::string::npos) {
				contentStart += 11;
				auto contentEnd = response.find("\"", contentStart);
				resp.content = response.substr(contentStart, contentEnd - contentStart);
			} else {
				resp.content = response;
			}
			resp.success = true;
		} else {
			resp.error = "HTTP " + std::to_string(httpCode) + ": " + response;
		}
	} else {
		resp.error = "CURL error: " + std::string(curl_easy_strerror(res));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return resp;
}

LLMResponse LLMClient::callLocal(const std::vector<LLMMessage>& messages) {
	LLMResponse resp;

	CURL* curl = curl_easy_init();
	if (!curl) {
		resp.error = "Failed to init curl";
		return resp;
	}

	std::string body = "{\"model\":\"" + m_model + "\",\"messages\":[";
	for (size_t i = 0; i < messages.size(); ++i) {
		if (i > 0) body += ",";
		body += "{\"role\":\"" + messages[i].role + "\",\"content\":\"";

		std::string escaped;
		for (char c : messages[i].content) {
			if (c == '"') escaped += "\\\"";
			else if (c == '\\') escaped += "\\\\";
			else if (c == '\n') escaped += "\\n";
			else if (c == '\r') escaped += "\\r";
			else if (c == '\t') escaped += "\\t";
			else escaped += c;
		}
		body += escaped;
		body += "\"}";
	}
	body += "]}";

	std::string response;
	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, m_endpoint.empty() ?
		"http://localhost:11434/v1/chat/completions" : m_endpoint.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		resp.success = true;
		auto contentStart = response.find("\"content\":\"");
		if (contentStart != std::string::npos) {
			contentStart += 11;
			auto contentEnd = response.find("\"", contentStart);
			resp.content = response.substr(contentStart, contentEnd - contentStart);
		} else {
			resp.content = response;
		}
	} else {
		resp.error = "CURL error: " + std::string(curl_easy_strerror(res));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return resp;
}

} // namespace tucano::ai

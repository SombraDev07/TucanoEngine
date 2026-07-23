#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tucano {

class AudioClip {
public:
  static AudioClip* loadWav(const char* path);
  static AudioClip* loadOgg(const char* path);

  void release();

  uint32_t sampleRate() const { return m_sampleRate; }
  uint32_t channelCount() const { return m_channels; }
  uint64_t frameCount() const { return m_frameCount; }
  float durationSeconds() const;

  const void* data() const { return m_data.data(); }
  uint64_t dataSize() const { return m_data.size(); }

  const std::string& path() const { return m_path; }

private:
  AudioClip() = default;

  std::vector<uint8_t> m_data;
  std::string m_path;
  uint32_t m_sampleRate = 0;
  uint32_t m_channels = 0;
  uint64_t m_frameCount = 0;
};

} // namespace tucano

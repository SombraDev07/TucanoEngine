#pragma once

namespace tucano {

class Audio {
public:
  static Audio& instance();

  void init();
  void shutdown();

  float masterVolume() const { return m_masterVolume; }
  void setMasterVolume(float v);

  bool isPaused() const { return m_paused; }
  void setPaused(bool p);

  bool isInitialized() const { return m_initialized; }

  void* engine() { return m_engine; }

private:
  Audio() = default;
  ~Audio();

  void* m_engine = nullptr;
  float m_masterVolume = 1.0f;
  bool m_paused = false;
  bool m_initialized = false;
};

} // namespace tucano

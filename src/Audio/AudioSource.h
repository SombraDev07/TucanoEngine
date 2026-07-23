#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace tucano {

class AudioClip;

class AudioSource {
public:
  AudioSource();
  ~AudioSource();

  void play(AudioClip* clip, float volume = 1.0f, bool loop = false);
  void stop();
  void pause();
  void resume();

  bool isPlaying() const;

  void setPosition(const glm::vec3& pos);
  glm::vec3 position() const { return m_position; }

  void setVolume(float v);
  float volume() const { return m_volume; }

  void setLooping(bool loop);
  bool looping() const { return m_looping; }

  void setAttenuationDistance(float minDist, float maxDist);

  AudioClip* clip() const { return m_clip; }

  void* handle() { return m_handle; }
  void updatePosition();

private:
  void* m_handle = nullptr;
  AudioClip* m_clip = nullptr;
  glm::vec3 m_position{0};
  float m_volume = 1.0f;
  bool m_looping = false;
  float m_minDist = 1.0f;
  float m_maxDist = 100.0f;
  bool m_playing = false;
};

} // namespace tucano

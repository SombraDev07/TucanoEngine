#define MINIAUDIO_IMPLEMENTATION
#include "Audio/Audio.h"
#include "Audio/AudioClip.h"
#include "Audio/AudioListener.h"
#include "Audio/AudioSource.h"
#include <miniaudio.h>
#include <cstdio>

namespace tucano {

Audio& Audio::instance() {
  static Audio inst;
  return inst;
}

void Audio::init() {
  if (m_initialized) return;

  auto* engine = new ma_engine;
  ma_result result = ma_engine_init(nullptr, engine);
  if (result != MA_SUCCESS) {
    delete engine;
    return;
  }

  ma_engine_set_volume(engine, m_masterVolume);
  m_engine = engine;
  m_initialized = true;
}

void Audio::shutdown() {
  if (!m_initialized) return;

  auto* engine = static_cast<ma_engine*>(m_engine);
  ma_engine_uninit(engine);
  delete engine;
  m_engine = nullptr;
  m_initialized = false;
}

Audio::~Audio() { shutdown(); }

void Audio::setMasterVolume(float v) {
  m_masterVolume = v;
  if (m_initialized)
    ma_engine_set_volume(static_cast<ma_engine*>(m_engine), v);
}

void Audio::setPaused(bool p) {
  m_paused = p;
  if (!m_initialized) return;
  auto* engine = static_cast<ma_engine*>(m_engine);
  if (p)
    ma_engine_stop(engine);
  else
    ma_engine_start(engine);
}

AudioClip* AudioClip::loadWav(const char* path) {
  ma_decoder decoder;
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
  if (ma_decoder_init_file(path, &config, &decoder) != MA_SUCCESS)
    return nullptr;

  auto* clip = new AudioClip;
  clip->m_sampleRate = static_cast<uint32_t>(decoder.outputSampleRate);
  clip->m_channels = static_cast<uint32_t>(decoder.outputChannels);
  clip->m_path = path;

  ma_uint64 totalFrames;
  ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
  clip->m_frameCount = totalFrames;

  size_t dataSize = static_cast<size_t>(totalFrames * decoder.outputChannels * sizeof(float));
  clip->m_data.resize(dataSize);

  ma_uint64 framesRead;
  ma_decoder_read_pcm_frames(&decoder, clip->m_data.data(), totalFrames, &framesRead);
  clip->m_frameCount = framesRead;
  clip->m_data.resize(static_cast<size_t>(framesRead * decoder.outputChannels * sizeof(float)));

  ma_decoder_uninit(&decoder);
  return clip;
}

AudioClip* AudioClip::loadOgg(const char* path) {
  return loadWav(path);
}

void AudioClip::release() {
  delete this;
}

float AudioClip::durationSeconds() const {
  if (m_sampleRate == 0) return 0;
  return static_cast<float>(m_frameCount) / static_cast<float>(m_sampleRate);
}

AudioSource::AudioSource() {
  if (!Audio::instance().isInitialized()) return;
  auto* engine = static_cast<ma_engine*>(Audio::instance().engine());
  auto* sound = new ma_sound;
  if (ma_sound_init_from_data_source(engine, nullptr, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, sound) != MA_SUCCESS) {
    ma_sound_uninit(sound);
    delete sound;
    return;
  }
  m_handle = sound;
}

AudioSource::~AudioSource() {
  stop();
  if (m_handle) {
    auto* sound = static_cast<ma_sound*>(m_handle);
    ma_sound_uninit(sound);
    delete sound;
    m_handle = nullptr;
  }
}

void AudioSource::play(AudioClip* clip, float volume, bool loop) {
  if (!m_handle || !clip) return;
  m_clip = clip;
  m_volume = volume;
  m_looping = loop;

  auto* engine = static_cast<ma_engine*>(Audio::instance().engine());
  auto* sound = static_cast<ma_sound*>(m_handle);

  ma_sound_uninit(sound);

  ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
    ma_format_f32, clip->channelCount(), clip->frameCount(), clip->data(), nullptr);

  auto* audioBuffer = new ma_audio_buffer;
  if (ma_audio_buffer_init(&bufferConfig, audioBuffer) != MA_SUCCESS) {
    delete audioBuffer;
    m_playing = false;
    return;
  }

  auto* dataSource = reinterpret_cast<ma_data_source*>(audioBuffer);

  ma_result r = ma_sound_init_from_data_source(
    engine, dataSource,
    MA_SOUND_FLAG_NO_SPATIALIZATION,
    nullptr, sound);

  if (r != MA_SUCCESS) {
    ma_audio_buffer_uninit(audioBuffer);
    delete audioBuffer;
    m_playing = false;
    return;
  }

  ma_sound_set_volume(sound, volume * Audio::instance().masterVolume());
  ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
  ma_sound_start(sound);

  m_playing = true;
}

void AudioSource::stop() {
  if (!m_handle) return;
  auto* sound = static_cast<ma_sound*>(m_handle);
  ma_sound_stop(sound);
  m_playing = false;
  m_clip = nullptr;
}

void AudioSource::pause() {
  if (!m_handle || !m_playing) return;
  auto* sound = static_cast<ma_sound*>(m_handle);
  ma_sound_stop(sound);
}

void AudioSource::resume() {
  if (!m_handle || !m_clip) return;
  auto* sound = static_cast<ma_sound*>(m_handle);
  ma_sound_set_looping(sound, m_looping ? MA_TRUE : MA_FALSE);
  ma_sound_start(sound);
}

bool AudioSource::isPlaying() const {
  if (!m_handle) return false;
  auto* sound = static_cast<ma_sound*>(m_handle);
  return ma_sound_is_playing(sound) != MA_FALSE;
}

void AudioSource::setPosition(const glm::vec3& pos) {
  m_position = pos;
  updatePosition();
}

void AudioSource::setVolume(float v) {
  m_volume = v;
  if (!m_handle) return;
  auto* sound = static_cast<ma_sound*>(m_handle);
  ma_sound_set_volume(sound, v * Audio::instance().masterVolume());
}

void AudioSource::setLooping(bool loop) {
  m_looping = loop;
  if (!m_handle) return;
  auto* sound = static_cast<ma_sound*>(m_handle);
  ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);
}

void AudioSource::setAttenuationDistance(float minDist, float maxDist) {
  m_minDist = minDist;
  m_maxDist = maxDist;
  updatePosition();
}

void AudioSource::updatePosition() {
  if (!m_handle) return;
  auto* sound = static_cast<ma_sound*>(m_handle);
  ma_sound_set_position(sound, m_position.x, m_position.y, m_position.z);
  ma_sound_set_attenuation_model(sound, ma_attenuation_model_linear);
  ma_sound_set_min_distance(sound, m_minDist);
  ma_sound_set_max_distance(sound, m_maxDist);
}

AudioListener& AudioListener::instance() {
  static AudioListener inst;
  return inst;
}

void AudioListener::setPosition(const glm::vec3& pos) {
  m_position = pos;
  applyToEngine();
}

void AudioListener::setOrientation(const glm::vec3& forward, const glm::vec3& up) {
  m_forward = forward;
  m_up = up;
  applyToEngine();
}

void AudioListener::applyToEngine() {
  if (!Audio::instance().isInitialized()) return;
  auto* engine = static_cast<ma_engine*>(Audio::instance().engine());
  ma_engine_listener_set_position(engine, 0, m_position.x, m_position.y, m_position.z);
  ma_engine_listener_set_direction(engine, 0, m_forward.x, m_forward.y, m_forward.z);
  ma_engine_listener_set_world_up(engine, 0, m_up.x, m_up.y, m_up.z);
}

} // namespace tucano

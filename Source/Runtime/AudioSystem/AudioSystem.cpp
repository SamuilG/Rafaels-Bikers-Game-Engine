#include "AudioSystem.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "../../../ThirdParty/miniaudio/miniaudio.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine {

    struct AudioSystem::Impl {
        struct SoundEntry {
            std::unique_ptr<ma_sound> sound;
			float volume = 1.0f;// Default volume (0.0 to 1.0)
			float pitch = 1.0f;// Default pitch (1.0 is normal speed)
			float runtimeVolume = 1.0f;// Runtime volume multiplier (0.0 to 1.0), applied on top of the base volume
        };

        ma_engine engine{};
        bool initialized = false;
        float masterVolume = 1.0f;
		// Map of sound names to their corresponding sound entries
		// Each entry contains the sound object and its properties like volume and pitch.
        std::unordered_map<std::string, SoundEntry> sounds;
        std::vector<std::unique_ptr<ma_sound>> oneShots;

        void ApplyVolume(SoundEntry& entry)
        {
            if (entry.sound) {
                ma_sound_set_volume(entry.sound.get(), entry.volume * entry.runtimeVolume);
            }
        }
    };

    AudioSystem::AudioSystem()
        : m_impl(std::make_unique<Impl>())
    {
    }

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    void AudioSystem::Init()
    {
        if (m_impl->initialized) return;

		
        ma_result result = ma_engine_init(nullptr, &m_impl->engine);
        if (result != MA_SUCCESS) {
            std::cerr << "[AudioSystem] Failed to initialize miniaudio engine. Error: "
                      << result << std::endl;
            return;
        }

        m_impl->initialized = true;
        ma_engine_set_volume(&m_impl->engine, m_impl->masterVolume);
        std::cout << "[AudioSystem] Initialized." << std::endl;
    }

    void AudioSystem::Update(float)
    {
        //ZoneScopedN("Audio");
        if (!m_impl->initialized) return;

		// One-shot instances are independent ma_sound copies. Once playback reaches the end, uninit and remove them to avoid leaking audio nodes//播放一次性实例是独立的ma_sound副本。一旦播放完成并且达到末尾，取消初始化并删除它们以避免泄漏音频节点。
        auto& oneShots = m_impl->oneShots;
        oneShots.erase(
            std::remove_if(oneShots.begin(), oneShots.end(),
                [](const std::unique_ptr<ma_sound>& sound) {
                    if (!sound) return true;

                    if (!ma_sound_is_playing(sound.get()) && ma_sound_at_end(sound.get())) {
                        ma_sound_uninit(sound.get());
                        return true;
                    }

                    return false;
                }),
            oneShots.end());
    }

    void AudioSystem::Shutdown()
    {
        if (!m_impl || !m_impl->initialized) return;

		// Uninitialize all one-shot instances//取消初始化所有一次性实例
        for (auto& sound : m_impl->oneShots) {
            if (sound) ma_sound_uninit(sound.get());
        }
        m_impl->oneShots.clear();

        for (auto& [name, entry] : m_impl->sounds) {
            if (entry.sound) ma_sound_uninit(entry.sound.get());
        }
        m_impl->sounds.clear();

        ma_engine_uninit(&m_impl->engine);
        m_impl->initialized = false;
        std::cout << "[AudioSystem] Shutdown." << std::endl;
    }

    bool AudioSystem::LoadSound(const std::string& name, const std::string& filePath)
    {
        if (!m_impl->initialized) {
            std::cerr << "[AudioSystem] Cannot load '" << name
                      << "' before AudioSystem::Init()." << std::endl;
            return false;
        }

        auto sound = std::make_unique<ma_sound>();
        ma_result result = ma_sound_init_from_file(
            &m_impl->engine,
            filePath.c_str(),
            MA_SOUND_FLAG_NO_SPATIALIZATION,// 2D sound for now; add 3D later.
            nullptr,
            nullptr,
            sound.get());

        if (result != MA_SUCCESS) {
            std::cerr << "[AudioSystem] Failed to load sound '" << name
                      << "' from '" << filePath << "'. Error: " << result << std::endl;
            return false;
        }
		// Reloading an existing sound should keep any UI/gameplay tuning that was already applied to that name.//重新加载现有声音应该保留已经应用于该名称的任何UI
        float volume = 1.0f;
        float pitch = 1.0f;
        float runtimeVolume = 1.0f;

        auto existing = m_impl->sounds.find(name);
        if (existing != m_impl->sounds.end()) {
            volume = existing->second.volume;
            pitch = existing->second.pitch;
            runtimeVolume = existing->second.runtimeVolume;

            if (existing->second.sound) {
                ma_sound_uninit(existing->second.sound.get());
            }
        }

        ma_sound_set_volume(sound.get(), volume * runtimeVolume);
        ma_sound_set_pitch(sound.get(), pitch);

        Impl::SoundEntry entry;
        entry.sound = std::move(sound);
        entry.volume = volume;
        entry.pitch = pitch;
        entry.runtimeVolume = runtimeVolume;
        m_impl->sounds[name] = std::move(entry);
        return true;
    }

    void AudioSystem::PlayOneShot(const std::string& name)
    {
        if (!m_impl->initialized) return;

        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end() || !it->second.sound) {
            std::cerr << "[AudioSystem] PlayOneShot failed. Unknown sound: "
                      << name << std::endl;
            return;
        }

        auto instance = std::make_unique<ma_sound>();
        ma_result result = ma_sound_init_copy(
            &m_impl->engine,
            it->second.sound.get(),
            MA_SOUND_FLAG_NO_SPATIALIZATION,
            nullptr,
            instance.get());

        if (result != MA_SUCCESS) {
            std::cerr << "[AudioSystem] Failed to create one-shot sound '"
                      << name << "'. Error: " << result << std::endl;
            return;
        }

        ma_sound_set_looping(instance.get(), MA_FALSE);
        ma_sound_set_volume(instance.get(), it->second.volume * it->second.runtimeVolume);
        ma_sound_set_pitch(instance.get(), it->second.pitch);
        ma_sound_seek_to_pcm_frame(instance.get(), 0);
        ma_sound_start(instance.get());
        m_impl->oneShots.push_back(std::move(instance));
    }

    void AudioSystem::PlayLoop(const std::string& name)
    {
        if (!m_impl->initialized) return;

        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end() || !it->second.sound) {
            std::cerr << "[AudioSystem] PlayLoop failed. Unknown sound: "
                      << name << std::endl;
            return;
        }

        ma_sound_set_looping(it->second.sound.get(), MA_TRUE);
        ma_sound_seek_to_pcm_frame(it->second.sound.get(), 0);
        ma_sound_start(it->second.sound.get());
    }

    void AudioSystem::SetVolume(const std::string& name, float volume)
    {
        if (!m_impl->initialized) return;

        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end() || !it->second.sound) {
            std::cerr << "[AudioSystem] SetVolume failed. Unknown sound: "
                      << name << std::endl;
            return;
        }

        it->second.volume = std::max(0.0f, volume);
        m_impl->ApplyVolume(it->second);
    }

    float AudioSystem::GetVolume(const std::string& name) const
    {
        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end()) return 0.0f;
        return it->second.volume;
    }

    void AudioSystem::SetPitch(const std::string& name, float pitch)
    {
        if (!m_impl->initialized) return;

        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end() || !it->second.sound) {
            std::cerr << "[AudioSystem] SetPitch failed. Unknown sound: "
                      << name << std::endl;
            return;
        }

        it->second.pitch = std::max(0.01f, pitch);
        ma_sound_set_pitch(it->second.sound.get(), it->second.pitch);
    }

    float AudioSystem::GetPitch(const std::string& name) const
    {
        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end()) return 1.0f;
        return it->second.pitch;
    }

    void AudioSystem::SetMasterVolume(float volume)
    {
        if (!m_impl->initialized) return;

        m_impl->masterVolume = std::clamp(volume, 0.0f, 1.0f);
        ma_engine_set_volume(&m_impl->engine, m_impl->masterVolume);
    }

    float AudioSystem::GetMasterVolume() const
    {
        return m_impl->masterVolume;
    }

    void AudioSystem::SetRuntimeVolume(const std::string& name, float volume)
    {
        if (!m_impl->initialized) return;

        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end() || !it->second.sound) {
            std::cerr << "[AudioSystem] SetRuntimeVolume failed. Unknown sound: "
                      << name << std::endl;
            return;
        }

        it->second.runtimeVolume = std::max(0.0f, volume);
        m_impl->ApplyVolume(it->second);
    }

    void AudioSystem::Stop(const std::string& name)
    {
        if (!m_impl->initialized) return;

        auto it = m_impl->sounds.find(name);
        if (it == m_impl->sounds.end() || !it->second.sound) {
            std::cerr << "[AudioSystem] Stop failed. Unknown sound: "
                      << name << std::endl;
            return;
        }

        ma_sound_stop(it->second.sound.get());
        ma_sound_seek_to_pcm_frame(it->second.sound.get(), 0);
    }

    std::vector<std::string> AudioSystem::GetSoundNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_impl->sounds.size());

        for (const auto& [name, entry] : m_impl->sounds) {
            names.push_back(name);
        }

        std::sort(names.begin(), names.end());
        return names;
    }

} // namespace engine

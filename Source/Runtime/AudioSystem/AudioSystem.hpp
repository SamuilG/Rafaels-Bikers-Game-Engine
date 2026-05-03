#pragma once

#include "../Core/System.h"
#include <memory>
#include <string>
#include <vector>
#include "tracy/Tracy.hpp"

namespace engine {

    class AudioSystem final : public System {
    public:
        AudioSystem();
        ~AudioSystem() override;

        void Init() override;
        void Update(float dt) override;
        void Shutdown() override;

        bool LoadSound(const std::string& name, const std::string& filePath);
        void PlayOneShot(const std::string& name);
        void PlayLoop(const std::string& name);
        void SetVolume(const std::string& name, float volume);
        float GetVolume(const std::string& name) const;
        void SetPitch(const std::string& name, float pitch);
        float GetPitch(const std::string& name) const;
        void SetMasterVolume(float volume);
        float GetMasterVolume() const;
        void SetRuntimeVolume(const std::string& name, float volume);
        void Stop(const std::string& name);
        std::vector<std::string> GetSoundNames() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace engine

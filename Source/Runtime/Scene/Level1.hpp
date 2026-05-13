#pragma once
#include "GameScene.hpp"
#include "../Physics/bikeController.hpp"
#include "../UI/VisualUIEditor/RuntimeUiController.hpp"
#include <flecs.h>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

namespace engine {

    class level : public GameScene {
    public:
        level();
        ~level() override;

        void Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, GameplayState* state, AnimationSystem* anima, AudioSystem* audio) override;
        void Update(float dt) override;
        void Shutdown() override;

    private:
        RenderSystem* m_render = nullptr;
        SceneManager* m_scene = nullptr;
        PhysicsSystem* m_physics = nullptr;
        InputSystem* m_input = nullptr;
        EventSystem* m_event = nullptr;
        GameplayState* mState = nullptr;
        AnimationSystem* m_anima = nullptr;
        AudioSystem* m_audio = nullptr;

        std::unique_ptr<BikeController> m_bikeController;

        // sound delay — matches TestScene pattern
        float m_allCollectSoundDelay = -1.0f;

        bool m_bgMusicPlaying = false;          // radio pickup: track whether BGM loop is active
        int  m_currentSongIndex = 0;            // radio: index of the currently playing song
        float m_radioBroadcastDelay = -1.0f;   // >0: counting down to playing boardcast.mp3
        float m_radioBgMusicDelay   = -1.0f;   // >0: counting down to starting bg music loop
        std::vector<std::string> m_radioSongs;  // radio: sound names loaded from RadioMusic/
        std::vector<std::string> m_radioLabels; // radio: display names (filename stems)

        flecs::entity m_bikeEntity;          // bike root entity — pickups are parented here

        // Spinning pickup entities
        flecs::entity m_springPickupEntity;
        flecs::entity m_hornPickupEntity;
        flecs::entity m_radioPickupEntity;
        std::vector<flecs::entity> m_gasPickupEntities;
        std::vector<flecs::entity> m_radioPickupEntities;
        bool m_radioCollected;
        // Mounted horn — kept after collection for squeeze animation
        flecs::entity m_hornMountedEntity;
        glm::mat4     m_hornBaseMountT  = glm::mat4(1.0f);
        float         m_hornAnimTimer   = -1.0f;           // -1 = idle; ≥0 = animating

        // Mounted spring — kept after collection for squeeze animation on jump
        flecs::entity m_springMountedEntity;
        glm::mat4     m_springBaseMountT = glm::mat4(1.0f);
        float         m_springAnimTimer  = -1.0f;          // -1 = idle; ≥0 = animating

        // Rocket — hidden at start, mounted to rear frame on first gas tank collection
        flecs::entity m_rocketEntity;

        // Newspaper pickup — shows UFO image for 5s on trigger
        flecs::entity m_newspaperEntity;
        float         m_ufoDisplayTimer = -1.0f; // counts down from 5s; -1 = inactive

        // Rocket2 scene launch — triggered at (232.94, 86.11, -222.40)
        std::vector<flecs::entity> m_rocket2Entities;  // ALL mesh parts of rocket2.glb
        glm::vec3     m_rocket2Center      = glm::vec3(260.92f, -20.0f, 0.0f); // pivot point (spawn pos), tracks ascent during launch
        bool          m_rocket2Launching   = false;
        float         m_rocket2LaunchTimer = -1.0f;
        size_t        m_rocketFlameParticleIndex = static_cast<size_t>(-1); // flame particles at rocket tail
        bool          m_rocketCameraActive = false; // camera follows rocket tail looking down

		bool m_respawnPromptVisible = false;
        float m_respawnStillnessTime = 0.0f;
        bool m_abilityUnlockPopupVisible = false;
        float m_abilityUnlockPopupTimer = 0.0f;
        bool m_winUiVisible = false;
        float m_winUiDelayTimer = -1.0f;
        bool m_previousAliveState = true;

		// Satellite — mounted to character's back after radio pickup
		flecs::entity m_satelliteEntity;

		// Checkpoint respawn — set when bike enters a checkpoint trigger
		bool      m_hasCheckpoint   = false;
		glm::vec3 m_checkpointPos   = glm::vec3(0.0f);
		float     m_checkpointYaw   = 0.0f;


        RuntimeUiController* GetRuntimeUiController() const;
        void RefreshAbilityHintUi() const;
        void ShowAbilityUnlockPopup(std::string_view abilityName);
        void HideAbilityUnlockPopup();
        void ShowWinScreen();
        void HideWinScreen();
    };

} // namespace engine

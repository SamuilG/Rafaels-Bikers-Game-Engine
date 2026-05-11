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

		bool m_respawnPromptVisible = false;
        float m_respawnStillnessTime = 0.0f;
        bool m_abilityUnlockPopupVisible = false;
        float m_abilityUnlockPopupTimer = 0.0f;

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
    };

} // namespace engine
#include "TestScene.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/bikeController.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../UserState/UserState.hpp"
#include "../Debug/DebugRenderer.hpp"

#include <unordered_set>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

    TestScene::TestScene() = default;
    TestScene::~TestScene() = default;

    void TestScene::Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, AnimationSystem* animation, UserState* state) {
        m_render = render;
        m_scene = scene;
        m_physics = physics;
        m_input = input;
        m_event = eventSys;
        m_animation = animation;
        m_state = state;

        m_scene->LoadModel(m_render, "Assets/Models/TScene.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));

        glm::mat4 bridgeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 60.0f));
        glm::mat4 cplaneSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(120.0f, 0.0f, 250.0f));
        glm::mat4 darkRoomSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 0.0f, 200.0f));

        m_scene->LoadModel(m_render, "Assets/Models/testBridge.glb", engine::ModelPhysicsType::Static, 0.0f, bridgeSpawnPos);
        m_scene->LoadModel(m_render, "Assets/Models/testCurvePlane.glb", engine::ModelPhysicsType::Static, 0.0f, cplaneSpawnPos);
        m_scene->LoadModel(m_render, "Assets/Models/darkRoom.glb", engine::ModelPhysicsType::Static, 0.0f, darkRoomSpawnPos);

        std::unordered_set<flecs::entity_t> existingEntities;
        m_scene->get_world().query<EntityStatus>().each([&](flecs::entity e, EntityStatus&) {
            existingEntities.insert(e.id());
        });

        const glm::mat4 playerTransform =
            glm::translate(glm::mat4(1.0f), m_playerPosition) *
            glm::rotate(glm::mat4(1.0f), m_playerYaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(m_playerScale));

        flecs::entity firstPlayer = m_scene->LoadModel(
            m_render,
            "Assets/X Bot/X Bot.mesh",
            engine::ModelPhysicsType::Static,
            0.0f,
            playerTransform);

        std::vector<flecs::entity> spawnedPlayerEntities;
        m_scene->get_world().query<SkeletonComponent>().each([&](flecs::entity e, SkeletonComponent&) {
            if (existingEntities.find(e.id()) != existingEntities.end()) {
                return;
            }

            spawnedPlayerEntities.push_back(e);
        });

        m_playerEntities.clear();
        for (flecs::entity playerEntity : spawnedPlayerEntities) {
            m_playerEntities.push_back(playerEntity);
            playerEntity.set<EntityStatus>({ true, false });
            if (playerEntity.has<PhysicsBody>()) {
                playerEntity.remove<PhysicsBody>();
            }
        }

        if (m_playerEntities.empty() && firstPlayer.is_valid()) {
            m_playerEntities.push_back(firstPlayer);
        }

        m_cameraTarget = m_scene->get_world().entity("Player_0")
            .set<EntityStatus>({ false, false })
            .set<LocalTransform>({ playerTransform })
            .set<WorldTransform>({ playerTransform });

        if (m_animation && !m_playerEntities.empty() && m_playerEntities.front().has<SkeletonComponent>()) {
            EngineModel* model = m_animation->GetModel();
            if (model) {
                m_playerSkeletonIndex = m_playerEntities.front().get<SkeletonComponent>().skeletonIndex;
                m_playerPoseIndex = m_playerSkeletonIndex;

                for (uint32_t i = 0; i < model->skeletonPoses.size(); ++i) {
                    if (model->skeletonPoses[i].skeletonIndex == m_playerSkeletonIndex) {
                        m_playerPoseIndex = i;
                        break;
                    }
                }

                Animator& animator = m_animation->CreateAnimator();
                (void)animator;
                if (m_animation->BindAnimator(m_playerAnimatorIndex, m_playerSkeletonIndex, m_playerPoseIndex)) {
                    m_idleClip = m_animation->FindClipByName("Idle");
                    m_walkClip = m_animation->FindClipByName("Female Walk");
                    m_runClip = m_animation->FindClipByName("Running");

                    if (!m_idleClip.IsValid() && !model->animationClips.empty()) {
                        for (size_t clipIndex = 0; clipIndex < model->animationClips.size(); ++clipIndex) {
                            if (model->animationClips[clipIndex].skeletonIndex == m_playerSkeletonIndex) {
                                m_idleClip = m_animation->MakeClipReference(clipIndex);
                                break;
                            }
                        }
                    }

                    SwitchToClip(m_idleClip.IsValid() ? m_idleClip : m_walkClip);
                }
            }
        }

        glm::mat4 emissivecubeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 3.0f, 200.0f));
        flecs::entity emCubeEntity = m_scene->LoadModel(m_render, "Assets/DELETE_LATER/em1.gltf", engine::ModelPhysicsType::Dynamic, 0.01f, emissivecubeSpawnPos, engine::RenderLayer::Emissive);
        m_scene->create_light_entity("emCubeLight", engine::LightType::Point, glm::vec3(1.0f, 1.0f, 1.0f), 8.0f, glm::mat4(1.0f), 10.0f, glm::vec3(0, -1, 0), 0, 0, emCubeEntity);

        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
        glm::mat4 sunTransform = glm::mat4(1.0f);
        sunTransform[3] = glm::vec4(sunDir, 0.0f);
        m_scene->create_light_entity("MainSun", engine::LightType::Directional, glm::vec3(1.2f, 0.95f, 0.8f), 2.5f, sunTransform, 0);

        if (m_state) {
            m_state->thirdPersonMode = true;
            m_state->followTargetPos = m_playerPosition + glm::vec3(0.0f, 0.9f, 0.0f);
            m_state->playerYaw = m_playerYaw;
            m_state->playerSpeed = 0.0f;
            m_state->bikeYaw = 0.0f;
            m_state->bikeSpeed = 0.0f;
        }

        m_scene->print_all_entities();
    }

    void TestScene::Update(float dt) {
        float forward = 0.0f;
        float strafe = 0.0f;
        if (m_input) {
            forward += m_input->GetActionValue("MoveForward");
            forward -= m_input->GetActionValue("MoveBackward");
            strafe += m_input->GetActionValue("StrafeRight");
            strafe -= m_input->GetActionValue("StrafeLeft");
        }

        const glm::vec2 moveInput(strafe, forward);
        const float inputMagnitude = glm::length(moveInput);
        const bool isRunning = m_input && m_input->IsActionHeld("Fast");
        const float moveSpeed = isRunning ? 6.0f : 3.0f;

        if (inputMagnitude > 0.001f) {
            glm::vec3 cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
            if (m_state) {
                cameraForward = -glm::vec3(m_state->camera2world[2]);
                cameraRight = glm::vec3(m_state->camera2world[0]);
            }

            cameraForward.y = 0.0f;
            cameraRight.y = 0.0f;
            if (glm::length(cameraForward) < 0.001f) cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
            if (glm::length(cameraRight) < 0.001f) cameraRight = glm::vec3(1.0f, 0.0f, 0.0f);
            cameraForward = glm::normalize(cameraForward);
            cameraRight = glm::normalize(cameraRight);

            glm::vec3 moveDir = cameraForward * forward + cameraRight * strafe;
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                m_playerPosition += moveDir * moveSpeed * dt;
                m_playerYaw = std::atan2(moveDir.x, moveDir.z);
            }
        }

        const glm::mat4 playerTransform =
            glm::translate(glm::mat4(1.0f), m_playerPosition) *
            glm::rotate(glm::mat4(1.0f), m_playerYaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(m_playerScale));

        for (flecs::entity playerPart : m_playerEntities) {
            if (playerPart.is_valid()) {
                m_scene->set_local_transform(playerPart, playerTransform);
            }
        }

        if (m_cameraTarget.is_valid()) {
            m_cameraTarget.set<LocalTransform>({ playerTransform });
            m_cameraTarget.set<WorldTransform>({ playerTransform });
        }

        if (m_state) {
            m_state->followTargetPos = m_playerPosition + glm::vec3(0.0f, 0.9f, 0.0f);
            m_state->playerYaw = m_playerYaw;
            m_state->playerSpeed = inputMagnitude > 0.001f ? moveSpeed : 0.0f;
            m_state->bikeYaw = 0.0f;
            m_state->bikeSpeed = 0.0f;
        }

        if (inputMagnitude <= 0.001f) {
            SwitchToClip(m_idleClip);
        }
        else if (isRunning && m_runClip.IsValid()) {
            SwitchToClip(m_runClip);
        }
        else {
            SwitchToClip(m_walkClip.IsValid() ? m_walkClip : m_idleClip);
        }

        m_render->mDebugRenderer.DrawBox(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_render->mDebugRenderer.DrawLine(glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(3.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        m_render->mDebugRenderer.DrawSphere(glm::vec3(-4.0f, 2.0f, 0.0f), 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        m_render->mDebugRenderer.DrawCapsule(glm::vec3(0.0f, 2.0f, 5.0f), 1.0f, 1.5f, glm::vec3(1.0f, 1.0f, 0.0f));
    }

    void TestScene::Shutdown() {
        m_bikeController.reset();
        m_playerEntities.clear();
        m_cameraTarget = flecs::entity::null();
    }

    void TestScene::SwitchToClip(const AnimationClip& clip)
    {
        if (!m_animation || !clip.IsValid()) {
            return;
        }

        Animator* animator = m_animation->GetAnimator(m_playerAnimatorIndex);
        if (!animator) {
            return;
        }

        const AnimationClip& current = animator->GetClip();
        if (current.clipIndex == clip.clipIndex) {
            return;
        }

        m_animation->PlayClip(m_playerAnimatorIndex, clip, true);
    }

} // namespace engine

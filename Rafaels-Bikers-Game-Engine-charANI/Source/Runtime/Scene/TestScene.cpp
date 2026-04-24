#include "TestScene.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/bikeController.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../UserState/UserState.hpp"
#include "../Debug/DebugRenderer.hpp"

namespace engine {

    TestScene::TestScene() = default;
    TestScene::~TestScene() = default;
    void TestScene::Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, UserState* state) {
        m_render = render;
        m_scene = scene;
        m_physics = physics;
        m_input = input;
        m_event = eventSys;
        m_state = state;

        // 1. 加载地形与静态模型
        m_scene->LoadModel(m_render, "Assets/Models/TScene.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));

        glm::mat4 bridgeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 60.0f));
        glm::mat4 CplaneSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(120.0f, 0.0f, 250.0f));
        glm::mat4 darkRoomSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 0.0f, 200.0f));

        m_scene->LoadModel(m_render, "Assets/Models/testBridge.glb", engine::ModelPhysicsType::Static, 0.0f, bridgeSpawnPos);
        m_scene->LoadModel(m_render, "Assets/Models/testCurvePlane.glb", engine::ModelPhysicsType::Static, 0.0f, CplaneSpawnPos);
        m_scene->LoadModel(m_render, "Assets/Models/darkRoom.glb", engine::ModelPhysicsType::Static, 0.0f, darkRoomSpawnPos);

        // 2. 加载自行车
        glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, 10.0f, 30.0f));
        glm::mat4 tbpos = glm::translate(BikeSpawnPos, glm::vec3(0.0f, 0.0f, -8.0f));
        m_scene->LoadModel(m_render, "Assets/Models/tbike.glb", engine::ModelPhysicsType::CustomC, 90.0f, tbpos);

        // 3. 初始化单车控制器
        m_bikeController = std::make_unique<BikeController>(m_physics->GetJoltSystem(), m_input, m_state);
        flecs::entity bikeEntity = m_scene->find_entity("Bike_0");
        if (bikeEntity.is_valid()) {
            uint32_t bikeBodyID = JPH::BodyID::cInvalidBodyID;
            if (bikeEntity.has<PhysicsBody>()) bikeBodyID = bikeEntity.get<PhysicsBody>().bodyID;
            else if (bikeEntity.has<CompoundParent>()) bikeBodyID = bikeEntity.get<CompoundParent>().bodyID;
            m_bikeController->Init(bikeBodyID);
        }

        // 4. 加载发光方块与灯光
        glm::mat4 emissivecubeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 3.0f, 200.0f));
        flecs::entity emCubeEntity = m_scene->LoadModel(m_render, "Assets/DELETE_LATER/em1.gltf", engine::ModelPhysicsType::Dynamic, 0.01f, emissivecubeSpawnPos, engine::RenderLayer::Emissive);

        m_scene->create_light_entity("emCubeLight", engine::LightType::Point, glm::vec3(1.0f, 1.0f, 1.0f), 8.0f, glm::mat4(1.0f), 10.0f, glm::vec3(0, -1, 0), 0, 0, emCubeEntity);

        // 车头灯
        glm::mat4 localLightOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.7f, 1.7f));
        flecs::entity headlight = m_scene->create_light_entity("headlight", engine::LightType::Spot, glm::vec3(1.0f, 0.95f, 0.85f), 15.0f, localLightOffset, 40.0f, glm::vec3(0.0f, 0, 1.0f), 15.0f, 25.0f);
        if (bikeEntity.is_valid()) headlight.child_of(bikeEntity);

        // 太阳与其他灯光
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
        glm::mat4 sunTransform = glm::mat4(1.0f); sunTransform[3] = glm::vec4(sunDir, 0.0f);
        m_scene->create_light_entity("MainSun", engine::LightType::Directional, glm::vec3(1.2f, 0.95f, 0.8f), 2.5f, sunTransform, 0);

        glm::mat4 light2SpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 3.0f, 30.0f));
        m_scene->create_light_entity("voidLight", engine::LightType::Point, glm::vec3(0.5f, 0.0f, 3.0f), 8, light2SpawnPos, 20.0f);

        // 5. 设置触发器 (Trigger)
        m_render->AddParticleGroup();
        auto& triggerParticles = m_render->GetParticles();
        size_t triggerParticleIndex = triggerParticles.size() - 1;
        triggerParticles[triggerParticleIndex]->config.emitterPos = glm::vec3(50.0f, 1.0f, 20.0f);
        triggerParticles[triggerParticleIndex]->config.isVisible = false;

        size_t triggerBox01 = m_render->GetTriggerSystem().AddBoxTrigger(
            glm::vec3(50.0f, 1.0f, 20.0f), glm::vec3(2.0f, 2.0f, 2.0f), triggerParticleIndex, glm::vec3(1.0f, 0.0f, 0.0f), glm::mat4(1.0f), true, false);

        m_render->GetTriggerSystem().SetTriggerCallbacks(triggerBox01,
            []() { engine::EngineUi::LogPrint("trigger box triggered!!\n"); },
            []() { engine::EngineUi::LogPrint("trigger box exited!!\n"); }
        );

        m_scene->print_all_entities();
    }

    void TestScene::Update(float dt) {
        // 更新单车物理
        if (m_bikeController) {
            m_bikeController->Update(dt);
        }

        // 绘制测试 Debug 线条 (只有在这个关卡才需要画这些)
        m_render->mDebugRenderer.DrawBox(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        m_render->mDebugRenderer.DrawLine(glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(3.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        m_render->mDebugRenderer.DrawSphere(glm::vec3(-4.0f, 2.0f, 0.0f), 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        m_render->mDebugRenderer.DrawCapsule(glm::vec3(0.0f, 2.0f, 5.0f), 1.0f, 1.5f, glm::vec3(1.0f, 1.0f, 0.0f));
    }

    void TestScene::Shutdown() {
        // 如果有特定的关卡资源清理，放在这里
        m_bikeController.reset();
    }

} // namespace engine
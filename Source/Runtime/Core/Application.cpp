#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"

namespace engine {

    Application::Application() {
        
        //basic systems
        
        //AddSystem<WindowSystem>();
        //AddSystem<InputSystem>();
        //AddSystem<SoundSystem>();

        //world systems
 
        //AddSystem<TerrainSystem>();
        //AddSystem<SceneManager>();

        //get input and calculate data
        
        physicsSystem = AddSystem<PhysicsSystem>();
        sceneManager  = AddSystem<SceneManager>(physicsSystem);

        //final render 
        
        //AddSystem<CameraSystem>();
        //AddSystem<UISystem>();
        renderSystem = AddSystem<RenderSystem>(Running, sceneManager);


        // left or right Init
        for (auto& sys : Systems) 
            sys->Init();

        // create falling sphere after all system Init() calls
        {
            const float sphereRadius = 0.5f;
            const glm::vec3 spawnPos{ 0.f, 100.f, 0.f }; // drop from y=100

            // 1. generate a UV sphere mesh and upload it to the GPU
            EngineMesh sphereMesh = generate_uv_sphere(sphereRadius, 16, 32);
            sphereMesh.materialIndex = 0; // use first (or default gray) material
            uint32_t sphereMeshIdx = renderSystem->add_runtime_mesh(sphereMesh);

            // 2. create the Jolt physics body
            JPH::BodyID bodyID = physicsSystem->create_sphere_body(spawnPos, sphereRadius);
            uint32_t bodyIDRaw = bodyID.GetIndexAndSequenceNumber();

            // 3. register a renderable ECS entity that will be synced each frame
            glm::mat4 initTransform = glm::translate(glm::mat4(1.f), spawnPos);
            sceneManager->create_dynamic_entity("PhysicsSphere", sphereMeshIdx, 0, initTransform, bodyIDRaw);
        }
    }

    Application::~Application() {
		// right to left Shutdown
        for (auto it = Systems.rbegin(); it != Systems.rend(); ++it) 
            (*it)->Shutdown();
        // auto clear unique_ptrs in vector
        Systems.clear();
    }

    void Application::Run() {
        while (Running) {
            float dt = CalcDeltaTime();
            for (auto& sys : Systems)
                sys->Update(dt);
        }
    }

}
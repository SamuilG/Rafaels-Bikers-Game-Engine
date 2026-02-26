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

        // create the ground physics body (matches visual ground at y=-0.5)
        physicsSystem->create_ground_plane(-0.5f);

        // create falling sphere after all system Init() calls
        {
            const float sphereRadius = 0.5f;
            const glm::vec3 spawnPos{ 0.f, 5.f, 0.f }; // drop from y=5 (camera far=100)

            // 1. generate a UV sphere mesh and upload it to the GPU
            EngineMesh sphereMesh = generate_uv_sphere(sphereRadius, 16, 32);
            sphereMesh.materialIndex = 0; // mesh-local material index (unused by renderer)
            uint32_t sphereMeshIdx = renderSystem->add_runtime_mesh(sphereMesh);

            // 2. create the Jolt physics body
            JPH::BodyID bodyID = physicsSystem->create_sphere_body(spawnPos, sphereRadius);
            uint32_t bodyIDRaw = bodyID.GetIndexAndSequenceNumber();

            // 3. register a renderable ECS entity that will be synced each frame
            // use get_runtime_mat_index() to get the gray descriptor
            uint32_t matIdx = renderSystem->get_runtime_mat_index();
            glm::mat4 initTransform = glm::translate(glm::mat4(1.f), spawnPos);
            sceneManager->create_dynamic_entity("PhysicsSphere", sphereMeshIdx, matIdx, initTransform, bodyIDRaw);

            // diagnostic: confirm indices
            std::printf("[Sphere] meshIdx=%u  matIdx=%u  numMaterials=%u  spawnY=%.1f\n",
                sphereMeshIdx, matIdx, renderSystem->get_material_count(), spawnPos.y);
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
        // Reset timer so init time doesn't spike the first dt.
        mLastTime = std::chrono::steady_clock::now();
        constexpr float kMaxDt = 0.05f; // cap at 50 ms (20 fps minimum)
        while (Running) {
            float dt = std::min(CalcDeltaTime(), kMaxDt);
            for (auto& sys : Systems)
                sys->Update(dt);
        }
    }

}
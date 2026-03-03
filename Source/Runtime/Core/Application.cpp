#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include <flecs.h>

namespace engine {

    Application::Application() {
        

        //basic systems
        
        //AddSystem<WindowSystem>();
        //AddSystem<InputSystem>();
        //AddSystem<SoundSystem>();

        //world systems
 
        //AddSystem<TerrainSystem>();

        //get input and calculate data
        
 

        physicsSystem = AddSystem<PhysicsSystem>();
        sceneManager  = AddSystem<SceneManager>(physicsSystem);

        //final render 
        
        //AddSystem<CameraSystem>();
        //AddSystem<UISystem>();
        renderSystem = AddSystem<RenderSystem>(Running, sceneManager);

        // Initialise all systems
        for (auto& sys : Systems) 
            sys->Init();

        // load models using the API in RenderSystem
        // TScene is completely static (ground + buildings)
        renderSystem->load_additional_model("Assets/Models/TScene.glb", true);
        
        // specify a small mass
        glm::mat4 spawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(25.0f, 100.0f, 35.0f));
        renderSystem->load_additional_model("Assets/Models/BaseballBat.glb", false, 1.5f, spawnPos);
        
        glm::mat4 charSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(15.0f, 10.0f, 25.0f));
        renderSystem->load_additional_model("Assets/Models/Animated Character Base.glb", false, 80.0f, charSpawnPos);

        // Scale cars down to 0.1
        glm::mat4 carSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 10.0f, 15.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
        renderSystem->load_additional_model("Assets/Models/Car.glb", false, 1500.0f, carSpawnPos);

        // Scale helicopter down to 0.3
        glm::mat4 heliSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(45.0f, 10.0f, 5.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.3f));
        renderSystem->load_additional_model("Assets/Models/Helicopter.glb", false, 3000.0f, heliSpawnPos);

        glm::mat4 missileSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(35.0f, 10.0f, 45.0f));
        renderSystem->load_additional_model("Assets/Models/Missile.glb", false, 50.0f, missileSpawnPos);

        glm::mat4 policeCarSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(25.0f, 10.0f, 15.0f));
        renderSystem->load_additional_model("Assets/Models/Police Car.glb", false, 1600.0f, policeCarSpawnPos);

        glm::mat4 romanSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 10.0f, 25.0f));
        renderSystem->load_additional_model("Assets/Models/Roman Centurion.glb", false, 90.0f, romanSpawnPos);

        sceneManager->print_all_entities();
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
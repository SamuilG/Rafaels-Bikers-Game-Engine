#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include <flecs.h>
// ================= debug =================
#include "../Debug/DebugRenderer.hpp"

namespace engine {

	Application::Application() {


		//basic systems

		//AddSystem<WindowSystem>();
		inputSystem = AddSystem<InputSystem>();
		eventSystem = AddSystem<EventSystem>();
		//AddSystem<SoundSystem>();

		//world systems

		//AddSystem<TerrainSystem>();

		//get input and calculate data



		physicsSystem = AddSystem<PhysicsSystem>();
		physicsSystem->SetEventSystem(eventSystem);
		physicsSystem->SetUserState(&mState);
		sceneManager = AddSystem<SceneManager>(physicsSystem);

		//final render 

		//AddSystem<CameraSystem>();
		//AddSystem<UISystem>();
		renderSystem = AddSystem<RenderSystem>(Running, sceneManager);
		renderSystem->SetUserState(&mState);
		// Initialise all systems
		for (auto& sys : Systems) {
			sys->Init();
		}

        if (inputSystem && renderSystem) {
            inputSystem->SetWindow(renderSystem->GetGLFWWindow());
            renderSystem->SetInputSystem(inputSystem);
            physicsSystem->SetInputSystem(inputSystem);
            
        }

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


		//burstlink's old bike.
		//glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, 10.0f, 30.0f));
		//renderSystem->load_additional_model("Assets/Models/bike.glb", false, 50.0f, BikeSpawnPos,true, true);

        glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, 10.0f, 30.0f));
        renderSystem->load_additional_model("Assets/Models/bicycle.glb", false, 50.0f, BikeSpawnPos ,false, true);



		glm::mat4 LampSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(40.0f, 2.0f, 15.0f));
		renderSystem->load_additional_model("Assets/Models/Lamp post.glb", false, 90.0f, LampSpawnPos);

		glm::mat4 CubeSpawnPos = glm::translate(BikeSpawnPos, glm::vec3(0.0f, -7.0f, 8.0f));
		renderSystem->load_additional_model("Assets/Models/em1.gltf", false, 90.0f, CubeSpawnPos);

		glm::mat4 tbpos = glm::translate(BikeSpawnPos, glm::vec3(0.0f, 0.0f, -8.0f));
		renderSystem->load_additional_model("Assets/Models/testBike1.gltf", false, 90.0f, tbpos, false, true);



		//=============================================Headlight ==============================================
		glm::mat4 localLightOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 1.2f));

		flecs::entity headlight = sceneManager->create_light_entity(
			"headlight",
			engine::LightType::Spot,
			glm::vec3(1.0f, 0.95f, 0.85f),
			15.0f,
			localLightOffset,
			40.0f,
			glm::vec3(0.0f, 0, 1.0f),
			15.0f,
			25.0f
		);



		flecs::entity bikeEntity = sceneManager->find_entity("立方体_0");

		if (bikeEntity.is_valid()) {
			headlight.child_of(bikeEntity); // 魔法就在这一行！
			std::print("Success: Headlight is now firmly attached to the Bike!\n");
		}
		else {
			std::print("Warning: Could not find CPart_0! Check your terminal output for the correct bike entity name.\n");
		}

		//=============================================Headlight End ==============================================




		   // ===================UI System===========================
		// 加载上次保存的 JSON 存档
		//load the last saved JSON save file
	  //  engine::EngineUi::LoadProject(sceneManager, renderSystem, "Assets/MySceneSave.json");
		// ===================UI System===========================
		// 
		// 最后再打印实体列表，确认灯光实体已创建
		sceneManager->print_all_entities();
		// --- 【修改】计算车头灯的初始位置 ---
// 假设车头在 Y轴偏上(1.0米)，Z轴偏前(1.2米) 的位置




		//add Lights Here
		// 定义方向：从右上方向左下方照射
		glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
		// 将方向存入矩阵的平移列（我们的 SceneManager 会提取它作为光源方向）
		glm::mat4 sunTransform = glm::mat4(1.0f);
		sunTransform[3] = glm::vec4(sunDir, 0.0f);

		sceneManager->create_light_entity(
			"MainSun",
			engine::LightType::Directional,
			glm::vec3(1.2f, 0.95f, 0.8f), //color
			2.5f,//intensity     
			sunTransform,
			0
		);



		//// 定义点光源位置
  //      glm::mat4 light1SpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(40.0f, 18.0f, 15.0f));
  //      sceneManager->create_light_entity(
  //          "lampLight",
  //          engine::LightType::Point,
  //          glm::vec3(0.5f, 1.0f, 2.0f), //color
  //          3,//intensity     

		//	light1SpawnPos ,
		//	40.0f // range
  //      );

		// 定义点光源位置
		glm::mat4 light2SpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 3.0f, 30.0f));
		sceneManager->create_light_entity(
			"voidLight",
			engine::LightType::Point,
			glm::vec3(0.5f, 0.0f, 3.0f), //color
			5,//intensity     

			light2SpawnPos,
			20.0f // range
		);
		//// ===================UI System===========================
		//// 加载上次保存的 JSON 存档
		////load the last saved JSON save file
		//engine::EngineUi::LoadProject(sceneManager, renderSystem, "Assets/MySceneSave.json");
		//// ===================UI System===========================
		//// 
		//// 最后再打印实体列表，确认灯光实体已创建
		//sceneManager->print_all_entities();


		// Physics Collision Verification
		eventSystem->Subscribe(EventType::Collision, [this](Event& e) {
			auto& collisionE = static_cast<CollisionEvent&>(e);

			uint32_t idA = std::stoul(collisionE.GetEntityA());
			uint32_t idB = std::stoul(collisionE.GetEntityB());

			std::string nameA = this->sceneManager->get_entity_name_from_body_id(idA);
			std::string nameB = this->sceneManager->get_entity_name_from_body_id(idB);

			std::printf("[PhysicsSystem] CRASH: %s and %s collided.\n",
				nameA.c_str(),
				nameB.c_str());
			});

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

			// =========================================================================
			//draw debug box
			renderSystem->mDebugRenderer.DrawBox(
				glm::vec3(0.0f, 1.0f, 0.0f),
				glm::vec3(1.0f, 1.0f, 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f)
			);

			//draw debug line
			renderSystem->mDebugRenderer.DrawLine(
				glm::vec3(3.0f, 0.0f, 0.0f),
				glm::vec3(3.0f, 5.0f, 0.0f),
				glm::vec3(1.0f, 0.0f, 0.0f)
			);

			//draw debug sphere
			renderSystem->mDebugRenderer.DrawSphere(
				glm::vec3(-4.0f, 2.0f, 0.0f),
				2.0f,
				glm::vec3(0.0f, 0.0f, 1.0f)
			);

			//draw debug capsule
			renderSystem->mDebugRenderer.DrawCapsule(
				glm::vec3(0.0f, 2.0f, 5.0f),
				1.0f,
				1.5f,
				glm::vec3(1.0f, 1.0f, 0.0f)
			);
			// =========================================================================

			for (auto& sys : Systems)
				sys->Update(dt);
		}
	}



}
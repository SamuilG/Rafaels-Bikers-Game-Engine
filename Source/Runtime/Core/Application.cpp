#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/bikeController.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
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

		// 【新增】：把 mState 也传给 SceneManager！
		sceneManager->SetUserState(&mState);

		// Animation system (must be added before RenderSystem so Update order is correct)
		animationSystem = AddSystem<AnimationSystem>();

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

        // Wire animation system to scene and renderer
        animationSystem->set_scene_manager(sceneManager);
        renderSystem->set_animation_system(animationSystem);



		// load models using the API in RenderSystem
		// TScene is completely static (ground + buildings)
		// renderSystem->load_additional_model("Assets/Models/TScene.glb", true);
		renderSystem->load_additional_model("Assets/Models/TScene.glb", true, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
		//renderSystem->load_additional_model("Assets/Models/warehouseSceneWithShelf_opt.glb" , true, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(1.0f)));

		


		//======trigger box start=====
		//create a particle group
		renderSystem->AddParticleGroup();
		auto& triggerParticles = renderSystem->GetParticles();
		size_t triggerParticleIndex = triggerParticles.size() - 1;

		triggerParticles[triggerParticleIndex]->config.emitterPos = glm::vec3(50.0f, 1.0f, 20.0f);
		triggerParticles[triggerParticleIndex]->config.isVisible = false;//不可见

		// trigger: create a visible box trigger
		size_t triggerBox01 = renderSystem->GetTriggerSystem().AddBoxTrigger(
			glm::vec3(50.0f, 1.0f, 20.0f),
			glm::vec3(2.0f, 2.0f, 2.0f),
			triggerParticleIndex,
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::mat4(1.0f),
			true,
			false
		);
		// trigger callback system
		renderSystem->GetTriggerSystem().SetTriggerCallbacks(triggerBox01,
			[]() {
				engine::EngineUi::LogPrint("trigger box triggered!!\n");
			},
			[]() {
				engine::EngineUi::LogPrint("trigger box exited!!\n");
			}
		);
		//======trigger box end=====

		// testBiek1,2  二选一+
		// testBike2 有compound bug
		


		glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, 10.0f, 30.0f));
		glm::mat4 tbpos = glm::translate(BikeSpawnPos, glm::vec3(0.0f, 0.0f, -8.0f));
		glm::mat4 bikeAnchorWorld = glm::mat4(0.0f); // sentinel; filled from the "Anchor" node if found
		renderSystem->load_additional_model("Assets/Models/tbikeWithAnchor.glb", false, 90.0f, tbpos, false, true, &bikeAnchorWorld);

		renderSystem->load_animated_model("Assets/Models/character.glb", tbpos);



		// ==============================================================
		// 【新增 2】：实例化并初始化 BikeController！
		// ==============================================================
		// 1. 实例化控制器，把需要的三个系统传给它
		// 注意：这里需要给 PhysicsSystem 加一个 GetJoltSystem() 的接口，或者你把它设成公有了？
		m_bikeController = std::make_unique<BikeController>(
			physicsSystem->GetJoltSystem(), // <--- 确保你在 PhysicsSystem 里暴露了这个获取底层 Jolt System 的指针
			inputSystem,
			&mState
		);

		// 2. 找到刚才加载出来的自行车的实体和物理 ID
		flecs::entity bikeEntity = sceneManager->find_entity("Bike_0");
		if (bikeEntity.is_valid()) {
			// 拿到自行车的物理 Body ID
			uint32_t bikeBodyID = JPH::BodyID::cInvalidBodyID;

			// 之前排错时我们知道，动态模型可能挂载的是 PhysicsBody 或者 CompoundParent
			if (bikeEntity.has<PhysicsBody>()) {
				bikeBodyID = bikeEntity.get<PhysicsBody>().bodyID;
			}
			else if (bikeEntity.has<CompoundParent>()) {
				bikeBodyID = bikeEntity.get<CompoundParent>().bodyID;
			}

			// 3. 将物理 ID 传给控制器进行初始化
			m_bikeController->Init(bikeBodyID);
		}
		else {
			std::print("[Error] BikeController Init Failed: Could not find Bike_0 entity!\n");
		}

	


		// =======================================================
		// Rider binding: attach character to bike seat + IK
		// =======================================================
		{
			// --- Find the first skinned entity (the character) ---
			flecs::entity charEntity;
			sceneManager->get_world().query<SkinComponent>()
				.each([&](flecs::entity e, SkinComponent&) {
					if (!charEntity.is_valid()) charEntity = e;
				});

			// --- Find specific bike-part entities by name substring ---
			// Names are GLTF node name + "_N" counter suffix set by load_C_model.
			flecs::entity handleLEntity;  // handle_left_4  – left handlebar grip
			flecs::entity handleREntity;  // handle_right_5 – right handlebar grip
			flecs::entity pedalLEntity;   // pedal_left_8   – left pedal plate
			flecs::entity pedalREntity;   // pedal_right_9  – right pedal plate
			sceneManager->get_world().query<CompoundParent>()
				.each([&](flecs::entity e, CompoundParent&) {
					const char* n = e.name();
					if (!n) return;
					if (!handleLEntity.is_valid() && strstr(n, "handle_left_4"))  handleLEntity = e;
					if (!handleREntity.is_valid() && strstr(n, "handle_right_5")) handleREntity = e;
					if (!pedalLEntity.is_valid()  && strstr(n, "pedal_left_8"))   pedalLEntity  = e;
					if (!pedalREntity.is_valid()  && strstr(n, "pedal_right_9"))  pedalREntity  = e;
				});

			std::print("[App] Bike parts found – handleL:{} handleR:{} pedalL:{} pedalR:{}\n",
				handleLEntity.is_valid(), handleREntity.is_valid(),
				pedalLEntity.is_valid(), pedalREntity.is_valid());

			if (charEntity.is_valid() && bikeEntity.is_valid()) {
				// Bone names confirmed from debug_print_nodes() output (Mixamo rig):
				//   Arms:  LeftArm / LeftForeArm / LeftHand
				//          RightArm / RightForeArm / RightHand
				//   Legs:  LeftUpLeg / LeftLeg / LeftFoot
				//          RightUpLeg / RightLeg / RightFoot

				// --- Seat offset: character root placed at the "Anchor" node in bike-local space ---
				// bikeAnchorWorld[3][3] == 1 means the Anchor node was found in the GLB.
				// We strip scale from the bike's initial world transform (matching what load_C_model
				// does when it sets up the physics body) then express the anchor in body-local space.
				glm::mat4 seatOffset;
				bool anchorFound = (bikeAnchorWorld[3][3] == 1.0f);
				std::print("[App] Anchor node {}, world pos ({:.3f}, {:.3f}, {:.3f})\n",
				    anchorFound ? "FOUND" : "NOT FOUND",
				    bikeAnchorWorld[3][0], bikeAnchorWorld[3][1], bikeAnchorWorld[3][2]);
				if (anchorFound) {
				    glm::mat4 bikeMat = bikeEntity.get<LocalTransform>().matrix;
				    glm::vec3 s, t, skew; glm::quat r; glm::vec4 persp;
				    glm::decompose(bikeMat, s, r, t, skew, persp);
				    glm::mat4 bodyTR = glm::translate(glm::mat4(1.0f), t)
				                    * glm::mat4_cast(glm::normalize(r));
				    seatOffset = glm::inverse(bodyTR) * bikeAnchorWorld
				               * glm::scale(glm::mat4(1.0f), glm::vec3(2.1f));
				} else {
				    seatOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.7f, 0.0f))
				               * glm::scale(glm::mat4(1.0f), glm::vec3(2.1f));
				}

				// --- Build shared IK chains (Mixamo bone names) ---
				RiderIKComponent rik;
				rik.bikeEntityId  = bikeEntity.id();
				rik.leanAngleDeg  = 35.0f; // torso forward lean in degrees

				// handle positions in bike-local space (from startup log):
				//   handle_left  = (+0.430, +1.265, +0.817)
				//   handle_right = (-0.424, +1.265, +0.817)
				// Pole: elbow should point outward and slightly downward for a natural grip.

				// Left hand → handle_left_4
				{
					IKChainConfig c;
					c.rootBone          = "LeftArm";
					c.midBone           = "LeftForeArm";
					c.endBone           = "LeftHand";
					c.targetEntityId    = handleLEntity.is_valid() ? handleLEntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);     // grip slightly above entity origin
					c.localBikeTarget   = glm::vec3(0.43f, 1.35f, 0.817f);  // exact handle pos (raised)
					c.localBikePole     = glm::vec3(0.80f, 0.60f, 0.30f);   // elbow out-left & down
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Right hand → handle_right_5
				{
					IKChainConfig c;
					c.rootBone          = "RightArm";
					c.midBone           = "RightForeArm";
					c.endBone           = "RightHand";
					c.targetEntityId    = handleREntity.is_valid() ? handleREntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);      // grip slightly above entity origin
					c.localBikeTarget   = glm::vec3(-0.424f, 1.35f, 0.817f); // exact handle pos (raised)
					c.localBikePole     = glm::vec3(-0.80f, 0.60f, 0.30f);   // elbow out-right & down
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Left foot → pedal_left_8  (pedal_left bike-local ≈ +0.30, 0.01, -0.12)
				{
					IKChainConfig c;
					c.rootBone          = "LeftUpLeg";
					c.midBone           = "LeftLeg";
					c.endBone           = "LeftFoot";
					c.targetEntityId    = pedalLEntity.is_valid() ? pedalLEntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);    // foot slightly above pedal center
					c.localBikeTarget   = glm::vec3(0.30f, 0.12f, -0.12f); // pedal pos fallback (raised)
					c.localBikePole     = glm::vec3(0.45f, 0.2f,  0.40f);  // knee out-left
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Right foot → pedal_right_9  (pedal_right bike-local ≈ -0.30, -0.29, +0.17)
				{
					IKChainConfig c;
					c.rootBone          = "RightUpLeg";
					c.midBone           = "RightLeg";
					c.endBone           = "RightFoot";
					c.targetEntityId    = pedalREntity.is_valid() ? pedalREntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);     // foot slightly above pedal center
					c.localBikeTarget   = glm::vec3(-0.30f, -0.10f, 0.17f); // pedal pos fallback (raised)
					c.localBikePole     = glm::vec3(-0.45f, 0.2f,  0.40f);  // knee out-right
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Apply binding + IK to ALL skinned character entities (character has 2 mesh parts).
				// Also add a dummy AnimationComponent (animIndex=-1) so AnimationSystem's query
				// picks them up even though this character has no animation clips.
// 1. 显式获取世界引用
				flecs::world& world = sceneManager->get_world();

				// 2. 开启延迟修改队列
				world.defer_begin();

				// 3. 执行遍历（此时所有的 set 操作不会立即生效，而是被压入队列）
				world.query<SkinComponent>()
					.each([&](flecs::entity e, SkinComponent&) {
					e.set<RiderBinding>({ bikeEntity.id(), seatOffset });
					e.set<RiderIKComponent>(rik);   // copy to each mesh part
					if (!e.has<AnimationComponent>()) {
						AnimationComponent ac{};
						ac.animIndex = -1;   // no clip → rest pose; IK still runs
						ac.playing = false;
						ac.looping = false;
						e.set<AnimationComponent>(ac);
					}
						});

				// 4. 结束延迟，批量合并修改，表此时已解锁，安全！
				world.defer_end();

				std::print("[App] Rider bound (2 mesh parts) → bike '{}'\n",
					bikeEntity.name() ? bikeEntity.name() : "?");
			} else {
				std::print("[App] Warning: rider bind failed (char={} bike={})\n",
					charEntity.is_valid(), bikeEntity.is_valid());
			}
		}
		//=============================================Headlight ==============================================
		glm::mat4 localLightOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.7f, 1.7f));

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



		bikeEntity = sceneManager->find_entity("Bike_0");

		if (bikeEntity.is_valid()) {
			headlight.child_of(bikeEntity); 
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
			//glm::vec3(1, 1, 1), //color
			8,//intensity     

			light2SpawnPos,
			20.0f // range
		);


		// Physics Collision Verification
		eventSystem->Subscribe(EventType::Collision, [this](Event& e) {
			auto& collisionE = static_cast<CollisionEvent&>(e);

			uint32_t idA = std::stoul(collisionE.GetEntityA());
			uint32_t idB = std::stoul(collisionE.GetEntityB());

			std::string nameA = this->sceneManager->get_entity_name_from_body_id(idA);
			std::string nameB = this->sceneManager->get_entity_name_from_body_id(idB);

			std::printf("[PhysicsSystem] COLLISION: %s and %s collided.\n",
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
			// ==========================================================
				// 【新增 3】：每帧更新单车的物理逻辑
				if (m_bikeController) {
					m_bikeController->Update(dt);
				}
			// ==========================================================


			for (auto& sys : Systems)
				sys->Update(dt);
		}
	}



}
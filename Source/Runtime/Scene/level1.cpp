#include "Level1.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/bikeController.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../UserState/GameplayState.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include <Jolt/Physics/Body/BodyLock.h>
#include <format>
#include <filesystem>
#include <algorithm>
#include <unordered_set>

namespace engine {
	namespace {
		constexpr const char* kRespawnPromptUiPath = "Assets/ui/RespawnPrompt.ui.json";
	}

	level::level() = default;
	level::~level() = default;
	void level::Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, GameplayState* state, AnimationSystem* anima, AudioSystem* audio) {
		InitBase(render);
		m_render = render;
		m_scene = scene;
		m_physics = physics;
		m_input = input;
		m_event = eventSys;
		mState = state;
		m_anima = anima;
		m_audio = audio;
		m_respawnPromptVisible = false;

		RemoveWidget(kRespawnPromptUiPath);


		// load the terrain and static models
		//m_scene->LoadModel(m_render, "Assets/Models/TScene.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
		m_scene->LoadModel(m_render, "Assets/Models/Level.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
		//
		//m_scene->LoadModel(m_render, "Assets/Models/forest.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));

		//m_scene->LoadModel(m_render, "Assets/Models/testCurvePlane.glb", engine::ModelPhysicsType::Static, 0.0f, CplaneSpawnPos);
		////m_scene->LoadModel(m_render, "Assets/Models/sponza.glb", engine::ModelPhysicsType::Static, 0.0f, CplaneSpawnPos);

		//m_scene->LoadModel(m_render, "Assets/Models/darkRoom.glb", engine::ModelPhysicsType::Static, 0.0f, darkRoomSpawnPos);

		// rocket stand and rocket at (225.92, 99.24, -291.96)
		// Collect ALL mesh entities created by this load (before/after diff),
		// so every part animates together on launch regardless of mesh index order.
		{
			// Snapshot entity IDs already in the world before loading
			std::unordered_set<uint64_t> existingIds;
			m_scene->get_world().query<const MeshComponent>()
				.each([&](flecs::entity e, const MeshComponent&) {
					existingIds.insert(e.id());
				});

			m_scene->LoadModel(
				m_render, "Assets/Models/rocket2.glb",
				engine::ModelPhysicsType::Static, 0.0f,
				glm::translate(glm::mat4(1.0f), m_rocket2Center), engine::RenderLayer::Emissive);

			// Collect only entities that appeared after the load
			m_scene->get_world().query<const MeshComponent>()
				.each([&](flecs::entity e, const MeshComponent&) {
					if (existingIds.find(e.id()) == existingIds.end())
						m_rocket2Entities.push_back(e);
				});
		}

		//TODO:change
		glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(-152, 3, -84));
		glm::mat4 tbpos = glm::translate(BikeSpawnPos, glm::vec3(2,4,-157));\
		glm::mat4 FinalPos = glm::translate(glm::mat4(1.0f), glm::vec3(218.83, 91.0f, -197.74f));
		glm::mat4 bikeAnchorWorld = glm::mat4(0.0f); // sentinel: [3][3]==0 means anchor not found

		m_render->load_animated_model("Assets/Models/character.glb", FinalPos);
		flecs::entity playerBike = m_scene->LoadModel(m_render, "Assets/Models/tbikeWithAnchor.glb", engine::ModelPhysicsType::CustomC, 90.0f, BikeSpawnPos);

		// 3. ��ʼ������������
		m_bikeController = std::make_unique<BikeController>(m_physics->GetJoltSystem(), m_input, mState);
		m_audio->LoadSound("Jump", "Assets/Sounds/jump_effect.mp3");
		m_audio->LoadSound("SpringJump", "Assets/Sounds/spring.mp3");
		m_audio->SetVolume("SpringJump", 1.2f);
		m_bikeController->SetAudioSystem(m_audio);
		flecs::entity bikeEntity = m_scene->find_entity("Bike_0");
		m_bikeEntity = bikeEntity;
		if (bikeEntity.is_valid()) {
			uint32_t bikeBodyID = JPH::BodyID::cInvalidBodyID;
			if (bikeEntity.has<PhysicsBody>()) bikeBodyID = bikeEntity.get<PhysicsBody>().bodyID;
			else if (bikeEntity.has<CompoundParent>()) bikeBodyID = bikeEntity.get<CompoundParent>().bodyID;
			m_bikeController->Init(bikeBodyID);
		}

		{
			// --- Find the first skinned entity (the character) ---
			flecs::entity charEntity;
			m_scene->get_world().query<SkinComponent>()
				.each([&](flecs::entity e, SkinComponent&) {
				if (!charEntity.is_valid()) charEntity = e;
					});

			// --- Find specific bike-part entities by name substring ---
			// Names are GLTF node name + "_N" counter suffix set by load_C_model.
			flecs::entity handleLEntity;  // handle_left_4  �C left handlebar grip
			flecs::entity handleREntity;  // handle_right_5 �C right handlebar grip
			flecs::entity pedalLEntity;   // pedal_left_8   �C left pedal plate
			flecs::entity pedalREntity;   // pedal_right_9  �C right pedal plate
			m_scene->get_world().query<CompoundParent>()
				.each([&](flecs::entity e, CompoundParent&) {
				const char* n = e.name();
				if (!n) return;
				if (!handleLEntity.is_valid() && strstr(n, "handle_left_4"))  handleLEntity = e;
				if (!handleREntity.is_valid() && strstr(n, "handle_right_5")) handleREntity = e;
				if (!pedalLEntity.is_valid() && strstr(n, "pedal_left_8"))   pedalLEntity = e;
				if (!pedalREntity.is_valid() && strstr(n, "pedal_right_9"))  pedalREntity = e;
					});

			printf("[App] Bike parts found - handleL:%d handleR:%d pedalL:%d pedalR:%d\n",
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
				printf("[App] Anchor node %s, world pos (%.3f, %.3f, %.3f)\n",
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
				}
				else {
					seatOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.7f, 0.0f))
						* glm::scale(glm::mat4(1.0f), glm::vec3(2.1f));
				}

				// --- Build shared IK chains (Mixamo bone names) ---
				RiderIKComponent rik;
				rik.bikeEntityId = bikeEntity.id();
				rik.leanAngleDeg = 35.0f; // torso forward lean in degrees

				// handle positions in bike-local space (from startup log):
				//   handle_left  = (+0.430, +1.265, +0.817)
				//   handle_right = (-0.424, +1.265, +0.817)
				// Pole: elbow should point outward and slightly downward for a natural grip.

				// Left hand �� handle_left_4
				{
					IKChainConfig c;
					c.rootBone = "LeftArm";
					c.midBone = "LeftForeArm";
					c.endBone = "LeftHand";
					c.targetEntityId = handleLEntity.is_valid() ? handleLEntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);     // grip slightly above entity origin
					c.localBikeTarget = glm::vec3(0.43f, 1.35f, 0.817f);  // exact handle pos (raised)
					c.localBikePole = glm::vec3(0.80f, 0.60f, 0.30f);   // elbow out-left & down
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Right hand �� handle_right_5
				{
					IKChainConfig c;
					c.rootBone = "RightArm";
					c.midBone = "RightForeArm";
					c.endBone = "RightHand";
					c.targetEntityId = handleREntity.is_valid() ? handleREntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);      // grip slightly above entity origin
					c.localBikeTarget = glm::vec3(-0.424f, 1.35f, 0.817f); // exact handle pos (raised)
					c.localBikePole = glm::vec3(-0.80f, 0.60f, 0.30f);   // elbow out-right & down
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Left foot �� pedal_left_8  (pedal_left bike-local �� +0.30, 0.01, -0.12)
				{
					IKChainConfig c;
					c.rootBone = "LeftUpLeg";
					c.midBone = "LeftLeg";
					c.endBone = "LeftFoot";
					c.targetEntityId = pedalLEntity.is_valid() ? pedalLEntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);    // foot slightly above pedal center
					c.localBikeTarget = glm::vec3(0.30f, 0.12f, -0.12f); // pedal pos fallback (raised)
					c.localBikePole = glm::vec3(0.45f, 0.2f, 0.40f);  // knee out-left
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Right foot �� pedal_right_9  (pedal_right bike-local �� -0.30, -0.29, +0.17)
				{
					IKChainConfig c;
					c.rootBone = "RightUpLeg";
					c.midBone = "RightLeg";
					c.endBone = "RightFoot";
					c.targetEntityId = pedalREntity.is_valid() ? pedalREntity.id() : 0;
					c.localTargetOffset = glm::vec3(0.0f, 0.06f, 0.0f);     // foot slightly above pedal center
					c.localBikeTarget = glm::vec3(-0.30f, -0.10f, 0.17f); // pedal pos fallback (raised)
					c.localBikePole = glm::vec3(-0.45f, 0.2f, 0.40f);  // knee out-right
					c.enabled = true;
					rik.chains.push_back(c);
				}

				// Apply binding + IK to ALL skinned character entities (character has 2 mesh parts).
				// Also add a dummy AnimationComponent (animIndex=-1) so AnimationSystem's query
				// picks them up even though this character has no animation clips.
				// 1. ��ʽ��ȡ��������
				flecs::world& world = m_scene->get_world();

				// 2. �����ӳ��޸Ķ���
				world.defer_begin();

				// 3. ִ�б�������ʱ���е� set ��������������Ч�����Ǳ�ѹ����У�
				world.query<SkinComponent>()
					.each([&](flecs::entity e, SkinComponent&) {
					e.set<RiderBinding>({ bikeEntity.id(), seatOffset });
					e.set<RiderIKComponent>(rik);   // copy to each mesh part
					if (!e.has<AnimationComponent>()) {
						AnimationComponent ac{};
						ac.animIndex = -1;   // no clip �� rest pose; IK still runs
						ac.playing = false;
						ac.looping = false;
						e.set<AnimationComponent>(ac);
					}
						});

				// 4. �����ӳ٣������ϲ��޸ģ�����ʱ�ѽ�������ȫ��
				world.defer_end();

				printf("[App] Rider bound (2 mesh parts) -> bike '%s'\n",
					bikeEntity.name() ? bikeEntity.name() : "?");
			}
			else {
				printf("[App] Warning: rider bind failed (char=%d bike=%d)\n",
					charEntity.is_valid(), bikeEntity.is_valid());
			}
		}

		//collision event test
		{
			std::string bikeBodyIDStr;
			if (bikeEntity.has<CompoundParent>())
				bikeBodyIDStr = std::to_string(bikeEntity.get<CompoundParent>().bodyID);

			m_event->Subscribe(EventType::Collision, [this, bikeBodyIDStr](Event& e) {
				auto& col = static_cast<CollisionEvent&>(e);
				if (col.GetEntityA() != bikeBodyIDStr && col.GetEntityB() != bikeBodyIDStr) return;
				if (mState->isGameOver) return; // already dead, ignore further events

				// --- Physics-based impact thresholds (SI units: m/s) ---
				// 15 km/h = 4.17 m/s  -> light hit
				// 30 km/h = 8.33 m/s  -> fatal
				constexpr float kLightHitSpeed = 8.33f;
				constexpr float kFatalSpeed = 12.0f;
				// normalAlignment: fraction of total bike speed directed into the surface
				//   ~1.0 = head-on (perpendicular), ~0.0 = pure side-scrape (tangential)
				constexpr float kScrapeThreshold = 0.35f; // below this -> side scrape, no damage
				constexpr float kFrontalThreshold = 0.50f; // above this -> counts as frontal for fatal

				float impactSpeed = col.GetRelativeSpeed(); // approach speed along normal (m/s)
				float bikeSpeed = mState->bikeSpeed;     // horizontal speed (m/s)

				// Normalised alignment: how much of the bike's speed is directed into the wall
				float normalAlignment = (bikeSpeed > 0.5f)
					? glm::clamp(impactSpeed / bikeSpeed, 0.0f, 1.0f)
					: 0.0f;

				if (impactSpeed < kLightHitSpeed) return; // too slow, no effect

				if (normalAlignment < kScrapeThreshold) {
					// Side scrape: tangential contact, no damage
					printf("[Collision] Side scrape ignored: speed=%.2f m/s align=%.2f\n",
						impactSpeed, normalAlignment);
					return;
				}

				if (impactSpeed >= kFatalSpeed && normalAlignment >= kFrontalThreshold) {
					// Fatal frontal collision -> game over
					mState->isAlive = false;


					m_audio->LoadSound("wasted", "Assets/Sounds/wasted.mp3");
					m_audio->SetVolume("wasted", 1.5f);
					m_audio->PlayOneShot("wasted");
					mState->deathTimer = 0.0f;
					mState->thirdPersonMode = false;
					printf("[Collision] FATAL: speed=%.2f m/s align=%.2f | %s vs %s\n",
						impactSpeed, normalAlignment,
						col.GetEntityA().c_str(), col.GetEntityB().c_str());
				}
				});
		}

		// light source 
		glm::mat4 emissivecubeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 3.0f, 200.0f));
		flecs::entity emCubeEntity = m_scene->LoadModel(m_render, "Assets/DELETE_LATER/em1.gltf", engine::ModelPhysicsType::Dynamic, 0.01f, emissivecubeSpawnPos, engine::RenderLayer::Emissive);

		flecs::entity emc = m_scene->create_light_entity("emCubeLight", engine::LightType::Point, glm::vec3(1.0f, 1.0f, 1.0f), 8.0f, glm::mat4(1.0f), 10.0f, glm::vec3(0, -1, 0), 0, 0, emCubeEntity);
		emc.get_mut<engine::LightComponent>().specularMultiplier = 0.0f;








		//light
		glm::mat4 localLightOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.7f, 1.7f));
		flecs::entity headlight = m_scene->create_light_entity("headlight", engine::LightType::Spot, glm::vec3(1.0f, 0.95f, 0.85f), 15.0f, localLightOffset, 40.0f, glm::vec3(0.0f, 0, 1.0f), 15.0f, 25.0f);
		flecs::entity playerLight = m_scene->create_light_entity("playerLight", engine::LightType::Point, glm::vec3(1.0f, 1.0f, 1.0f), 1.0f, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), 20.0f, glm::vec3(0, -1, 0), 0, 0, playerBike);
		playerLight.get_mut<engine::LightComponent>().specularMultiplier = 0.0f;
		if (bikeEntity.is_valid())
		{
			headlight.child_of(bikeEntity);
		}

		// ̫���������ƹ�
		glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
		glm::mat4 sunTransform = glm::mat4(1.0f); sunTransform[3] = glm::vec4(sunDir, 0.0f);
		m_scene->create_light_entity("MainSun", engine::LightType::Directional, glm::vec3(1.2f, 0.95f, 0.8f), 2.5f, sunTransform, 0);

		glm::mat4 light2SpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 3.0f, 30.0f));
		flecs::entity vidl =  m_scene->create_light_entity("voidLight", engine::LightType::Point, glm::vec3(0.5f, 0.0f, 3.0f), 8, light2SpawnPos, 20.0f);
		vidl.get_mut<engine::LightComponent>().specularMultiplier = 0.0f;
		// 5. ���ô����� (Trigger)
		m_render->AddParticleGroup();
		auto& triggerParticles = m_render->GetParticles();
		size_t triggerParticleIndex = triggerParticles.size() - 1;
		triggerParticles[triggerParticleIndex]->config.emitterPos = glm::vec3(50.0f, 1.0f, 20.0f);
		triggerParticles[triggerParticleIndex]->config.isVisible = false;

		size_t triggerBox01 = m_render->GetTriggerSystem().AddBoxTrigger(
			glm::vec3(50.0f, 1.0f, 20.0f), glm::vec3(2.0f, 2.0f, 2.0f), triggerParticleIndex, glm::vec3(1.0f, 0.0f, 0.0f), glm::mat4(1.0f), true, false);

		m_render->GetTriggerSystem().SetTriggerCallbacks(triggerBox01,
			[]() { GameScene::Log("trigger box triggered!!\n"); },
			[]() { GameScene::Log("trigger box exited!!\n"); }
		);

		m_scene->print_all_entities();


		{

			uint32_t bikeBodyID_raw = JPH::BodyID::cInvalidBodyID;
			if (bikeEntity.is_valid()) {
				if (bikeEntity.has<CompoundParent>()) bikeBodyID_raw = bikeEntity.get<CompoundParent>().bodyID;
				else if (bikeEntity.has<PhysicsBody>())  bikeBodyID_raw = bikeEntity.get<PhysicsBody>().bodyID;
			}

			//280，47，513
			//-175，-28，8
			static const glm::vec3 kCollectPos[5] = {
				{  60.0f, 21.0f,  70.0f }, { 270.0f, 66.0f,  311.0f }, { 107.0f, -43.0f,  282.0f },
				{ 280.0f, 47.0f,  513.0f }, {  -175.0f, -28.0f, 8.0f }, //{ 15.0f, 1.0f,   2.0f },
				
			};
			
			constexpr int kTotalCollectibles = 15;

			// Load GLB once to register mesh/material assets, then hide all created entities
			flecs::entity gasAsset = m_scene->LoadModel(
				m_render, "Assets/Models/gas_tank.glb",
				engine::ModelPhysicsType::Static, 0.0f,
				glm::translate(glm::mat4(1.0f), kCollectPos[0]), engine::RenderLayer::Emissive);
			
			uint32_t collectMeshIdx = 0;
			uint32_t collectMatIdx  = 0;
			if (gasAsset.is_valid()) {

				if (gasAsset.has<MeshComponent>())     collectMeshIdx = gasAsset.get<MeshComponent>().meshIndex;
				if (gasAsset.has<MaterialComponent>()) collectMatIdx  = gasAsset.get<MaterialComponent>().materialIndex;

				m_scene->get_world().query<const MeshComponent>()
					.each([&](flecs::entity e, const MeshComponent& mc) {
						if (mc.meshIndex < collectMeshIdx) return;
						e.set<EntityStatus>({ false, false });
						if (e.has<PhysicsBody>()) {
							JPH::BodyID bid(e.get<PhysicsBody>().bodyID);
							JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
							if (bi.IsAdded(bid)) { bi.RemoveBody(bid); bi.DestroyBody(bid); }
							e.remove<PhysicsBody>();
						}

					});

			}

			// Tilt applied once; spinning rotates around local (tilted) Y for wobble effect
			const glm::mat4 kTankTilt = glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(1.0f, 0.0f, 0.0f));

			// Create 15 physics-free visual entities, each with tilt
		// Create 15 physics-free visual entities, each with tilt
			m_gasPickupEntities.resize(kTotalCollectibles);
			for (int i = 0; i < kTotalCollectibles; ++i) {
				std::string eName = "gas_tank_pickup_" + std::to_string(i);
				glm::mat4 t = glm::translate(glm::mat4(1.0f), kCollectPos[i]) * kTankTilt;
				m_gasPickupEntities[i] = m_scene->create_dynamic_entity(
					eName.c_str(), collectMeshIdx, collectMatIdx, t);
				m_gasPickupEntities[i].set<EntityStatus>({ true, false });
				m_gasPickupEntities[i].set<engine::LayerComponent>({ engine::RenderLayer::Emissive });

				// =======================================================
				// 【新增】：为每个收集物挂载一个点光源
				// =======================================================
				std::string lightName = "gas_light_" + std::to_string(i);

				// 因为光源是收集物的子实体，所以这里的 Transform 是“局部偏移”
				// 使用单位矩阵，意味着光源就在收集物的正中心
				glm::mat4 localLightOffset = glm::mat4(1.0f);

				flecs::entity pickupLight = m_scene->create_light_entity(
					lightName.c_str(),
					engine::LightType::Point,
					glm::vec3(1.1f, 0.8f, 0.5f), // 颜色：科技感青蓝色 (你可以按喜好调成金黄色 1.0, 0.8, 0.2)
					10.5f,                        // 光照强度：不需要太亮，起点缀作用
					localLightOffset,            // 位置偏移：模型正中心
					5.0f,                        // 影响半径：5 米范围，避免性能浪费
					glm::vec3(0, -1, 0),         // 点光源方向随意
					0.0f, 0.0f,                  // 聚光灯锥角随意
					m_gasPickupEntities[i]       // 【核心】绑定为该收集物的子实体！
				);

				// (可选) 如果你觉得这个光反光太抢眼，也可以像单车灯一样关掉高光
				pickupLight.get_mut<engine::LightComponent>().specularMultiplier = 0.0f;
			}

			auto collectedCount = std::make_shared<int>(0);

			for (int i = 0; i < kTotalCollectibles; ++i) {
				size_t tid = m_render->GetTriggerSystem().AddSphereTrigger(
					kCollectPos[i],
					/*radius=*/2.5f,
					/*particleIndex=*/static_cast<size_t>(-1),
					/*color=*/glm::vec3(1.0f, 0.85f, 0.0f),
					/*isVisible=*/false,
					/*oneShot=*/true
				);
				// ... 前面的代码保持不变 ...
				m_render->GetTriggerSystem().SetTriggerCallbacks(tid,
					[this, i, collectedCount, kTotalCollectibles]() mutable {
						if (i < static_cast<int>(m_gasPickupEntities.size()) && m_gasPickupEntities[i].is_valid()) {

							// 关闭绑定的子光源
							m_gasPickupEntities[i].children([&](flecs::entity child) {
								if (child.has<engine::LightComponent>()) {
									child.set<EntityStatus>({ false, false });
								}
								});

							if (m_bikeEntity.is_valid()) {
								// =========================================================
								// 【核心修复】：获取它实际是第几个被收集到的（从 0 开始）
								// =========================================================
								int currentOrderIndex = *collectedCount;

								// 基础位置
								float startZ = -0.7f;
								// 每个收集物之间的间距
								float spacingZ = 0.15f;

								// 【修复】：使用 currentOrderIndex 而不是 i 来计算 Z 轴偏移
								glm::vec3 mountPos(-0.2f, 0.5f, startZ - (currentOrderIndex * spacingZ));

								// 生成 Transform
								glm::mat4 mountT =
									glm::translate(glm::mat4(1.0f), mountPos) *
									glm::scale(glm::mat4(1.0f), glm::vec3(0.45f));

								m_gasPickupEntities[i].child_of(m_bikeEntity);
								m_gasPickupEntities[i].set<LocalTransform>({ mountT });
							}
							else {
								m_gasPickupEntities[i].set<EntityStatus>({ false, false });
							}
							m_gasPickupEntities[i] = {}; // stop spinning
						}

						// 挂载火箭的逻辑保持不变，但触发条件可以更严谨一点：
						// 第一个被收集的物品 (currentOrderIndex == 0) 时触发火箭
						if (*collectedCount == 0 && m_rocketEntity.is_valid() && m_bikeEntity.is_valid()) {
							glm::mat4 rocketMountT =
								glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.4f, -1.2f)) *
								glm::scale(glm::mat4(1.0f), glm::vec3(0.3f));
							m_rocketEntity.child_of(m_bikeEntity);
							m_rocketEntity.set<LocalTransform>({ rocketMountT });
							m_rocketEntity.set<EntityStatus>({ true, false });
						}

						// 更新总收集数并发送事件
						int total = ++(*collectedCount);
						m_event->QueueEvent(std::make_unique<ItemCollectedEvent>(i, total));
						if (total >= kTotalCollectibles) {
							m_event->QueueEvent(std::make_unique<AllItemsCollectedEvent>());
						}
					},
					nullptr
				);
			}


			m_audio->LoadSound("Collect",     "Assets/Sounds/equip.mp3");
			m_audio->SetVolume("Collect",     0.5f);
			m_audio->LoadSound("AllCollectd", "Assets/Sounds/AllCollectd.mp3");
			m_audio->SetVolume("AllCollectd", 0.8f);

		/*	if (mState->isExtremeSpeed == true)
			{
				audioSystem->LoadSound("ExtremeSpeed0", "Assets/Sounds/deepBass.mp3");
				audioSystem->LoadSound("ExtremeSpeed1", "Assets/Sounds/breath.mp3");

				audioSystem->SetVolume("ExtremeSpeed0", 2.5f);
				audioSystem->SetVolume("ExtremeSpeed1", 2.5f);
				audioSystem->SetPitch("ExtremeSpeed0", 1.0f);
				audioSystem->SetPitch("ExtremeSpeed1", 1.0f);

				audioSystem->PlayLoop("ExtremeSpeed0");
				audioSystem->PlayOneShot("ExtremeSpeed1");
			}*/
			m_audio->LoadSound("ExtremeSpeed0", "Assets/Sounds/deepBass.mp3");
			m_audio->LoadSound("ExtremeSpeed1", "Assets/Sounds/breath.mp3");
			
			


			if (mState->isExtremeSpeed)
			{
				
				m_audio->SetVolume("ExtremeSpeed0", 2.5f);
				m_audio->SetVolume("ExtremeSpeed1", 2.5f);
				m_audio->SetPitch("ExtremeSpeed0", 1.0f);
				m_audio->SetPitch("ExtremeSpeed1", 1.0f);

				m_audio->PlayLoop("ExtremeSpeed0");
				m_audio->PlayOneShot("ExtremeSpeed1");
			}

			m_event->Subscribe(EventType::ItemCollected, [this, bikeBodyID_raw](Event& e) {
				auto& col = static_cast<ItemCollectedEvent&>(e);
				int collected = col.GetCurrentTotal();
				mState->collectedItems = collected;
				Log(std::format("[Collection] {}/{} collected\n",
					collected, mState->totalCollectibles));

				m_audio->PlayOneShot("Collect");

				// 1. 保留特定里程碑（每收集 5 个）的特殊音效/延迟逻辑
				if (collected % 5 == 0) {
					m_allCollectSoundDelay = 0.6f;
				}

				// 2. 将减重逻辑移出 % 5 判断，使其每次收集必定触发
				if (bikeBodyID_raw != JPH::BodyID::cInvalidBodyID) {

					constexpr float kInitialMass = 90.0f;
					constexpr float kMassDropPerItem = 8.0f; // 每次收集减去 3.0kg (相当于原来的 15/5)
					constexpr float kMinMass = 45.0f;        // 【关键保护】单车不能无限变轻，设置一个 15kg 的保底重量

					// 线性计算新重量，并限制不低于保底重量
					float newMass = kInitialMass - (collected * kMassDropPerItem);
					newMass = std::max(newMass, kMinMass);

					// 更新 Jolt 物理引擎
					JPH::BodyID bid(bikeBodyID_raw);
					JPH::BodyLockWrite lock(m_physics->GetJoltSystem()->GetBodyLockInterface(), bid);
					if (lock.Succeeded()) {
						lock.GetBody().GetMotionProperties()->SetInverseMass(1.0f / newMass);
						printf("[Collect] Bike mass -> %.1f kg (%d/%d)\n",
							newMass, collected, mState->totalCollectibles);
					}
				}
				});

			// === 事件订阅：全部收集完毕 ===
			m_event->Subscribe(EventType::AllItemsCollected, [this](Event& /*e*/) {
				mState->allCollected = true;
				Log(std::format("[Collection] ALL {} ITEMS COLLECTED!\n",
					mState->totalCollectibles));
				Toast("All gas tanks collected! Bike fully lightened!");
			});
		}
		// ========================================================= 

		m_input->MapKeyboardAction("Horn", GLFW_KEY_F);

		// =========================================================
		// Pickup: Jump unlock — model: spring.glb
		// Placed to the right of the bike spawn point
		// =========================================================
		{//-127,25 -72
			
			
			constexpr glm::vec3 kSpringPickupPos = glm::vec3(413.0f, 56.0f, 458.0f);
			const glm::mat4 kSpringTransform =
				glm::translate(glm::mat4(1.0f), kSpringPickupPos) *
				glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(0.1f / 3.0f));

			// Load GLB to register mesh/material assets with the renderer
			flecs::entity springAsset = m_scene->LoadModel(
				m_render, "Assets/Models/spring.glb",
				engine::ModelPhysicsType::Static, 0.0f, kSpringTransform);

			// Get base mesh/mat indices from the first entity, then hide & strip physics
			// from ALL entities this load created (one per GLB mesh node)
			uint32_t springMeshIdx = 0, springMatIdx = 0;
			if (springAsset.is_valid()) {
				if (springAsset.has<MeshComponent>())     springMeshIdx = springAsset.get<MeshComponent>().meshIndex;
				if (springAsset.has<MaterialComponent>()) springMatIdx  = springAsset.get<MaterialComponent>().materialIndex;
				m_scene->get_world().query<const MeshComponent>()
					.each([&](flecs::entity e, const MeshComponent& mc) {
						if (mc.meshIndex < springMeshIdx) return;
						e.set<EntityStatus>({ false, false });
						if (e.has<PhysicsBody>()) {
							JPH::BodyID bid(e.get<PhysicsBody>().bodyID);
							JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
							if (bi.IsAdded(bid)) { bi.RemoveBody(bid); bi.DestroyBody(bid); }
							e.remove<PhysicsBody>();
						}
					});
			}
			// Create a single physics-free visual entity (physicsBodyID = ~0u → no body)
			m_springPickupEntity = m_scene->create_dynamic_entity(
				"SpringPickup", springMeshIdx, springMatIdx, kSpringTransform);

			size_t jumpPickupTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kSpringPickupPos,
				/*radius=*/2.5f,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(0.2f, 0.8f, 1.0f),
				/*isVisible=*/false,
				/*oneShot=*/true
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(jumpPickupTrigger,
				[this]() {
					mState->jumpEnabled = true;
					if (m_springPickupEntity.is_valid() && m_bikeEntity.is_valid()) {
						// Mount spring to bottom-center of bike frame, flipped upside down
						glm::mat4 mountT =
							glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.35f, 0.4f)) *
							glm::scale(glm::mat4(1.0f), glm::vec3(0.1f / 3.0f));
						m_springBaseMountT = mountT;
						m_springPickupEntity.child_of(m_bikeEntity);
						m_springPickupEntity.set<LocalTransform>({ mountT });
						m_springMountedEntity = m_springPickupEntity; // keep ref for jump animation
						m_springPickupEntity = {};                    // stop spinning
					} else if (m_springPickupEntity.is_valid()) {
						m_springPickupEntity.set<EntityStatus>({ false, false });
						m_springPickupEntity = {};
					}
					m_audio->PlayOneShot("AllCollectd");
					Log("[Pickup] Jump ability unlocked!\n");
					Toast("Jump unlocked! Press Space to jump.");
				},
				nullptr
			);
		}

		// =========================================================
		// Pickup: Horn unlock — model: air_horn.glb
		// Placed to the left of the bike spawn point
		// =========================================================
		{
			constexpr glm::vec3 kHornPickupPos = glm::vec3(-127.0f, 25.0f, -72.0f);
			const glm::mat4 kHornTransform =
				glm::translate(glm::mat4(1.0f), kHornPickupPos) *
				glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *  // correct upside-down orientation
				glm::rotate(glm::mat4(1.0f), glm::radians(30.0f),  glm::vec3(1.0f, 0.0f, 0.0f)) *  // cosmetic tilt
				glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / 3.0f));

			// Load GLB to register mesh/material assets with the renderer
			flecs::entity hornAsset = m_scene->LoadModel(
				m_render, "Assets/Models/air_horn.glb",
				engine::ModelPhysicsType::Static, 0.0f, kHornTransform);

			// Get base mesh/mat indices from the first entity, then hide & strip physics
			// from ALL entities this load created (one per GLB mesh node)
			uint32_t hornMeshIdx = 0, hornMatIdx = 0;
			if (hornAsset.is_valid()) {
				if (hornAsset.has<MeshComponent>())     hornMeshIdx = hornAsset.get<MeshComponent>().meshIndex;
				if (hornAsset.has<MaterialComponent>()) hornMatIdx  = hornAsset.get<MaterialComponent>().materialIndex;
				m_scene->get_world().query<const MeshComponent>()
					.each([&](flecs::entity e, const MeshComponent& mc) {
						if (mc.meshIndex < hornMeshIdx) return;
						e.set<EntityStatus>({ false, false });
						if (e.has<PhysicsBody>()) {
							JPH::BodyID bid(e.get<PhysicsBody>().bodyID);
							JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
							if (bi.IsAdded(bid)) { bi.RemoveBody(bid); bi.DestroyBody(bid); }
							e.remove<PhysicsBody>();
						}
					});
			}
			// Create a single physics-free visual entity (physicsBodyID = ~0u → no body)
			m_hornPickupEntity = m_scene->create_dynamic_entity(
				"HornPickup", hornMeshIdx, hornMatIdx, kHornTransform);

			size_t hornPickupTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kHornPickupPos,
				/*radius=*/2.5f,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(1.0f, 0.9f, 0.1f),
				/*isVisible=*/false,
				/*oneShot=*/true
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(hornPickupTrigger,
				[this]() {
					mState->hornEnabled = true;
					if (m_hornPickupEntity.is_valid() && m_bikeEntity.is_valid()) {
						// Mount horn above right handlebar, no tilt (keep 180° X flip for model orientation)
						m_hornBaseMountT =
							glm::translate(glm::mat4(1.0f), glm::vec3(-0.38f, 1.75f, 1.30f)) *
							glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
							glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),  glm::vec3(1.0f, 0.0f, 0.0f)) *
							glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / 3.0f));
						m_hornPickupEntity.child_of(m_bikeEntity);
						m_hornPickupEntity.set<LocalTransform>({ m_hornBaseMountT });
						m_hornMountedEntity = m_hornPickupEntity; // keep ref for squeeze animation
						m_hornPickupEntity = {};                  // stop spinning
					} else if (m_hornPickupEntity.is_valid()) {
						m_hornPickupEntity.set<EntityStatus>({ false, false });
						m_hornPickupEntity = {};
					}
					m_audio->PlayOneShot("AllCollectd");
					Log("[Pickup] Horn ability unlocked!\n");
					Toast("Horn unlocked! Press F to honk.");
				},
				nullptr
			);
		}

		// =========================================================
		// Pickup: Radio — model placed at (-18.399, 1, -100.533).
		// Touch the trigger once to unlock background music.
		// All .mp3 files in Assets/Sounds/RadioMusic/ are loaded
		// automatically. Songs cycle with the N key.
		// =========================================================
		{
			namespace fs = std::filesystem;

			const glm::vec3 kRadioPickupPos = glm::vec3(-133.11f, 1.0f, -95.13f);
			const glm::mat4 kRadioTransform = glm::translate(glm::mat4(1.0f), kRadioPickupPos)
				* glm::scale(glm::mat4(1.0f), glm::vec3(3.0f));

			// Load radio.glb as a static model — EntityStatus{true,true} auto-visible
			m_radioPickupEntity = m_scene->LoadModel(
				m_render, "Assets/Models/radio.glb",
				engine::ModelPhysicsType::Static, 0.0f, kRadioTransform);

			// Collect ALL mesh nodes of the radio (base meshIdx and above) so the
			// entire model disappears on pickup, not just the first node.
			if (m_radioPickupEntity.is_valid() && m_radioPickupEntity.has<MeshComponent>()) {
				uint32_t radioBaseMesh = m_radioPickupEntity.get<MeshComponent>().meshIndex;
				m_scene->get_world().query<const MeshComponent>()
					.each([&](flecs::entity e, const MeshComponent& mc) {
						if (mc.meshIndex >= radioBaseMesh)
							m_radioPickupEntities.push_back(e);
					});
			}

			// Scan RadioMusic/ and load every .mp3 found (sorted by filename)
			const fs::path radioDir = "Assets/Sounds/RadioMusic";
			std::vector<fs::path> songFiles;
			if (fs::exists(radioDir) && fs::is_directory(radioDir)) {
				for (const auto& entry : fs::directory_iterator(radioDir)) {
					const auto& p = entry.path();
					if (p.extension() == ".mp3" || p.extension() == ".MP3")
						songFiles.push_back(p);
				}
				std::sort(songFiles.begin(), songFiles.end());
			}
			for (int i = 0; i < static_cast<int>(songFiles.size()); ++i) {
				std::string soundName = std::format("RadioSong{}", i);
				std::string filePath  = songFiles[i].generic_string();
				std::string label     = songFiles[i].stem().string();
				m_audio->LoadSound(soundName, filePath);
				m_audio->SetVolume(soundName, 0.35f);
				m_radioSongs.push_back(std::move(soundName));
				m_radioLabels.push_back(std::move(label));
				printf("[Radio] Loaded song %d: %s\n", i, songFiles[i].filename().string().c_str());
			}
			if (m_radioSongs.empty())
				printf("[Radio] WARNING: No .mp3 files found in %s\n", radioDir.string().c_str());

			// oneShot=true: fires once on first contact, then permanently disabled
			size_t radioTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kRadioPickupPos,
				/*radius=*/2.5f,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(0.0f, 1.0f, 0.5f),
				/*isVisible=*/false,
				/*oneShot=*/true
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(radioTrigger,
				[this]() {
					for (auto& re : m_radioPickupEntities)
						if (re.is_valid()) re.set<EntityStatus>({ false, false });
					if (m_radioSongs.empty()) return;
					// Step 1: play pickup jingle immediately
					m_audio->LoadSound("RadioOn", "Assets/Sounds/radio_on.mp3");
					m_audio->SetVolume("RadioOn", 0.5f);
					m_audio->PlayOneShot("RadioOn");
					// Step 2 & 3 handled by timers in Update:
					//   0.5s → boardcast.mp3
					//   boardcast duration → bg music loop starts
					m_radioBroadcastDelay = 0.5f;

				// Mount satellite to character's back (bike-local space)
				if (m_satelliteEntity.is_valid() && m_bikeEntity.is_valid()) {
					const glm::mat4 satMountT =
						glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -0.5f)) *
						glm::scale(glm::mat4(1.0f), glm::vec3(0.009f));
					m_satelliteEntity.child_of(m_bikeEntity);
					m_satelliteEntity.set<LocalTransform>({ satMountT });
					m_satelliteEntity.set<EntityStatus>({ true, true });
				}
				},
				nullptr
			);

			// N key cycles songs
			m_input->MapKeyboardAction("NextSong", GLFW_KEY_N);
		}

		// =========================================================
		// Newspaper pickup — near radio; shows UFO.png for 5 s on trigger
		// =========================================================
		{
			constexpr glm::vec3 kNewsPickupPos = glm::vec3(-142.0f, 2.0f, -95.13f); // 收音机右边
			const glm::mat4 kNewsTransform =
				glm::translate(glm::mat4(1.0f), kNewsPickupPos) *
				glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));

			// Load GLB to register mesh/material assets, then hide all created nodes
			flecs::entity newsAsset = m_scene->LoadModel(
				m_render, "Assets/Models/newspapaer.glb",
				engine::ModelPhysicsType::Static, 0.0f, kNewsTransform);

			uint32_t newsMeshIdx = 0, newsMatIdx = 0;
			if (newsAsset.is_valid()) {
				if (newsAsset.has<MeshComponent>())     newsMeshIdx = newsAsset.get<MeshComponent>().meshIndex;
				if (newsAsset.has<MaterialComponent>()) newsMatIdx  = newsAsset.get<MaterialComponent>().materialIndex;
				m_scene->get_world().query<const MeshComponent>()
					.each([&](flecs::entity e, const MeshComponent& mc) {
						if (mc.meshIndex < newsMeshIdx) return;
						e.set<EntityStatus>({ false, false });
						if (e.has<PhysicsBody>()) {
							JPH::BodyID bid(e.get<PhysicsBody>().bodyID);
							JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
							if (bi.IsAdded(bid)) { bi.RemoveBody(bid); bi.DestroyBody(bid); }
							e.remove<PhysicsBody>();
						}
					});
			}
			// Single physics-free visual entity for the spinning pickup
			m_newspaperEntity = m_scene->create_dynamic_entity(
				"NewspaperPickup", newsMeshIdx, newsMatIdx, kNewsTransform);

			size_t newsTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kNewsPickupPos,
				/*radius=*/2.5f,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(1.0f, 1.0f, 0.5f),
				/*isVisible=*/false,
				/*oneShot=*/true
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(newsTrigger,
				[this]() {
					// Play flip sound
					m_audio->LoadSound("Flip", "Assets/Sounds/flip.mp3");
					m_audio->PlayOneShot("Flip");
					// Hide newspaper model
					if (m_newspaperEntity.is_valid())
						m_newspaperEntity.set<EntityStatus>({ false, false });
					// Show UFO fullscreen image; timer will dismiss it after 5s
					AddWidget("Assets/ui/UFONews.ui.json");
					m_ufoDisplayTimer = 5.0f;
					Log("[Newspaper] UFO news! Showing for 5s.\n");
				},
				nullptr
			);
		}

		// =========================================================
		// Satellite — pre-loaded hidden; mounted to character's back on radio pickup
		// =========================================================
		{
			flecs::entity satAsset = m_scene->LoadModel(
				m_render, "Assets/Models/satellite.glb",
				engine::ModelPhysicsType::Static, 0.0f, glm::mat4(1.0f));

			uint32_t satMeshIdx = 0, satMatIdx = 0;
			if (satAsset.is_valid()) {
				if (satAsset.has<MeshComponent>())     satMeshIdx = satAsset.get<MeshComponent>().meshIndex;
				if (satAsset.has<MaterialComponent>()) satMatIdx  = satAsset.get<MaterialComponent>().materialIndex;
				m_scene->get_world().query<const MeshComponent>()
					.each([&](flecs::entity e, const MeshComponent& mc) {
						if (mc.meshIndex < satMeshIdx) return;
						e.set<EntityStatus>({ false, false });
						if (e.has<PhysicsBody>()) {
							JPH::BodyID bid(e.get<PhysicsBody>().bodyID);
							JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
							if (bi.IsAdded(bid)) { bi.RemoveBody(bid); bi.DestroyBody(bid); }
							e.remove<PhysicsBody>();
						}
					});
			}
			m_satelliteEntity = m_scene->create_dynamic_entity(
				"SatelliteMount", satMeshIdx, satMatIdx, glm::mat4(1.0f));
			m_satelliteEntity.set<EntityStatus>({ false, false }); // hidden until radio picked up
		}

		// =========================================================
		// Rocket — hidden at start, mounted to rear frame on first gas tank collection
		// =========================================================
		{
			//flecs::entity rocketAsset = m_scene->LoadModel(
			//	m_render, "Assets/Models/rocket.glb",
			//	engine::ModelPhysicsType::Static, 0.0f,
			//	glm::mat4(1.0f));

			//uint32_t rocketMeshIdx = 0, rocketMatIdx = 0;
			//if (rocketAsset.is_valid()) {
			//	if (rocketAsset.has<MeshComponent>())     rocketMeshIdx = rocketAsset.get<MeshComponent>().meshIndex;
			//	if (rocketAsset.has<MaterialComponent>()) rocketMatIdx  = rocketAsset.get<MaterialComponent>().materialIndex;
			//	m_scene->get_world().query<const MeshComponent>()
			//		.each([&](flecs::entity e, const MeshComponent& mc) {
			//			if (mc.meshIndex < rocketMeshIdx) return;
			//			e.set<EntityStatus>({ false, false });
			//			if (e.has<PhysicsBody>()) {
			//				JPH::BodyID bid(e.get<PhysicsBody>().bodyID);
			//				JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
			//				if (bi.IsAdded(bid)) { bi.RemoveBody(bid); bi.DestroyBody(bid); }
			//				e.remove<PhysicsBody>();
			//			}
			//		});

			//	m_rocketEntity = m_scene->create_dynamic_entity(
			//		"RocketMount", rocketMeshIdx, rocketMatIdx, glm::mat4(1.0f));
			//	m_rocketEntity.set<EntityStatus>({ false, false }); // hidden until first gas tank
			//}
		}


		{
			const glm::vec3 kCheckpointPos   = glm::vec3(374.91f, 77.54f, 296.48f);
			const float     kCheckpointRadius = 19.80f;

			size_t checkpointTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kCheckpointPos,
				kCheckpointRadius,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(0.0f, 1.0f, 0.0f),
				/*isVisible=*/false,
				/*oneShot=*/false
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(checkpointTrigger,
				[this, kCheckpointPos]() {
					m_checkpointPos = kCheckpointPos;
					m_checkpointYaw = 0.0f;
					m_hasCheckpoint = true;
					printf("[Checkpoint] Respawn point updated to (%.2f, %.2f, %.2f)\n",
						kCheckpointPos.x, kCheckpointPos.y, kCheckpointPos.z);
				},
				[]() {}
			);
		}


		{
			const glm::vec3 kClearCheckpointPos    = glm::vec3(402.77f, -33.045f, 278.87f);
			const float     kClearCheckpointRadius = 10.0f;

			size_t clearCheckpointTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kClearCheckpointPos,
				kClearCheckpointRadius,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(1.0f, 1.0f, 0.0f),
				/*isVisible=*/false,
				/*oneShot=*/false
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(clearCheckpointTrigger,
				[this]() {
					m_hasCheckpoint = false;
					printf("[Checkpoint] Respawn reset to in-place\n");
				},
				[]() {}
			);
		}


		{
			const glm::vec3 kIblOffPos    = glm::vec3(407.691f, 65.906f, 280.383f);
			const float     kIblOffRadius = 10.0f;

			size_t iblOffTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kIblOffPos,
				kIblOffRadius,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(1.0f, 0.5f, 0.0f),
				/*isVisible=*/false,
				/*oneShot=*/false
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(iblOffTrigger,
				[this]() {
					if (mState) {
						mState->iblEnabled = false;
						printf("[Trigger] IBL disabled\n");
					}
				},
				[]() {}
			);
		}


		{
			const glm::vec3 kIblOnPos    = glm::vec3(-178.163f, 9.492f, -77.001f);
			const float     kIblOnRadius = 4.97f;

			size_t iblOnTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kIblOnPos,
				kIblOnRadius,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(0.0f, 0.5f, 1.0f),
				/*isVisible=*/false,
				/*oneShot=*/false
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(iblOnTrigger,
				[this]() {
					if (mState) {
						mState->iblEnabled = true;
						printf("[Trigger] IBL enabled\n");
					}
				},
				[]() {}
			);
		}

		// =========================================================
		// Rocket launch trigger at (232.94, 86.11, -222.40)
		// No separate pickup model — avoids GLTF node name collision with rocket2.
		// Trigger is invisible; when entered, rocket2 spins and ascends.
		// =========================================================
		{
			constexpr glm::vec3 kRocketTriggerPos = glm::vec3(226.0f, 87.0f, -25.0f);

			size_t rocketLaunchTrigger = m_render->GetTriggerSystem().AddSphereTrigger(
				kRocketTriggerPos,
				/*radius=*/5.0f,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(1.0f, 0.4f, 0.0f),
				/*isVisible=*/false,
				/*oneShot=*/true
			);
			m_render->GetTriggerSystem().SetTriggerCallbacks(rocketLaunchTrigger,
				[this]() {
					if (!m_rocket2Entities.empty()) {
						m_rocket2Launching   = true;
						m_rocket2LaunchTimer = 0.0f;
						// Show flame particles when launch begins
						if (m_rocketFlameParticleIndex != static_cast<size_t>(-1)) {
							auto& particles = m_render->GetParticles();
							if (m_rocketFlameParticleIndex < particles.size())
								particles[m_rocketFlameParticleIndex]->config.isVisible = true;
						}
						printf("[Trigger] Rocket2 launch initiated! (%zu parts)\n", m_rocket2Entities.size());
					}
					// Hide bike and character
					if (m_bikeEntity.is_valid())
						m_bikeEntity.set<EntityStatus>({ false, false });
					m_scene->get_world().query<SkinComponent>()
						.each([](flecs::entity e, SkinComponent&) {
							e.set<EntityStatus>({ false, false });
						});
					// Switch to rocket-follow camera
					m_rocketCameraActive = true;
					if (mState) mState->thirdPersonMode = false;

					// 停止所有正在播放的声音
					if (m_bgMusicPlaying && !m_radioSongs.empty())
						m_audio->Stop(m_radioSongs[m_currentSongIndex]);
					m_audio->Stop("Broadcast");
					m_bgMusicPlaying = false;
					m_radioBgMusicDelay   = -1.0f;
					m_radioBroadcastDelay = -1.0f;
					m_audio->LoadSound("RocketLaunch", "Assets/Sounds/rocket_launch.mp3");
					m_audio->PlayOneShot("RocketLaunch");
					Log("[Trigger] Rocket launched!\n");
					Toast("Rocket launched!");
				},
				nullptr
			);

			// Rocket flame particles — hidden until launch, emitting downward (-Y)
			m_render->AddParticleGroup(13000);
			auto& flameParticles = m_render->GetParticles();
			m_rocketFlameParticleIndex = flameParticles.size() - 1;
			auto& flameCfg = flameParticles[m_rocketFlameParticleIndex]->config;
			flameCfg.textureDescriptor = m_render->GetParticleTextureDescriptor(cfg::RocketFlameTexture);
			flameCfg.useTexture = 1;
			flameCfg.animateAtlas = true;
			flameCfg.atlasCols = 8;
			flameCfg.atlasRows = 8;
			flameCfg.isVisible = true;
			flameCfg.emitterPos = m_rocket2Center + glm::vec3(0.0f, 23.0f, 0.0f);
			flameCfg.emitDir = glm::vec3(0.0f, -1.0f, 0.0f); // 向下喷射
			flameCfg.gravity = glm::vec3(0.0f, -150.0f, 0.0f);
			flameCfg.speedMin = 15.581f;
			flameCfg.speedMax = 19.4f;
			flameCfg.lifeMin = 1.084f;
			flameCfg.lifeMax = 2.f;
			flameCfg.sizeMin = 923.0f;
			flameCfg.sizeMax = 1000.0f;
			flameCfg.coneSpread = 80.27f;
			flameCfg.startColor = glm::vec4(1.0f, 0.0f, 0.0f, 250.0f / 255.0f);
			flameCfg.endColor = glm::vec4(1.0f, 60.0f / 255.0f, 0.0f, 53.0f / 255.0f);
			flameCfg.startSizeScale = 6.0f;
			flameCfg.endSizeScale = 10.0f;
			flameCfg.sphereRadius = 2.2f;
			flameParticles[m_rocketFlameParticleIndex]->setEmitterShape(EmitterShape::Cone);
		}
	}

	void level::Update(float dt) {

		const float respawnStillnessDelay = 0.25f;
		bool canRespawnNow = false;

		if (mState && !mState->isAlive) {
			flecs::entity bikeEntity = m_scene ? m_scene->find_entity("Bike_0") : flecs::entity();
			if (bikeEntity.is_valid()) {
				uint32_t bikeBodyID = JPH::BodyID::cInvalidBodyID;
				if (bikeEntity.has<PhysicsBody>()) bikeBodyID = bikeEntity.get<PhysicsBody>().bodyID;
				else if (bikeEntity.has<CompoundParent>()) bikeBodyID = bikeEntity.get<CompoundParent>().bodyID;

				if (bikeBodyID != JPH::BodyID::cInvalidBodyID && m_physics) {
					JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
					JPH::BodyID id(bikeBodyID);

					if (!bi.IsActive(id)) {
						m_respawnStillnessTime += dt;
						canRespawnNow = m_respawnStillnessTime >= respawnStillnessDelay;
					}
					else {
						m_respawnStillnessTime = 0.0f;
					}
				}
				else {
					m_respawnStillnessTime = 0.0f;
				}
			}
			else {
				m_respawnStillnessTime = 0.0f;
			}
		}
		else {
			m_respawnStillnessTime = 0.0f;
		}

		if (mState) {
			if (!mState->isAlive && canRespawnNow) {
				if (!m_respawnPromptVisible) {
					m_respawnPromptVisible = AddWidget(kRespawnPromptUiPath);
				}
			}
			else if (m_respawnPromptVisible) {
				RemoveWidget(kRespawnPromptUiPath);
				m_respawnPromptVisible = false;
			}
		}

		if (m_allCollectSoundDelay >= 0.0f) {
			m_allCollectSoundDelay -= dt;
			if (m_allCollectSoundDelay < 0.0f) {
				m_audio->PlayOneShot("AllCollectd");
			}
		}

		// Stage 1: 0.5s after radio pickup → play boardcast.mp3
		if (m_radioBroadcastDelay >= 0.0f) {
			m_radioBroadcastDelay -= dt;
			if (m_radioBroadcastDelay < 0.0f) {
				m_audio->LoadSound("Broadcast", "Assets/Sounds/boardcast.mp3");
				m_audio->PlayOneShot("Broadcast");
				// Adjust kBroadcastDuration to match boardcast.mp3 actual length (seconds)
				constexpr float kBroadcastDuration = 10.0f;
				m_radioBgMusicDelay = kBroadcastDuration;
			}
		}

		// UFO image: dismiss after 5 s
		if (m_ufoDisplayTimer >= 0.0f) {
			m_ufoDisplayTimer -= dt;
			if (m_ufoDisplayTimer < 0.0f)
				RemoveWidget("Assets/ui/UFONews.ui.json");
		}

		// Stage 2: after boardcast finishes → enable bg music + N-key switching
		if (m_radioBgMusicDelay >= 0.0f) {
			m_radioBgMusicDelay -= dt;
			if (m_radioBgMusicDelay < 0.0f && !m_radioSongs.empty()) {
				m_bgMusicPlaying   = true;
				m_currentSongIndex = 0;
				m_audio->PlayLoop(m_radioSongs[0]);
				Log(std::format("[Radio] Now playing: {}\n", m_radioLabels[0]));
				Toast(std::format("Radio! Now playing: {}. Press N to switch.", m_radioLabels[0]));
			}
		}

		// Spin pickup items around world Y axis through their own position
		{
			constexpr float kSpinSpeed = 3.0f; // radians per second
			const glm::mat4 kWorldYRot = glm::rotate(glm::mat4(1.0f), kSpinSpeed * dt, glm::vec3(0.0f, 1.0f, 0.0f));
			auto spinPickup = [&](flecs::entity e) {
				if (!e.is_valid()) return;
				if (e.has<EntityStatus>() && !e.get<EntityStatus>().should_render) return;
				auto* lt = &e.get_mut<LocalTransform>();
				// Rotate around world Y through the object's own position:
				// T(p) * R * T(-p) * M
				glm::vec3 pos(lt->matrix[3]);
				glm::mat4 T    = glm::translate(glm::mat4(1.0f),  pos);
				glm::mat4 Tinv = glm::translate(glm::mat4(1.0f), -pos);
				lt->matrix = T * kWorldYRot * Tinv * lt->matrix;
				e.modified<LocalTransform>();
			};
			spinPickup(m_springPickupEntity);
			spinPickup(m_hornPickupEntity);
			for (auto& ge : m_gasPickupEntities) spinPickup(ge);
		}
		m_audio->LoadSound("NextSong", "Assets/Sounds/Button.mp3");
		m_audio->SetVolume("NextSong", 0.7f);
		// Radio: press N to play the next song in RadioMusic/ (only after radio is picked up)
		if (m_bgMusicPlaying && !m_radioSongs.empty() && m_input && m_input->IsActionPressed("NextSong")) {
			m_audio->Stop(m_radioSongs[m_currentSongIndex]);
			m_currentSongIndex = (m_currentSongIndex + 1) % static_cast<int>(m_radioSongs.size());
			m_audio->PlayLoop(m_radioSongs[m_currentSongIndex]);
			Log(std::format("[Radio] Switched to: {}\n", m_radioLabels[m_currentSongIndex]));
			Toast(std::format("Now playing: {}", m_radioLabels[m_currentSongIndex]));
			m_audio->PlayOneShot("NextSong");
		}

		if (mState->hornEnabled && m_input && m_audio && m_input->IsActionPressed("Horn")) {
			m_audio->LoadSound("Horn", "Assets/Sounds/bicycle_horn.mp3");
			m_audio->SetVolume("Horn", 0.7f);
			m_audio->PlayOneShot("Horn");
			m_hornAnimTimer = 0.0f; // trigger squeeze animation
		}

		// Spring squeeze animation on jump (Space)
		if (mState->jumpEnabled && m_input && m_input->IsActionPressed("Jump"))
			m_springAnimTimer = 0.0f;

		// Horn squeeze animation: scale up then back to original
		if (m_hornAnimTimer >= 0.0f && m_hornMountedEntity.is_valid()) {
			m_hornAnimTimer += dt;
			constexpr float kGrowTime   = 0.12f;
			constexpr float kShrinkTime = 0.18f;
			constexpr float kPeakScale  = 1.6f;
			float sf = 1.0f;
			if (m_hornAnimTimer < kGrowTime) {
				sf = 1.0f + (kPeakScale - 1.0f) * (m_hornAnimTimer / kGrowTime);
			} else if (m_hornAnimTimer < kGrowTime + kShrinkTime) {
				float t = (m_hornAnimTimer - kGrowTime) / kShrinkTime;
				sf = kPeakScale - (kPeakScale - 1.0f) * t;
			} else {
				sf = 1.0f;
				m_hornAnimTimer = -1.0f;
			}
			m_hornMountedEntity.set<LocalTransform>({ m_hornBaseMountT * glm::scale(glm::mat4(1.0f), glm::vec3(sf)) });
		}

		// Spring animation: elongate downward (Y-only scale, top anchored)
		if (m_springAnimTimer >= 0.0f && m_springMountedEntity.is_valid()) {
			m_springAnimTimer += dt;
			constexpr float kGrowTime   = 0.10f;
			constexpr float kShrinkTime = 0.20f;
			constexpr float kPeakScale  = 2.0f;
			float sf = 1.0f;
			if (m_springAnimTimer < kGrowTime) {
				sf = 1.0f + (kPeakScale - 1.0f) * (m_springAnimTimer / kGrowTime);
			} else if (m_springAnimTimer < kGrowTime + kShrinkTime) {
				float t = (m_springAnimTimer - kGrowTime) / kShrinkTime;
				sf = kPeakScale - (kPeakScale - 1.0f) * t;
			} else {
				sf = 1.0f;
				m_springAnimTimer = -1.0f;
			}
			// Fixed-top, extend downward:
			//   center_y = mountY - (sf-1)*renderedHalf  → top stays constant, bottom drops
			constexpr float kSBaseScale       = 0.1f / 3.0f;
			constexpr float kSpringRenderHalf = 0.25f; // must exceed upward Y-scale extension to net -Y
			float centerY = -0.35f - (sf - 1.0f) * kSpringRenderHalf;
			glm::mat4 animT =
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, centerY, 0.4f)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(kSBaseScale, kSBaseScale * sf, kSBaseScale));
			m_springMountedEntity.set<LocalTransform>({ animT });
		}

		// Rocket camera
		if (m_rocketCameraActive && mState) {
			constexpr float kFixedCamDuration = 10.0f;
			glm::vec3 lookTarget = glm::vec3(m_rocket2Center.x, m_rocket2Center.y + 200.0f, m_rocket2Center.z);
			glm::vec3 camPos;
			glm::vec3 up;
			if (m_rocket2LaunchTimer < kFixedCamDuration) {
				// 前 10 秒：固定机位看向火箭中心 y+200
				camPos = glm::vec3(527.35f, 98.08f, 205.77f);
				up     = glm::vec3(0.0f, 1.0f, 0.0f);
			} else {
				// 10 秒后：跟随火箭尾部俯视
				camPos = m_rocket2Center + glm::vec3(0.0f, -5.0f, 0.0f);
				lookTarget = camPos + glm::vec3(0.0f, -1.0f, 0.0f);
				up     = glm::vec3(0.0f, 0.0f, -1.0f);
			}
			mState->camera2world = glm::inverse(glm::lookAt(camPos, lookTarget, up));
		}

		// Satellite spin — rotate around Y axis while mounted
		if (m_satelliteEntity.is_valid() && m_satelliteEntity.has<EntityStatus>()
			&& m_satelliteEntity.get<EntityStatus>().should_render) {
			constexpr float kSatSpinSpeed = 2.0f; // radians per second
			auto* lt = &m_satelliteEntity.get_mut<LocalTransform>();
			const glm::vec3 pos = glm::vec3(lt->matrix[3]);
			const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), kSatSpinSpeed * dt, glm::vec3(0.0f, 1.0f, 0.0f));
			lt->matrix = glm::translate(glm::mat4(1.0f), pos) * rot * glm::translate(glm::mat4(1.0f), -pos) * lt->matrix;
			m_satelliteEntity.modified<LocalTransform>();
		}

		// Rocket2 launch animation: ALL parts spin around shared pivot + ascend upward
		if (m_rocket2Launching && !m_rocket2Entities.empty()) {
			m_rocket2LaunchTimer += dt;
			constexpr float kSpinSpeed   = 4.0f;   // radians/sec
			constexpr float kAscentSpeed = 50.0f;   // units/sec at full thrust
			constexpr float kRampTime    = 2.5f;   // ramp-up duration in seconds
			constexpr float kHideAfter   = 60.0f;  // hide after this many seconds

			float ramp   = glm::min(m_rocket2LaunchTimer / kRampTime, 1.0f);
			float ascent = kAscentSpeed * ramp * dt;

			// Shared pivot: the rocket's current centre (moves up each frame)
			m_rocket2Center.y += ascent;
			glm::mat4 T    = glm::translate(glm::mat4(1.0f),  m_rocket2Center);
			glm::mat4 Tinv = glm::translate(glm::mat4(1.0f), -m_rocket2Center);
			glm::mat4 rot  = glm::rotate(glm::mat4(1.0f), kSpinSpeed * dt, glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 up   = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, ascent, 0.0f));
			glm::mat4 step = up * T * rot * Tinv; // rotate-around-pivot then ascend

			// Update flame emitter position to follow the rocket's tail
			if (m_rocketFlameParticleIndex != static_cast<size_t>(-1)) {
				auto& particles = m_render->GetParticles();
				if (m_rocketFlameParticleIndex < particles.size())
					particles[m_rocketFlameParticleIndex]->config.emitterPos =
						m_rocket2Center + glm::vec3(0.0f, 150.0f, 0.0f);
			}

			bool hide = (m_rocket2LaunchTimer > kHideAfter);
			for (auto& e : m_rocket2Entities) {
				if (!e.is_valid()) continue;
				if (hide) {
					e.set<EntityStatus>({ false, false });
					// Hide flame particles too
					if (m_rocketFlameParticleIndex != static_cast<size_t>(-1)) {
						auto& particles = m_render->GetParticles();
						if (m_rocketFlameParticleIndex < particles.size())
							particles[m_rocketFlameParticleIndex]->config.isVisible = false;
					}
				} else {
					auto* lt = &e.get_mut<LocalTransform>();
					lt->matrix = step * lt->matrix;
					e.modified<LocalTransform>();
				}
			}
		}

		if (m_input && m_input->IsActionPressed("DEPLOY")) {

			if (!mState->isAlive) {
				flecs::entity bikeEntity = m_scene->find_entity("Bike_0");
				if (bikeEntity.is_valid()) {
					uint32_t bikeBodyID = JPH::BodyID::cInvalidBodyID;
					if (bikeEntity.has<PhysicsBody>()) bikeBodyID = bikeEntity.get<PhysicsBody>().bodyID;
					else if (bikeEntity.has<CompoundParent>()) bikeBodyID = bikeEntity.get<CompoundParent>().bodyID;

					if (bikeBodyID != JPH::BodyID::cInvalidBodyID) {
						JPH::BodyInterface& bi = m_physics->GetJoltSystem()->GetBodyInterface();
						JPH::BodyID id(bikeBodyID);

						float linVelSq = bi.GetLinearVelocity(id).LengthSq();
						float angVelSq = bi.GetAngularVelocity(id).LengthSq();




						const float stopThresholdSq = 1.95f;

						// Checkpoint respawn: no speed requirement
						// In-place respawn: wait until bike has stopped
						if (m_hasCheckpoint || (linVelSq < stopThresholdSq && angVelSq < stopThresholdSq)) {

							JPH::RVec3 respawnPos;
							JPH::Quat  uprightRot;

							if (m_hasCheckpoint) {

								respawnPos = JPH::RVec3(m_checkpointPos.x, m_checkpointPos.y - 3.5f, m_checkpointPos.z);
								uprightRot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), m_checkpointYaw);
								printf("[Gameplay] Bike respawned at checkpoint (%.2f, %.2f, %.2f)\n",
									m_checkpointPos.x, m_checkpointPos.y, m_checkpointPos.z);
								mState->iblEnabled = true;
							}
							else {

								JPH::RVec3 currentPos = bi.GetPosition(id);

								JPH::Quat currentRot = bi.GetRotation(id);
								JPH::Vec3 fwd = currentRot.RotateAxisZ();
								JPH::Vec3 flatFwd(fwd.GetX(), 0.0f, fwd.GetZ());
								if (flatFwd.LengthSq() > 0.0001f) {
									flatFwd = flatFwd.Normalized();
								}
								else {
									flatFwd = JPH::Vec3(0.0f, 0.0f, -1.0f);
								}

								float backwardOffset = 0.3f;
								currentPos.SetX(currentPos.GetX() - flatFwd.GetX() * backwardOffset);
								currentPos.SetZ(currentPos.GetZ() - flatFwd.GetZ() * backwardOffset);
								currentPos.SetY(currentPos.GetY() + 1.0f);

								float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());
								uprightRot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), currentYaw + JPH::JPH_PI);
								respawnPos = currentPos;
								
							}

							bi.SetPositionAndRotation(id, respawnPos, uprightRot, JPH::EActivation::Activate);
							bi.SetLinearVelocity(id, JPH::Vec3::sZero());
							bi.SetAngularVelocity(id, JPH::Vec3::sZero());

							mState->isAlive = true;
							mState->deathTimer = 0.0f;
							mState->isGameOver = false;
							mState->bikeLeanAngle = 0.0f;
							mState->bikeSteerAngle = 0.0f;
							mState->thirdPersonMode = true;
							mState->bikeSpeed = 0.0f;
							mState->engineForce = 0.0f;
							mState->lastPedal = -1;

							RemoveWidget(kRespawnPromptUiPath);
							m_respawnPromptVisible = false;
							
							printf("[Gameplay] Bike stopped and respawned in place!\n");
						}
						else {
							// === 单车还在滚动，拒绝复活，可以考虑在这里触发个 UI 提示音 ===
							// printf("[Gameplay] Cannot respawn yet, bike is still tumbling...\n");
						}
						// -----------------------------------------------------
					}
				}
			}
		}
		// =========================================================
		// ���µ�������
		if (m_bikeController) {
			m_bikeController->Update(dt);
		}

		// ���Ʋ��� Debug ���� (ֻ��������ؿ�����Ҫ����Щ)
#ifndef NDEBUG
		m_render->mDebugRenderer.DrawBox(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		m_render->mDebugRenderer.DrawLine(glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(3.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		m_render->mDebugRenderer.DrawSphere(glm::vec3(-4.0f, 2.0f, 0.0f), 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		m_render->mDebugRenderer.DrawCapsule(glm::vec3(0.0f, 2.0f, 5.0f), 1.0f, 1.5f, glm::vec3(1.0f, 1.0f, 0.0f));
#endif
	}

	void level::Shutdown() {

		RemoveWidget(kRespawnPromptUiPath);
		m_respawnPromptVisible = false;
		m_respawnStillnessTime = 0.0f;
		
		m_bikeController.reset();
	}

} // namespace engine
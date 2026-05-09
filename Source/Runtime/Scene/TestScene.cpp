#include "TestScene.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Physics/bikeController.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../UserState/GameplayState.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include <format>

namespace engine {

	TestScene::TestScene() = default;
	TestScene::~TestScene() = default;
	void TestScene::Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics, InputSystem* input, EventSystem* eventSys, GameplayState* state, AnimationSystem* anima, AudioSystem* audio) {
		InitBase(render);
		m_render = render;
		m_scene = scene;
		m_physics = physics;
		m_input = input;
		m_event = eventSys;
		mState = state;
		m_anima = anima;
		m_audio = audio;

		// load the terrain and static models
		m_scene->LoadModel(m_render, "Assets/Models/TScene.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
		//m_scene->LoadModel(m_render, "Assets/Models/Level.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));
		//
		//m_scene->LoadModel(m_render, "Assets/Models/forest.glb", engine::ModelPhysicsType::Static, 0.0f, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)));

		glm::mat4 bridgeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 60.0f));
		glm::mat4 CplaneSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(120.0f, 0.0f, 250.0f));
		glm::mat4 darkRoomSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 0.0f, 200.0f));

		m_scene->LoadModel(m_render, "Assets/Models/testBridge.glb", engine::ModelPhysicsType::Static, 0.0f, bridgeSpawnPos);
		m_scene->LoadModel(m_render, "Assets/Models/testCurvePlane.glb", engine::ModelPhysicsType::Static, 0.0f, CplaneSpawnPos);
		////m_scene->LoadModel(m_render, "Assets/Models/sponza.glb", engine::ModelPhysicsType::Static, 0.0f, CplaneSpawnPos);

		m_scene->LoadModel(m_render, "Assets/Models/darkRoom.glb", engine::ModelPhysicsType::Static, 0.0f, darkRoomSpawnPos);

		// 2. �������г�
		glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, 10.0f, 30.0f));
		glm::mat4 tbpos = glm::translate(BikeSpawnPos, glm::vec3(0.0f, 0.0f, -8.0f));
		glm::mat4 bikeAnchorWorld = glm::mat4(0.0f); // sentinel: [3][3]==0 means anchor not found

        m_render->load_animated_model("Assets/Models/character.glb", tbpos);
        m_scene->LoadModel(m_render, "Assets/Models/tbikeWithAnchor.glb", engine::ModelPhysicsType::CustomC, 90.0f, tbpos);

		// 3. ��ʼ������������
		m_bikeController = std::make_unique<BikeController>(m_physics->GetJoltSystem(), m_input, mState);
		m_audio->LoadSound("Jump", "Assets/Sounds/jump_effect.mp3");
		m_bikeController->SetAudioSystem(m_audio);
		flecs::entity bikeEntity = m_scene->find_entity("Bike_0");
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
				// 150 km/h = 41.67 m/s -> fatal
				constexpr float kLightHitSpeed = 14.0f;  // raised: ignore more low-speed terrain contacts
				constexpr float kFatalSpeed = 41.67f;    // 150 km/h: require a very high-speed impact to die
				// normalAlignment: fraction of total bike speed directed into the surface
				//   ~1.0 = head-on (perpendicular), ~0.0 = pure side-scrape (tangential)
				constexpr float kScrapeThreshold = 0.90f;  // raised: small slopes filtered as scrapes
				constexpr float kFrontalThreshold = 0.90f; // raised: require near-head-on collision to be fatal

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
					m_audio->SetVolume("wasted", 1.2f);
					m_audio->PlayOneShot("wasted");
					mState->deathTimer = 0.0f;
					mState->thirdPersonMode = false;
					printf("[Collision] FATAL: speed=%.2f m/s align=%.2f | %s vs %s\n",
						impactSpeed, normalAlignment,
						col.GetEntityA().c_str(), col.GetEntityB().c_str());
				}
				});
		}
		

		glm::mat4 emissivecubeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(60.0f, 3.0f, 200.0f));
		flecs::entity emCubeEntity = m_scene->LoadModel(m_render, "Assets/DELETE_LATER/em1.gltf", engine::ModelPhysicsType::Dynamic, 0.01f, emissivecubeSpawnPos, engine::RenderLayer::Emissive);

		m_scene->create_light_entity("emCubeLight", engine::LightType::Point, glm::vec3(1.0f, 1.0f, 1.0f), 8.0f, glm::mat4(1.0f), 10.0f, glm::vec3(0, -1, 0), 0, 0, emCubeEntity);


		glm::mat4 localLightOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.7f, 1.7f));
		flecs::entity headlight = m_scene->create_light_entity("headlight", engine::LightType::Spot, glm::vec3(1.0f, 0.95f, 0.85f), 15.0f, localLightOffset, 40.0f, glm::vec3(0.0f, 0, 1.0f), 15.0f, 25.0f);
		if (bikeEntity.is_valid()) headlight.child_of(bikeEntity);

		glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
		glm::mat4 sunTransform = glm::mat4(1.0f); sunTransform[3] = glm::vec4(sunDir, 0.0f);
		m_scene->create_light_entity("MainSun", engine::LightType::Directional, glm::vec3(1.2f, 0.95f, 0.8f), 2.5f, sunTransform, 0);

		glm::mat4 light2SpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, 3.0f, 30.0f));
		m_scene->create_light_entity("voidLight", engine::LightType::Point, glm::vec3(0.5f, 0.0f, 3.0f), 8, light2SpawnPos, 20.0f);


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

		m_input->MapKeyboardAction("Horn", GLFW_KEY_F);

		static const glm::vec3 kCollectPositions[4] = {
			{ 40.0f,  0.0f,  35.0f }, 
			{ 80.0f,  0.0f,  55.0f }, 
			{ 70.0f,  0.0f, 150.0f },
			{115.0f,  0.0f, 250.0f },
		};

		auto collectedCount = std::make_shared<int>(0);
		constexpr int kTotalCollectibles = 4;

		for (int i = 0; i < kTotalCollectibles; ++i) {
			size_t tid = m_render->GetTriggerSystem().AddSphereTrigger(
				kCollectPositions[i],
				/*radius=*/5.0f,
				/*particleIndex=*/static_cast<size_t>(-1),
				/*color=*/glm::vec3(1.0f, 0.85f, 0.0f),
				/*isVisible=*/true,
				/*oneShot=*/true
			);

			m_render->GetTriggerSystem().SetTriggerCallbacks(tid,
				[this, i, collectedCount, kTotalCollectibles]() {
					int total = ++(*collectedCount);
					m_event->QueueEvent(std::make_unique<ItemCollectedEvent>(i, total));
					if (total >= kTotalCollectibles)
						m_event->QueueEvent(std::make_unique<AllItemsCollectedEvent>());
				},
				nullptr
			);
		}

		m_audio->LoadSound("Collect",    "Assets/Sounds/Collect.mp3");
		m_audio->SetVolume("Collect", 0.4f);
		m_audio->LoadSound("AllCollectd", "Assets/Sounds/AllCollectd.mp3");
		m_audio->SetVolume("AllCollectd", 0.8f);


		m_event->Subscribe(EventType::ItemCollected, [this](Event& e) {
			auto& col = static_cast<ItemCollectedEvent&>(e);
			mState->collectedItems = col.GetCurrentTotal();
			Log(std::format("[Collection] {}/{} collected\n",
				col.GetCurrentTotal(), mState->totalCollectibles));

			m_audio->PlayOneShot("Collect");
		});

		m_event->Subscribe(EventType::AllItemsCollected, [this](Event& e) {
			mState->allCollected = true;
			mState->bikeTuning.maxSpeed *= 2.0f;
			Log("[Collection] ALL ITEMS COLLECTED — max speed unlocked!\n");
			Toast("All collectibles found! MAX SPEED UNLOCKED!");
			m_allCollectSoundDelay = 0.8f; 
		});
	}

	void TestScene::Update(float dt) {



		if (m_allCollectSoundDelay >= 0.0f) {
			m_allCollectSoundDelay -= dt;
			if (m_allCollectSoundDelay < 0.0f) {
				m_audio->PlayOneShot("AllCollectd");
			}
		}

		if (m_input && m_audio && m_input->IsActionPressed("Horn")) {
			m_audio->LoadSound("Horn", "Assets/Sounds/bicycle_horn.mp3");
			m_audio->SetVolume("Horn", 0.2f);
			m_audio->PlayOneShot("Horn");
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

						const float stopThresholdSq = 0.25f;

						if (linVelSq < stopThresholdSq && angVelSq < stopThresholdSq) {

							JPH::RVec3 currentPos = bi.GetPosition(id);
							currentPos.SetY(currentPos.GetY() + 1.0f);

							JPH::Quat currentRot = bi.GetRotation(id);
							JPH::Vec3 fwd = currentRot.RotateAxisZ();
							float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());
							JPH::Quat uprightRot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), currentYaw + JPH::JPH_PI);

							bi.SetPositionAndRotation(id, currentPos, uprightRot, JPH::EActivation::Activate);
							bi.SetLinearVelocity(id, JPH::Vec3::sZero());
							bi.SetAngularVelocity(id, JPH::Vec3::sZero());

							mState->isAlive = true;
							mState->deathTimer = 0.0f;
							mState->isGameOver = false;
							mState->bikeLeanAngle = 0.0f;
							mState->bikeSteerAngle = 0.0f;
							mState->thirdPersonMode = true;
							printf("[Gameplay] Bike stopped and respawned in place!\n");
						}
						else {

						}
						// -----------------------------------------------------
					}
				}
			}
		}

		if (m_bikeController) {
			m_bikeController->Update(dt);
		}


		m_render->mDebugRenderer.DrawBox(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		m_render->mDebugRenderer.DrawLine(glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(3.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		m_render->mDebugRenderer.DrawSphere(glm::vec3(-4.0f, 2.0f, 0.0f), 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		m_render->mDebugRenderer.DrawCapsule(glm::vec3(0.0f, 2.0f, 5.0f), 1.0f, 1.5f, glm::vec3(1.0f, 1.0f, 0.0f));
	}

	void TestScene::Shutdown() {

		m_bikeController.reset();
	}

} // namespace engine
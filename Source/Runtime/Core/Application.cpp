#include "Application.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include <flecs.h>

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
        sceneManager  = AddSystem<SceneManager>(physicsSystem);
        sceneManager->SetInputSystem(inputSystem);

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



        glm::mat4 BikeSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, 10.0f, 30.0f));
        renderSystem->load_additional_model("Assets/Models/bike.glb", false, 50.0f, BikeSpawnPos, true);

        glm::mat4 LampSpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(40.0f, 2.0f, 15.0f));
        renderSystem->load_additional_model("Assets/Models/Lamp post.glb", false, 90.0f, LampSpawnPos);

        glm::mat4 CubeSpawnPos = glm::translate( BikeSpawnPos, glm::vec3(0.0f, -7.0f, 8.0f));
        renderSystem->load_additional_model("Assets/Models/em1.gltf", false, 90.0f, CubeSpawnPos);

        glm::mat4 tbpos = glm::translate(BikeSpawnPos, glm::vec3(0.0f, 0.0f, -8.0f));
        renderSystem->load_additional_model("Assets/Models/testBike1.gltf", false, 90.0f, tbpos,false, true);


        // 闁?Application.cpp 闁汇劌瀚悗顖炴焻閻樻彃姣愰柡浣哄濠€顖滀焊閹惧懐绀塸rint_all_entities 濞戞柨顑呮晶鐘睬庣拠鎻掝潱闁?







           // ===================UI System===========================
        // 闁告梻濮惧ù鍥ㄧ▔婵犲喚鍋уǎ鍥ㄧ箓閻°劑鎯?JSON 閻庢稒蓱閵?
        //load the last saved JSON save file
      //  engine::EngineUi::LoadProject(sceneManager, renderSystem, "Assets/MySceneSave.json");
        // ===================UI System===========================
        // 
        // 闁哄牃鍋撻柛姘閸熲偓闁瑰灚鎸稿畵鍐偓鍦仒缂嶅宕氬Δ鍕┾偓鍐晬瀹€鈧垾妯兼媼閵堝洣绱栭柛蹇擃槸閻ゅ嫭鎷呴幘鍐插殥闁告帗绋戠紓?
        sceneManager->print_all_entities();
        // --- 闁靛棙鍔掗幈銊╁绩楠炲簱鍋撻幋锝庡悁缂佺姵顨夊┃鍛緞鐎甸晲绱栭柣銊ュ閸ㄥ灚鎱ㄧ€ｂ晝绉寸紓?---
// 闁稿娲╅鏇熸姜閿曗偓閵囨棃宕?Y閺夌偞娼欐禍鍛婄▔?1.0缂?闁挎稑顒甸弶鐐存綑娴滄悂宕?1.2缂? 闁汇劌瀚紞鍛磾?
        glm::vec3 headlightOffset = glm::vec3(0.0f, 2.8f, -0.6f);
        glm::mat4 LightTransform = glm::translate(BikeSpawnPos, headlightOffset);

        // --- 闁靛棙鍔掗幈銊╁绩楠炲簱鍋撻幋婵嗙仭鐎点倝缂氭禒娑㈠礂婢跺奔绱?(閺夌儑绠戦妵鏃堟倶? ---
                auto headlightEntity = sceneManager->create_light_entity(
            "headlight",
            engine::LightType::Spot,
                    glm::vec3(1.0f, 0.95f, 0.85f), // 閺夌儑濡囨导鍛紣濠婂棗顥忛柨娑欒壘娴滄洖顕ラ闂寸剨闁哄棙鐗犵划宥夋嚌閼碱剚鐣遍柛妞煎€楃粈?LED 闁诲浚鍨遍崝鍛喆?
                    15.0f,                         // 鐎殿喖鎼€规娊鏁嶉崸鏄籺ensity闁挎稑顧€缁辨壆绮欏鍛俺閻犲鍟╃€垫帗绋夐埀顒勬倷?
                    LightTransform,                // 闁告帗绻傞～鎰媴瀹ュ洨鏋傞柣顓涙櫊濡偓
                    40.0f,                         // 闁绘挆鍐闁肩厧鍟ú鍧楁晬閸х敘nge闁挎稑顧€缁辩増娼敂鍙ョ礀闁煎磭鏅崣搴㈢椤旂厧顤呴柡?40 缂?
                    glm::vec3(0.0f, 0, 1.0f),      // 闁绘挆鍐闁哄倻鎳撻幃婊堟晬閸у”rection闁挎稑顧€缁辨澘顫㈤敐鍛枀闁哄倻娅㈢槐婵嗩嚗椤旈棿绨抽柛姘灣缁楀懘宕愰悙顒佺仒闁绘挆鍌氱槰閻犱警鍨跺?
                    15.0f,                         // 闁告劕鎳橀弫鐔烘喆閹虹偟绀刬nnerCutOff闁挎稑顧€缁辩増绋夐鐐靛 15 閹艰揪绠掔€垫牠宕堕弶鎴濇暥闁哄牃鍋撳ù?
                    25.0f                          // 濠㈣埖鐗犻弫鐔烘喆閹虹偟绀刼uterCutOff闁挎稑顧€缁辩増娼忛崷顓犲枠閻炴稒婢橀崳娲礆?25 閹艰揪璁ｇ槐婵嬫焻閹邦厾鐟ら柛娆惿戝▓?

        );

        uint32_t bikeCompoundBodyID = sceneManager->find_compound_body_near(glm::vec3(BikeSpawnPos[3]), 12.0f);
        if (bikeCompoundBodyID != ~0u) {
            mState.controlledCompoundBodyID = bikeCompoundBodyID;
            headlightEntity.set<AttachedToCompoundBody>({ bikeCompoundBodyID, glm::translate(glm::mat4(1.0f), headlightOffset) });
        }


        //add Lights Here
        // 閻庤鐭粻鐔煎棘閻熺増鍊婚柨娑欑煯缁娀宕ｉ崗鍛憪闁哄倻鎳撻幃婊冾啅閿旇法鐟撻柡鍌氭贡閸欏海浜?
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, -0.3f));
        // 閻忓繐妫欓弻鐔煎触閹存繄鎽犻柛蹇嬪劤閻撯晠姊奸悽鍨暠妤犵偛纾簺闁告帗顨愮槐娆撳箣閹存粍绮﹂柣?SceneManager 濞村吋纰嶈ぐ渚€宕ｉ弽褏鏆婂ù锝嗙矆鐠愮喖宕楁径瀣埍闁哄倻鎳撻幃婊堟晬?
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



		//// 閻庤鐭粻鐔兼倷閻熸澘甯ㄦ繝褎鍔掔紞鍛磾?
  //      glm::mat4 light1SpawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(40.0f, 18.0f, 15.0f));
  //      sceneManager->create_light_entity(
  //          "lampLight",
  //          engine::LightType::Point,
  //          glm::vec3(0.5f, 1.0f, 2.0f), //color
  //          3,//intensity     

		//	light1SpawnPos ,
		//	40.0f // range
  //      );

        // 閻庤鐭粻鐔兼倷閻熸澘甯ㄦ繝褎鍔掔紞鍛磾?
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
        //// 闁告梻濮惧ù鍥ㄧ▔婵犲喚鍋уǎ鍥ㄧ箓閻°劑鎯?JSON 閻庢稒蓱閵?
        ////load the last saved JSON save file
        //engine::EngineUi::LoadProject(sceneManager, renderSystem, "Assets/MySceneSave.json");
        //// ===================UI System===========================
        //// 
        //// 闁哄牃鍋撻柛姘閸熲偓闁瑰灚鎸稿畵鍐偓鍦仒缂嶅宕氬Δ鍕┾偓鍐晬瀹€鈧垾妯兼媼閵堝洣绱栭柛蹇擃槸閻ゅ嫭鎷呴幘鍐插殥闁告帗绋戠紓?
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
            for (auto& sys : Systems)
                sys->Update(dt);
        }
    }
    

    
}










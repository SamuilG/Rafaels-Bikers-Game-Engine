#pragma execution_character_set("utf-8")
#include "EngineUi.hpp"
#include "SwitchLanguage.hpp" 

#include <imgui.h>
#include <cstdio>
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp" 
#include <glm/gtc/matrix_transform.hpp> 
#include "../Particle/ParticleSystem.hpp"
#include "..\..\ThirdParty\imgui\ImGuizmo\ImGuizmo.h"

#include <glm/gtc/type_ptr.hpp>





namespace engine {


    void EngineUi::DrawControlPanel(UserState& state, RenderSystem* renderSys, SceneManager* sceneManager)
    {
        //Set initial window coordinates设置初始坐标 (450, 20)
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
		//Set initial window size 设置初始大小：宽 400，高 600
        ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(_SL("Engine Control Panel")))
        {
			// switch language button 切换语言按钮
            if (ImGui::Button(_SL("Switch Language"))) {
                if (Translator::CurrentLanguage == Language::English) {
                    Translator::SetLanguage(Language::Chinese);
                }
                else {
                    Translator::SetLanguage(Language::English);
                }
            }
            ImGui::Separator();

            //Performance 性能
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), _SL("[ Performance ]"));
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text(_SL("FPS: %.3f ms"), 1000.0f / ImGui::GetIO().Framerate);

			//render mode 渲染模式
            const char* modes[] = { "Default", "Mipmaps", "Depth", "Derivatives", "Overdraw", "Overshading" };
            ImGui::Combo(_SL("View Mode"), &state.renderMode, modes, IM_ARRAYSIZE(modes));

			// switches 开关
            ImGui::Checkbox(_SL("Particle System"), &state.particlesEnabled);
            ImGui::Checkbox(_SL("Enable Mosaic Post-Process"), &state.mosaicEnabled);

            ImGui::Separator();

			//generator 生成器
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), _SL("[ generator ]"));

            static float spawnHeight = 25.0f;
            ImGui::SliderFloat(_SL("Height"), &spawnHeight, 0.0f, 100.0f);

            // 物品选择下拉菜单
            const char* itemNames[] = { "BaseballBat", "Car", "Missile", "Police Car","Animated Character Base","Helicopter","Roman Centurion" };
            static int selectedItem = 1;
            ImGui::Combo(_SL("select"), &selectedItem, itemNames, IM_ARRAYSIZE(itemNames));

			// spawn button 生成按钮
            if (ImGui::Button(_SL("Spawn!!!!"), ImVec2(200, 40)))
            {
                if (renderSys) {
                    std::printf("Spawning %s at Y = %.1f\n", itemNames[selectedItem], spawnHeight);

                    glm::mat4 spawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, spawnHeight, 20.0f));

                    if (selectedItem == 0) {
                        renderSys->load_additional_model("Assets/Models/BaseballBat.glb", false, 1.5f, spawnPos);
                    }
                    else if (selectedItem == 1) {
                        glm::mat4 carPos = spawnPos * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
                        renderSys->load_additional_model("Assets/Models/Car.glb", false, 1500.0f, carPos);
                    }
                    else if (selectedItem == 2) {
                        renderSys->load_additional_model("Assets/Models/Missile.glb", false, 50.0f, spawnPos);
                    }
                    else if (selectedItem == 3) {
                        renderSys->load_additional_model("Assets/Models/Police Car.glb", false, 1600.0f, spawnPos);
                    }
                    else if (selectedItem == 4) {
                        renderSys->load_additional_model("Assets/Models/Animated Character Base.glb", false, 1600.0f, spawnPos);
                    }
                    else if (selectedItem == 5) {
                        renderSys->load_additional_model("Assets/Models/Helicopter.glb", false, 1600.0f, spawnPos);
                    }
                    else if (selectedItem == 6) {
                        renderSys->load_additional_model("Assets/Models/Roman Centurion.glb", false, 1600.0f, spawnPos);
                    }
                }
            }
            
            ImGui::Separator();

			//particle editor 粒子编辑器
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), _SL("[ Particle Editor ]"));

            if (renderSys && state.particlesEnabled)
            {
                auto& particles = renderSys->GetParticles();
                static int selectedParticle = 0;

				//add/remove group buttons 添加/删除粒子组按钮
                if (ImGui::Button(_SL("Add Group"))) {
                    renderSys->AddParticleGroup();
                    selectedParticle = (int)particles.size() - 1; // 自动选中最新创建的
                }
                ImGui::SameLine();
                if (ImGui::Button(_SL("Delete Group")) && !particles.empty()) {
                    renderSys->RemoveParticleGroup(selectedParticle);
                    selectedParticle = 0;
                }

                if (!particles.empty())
                {
                    static int selectedParticle = 0;
                    if (selectedParticle >= particles.size()) selectedParticle = (int)particles.size() - 1;

                    // 下拉菜单粒子组
                    std::string currentName = "Group " + std::to_string(selectedParticle + 1);
                    if (ImGui::BeginCombo(_SL("Select Particle"), currentName.c_str()))
                    {
                        for (int i = 0; i < particles.size(); ++i)
                        {
                            bool is_selected = (selectedParticle == i);
                            std::string pName = "Group " + std::to_string(i + 1);
                            if (ImGui::Selectable(pName.c_str(), is_selected)) selectedParticle = i;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    auto& config = particles[selectedParticle]->config;

					//visible checkbox 可见 选框
                    ImGui::Checkbox(_SL("Visible"), &config.isVisible);

					//particle count adjustment粒子数量调整
                    static int newCount = 0;
                    if (newCount == 0) newCount = (int)particles[selectedParticle]->count();

                    ImGui::SetNextItemWidth(150);
                    ImGui::InputInt(_SL("Max Particles"), &newCount);
                    ImGui::SameLine();

                    if (ImGui::Button(_SL("Apply Changes"))) {
                        if (newCount > 0 && newCount < 100000) {
                            renderSys->ResizeParticleGroup(selectedParticle, (uint32_t)newCount);
                        }
                    }

                    ImGui::SameLine();
					// InputText for particle group name 粒子组名称输入框
                    ImGui::InputText(_SL("Name"), config.name, IM_ARRAYSIZE(config.name));

                    
                    //发射器设置 (Emitter Settings)
                    if (ImGui::CollapsingHeader(_SL("Emitter Settings"), ImGuiTreeNodeFlags_DefaultOpen))
                    {
						ImGui::Checkbox(_SL("Show Debug Wireframe"), &config.particleDebug);// show paticle debug 显示粒子调试线框
                        ImGui::DragFloat3(_SL("Emitter Pos"), &config.emitterPos.x, 0.1f);

                        const char* shapeNames[] = { "Cone", "Sphere", "Box" };
                        int currentShape = (int)particles[selectedParticle]->getEmitterShape();
                        if (ImGui::Combo(_SL("Emitter Shape"), &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                            particles[selectedParticle]->setEmitterShape((EmitterShape)currentShape);
                        }

						//default emmiter 默认发射器参数
                        if (currentShape == (int)EmitterShape::Cone) {
                            ImGui::SliderFloat(_SL("Cone Spread"), &config.coneSpread, 0.01f, 3.14f);
                        }
                        else if (currentShape == (int)EmitterShape::Sphere) {
                            ImGui::SliderFloat(_SL("Sphere Radius"), &config.sphereRadius, 0.01f, 10.0f);
                        }
                        else if (currentShape == (int)EmitterShape::Box) {
                            ImGui::DragFloat3(_SL("Box Area"), &config.boxArea.x, 0.1f, 0.1f, 100.0f);
                        }
                    }

					//particle physics parameters 粒子物理参数
                    if (ImGui::CollapsingHeader(_SL("Physics & Movement")))
                    {
                        ImGui::DragFloat3(_SL("Gravity"), &config.gravity.x, 0.001f);
                        ImGui::SliderFloat(_SL("Speed Min"), &config.speedMin, 0.0f, 20.0f);
                        ImGui::SliderFloat(_SL("Speed Max"), &config.speedMax, 0.0f, 20.0f);
                        ImGui::SliderFloat(_SL("Rotation Min"), &config.rotationMin, -360.0f, 360.0f);
                        ImGui::SliderFloat(_SL("Rotation Max"), &config.rotationMax, -360.0f, 360.0f);
                    }

					//particle appearance parameters 粒子外观参数
                    if (ImGui::CollapsingHeader(_SL("Appearance & Color")))
                    {
                        ImGui::SliderFloat(_SL("Size Min"), &config.sizeMin, 1.0f, 1000.0f);
                        ImGui::SliderFloat(_SL("Size Max"), &config.sizeMax, 1.0f, 1000.0f);
                        ImGui::SliderFloat(_SL("Start Size Scale"), &config.startSizeScale, 0.0f, 10.0f);
                        ImGui::SliderFloat(_SL("End Size Scale"), &config.endSizeScale, 0.0f, 10.0f);

                        ImGui::ColorEdit4(_SL("Start Color"), &config.startColor.x, ImGuiColorEditFlags_AlphaBar);
                        ImGui::ColorEdit4(_SL("End Color"), &config.endColor.x, ImGuiColorEditFlags_AlphaBar);

                        ImGui::Separator();
                        //useTexture? 是否使用贴图
                        bool useTexBool = (config.useTexture != 0);
                        if (ImGui::Checkbox(_SL("Use Texture"), &useTexBool)) {
                            config.useTexture = useTexBool ? 1 : 0;
                        }
                        //select texture 选择贴图
                        if (useTexBool)
                        {
                            ImGui::TextDisabled(_SL("Select Texture"));
                            ImGui::Spacing();

                            std::vector<std::string> texNames = renderSys->GetParticleTextureNames();

                            // 遍历所有贴图，生成一个横向排列的图片网格
                            for (int i = 0; i < texNames.size(); ++i)
                            {
                                const std::string& path = texNames[i];
                                VkDescriptorSet engineDesc = renderSys->GetParticleTextureDescriptor(path);
                                VkDescriptorSet imguiDesc = renderSys->GetImGuiTextureDescriptor(path);

                                if (imguiDesc)
                                {
									//if this texture is currently selected in the config
                                    bool isSelected = (config.textureDescriptor == engineDesc);

                                    // 如果被选中，按钮加上背景高亮
                                    if (isSelected) {
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.2f, 0.6f));
                                    }
                                    else {
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.0f));
                                    }

									// image button 显示贴图按钮
                                    if (ImGui::ImageButton(path.c_str(), (ImTextureID)imguiDesc, ImVec2(64, 64))) {
                                        config.textureDescriptor = engineDesc; 
                                    }

                                    ImGui::PopStyleColor();

                                    // 每行显示 4 个图标，没满 4 个就用 SameLine() 横向排列
                                    if ((i + 1) % 4 != 0 && i != texNames.size() - 1) {
                                        ImGui::SameLine();
                                    }
                                }
                            }

                            ImGui::Spacing();
                            ImGui::Separator();

                            ImGui::Checkbox(_SL("Animate Atlas"), &config.animateAtlas);
                            if (config.animateAtlas) {
                                ImGui::SliderInt(_SL("Atlas Cols"), &config.atlasCols, 1, 16);
                                ImGui::SliderInt(_SL("Atlas Rows"), &config.atlasRows, 1, 16);
                            }
                        }


						//particle life parameters 粒子生命周期参数
                        if (ImGui::CollapsingHeader(_SL("Life Cycle")))
                        {
                            ImGui::SliderFloat(_SL("Life Min"), &config.lifeMin, 0.1f, 10.0f);
                            ImGui::SliderFloat(_SL("Life Max"), &config.lifeMax, 0.1f, 10.0f);
                        }
                    }
                }
                else if (!state.particlesEnabled)
                {
                    ImGui::TextDisabled("Please check 'Particle System' above to edit.");
                }


                ImGui::Separator();
                if (sceneManager) {
                    
                }
            }
            ImGui::End();
        }
    }
 

    void EngineUi::DrawSceneHierarchy(SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id)
    {
        // 当前屏幕分辨率
        float screenWidth = ImGui::GetIO().DisplaySize.x;
        float screenHeight = ImGui::GetIO().DisplaySize.y;

		//windows position and size for hierarchy 面板位置和大小
        ImGui::SetNextWindowPos(ImVec2(screenWidth - 320, 20), ImGuiCond_FirstUseEver); 
        ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(_SL("Scene Hierarchy")))
        {
            if (sceneManager && &sceneManager->get_world() != nullptr) {
                ImGui::Text(_SL("Total Entities: %d"), sceneManager->get_entity_count());
                ImGui::Separator();

                auto& world = sceneManager->get_world();

                if (ImGui::BeginChild("EntityList", ImVec2(0, 0), true)) {
                    // 遍历所有带有 MeshComponent 的实体
                    world.each([&](flecs::entity entity, MeshComponent& meshComponent) {
                        std::string name = entity.name().size() > 0 ? entity.name().c_str() : "ID: " + std::to_string(entity.id());

                        bool is_selected = (selected_id == entity.id());
                        if (ImGui::Selectable(name.c_str(), is_selected)) {
                            selected_id = entity.id(); 
                        }
                        });
                    ImGui::EndChild();
                }
            }
        }
        ImGui::End();

		//entity inspector 物体的各种参数
        if (selected_id != 0 && sceneManager) {
            auto& world = sceneManager->get_world();

			flecs::entity selectedEntity = world.entity(selected_id);//current selected entity 当前选中实体

            if (!selectedEntity.is_alive()) {
                selected_id = 0;
                return;
            }

			//new  window 新窗口位置和大小
            ImGui::SetNextWindowPos(ImVec2(screenWidth - 320, 430), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

            if (ImGui::Begin(_SL("Entity Inspector"))) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ %s ]", selectedEntity.name().c_str());
                ImGui::Separator();

				// visible checkbox 可见选框
                if (selectedEntity.has<EntityStatus>()) {
                    EntityStatus* entityStatus = &selectedEntity.get_mut<EntityStatus>();
                    if (entityStatus) {
                        ImGui::Checkbox(_SL("Visible"), &entityStatus->should_render);
                    }
                }

                // Transform 组件的 UI
                if (selectedEntity.has<LocalTransform>()) {
                    LocalTransform* localTransform = &selectedEntity.get_mut<LocalTransform>();
                    if (localTransform) {
                        float matrixTranslation[3], matrixRotation[3], matrixScale[3];
                        ImGuizmo::DecomposeMatrixToComponents(
                            glm::value_ptr(localTransform->matrix),
                            matrixTranslation,
                            matrixRotation,
                            matrixScale
                        );
						// select new object when selected_id changes 选中新的对象时更新UI显示
                        if (m_current_inspected_id != selectedEntity.id()) {
                            m_current_inspected_id = selectedEntity.id();
                            ImGuizmo::DecomposeMatrixToComponents(
                                glm::value_ptr(localTransform->matrix),
                                m_ui_translation, m_ui_rotation, m_ui_scale
                            );
                        }

                        bool is_modified = false;

						//bind UI to matrix 将UI绑定到矩阵
                        ImGui::Text(_SL("Position (XYZ)"));
                        if (ImGui::DragFloat3("##Pos", m_ui_translation, 0.1f)) is_modified = true;

                        ImGui::Text(_SL("Rotation (Pitch Yaw Roll)"));
                        if (ImGui::DragFloat3("##Rot", m_ui_rotation, 1.0f)) is_modified = true;

                        ImGui::Text(_SL("Scale (XYZ)"));
                        if (ImGui::DragFloat3("##Scl", m_ui_scale, 0.1f)) is_modified = true;

						//mirror buttons 镜像按钮
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Mirror:");
                        ImGui::SameLine();
                        if (ImGui::Button("Flip X")) { m_ui_scale[0] = -m_ui_scale[0]; is_modified = true; }
                        ImGui::SameLine();
                        if (ImGui::Button("Flip Y")) { m_ui_scale[1] = -m_ui_scale[1]; is_modified = true; }
                        ImGui::SameLine();
                        if (ImGui::Button("Flip Z")) { m_ui_scale[2] = -m_ui_scale[2]; is_modified = true; }
                        // ==========================================================

						//recompose matrix if modified 重新合成矩阵
                        if (is_modified) {
                            ImGuizmo::RecomposeMatrixFromComponents(
                                m_ui_translation, m_ui_rotation, m_ui_scale,
                                glm::value_ptr(localTransform->matrix)
                            );
                            selectedEntity.modified<LocalTransform>();
                        }
                    }
                }

                ImGui::Separator();
				// delete button 删除按钮
                if (ImGui::Button(_SL("Delete Entity"), ImVec2(-1, 0))) {
                    selectedEntity.destruct();    // delete 从内存和场景中彻底销毁该实体
					selected_id = 0;              // clear selection 清除选中状态
                }
				//键盘快捷键删除 Delete 
                if (selectedEntity.is_alive() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
                    selectedEntity.destruct();
                    selected_id = 0;
                }
            }
            ImGui::End();

            //全局透明绘图层 (ImGuizmo 3D 坐标轴)
            if (selectedEntity.is_alive() && selectedEntity.has<LocalTransform>()) {
                LocalTransform* localTransform = &selectedEntity.get_mut<LocalTransform>();

                ImGuizmo::BeginFrame();

                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));
                ImGui::Begin("GizmoCanvas", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoInputs);

				//drawlist, orthographic, rect 绘图设置
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetRect(0, 0, screenWidth, screenHeight); // 设定鼠标坐标映射的屏幕范围


				//keyboard shortcuts for gizmo 操作模式快捷键 移动move(W)、旋转rotate(E)、缩放scale(R)
                static ImGuizmo::OPERATION currentOp = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_W)) currentOp = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) currentOp = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOp = ImGuizmo::SCALE;

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    currentOp,
                    ImGuizmo::LOCAL,
                    glm::value_ptr(localTransform->matrix)
                );

				// IsUsing() clicked and dragging the gizmo 是否正在使用 gizmo（点击并拖动）
                if (ImGuizmo::IsUsing()) {
                    float temp_t[3], temp_r[3], temp_s[3];
                    ImGuizmo::DecomposeMatrixToComponents(
                        glm::value_ptr(localTransform->matrix),
                        temp_t, temp_r, temp_s
                    );

                    if (currentOp == ImGuizmo::SCALE) {
                        for (int i = 0; i < 3; ++i) {
                            if (std::abs(temp_r[i] - m_ui_rotation[i]) > 90.0f) {
                                // 说明这个轴的缩放穿过了 0，变成了负数
                                // 强行把原本的正数 scale 加上负号，并抵消它的旋转
                                m_ui_scale[i] = -temp_s[i];
                            }
                            else {
                                // 正常缩放，继承之前的正负号状态
                                m_ui_scale[i] = (m_ui_scale[i] < 0) ? -std::abs(temp_s[i]) : std::abs(temp_s[i]);
                            }
                        }
                    }
                    else {
                        // 平移&旋转
                        m_ui_translation[0] = temp_t[0]; m_ui_translation[1] = temp_t[1]; m_ui_translation[2] = temp_t[2];
                        m_ui_rotation[0] = temp_r[0]; m_ui_rotation[1] = temp_r[1]; m_ui_rotation[2] = temp_r[2];
                        // 缩放保持不变
                    }
					//recompose matrix 重新合成矩阵
                    ImGuizmo::RecomposeMatrixFromComponents(
                        m_ui_translation, m_ui_rotation, m_ui_scale,
                        glm::value_ptr(localTransform->matrix)
                    );

					selectedEntity.modified<LocalTransform>(); // update entity 更新实体以应用更改
                }

                ImGui::End();
            }
        }
    }

    void EngineUi::DrawMainMenu(RenderSystem* renderSys, bool& appRunning, bool& isGameStarted) {

        // 1. 获取屏幕信息
		//get screen info
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;

        // 2. 获取背景贴图
		//get background texture
        std::string bgName = cfg::ParticleTextures[0];
        VkDescriptorSet imguiBgTex = renderSys->GetImGuiTextureDescriptor(bgName);

        // 绘制背景图片 (全屏、无边距、无圆角)
		//draw full-screen background
        if (imguiBgTex) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));

            // 样式：背景色、圆角、边距和边框
			//style: background color, rounding, padding, border
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

            if (ImGui::Begin("BackgroundWindow", nullptr, bgFlags))
            {
                ImVec4 tintColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                ImVec4 borderColor = ImVec4(0, 0, 0, 0);

                ImGui::Image(
                    (ImTextureID)imguiBgTex,
                    ImVec2(screenWidth, screenHeight),
					ImVec2(0, 1),//flip texture vertically 翻转贴图坐标
                    ImVec2(1, 0),
                    tintColor,
                    borderColor
                );
            }
            ImGui::End();

            // 弹出样式
			// pop styles
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        //绘制按钮窗口 (动态居中与自适应大小)
		//draw button window (centered and adaptive size)

        //根据屏幕比例设置大小（宽占20%，高占30%）
		// set size based on screen ratio (20% width, 30% height)
        float windowWidth = screenWidth * 0.20f;
        float windowHeight = screenHeight * 0.30f;

        // 设置最小尺寸保护，防止在高分辨率下看起来太小或低分辨率下挤在一起
		// set minimum size limits to prevent it from looking too small on high-res or too cramped on low-res
        if (windowWidth < 280.0f) windowWidth = 280.0f;
        if (windowHeight < 240.0f) windowHeight = 240.0f;

        float posX = (screenWidth - windowWidth) * 0.5f;
        float posY = (screenHeight - windowHeight) * 0.5f;

        // 使用 ImGuiCond_Always 确保在窗口缩放时实时重算位置
		// use ImGuiCond_Always to ensure it recalculates position in real-time when resizing
        ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);

        // 按钮窗口：半透明背景 + 圆角
		// button window: semi-transparent background + rounded corners
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

        ImGuiWindowFlags buttonFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;

        if (ImGui::Begin("MainMenuButtons", nullptr, buttonFlags)) {
            // 内部控件水平居中
			// center the inner contents horizontally
            float availWidth = ImGui::GetContentRegionAvail().x;
            float textWidth = ImGui::CalcTextSize(_SL("[ MAIN MENU ]")).x;

            ImGui::SetCursorPosX((availWidth - textWidth) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), _SL("[ MAIN MENU ]"));

            ImGui::Separator();
            ImGui::Spacing();

            // 按钮宽度设为 -1 自动填满窗口可用宽度
            if (ImGui::Button(_SL("Start Game"), ImVec2(-1, 60))) {
                isGameStarted = true;
                std::printf("Game Started!\n");
            }

            ImGui::Spacing();
            if (ImGui::Button(_SL("Setting"), ImVec2(-1, 60))) {
				//未完成 not ffinished yet
                std::printf("Setting...(coming soon)\n");
            }
            ImGui::Spacing();
            if (ImGui::Button(_SL("Exit Game"), ImVec2(-1, 60))) {
                appRunning = false;
                std::printf("Exiting Game...\n");
            }
            ImGui::Spacing();
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void EngineUi::DrawGamePause(RenderSystem* renderSys, UserState& state, bool& appRunning) {
        // 1. 获取屏幕信息
        //get screen info
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;

        // 2. 获取背景贴图
        //get background texture
        std::string bgName = "Assets/Textures/GamePause_Bg.png";
        VkDescriptorSet imguiBgTex = renderSys->GetImGuiTextureDescriptor(bgName);

        ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

        //全屏背景绘制
		//ffull-screen background
        if (imguiBgTex) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            if (ImGui::Begin("PauseBackground", nullptr, bgFlags)) {
                ImVec4 gameOverTint = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
                ImVec4 noBorder = ImVec4(0, 0, 0, 0);

                ImGui::Image(
                    (ImTextureID)imguiBgTex,
                    ImVec2(screenWidth, screenHeight),
                    ImVec2(0, 1),   // uv0
                    ImVec2(1, 0),   // uv1
                    gameOverTint,   // 着色
                    noBorder        // 边框
                );
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        //暂停菜单按钮
		//pause menu buttons
        float winW = screenWidth * 0.25f;
        float winH = screenHeight * 0.45f;
        if (winW < 300.0f) winW = 300.0f;
		// 使用 ImGuiCond_Always 确保在窗口缩放时实时重算位置
        ImGui::SetNextWindowPos(ImVec2((screenWidth - winW) * 0.5f, (screenHeight - winH) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.0f, 0.0f, 0.8f)); // 深红色半透明背景
        
        if (ImGui::Begin("GamePauseMenu", nullptr, ImGuiWindowFlags_NoDecoration)) {

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), _SL("Pause"));
            ImGui::Separator();
            ImGui::Spacing();

            float btnW = ImGui::GetContentRegionAvail().x;

            // 重新开始 (Restart)
            if (ImGui::Button(_SL("Restart Game"), ImVec2(btnW, 60))) {
                // 这里通常需要调用一个 ResetScene() 函数来重置物理和实体位置
                state.isGameOver = false;
                state.isGameStarted = true;
                std::printf("Restarting Level...\n");
            }

            ImGui::Spacing();

            //回到主菜单 (back to Main Menu)
            if (ImGui::Button(_SL("Back to Main Menu"), ImVec2(btnW, 60))) {
                state.isGameOver = false;
                state.isGameStarted = false; // 切换回主菜单状态
            }

            ImGui::Spacing();

            // 退出(Exit)
            if (ImGui::Button(_SL("Exit Game"), ImVec2(btnW, 60))) {
                appRunning = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    void EngineUi::DrawGameOver(RenderSystem* renderSys, UserState& state, bool& appRunning) {
        // 1. 获取屏幕信息
        //get screen info
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;

        // 2. 获取背景贴图
        //get background texture
        std::string bgName = "Assets/Textures/GameOver_Bg.png";
        VkDescriptorSet imguiBgTex = renderSys->GetImGuiTextureDescriptor(bgName);

        ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

        //全屏背景绘制
        //ffull-screen background
        if (imguiBgTex) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            if (ImGui::Begin("GameOverBackground", nullptr, bgFlags)) {
            
                ImVec4 gameOverTint = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); 
                ImVec4 noBorder = ImVec4(0, 0, 0, 0);

                ImGui::Image(
                    (ImTextureID)imguiBgTex,
                    ImVec2(screenWidth, screenHeight),
                    ImVec2(0, 1),   // uv0
                    ImVec2(1, 0),   // uv1
                    gameOverTint,   // 着色
                    noBorder        // 边框
                );
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        //死亡菜单按钮
		//death menu buttons
        float winW = screenWidth * 0.25f;
        float winH = screenHeight * 0.45f;
        if (winW < 300.0f) winW = 300.0f;
		// 使用 ImGuiCond_Always 确保在窗口缩放时实时重算位置
        ImGui::SetNextWindowPos(ImVec2((screenWidth - winW) * 0.5f, (screenHeight - winH) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.0f, 0.0f, 0.8f)); // 深红色半透明背景
        if (ImGui::Begin("GameOverMenu", nullptr, ImGuiWindowFlags_NoDecoration)) {

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), _SL("   GAME OVER"));
            ImGui::Separator();
            ImGui::Spacing();

            float btnW = ImGui::GetContentRegionAvail().x;

            //重新开始 (Restart)
            if (ImGui::Button(_SL("Restart Game"), ImVec2(btnW, 60))) {
                // 重新开始后调用一个 ResetScene的函数来重置物理和实体位置
				//reset game state to restart level
                state.isGameOver = false;
                state.isGameStarted = true;
                std::printf("Restarting Level...\n");
            }

            ImGui::Spacing();

            //回到主菜单 (Main Menu)
            if (ImGui::Button(_SL("Back to Main Menu"), ImVec2(btnW, 60))) {
                state.isGameOver = false;
                state.isGameStarted = false; // 切换回主菜单状态
            }

            ImGui::Spacing();

            //退出游戏 (Exit)
            if (ImGui::Button(_SL("Exit Game"), ImVec2(btnW, 60))) {
                appRunning = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
} // namespace engine


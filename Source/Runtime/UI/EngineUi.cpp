#pragma execution_character_set("utf-8")

#include "EngineUi.hpp"
#include "EditorLayout.hpp"
#include "EditorTransform.hpp"
#include "EditorTheme.hpp"
#include "../UserState/CameraController.hpp"
#include "../UserState/EditorState.hpp"
#include "../UserState/RenderSettings.hpp"
#include "../Renderer/RenderUtilities/ViewMode.hpp"

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cmath>

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <unordered_set>
#include <format>
#include <algorithm>

#include <imgui.h>
#include "../../ThirdParty/imgui/ImGuizmo/ImGuizmo.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "../../ThirdParty/json/json.hpp"

#include "SwitchLanguage.hpp" 
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp" 
#include "../Scene/EditorSceneAdapter.hpp"
#include "../Particle/ParticleSystem.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Renderer/RenderUtilities/light.hpp"

#include "../AudioSystem/AudioSystem.hpp"
#include "VisualUIEditor/UIEditorWindow.hpp"


namespace fs = std::filesystem;
using json = nlohmann::json;



namespace engine {

	namespace {
        bool s_ResetEditorLayout = false;

        std::string PanelTitle(const char* label, const char* id) {
            return std::string(_SL(label)) + "###" + id;
        }

		glm::vec3 NormalizeOrFallback(const glm::vec3& value, const glm::vec3& fallback) {
			float length = glm::length(value);
			if (length <= 0.0001f) {
				return fallback;
			}
			return value / length;
		}
		std::string ToLowerCopy(std::string value) { //Normalize extensions for case-insensitive asset checks.
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::string ToUpperCopy(std::string value) { //Build readable labels for assets without thumbnails.
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
				return static_cast<char>(std::toupper(ch));
				});
			return value;
		}

		std::string NormalizeAssetBrowserPath(const fs::path& path) { //Keep browser paths in stable Assets/... form.
			return path.lexically_normal().generic_string();
		}

		bool IsModelAssetPath(const fs::path& path) { //Preserve drag-and-drop for model assets.
			const std::string ext = ToLowerCopy(path.extension().string());
			return ext == ".glb";
		}

		bool IsTextureAssetPath(const fs::path& path) { //Detect texture assets for thumbnail previews.
			const std::string ext = ToLowerCopy(path.extension().string());
			return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
		}

		glm::mat4 BuildDroppedModelTransform(const char* assetPath, const glm::vec3& worldPosition) {
			glm::mat4 transform = glm::translate(glm::mat4(1.0f), worldPosition);
			if (assetPath && strstr(assetPath, "Car")) {
				transform = glm::scale(transform, glm::vec3(0.1f));
			}
			else if (assetPath && strstr(assetPath, "Helicopter")) {
				transform = glm::scale(transform, glm::vec3(0.3f));
			}
			return transform;
		}

		bool TryBuildViewportDropTransform(
			const ImVec2& mousePos,
			const ImVec2& viewportPos,
			const ImVec2& viewportSize,
			SceneManager* sceneManager,
			const glm::mat4& view,
			const glm::mat4& proj,
			const char* assetPath,
			glm::mat4& outTransform) {
			if (!assetPath || viewportSize.x <= 1.0f || viewportSize.y <= 1.0f) {
				return false;
			}

			const float localX = mousePos.x - viewportPos.x;
			const float localY = mousePos.y - viewportPos.y;
			const float ndcX = (2.0f * localX) / viewportSize.x - 1.0f;
			const float ndcY = 1.0f - (2.0f * localY) / viewportSize.y;

			const glm::mat4 invProjView = glm::inverse(proj * view);
			glm::vec4 nearP = invProjView * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
			glm::vec4 farP = invProjView * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
			if (std::abs(nearP.w) <= 0.0001f || std::abs(farP.w) <= 0.0001f) {
				return false;
			}

			nearP /= nearP.w;
			farP /= farP.w;

			const glm::vec3 rayOrigin = glm::vec3(nearP);
			const glm::vec3 rayDir = glm::normalize(glm::vec3(farP - nearP));

			glm::vec3 targetPos = rayOrigin + rayDir * 15.0f;
			if (sceneManager) {
				if (PhysicsSystem* physics = sceneManager->get_physics_system()) {
					glm::vec3 hitPoint(0.0f);
					if (physics->cast_ray_hit_point(rayOrigin, rayDir, 1000.0f, hitPoint)) {
						targetPos = hitPoint;
					}
				}
			}

			if (std::abs(rayDir.y) > 0.0001f) {
				const float t = -rayOrigin.y / rayDir.y;
				if (t > 0.0f) {
					const glm::vec3 planeHit = rayOrigin + rayDir * t;
					if (!sceneManager || planeHit.y > targetPos.y || targetPos.y < 0.0f) {
						targetPos = planeHit;
					}
				}
			}

			targetPos.y = std::max(targetPos.y, 0.0f);
			outTransform = BuildDroppedModelTransform(assetPath, targetPos);
			return true;
		}

		flecs::entity SpawnDroppedModel(
			RenderSystem* renderSys,
			SceneManager* sceneManager,
			const char* assetPath,
			const glm::mat4& transform) {
			if (!renderSys || !sceneManager || !assetPath) {
				return flecs::entity::null();
			}

			return sceneManager->LoadModel(
				renderSys,
				assetPath,
				ModelPhysicsType::Static,
				0.0f,
				transform);
		}

		bool IsFontAssetPath(const fs::path& path) { //Detect font assets so Assets/Fonts is represented clearly.
			const std::string ext = ToLowerCopy(path.extension().string());
			return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
		}

		bool IsSameOrChildPath(const std::string& parentPath, const std::string& childPath) { //Auto-open the active branch in the folder tree.
			if (childPath == parentPath) {
				return true;
			}
			if (childPath.size() <= parentPath.size()) {
				return false;
			}
			return childPath.rfind(parentPath + "/", 0) == 0;
		}

		std::vector<fs::path> CollectChildDirectories(const fs::path& directory) { //Collect only subfolders for the left tree panel.
			std::vector<fs::path> childDirectories;
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(directory, ec)) {
				if (ec) {
					break;
				}
				if (entry.is_directory()) {
					childDirectories.push_back(entry.path());
				}
			}
			std::sort(childDirectories.begin(), childDirectories.end(), [](const fs::path& lhs, const fs::path& rhs) {
				return ToLowerCopy(lhs.filename().string()) < ToLowerCopy(rhs.filename().string());
				});
			return childDirectories;
		}

		std::string GetAssetTileLabel(const fs::path& path) { //Show a fallback type label when no thumbnail exists.
			if (path.has_filename() && path.filename() == "..") {
				return "UP";
			}
			if (fs::is_directory(path)) {
				return "FOLDER";
			}
			if (IsModelAssetPath(path)) {
				return "MODEL";
			}
			if (IsTextureAssetPath(path)) {
				return "TEXTURE";
			}
			if (IsFontAssetPath(path)) {
				return "FONT";
			}

			std::string ext = path.extension().string();
			if (ext.empty()) {
				return "FILE";
			}

			ext = ToUpperCopy(ext.substr(1));
			if (ext.size() > 6) {
				ext = ext.substr(0, 6);
			}
			return ext;
		}

		bool TryCreateAssetFolder(const fs::path& parentDirectory, const char* folderName, std::string& createdPath, std::string& errorMessage) { //Allow creating new folders in the current browser directory.
			createdPath.clear();
			errorMessage.clear();

			std::string newFolderName = folderName ? folderName : "";
			const std::size_t firstNonWhitespace = newFolderName.find_first_not_of(" \t\r\n");
			if (firstNonWhitespace == std::string::npos) {
				errorMessage = "Folder name cannot be empty.";
				return false;
			}
			newFolderName.erase(0, firstNonWhitespace);
			newFolderName.erase(newFolderName.find_last_not_of(" \t\r\n") + 1);

			if (newFolderName.empty()) {
				errorMessage = "Folder name cannot be empty.";
				return false;
			}

			if (newFolderName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
				errorMessage = "Folder name contains invalid characters.";
				return false;
			}

			const fs::path targetDirectory = parentDirectory / newFolderName;
			if (fs::exists(targetDirectory)) {
				errorMessage = "Folder already exists.";
				return false;
			}

			std::error_code ec;
			if (!fs::create_directory(targetDirectory, ec)) {
				errorMessage = ec ? ec.message() : "Failed to create folder.";
				return false;
			}

			createdPath = NormalizeAssetBrowserPath(targetDirectory);
			return true;
		}

		void DrawAssetDirectoryTreeNode(const fs::path& directory, std::string& currentDirectory) { //Render the Assets folder hierarchy in the left tree.
			const std::string directoryPath = NormalizeAssetBrowserPath(directory);
			const std::vector<fs::path> childDirectories = CollectChildDirectories(directory);

			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (currentDirectory == directoryPath) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			if (childDirectories.empty()) {
				flags |= ImGuiTreeNodeFlags_Leaf;
			}
			if (directoryPath == "Assets" || IsSameOrChildPath(directoryPath, currentDirectory)) {
				flags |= ImGuiTreeNodeFlags_DefaultOpen;
			}

			const std::string displayName = directory.filename().empty() ? directoryPath : directory.filename().string();
			const bool opened = ImGui::TreeNodeEx(directoryPath.c_str(), flags, "%s", displayName.c_str());
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
				currentDirectory = directoryPath;
			}

			if (opened) {
				for (const auto& childDirectory : childDirectories) {
					DrawAssetDirectoryTreeNode(childDirectory, currentDirectory);
				}
				ImGui::TreePop();
			}
		}

	}


	// ========== console system 内置控制台系统==========
	struct EngineConsole {
		std::vector<std::string> Items;// 存放日志数据//store log data
		bool ScrollToBottom = true;// 控制自动滚动//control auto-scroll

		void Draw(const char* title, bool* p_open = nullptr) {
			if (p_open && !*p_open) return;
			if (!ImGui::Begin(title, p_open)) { ImGui::End(); return; }

			// 清空日志按钮//Clear logs button
			if (ImGui::Button("Clear Logs")) {
				Items.clear();
			}
			ImGui::Separator();

			for (const auto& item : Items) {
				if (item.find("[Error]") != std::string::npos) {
					ImGui::TextColored(editor_theme::Color(editor_theme::kError), "%s", item.c_str());
				}
				else if (item.find("[Warning]") != std::string::npos) {
					ImGui::TextColored(editor_theme::Color(editor_theme::kWarning), "%s", item.c_str());
				}
				else {
					ImGui::TextUnformatted(item.c_str());
				}
			}
			if (ScrollToBottom && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
				ImGui::SetScrollHereY(1.0f);
			}
			ScrollToBottom = false;
			ImGui::End();
		}
	};

	static EngineConsole s_Console; // 全局控制台实例

	//============push log message to console and backend output system 将日志消息推送到控制台和后台输出系统===========
	void EngineUi::PushLogMessage(const std::string& msg) {
		std::string cleanMsg = msg;

		if (!cleanMsg.empty() && cleanMsg.back() == '\n') {
			cleanMsg.pop_back();
		}

		s_Console.Items.push_back(cleanMsg);
		s_Console.ScrollToBottom = true;

		// 同时输出到标准控制台输出//Also output to standard output (console)
		std::cout << msg;
		if (msg.empty() || msg.back() != '\n') std::cout << "\n";
	}

	// printf
	void EngineUi::LogPrintf(const char* fmt, ...) {
		char buf[2048];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
		va_end(args);

		PushLogMessage(std::string(buf));
	}



	// 四舍五入(保留 5位小数)//Round to a specific number of decimal places (default 5)
	inline double RoundFloat(float value, int decimals = 5) {
		double multiplier = std::pow(10.0, decimals);
		// 先把传入的 float 转成 double 再进行乘除运算
		return std::round(static_cast<double>(value) * multiplier) / multiplier;
	}


	//写入 JSON // Write to JSON
	inline json Vec3ToJson(const glm::vec3& v) {
		return { RoundFloat(v.x), RoundFloat(v.y), RoundFloat(v.z) };
	}
	inline json Vec4ToJson(const glm::vec4& v) {
		return { RoundFloat(v.x), RoundFloat(v.y), RoundFloat(v.z), RoundFloat(v.w) };
	}

	// 读取 JSON// Read from JSON
	inline glm::vec3 JsonToVec3(const json& j) {
		return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
	}
	inline glm::vec4 JsonToVec4(const json& j) {
		return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
	}

	// ========== Toast system UI提示消息系统==========
	//Toast 变量//variables for Toast system
	static float s_ToastTimer = 0.0f;
	static std::string s_ToastMessage = "";
	static const float TOAST_DURATION = 2.0f;

	//Toast 函数实现
	void EngineUi::ShowToast(const std::string& message) {
		s_ToastMessage = message;
		s_ToastTimer = TOAST_DURATION;
	}

	void EngineUi::DrawToast(float dt) {
		if (s_ToastTimer <= 0.0f) return;

		s_ToastTimer -= dt; // 倒计时// Countdown

		// 计算平滑的 Alpha//Calculate smooth alpha for fade in/out
		float alpha = 1.0f;
		if (s_ToastTimer > TOAST_DURATION - 0.2f) {
			// 前 0.2 秒淡入//Fade in during the first 0.2 seconds
			alpha = (TOAST_DURATION - s_ToastTimer) / 0.2f;
		}
		else if (s_ToastTimer < 0.5f) {
			// 最后 0.5 秒淡出//Fade out during the last 0.5 seconds
			alpha = s_ToastTimer / 0.5f;
		}

		ImGuiIO& io = ImGui::GetIO();

		//屏幕正下方距离底部 80 像素的位置// Position at the bottom center of the screen, 80 pixels above the bottom edge
		ImVec2 windowPos(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 80.0f);

		// 设置位置居中// Set the position to be centered on the specified point
		ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		// 设置窗口的背景透明度// Set the window background alpha
		ImGui::SetNextWindowBgAlpha(alpha * 0.8f);

		// 去除所有边框、标题栏、交互// Remove all decorations, title bar, and interaction
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().WindowRounding);
		ImVec4 toastText = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		toastText.w *= std::clamp(alpha, 0.0f, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, toastText);

		if (ImGui::Begin("ToastWindow", nullptr, flags)) {
			ImGui::Text("  %s  ", s_ToastMessage.c_str());
		}
		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}




	// ========== project save&load==========
	bool EngineUi::SaveProject(SceneManager* sceneManager, RenderSystem* renderSys, const std::string& filepath) {
		json root;


		//保存实体变换矩阵//Save entity transforms
		json entitiesArray = json::array();
		if (sceneManager) {
			auto& world = sceneManager->get_world();
			world.each([&](flecs::entity e, const LocalTransform& lt) {
				std::string name = e.name().length() > 0 ? e.name().c_str() : "";

				if (!name.empty() && name != "Preview_Object") {
					json entityJson;
					entityJson["Name"] = name;

					//用于接收拆解结果的变量
					glm::vec3 scale;
					glm::quat rotation;
					glm::vec3 translation;
					glm::vec3 skew;
					glm::vec4 perspective;

					//拆解矩阵// Decompose the matrix into TRS components
					glm::decompose(lt.matrix, scale, rotation, translation, skew, perspective);

					// To度数// Convert quaternion to Euler angles in degrees for better readability
					glm::vec3 eulerRotation = glm::degrees(glm::eulerAngles(rotation));

					//存入 JSON//Store in JSON
					entityJson["Position"] = Vec3ToJson(translation);
					entityJson["Rotation"] = Vec3ToJson(eulerRotation);
					entityJson["Scale"] = Vec3ToJson(scale);

					entitiesArray.push_back(entityJson);
				}
				});
		}
		root["Entities"] = entitiesArray;


		//保存粒子系统 save Particle Systems
		json particlesArray = json::array();
		if (renderSys) {
			auto& particles = renderSys->GetParticles();
			for (size_t i = 0; i < particles.size(); ++i) {
				auto& ps = particles[i];
				const auto& config = ps->config;

				json pJson;
				pJson["Name"] = config.name;
				pJson["Shape"] = (int)ps->getEmitterShape();
				pJson["EmitterPos"] = Vec3ToJson(config.emitterPos);
				pJson["Gravity"] = Vec3ToJson(config.gravity);

				pJson["LifeMin"] = config.lifeMin;
				pJson["LifeMax"] = config.lifeMax;
				pJson["SpeedMin"] = config.speedMin;
				pJson["SpeedMax"] = config.speedMax;
				pJson["SizeMin"] = config.sizeMin;
				pJson["SizeMax"] = config.sizeMax;

				pJson["StartColor"] = Vec4ToJson(config.startColor);
				pJson["EndColor"] = Vec4ToJson(config.endColor);

				pJson["ConeSpread"] = config.coneSpread;
				pJson["SphereRadius"] = config.sphereRadius;
				pJson["UseTexture"] = config.useTexture;
				pJson["AnimateAtlas"] = config.animateAtlas;
				pJson["AtlasCols"] = config.atlasCols;
				pJson["AtlasRows"] = config.atlasRows;

				pJson["MaxParticles"] = ps->count();

				particlesArray.push_back(pJson);
			}
		}
		root["Particles"] = particlesArray;


		// 写入文件// Write to file
		std::ofstream file(filepath);
		if (file.is_open()) {
			file << root.dump(4);
			file.close();
			if (!file) return false;
            LogPrintf("Scene snapshot saved to %s\n", filepath.c_str());
            return true;
		}
		else {
			LogPrintf("[Error] Could not open %s for saving.\n", filepath.c_str());
            return false;
		}
	}

	bool EngineUi::LoadProject(SceneManager* sceneManager, RenderSystem* renderSys, const std::string& filepath) {
		std::ifstream file(filepath);
		if (!file.is_open()) {
			LogPrintf("[Serialization] No save file found at %s\n", filepath.c_str());
			return false;
		}

		json root;
		try {
			file >> root;
		}
		catch (json::parse_error& e) {
			LogPrintf("[Error] JSON parsing failed: %s\n", e.what());
			return false;
		}

		//读取并应用实体// Read and apply entities

		if (sceneManager && root.contains("Entities")) {
			auto& world = sceneManager->get_world();
			for (const auto& entityJson : root["Entities"]) {
				std::string name = entityJson["Name"].get<std::string>();
				flecs::entity e = world.lookup(name.c_str());

				if (e.is_alive() && e.has<LocalTransform>()) {
					//读取TRS// Read TRS components
					glm::vec3 pos = JsonToVec3(entityJson["Position"]);
					glm::vec3 eulerRot = JsonToVec3(entityJson["Rotation"]);
					glm::vec3 scale = JsonToVec3(entityJson["Scale"]);

					glm::mat4 reconstructedMat(1.0f);
					reconstructedMat = glm::translate(reconstructedMat, pos);

					reconstructedMat *= glm::mat4_cast(glm::quat(glm::radians(eulerRot)));
					reconstructedMat = glm::scale(reconstructedMat, scale);

					//更新Update
					auto* lt = &e.get_mut<LocalTransform>();
					lt->matrix = reconstructedMat;
					e.modified<LocalTransform>();

					//update physics 
					if (e.has<PhysicsBody>()) {
						auto pb = e.get<PhysicsBody>();
						auto* physics = sceneManager->get_physics_system();
						if (physics) {
							physics->set_body_transform(pb.bodyID, reconstructedMat);
						}
					}
				}
			}
		}


		// read粒子系统 (Particles)
		if (renderSys && root.contains("Particles")) {
			const auto& particlesJson = root["Particles"];
			auto& particles = renderSys->GetParticles();


			for (size_t i = 0; i < particlesJson.size(); ++i) {

				if (i >= particles.size()) {
					renderSys->AddParticleGroup();
				}

				const auto& pJson = particlesJson[i];
				auto& ps = particles[i];
				auto& config = ps->config;


				if (pJson.contains("Name")) {
					std::string n = pJson["Name"].get<std::string>();
					snprintf(config.name, sizeof(config.name), "%s", n.c_str());
				}
				if (pJson.contains("Shape")) ps->setEmitterShape((EmitterShape)pJson["Shape"].get<int>());
				if (pJson.contains("EmitterPos")) config.emitterPos = JsonToVec3(pJson["EmitterPos"]);
				if (pJson.contains("Gravity")) config.gravity = JsonToVec3(pJson["Gravity"]);

				if (pJson.contains("LifeMin")) config.lifeMin = pJson["LifeMin"].get<float>();
				if (pJson.contains("LifeMax")) config.lifeMax = pJson["LifeMax"].get<float>();
				if (pJson.contains("SpeedMin")) config.speedMin = pJson["SpeedMin"].get<float>();
				if (pJson.contains("SpeedMax")) config.speedMax = pJson["SpeedMax"].get<float>();
				if (pJson.contains("SizeMin")) config.sizeMin = pJson["SizeMin"].get<float>();
				if (pJson.contains("SizeMax")) config.sizeMax = pJson["SizeMax"].get<float>();

				if (pJson.contains("StartColor")) config.startColor = JsonToVec4(pJson["StartColor"]);
				if (pJson.contains("EndColor")) config.endColor = JsonToVec4(pJson["EndColor"]);

				if (pJson.contains("ConeSpread")) config.coneSpread = pJson["ConeSpread"].get<float>();
				if (pJson.contains("SphereRadius")) config.sphereRadius = pJson["SphereRadius"].get<float>();
				if (pJson.contains("UseTexture")) config.useTexture = pJson["UseTexture"].get<int>();
				if (pJson.contains("AnimateAtlas")) config.animateAtlas = pJson["AnimateAtlas"].get<bool>();

				if (pJson.contains("MaxParticles")) {
					uint32_t savedMax = pJson["MaxParticles"].get<uint32_t>();
					if (savedMax != ps->count()) {
						renderSys->ResizeParticleGroup(i, savedMax);
					}
				}
			}
		}

		LogPrintf("Scene snapshot loaded from %s\n", filepath.c_str());
        return true;
	}



	// ========== Scene Viewport 场景视口==========
	static ImVec2 s_SceneViewportSize = ImVec2(1280.0f, 720.0f); // 默认尺寸
	static ImVec2 s_SceneViewportPos = ImVec2(0.0f, 0.0f);//默认位置
	static ImDrawList* s_SceneViewportDrawList = nullptr;
	ImVec2 EngineUi::GetSceneViewportSize() {
		return s_SceneViewportSize;
	}
	ImVec2 EngineUi::GetSceneViewportPos() {
		return s_SceneViewportPos;
	}

	ImDrawList* EngineUi::GetSceneViewportDrawList() {
		return s_SceneViewportDrawList;
	}


	void EngineUi::DrawSceneViewport(VkDescriptorSet sceneTexId, RenderSystem* renderSys, SceneManager* sceneManager, EditorSceneAdapter& editorScene, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id, EditorState& editor, const RenderSettings& render) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, editor.showEngineUi ? 1.0f : 0.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, editor.showEngineUi
			? editor_theme::Color(editor_theme::kViewport) : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

		const char* viewportWindowName = editor.showEngineUi ? "Scene Viewport###SceneViewportEditor": "Game View###SceneViewportFullscreen";
		ImGuiWindowFlags viewportFlags = 0;
		if (!editor.showEngineUi) {
			ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowViewport(mainViewport->ID);
			ImGui::SetNextWindowPos(mainViewport->Pos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(mainViewport->Size, ImGuiCond_Always);
			ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
			viewportFlags = ImGuiWindowFlags_NoDecoration
				| ImGuiWindowFlags_NoDocking
				| ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoBringToFrontOnFocus
				| ImGuiWindowFlags_NoNavFocus;
		}

		if (!ImGui::Begin(viewportWindowName, nullptr, viewportFlags)) {
            editor.isSceneViewportHovered = false;
            s_SceneViewportDrawList = nullptr;
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
		s_SceneViewportDrawList = ImGui::GetWindowDrawList();

		// 1. 获取视口绝大坐标和尺寸// Get the absolute position and size of the viewport
		ImVec2 vMin = ImGui::GetWindowContentRegionMin();
		ImVec2 vPos = ImGui::GetWindowPos();
		s_SceneViewportPos = ImVec2(vMin.x + vPos.x, vMin.y + vPos.y);

		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		if (viewportSize.x > 10.0f && viewportSize.y > 10.0f) {
			s_SceneViewportSize = viewportSize;
		}

		// 2. 绘制 3D 画面//	Draw the 3D scene (as an ImGui image)
		if (sceneTexId != VK_NULL_HANDLE) {
			ImGui::Image((ImTextureID)sceneTexId, viewportSize);
		}
		else {
			ImGui::Dummy(viewportSize);
		}

		//  3. 拖放目标 (Drag & Drop)

		if (editor.showEngineUi && ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
				const char* droppedPath = (const char*)payload->Data;
				glm::mat4 dropTransform(1.0f);
				if (!TryBuildViewportDropTransform(ImGui::GetMousePos(), s_SceneViewportPos, s_SceneViewportSize, sceneManager, view, proj, droppedPath, dropTransform)) {
					if (renderSys) {
						renderSys->ClearModelPreview();
					}
				}
				else if (!payload->IsDelivery()) {
					if (renderSys) {
						renderSys->SetModelPreview(droppedPath, dropTransform);
					}
				}
				else if (renderSys) {
					renderSys->ClearModelPreview();
					flecs::entity spawnedEntity = SpawnDroppedModel(renderSys, sceneManager, droppedPath, dropTransform);
					if (spawnedEntity.is_valid()) {
						selected_id = spawnedEntity.id();
						LogPrint("[DragDrop] Deployed %s\n", droppedPath);
						EngineUi::ShowToast("[ Asset Deployed ]");
					}
					else {
						LogPrint("[DragDrop] Failed to deploy %s\n", droppedPath);
						EngineUi::ShowToast("[ Asset Deploy Failed ]");
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		else {
			if (!ImGui::GetDragDropPayload() && renderSys) renderSys->ClearModelPreview();
		}


		//  4. 粒子 Billboard 图标和点击// Particle billboard icons and select
		if (editor.showEngineUi && renderSys && render.particlesEnabled && editor.renderMode == 0) {
			glm::mat4 viewProj = proj * view;
			glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			//ImGui 裁剪// ImGui clipping
			ImVec2 clipMin = s_SceneViewportPos;
			ImVec2 clipMax = ImVec2(s_SceneViewportPos.x + s_SceneViewportSize.x, s_SceneViewportPos.y + s_SceneViewportSize.y);
			drawList->PushClipRect(clipMin, clipMax, true);

			const ImVec2 iconSize = ImVec2(24.0f, 24.0f);
			const float  hitRadius = 16.0f;

			auto& particles = renderSys->GetParticles();
			for (int i = 0; i < particles.size(); ++i) {
				glm::vec3 emitterPos = particles[i]->config.emitterPos;

				// 剔除摄像机背后的粒子// Cull particles behind the camera
				glm::vec3 pointToCam = emitterPos - cameraPos;
				if (glm::dot(pointToCam, pointToCam) < 0.01f) continue;
				glm::vec4 ndc = viewProj * glm::vec4(emitterPos, 1.0f);
				if (ndc.w <= 0.0f) continue;
				ndc /= ndc.w;

				// 算出图标在屏幕上的绝对位置// Calculate the absolute screen position of the icon
				ImVec2 screenPos;
				screenPos.x = s_SceneViewportPos.x + (ndc.x + 1.0f) * 0.5f * s_SceneViewportSize.x;
				screenPos.y = s_SceneViewportPos.y + (1.0f - ndc.y) * 0.5f * s_SceneViewportSize.y;

				//  绘制图标 // Draw the icon
				ImVec2 iconMin = ImVec2(screenPos.x - iconSize.x * 0.5f, screenPos.y - iconSize.y * 0.5f);
				ImVec2 iconMax = ImVec2(screenPos.x + iconSize.x * 0.5f, screenPos.y + iconSize.y * 0.5f);

				bool is_selected = (editor.activeParticleIndex == i);
				ImU32 iconColor = editor_theme::ColorU32(editor_theme::kPaper);

				//从粒子 config 里拿贴图 ID// Get the texture ID from the particle config
				VkDescriptorSet currentIconTex = particles[i]->config.uiIconDescriptor;


				const char* fixedTexPath = cfg::ParticleTextures[5];
				if (renderSys->particleImGuiTextureDict.count(fixedTexPath)) {
					currentIconTex = renderSys->particleImGuiTextureDict[fixedTexPath];
				}

				if (currentIconTex != VK_NULL_HANDLE) {
					drawList->AddImage((ImTextureID)currentIconTex, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, -1));
				}
				else {
					// Fallback显示红圈 P// Fallback: draw a red circle with "P" if no texture is available
					drawList->AddCircleFilled(screenPos, hitRadius * 0.5f, iconColor);
					drawList->AddText(ImVec2(screenPos.x - 4, screenPos.y - 7), editor_theme::ColorU32(editor_theme::kText), "P");
				}
				drawList->AddCircle(screenPos, hitRadius, editor_theme::ColorU32(
					is_selected ? editor_theme::kAccent : editor_theme::kBorder), 0, is_selected ? 2.0f : 1.0f);


				//InvisibleButton

				// 光标移动到图标左上角
				ImGui::SetCursorScreenPos(ImVec2(screenPos.x - hitRadius, screenPos.y - hitRadius));

				ImGui::PushID(i);

				//InvisibleButton
				if (ImGui::InvisibleButton("ParticleIcon", ImVec2(hitRadius * 2, hitRadius * 2))) {
					editor.activeParticleIndex = i;
                    editor.showParticlePanel = true;
					selected_id = 0; // clear
					LogPrint("[BillboardPick] Hit Particle Group %d\n", i + 1);
				}

				// 悬停状态//hover tooltip
				if (ImGui::IsItemHovered()) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					ImGui::BeginTooltip();
					ImGui::Text("[ Particle Group %d ]", i + 1);
					ImGui::Text("Click to inspect particle settings");
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}

			//结束裁剪// End clipping
			drawList->PopClipRect();
		}


		//  5. ImGuizmo 坐标轴
		bool drawGizmo = false;
		glm::mat4 gizmoMatrix = glm::mat4(1.0f);
		if (editor.showEngineUi && renderSys && render.particlesEnabled && editor.activeParticleIndex >= 0 && editor.activeParticleIndex < renderSys->GetParticles().size()) {
			auto& particle = *renderSys->GetParticles()[editor.activeParticleIndex];
            drawGizmo = !renderSys->IsParticleGroupSceneControlled(editor.activeParticleIndex) &&
                (particle.getEmitterShape() != EmitterShape::Sphere || particle.config.triggerControlled);
            gizmoMatrix = glm::translate(glm::mat4(1.0f), particle.config.emitterPos);
		}
		else if (editor.showEngineUi && selected_id != 0 && editorScene.Scene()) {
			const EditorEntitySnapshot selectedEntity = editorScene.Inspect(selected_id);
			if (selectedEntity.alive && selectedEntity.hasTransform) {
				drawGizmo = true;
				gizmoMatrix = selectedEntity.transform;
                if (m_current_inspected_id != selectedEntity.id || (!ImGuizmo::IsUsing() && !ImGui::IsAnyItemActive()))
                    drawGizmo = SyncTransformCache(selectedEntity.id, gizmoMatrix);
			}
		}

		if (drawGizmo) {
			ImGuizmo::BeginFrame();

			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetRect(s_SceneViewportPos.x, s_SceneViewportPos.y, s_SceneViewportSize.x, s_SceneViewportSize.y);

			static ImGuizmo::OPERATION currentOp = ImGuizmo::TRANSLATE;
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().KeyCtrl && !ImGui::IsAnyItemActive() && !(editor.showGameUiEditor && UIEditorWindow::WantsKeyboardCapture())) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) currentOp = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) currentOp = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOp = ImGuizmo::SCALE;
            }
            if (editor.activeParticleIndex >= 0) currentOp = ImGuizmo::TRANSLATE;

			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), currentOp, ImGuizmo::LOCAL, glm::value_ptr(gizmoMatrix));

			if (ImGuizmo::IsUsing()) {
				if (editor.activeParticleIndex >= 0) {
					renderSys->GetParticles()[editor.activeParticleIndex]->config.emitterPos = glm::vec3(gizmoMatrix[3]);
				}
				else if (selected_id != 0 && sceneManager) {
                    editorScene.SetTransform(selected_id, gizmoMatrix);
                    SyncTransformCache(selected_id, gizmoMatrix);
				}
			}
		}

		editor.isSceneViewportHovered = ImGui::IsWindowHovered();

		ImGui::End(); //  Viewport 闭合
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		ImGui::PopStyleVar();
	}


	void EngineUi::DrawContentBrowser(RenderSystem* renderSys, SceneManager* sceneManager, EditorState& editor) {
        if (!editor.showContentBrowser) return;
		(void)sceneManager; //The browser only needs asset browsing and drag/drop here.
		static std::string s_CurrentDirectory = "Assets"; //当前浏览的文件夹路径//Track the active folder inside Assets.
		static char s_NewFolderName[128] = "NewFolder"; //Buffer for the create-folder popup.
		static std::string s_CreateFolderError; //Show inline folder-creation errors in the popup.
		const fs::path assetsRoot("Assets");
		const std::string folderIconPath = "Assets/Textures/Folder.png"; //文件夹图标// Use Folder.png as the shared folder thumbnail source.

		// 设置初始窗口大小和位置



		if (ImGui::Begin(PanelTitle("Assets", "ContentBrowser").c_str(), &editor.showContentBrowser)) //开始绘制窗口// Begin drawing the window
		{
			if (!fs::exists(assetsRoot) || !fs::is_directory(assetsRoot)) { //Fail clearly when the Assets root is missing.
				ImGui::TextColored(editor_theme::Color(editor_theme::kError), "Folder Assets not found!");
			}
			else {
				if (!fs::exists(fs::path(s_CurrentDirectory)) || !fs::is_directory(fs::path(s_CurrentDirectory))) { //Reset to Assets if the current folder was removed.
					s_CurrentDirectory = NormalizeAssetBrowserPath(assetsRoot);
				}

				if (ImGui::BeginTable("ContentBrowserLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
					ImGui::TableSetupColumn("FolderTree", ImGuiTableColumnFlags_WidthFixed, 160.0f);
					ImGui::TableSetupColumn("AssetGrid", ImGuiTableColumnFlags_WidthStretch);

					ImGui::TableNextColumn();
					ImGui::BeginChild("ContentBrowserTree");
					ImGui::TextDisabled("Assets");
					ImGui::Separator();
					DrawAssetDirectoryTreeNode(assetsRoot, s_CurrentDirectory); //Show the full Assets folder tree instead of only Models.
					ImGui::EndChild();

					ImGui::TableNextColumn();
					ImGui::BeginChild("ContentBrowserGrid");

					const fs::path currentDirectoryPath(s_CurrentDirectory);
					const std::string currentDirectoryLabel = NormalizeAssetBrowserPath(currentDirectoryPath);

					ImGui::Text("Current: %s", currentDirectoryLabel.c_str()); //Display the current folder path at the top.
					if (currentDirectoryLabel != "Assets") {
						ImGui::SameLine();
						if (ImGui::Button("Up")) //返回上一级目录//Jump to the parent directory.
						{
							s_CurrentDirectory = NormalizeAssetBrowserPath(currentDirectoryPath.parent_path()); //Jump back to the parent directory quickly.
						}
					}

					//创建文件夹部分========// Create folder section ========
					ImGui::SameLine();
					if (ImGui::Button("New Folder")) //创建新文件夹// Create a new folder in the current directory.
					{
						std::snprintf(s_NewFolderName, IM_ARRAYSIZE(s_NewFolderName), "%s", "NewFolder");
						s_CreateFolderError.clear();
						ImGui::OpenPopup("CreateAssetFolderPopup"); //Create folders directly from the browser//创建文件夹直接从浏览器
					}

					if (ImGui::BeginPopupModal("CreateAssetFolderPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						ImGui::Text("Create folder in %s", currentDirectoryLabel.c_str());
						ImGui::InputText("Folder Name", s_NewFolderName, IM_ARRAYSIZE(s_NewFolderName));
						if (!s_CreateFolderError.empty()) {
							ImGui::TextColored(editor_theme::Color(editor_theme::kError), "%s", s_CreateFolderError.c_str());
						}

						if (ImGui::Button("Create")) {
							std::string createdDirectoryPath;
							if (TryCreateAssetFolder(currentDirectoryPath, s_NewFolderName, createdDirectoryPath, s_CreateFolderError)) {
								s_CurrentDirectory = createdDirectoryPath;
								EngineUi::ShowToast("[ Folder Created ]"); //Confirm creation and move into the new folder.
								ImGui::CloseCurrentPopup();
							}
						}

						ImGui::SameLine();
						if (ImGui::Button("Cancel")) {
							s_CreateFolderError.clear();
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					ImGui::Separator();
					//创建文件夹部分========//	End create folder section

					//扫描当前目录下的文件和文件夹// Enumerate folders and files in the current directory
					std::vector<fs::directory_entry> directoryEntries;
					std::vector<fs::directory_entry> fileEntries;
					std::error_code ec;
					for (const auto& entry : fs::directory_iterator(currentDirectoryPath, ec)) { // Enumerate both folders and files in the active directory.
						if (ec) {
							break;
						}
						if (entry.is_directory()) {
							directoryEntries.push_back(entry);//文件夹//fileder
						}
						else if (entry.is_regular_file()) {
							fileEntries.push_back(entry);//文件//file
						}
					}

					//按名字排序 Keep folders and files sorted by name for easier browsing.
					auto sortByName = [](const fs::directory_entry& lhs, const fs::directory_entry& rhs) {
						return ToLowerCopy(lhs.path().filename().string()) < ToLowerCopy(rhs.path().filename().string());
						};
					std::sort(directoryEntries.begin(), directoryEntries.end(), sortByName); //Keep folders sorted before files.
					std::sort(fileEntries.begin(), fileEntries.end(), sortByName); //Keep files sorted for stable browsing.

					// 设定格子的宽度// Set the width of each cell
					const float cellSize = 118.0f; //Use a consistent tile size for all assets.
					const float panelWidth = ImGui::GetContentRegionAvail().x;
					const int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

					if (ImGui::BeginTable("ContentGrid", columnCount)) //创建一个动态列数的表格 Create a table with a dynamic number of columns based on available width
					{
						auto drawAssetEntry = [&](const fs::path& entryPath, bool isDirectoryEntry) {
							ImGui::TableNextColumn();

							const std::string normalizedPath = NormalizeAssetBrowserPath(entryPath);
							const std::string filename = entryPath.filename().string();
							const std::string tileLabel = GetAssetTileLabel(entryPath);
							VkDescriptorSet previewDescriptor = VK_NULL_HANDLE;

							if (isDirectoryEntry && renderSys) {
								previewDescriptor = renderSys->GetContentBrowserThumbnail(folderIconPath); // Every folder reuses Folder.png instead of the old custom icon.
							}
							else if (!isDirectoryEntry && renderSys) {
								previewDescriptor = renderSys->GetContentBrowserThumbnail(normalizedPath); // Files still use the unified thumbnail pipeline.
							}

							ImGui::PushID(normalizedPath.c_str());
							bool tilePressed = false; //Share click handling between folders and file tiles.
							// 画文件图标按钮// Draw the icon button
							if (previewDescriptor != VK_NULL_HANDLE) {
								const bool shouldFlipThumbnail =
									isDirectoryEntry || IsTextureAssetPath(entryPath);

								const ImVec2 uv0 = shouldFlipThumbnail
									? ImVec2(0.0f, 1.0f)
									: ImVec2(0.0f, 0.0f);

								const ImVec2 uv1 = shouldFlipThumbnail
									? ImVec2(1.0f, 0.0f)
									: ImVec2(1.0f, 1.0f);

								ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));//按钮背景透明
								ImGui::PushStyleColor(ImGuiCol_ButtonHovered, editor_theme::Color(editor_theme::kHover));
								ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));

								ImGui::ImageButton(
									filename.c_str(),
									(ImTextureID)previewDescriptor,
									ImVec2(100.0f, 100.0f),
									uv0,
									uv1
								);

								ImGui::PopStyleColor(3); //Push 了 3 个颜色，所以要 Pop 3 个

								tilePressed = ImGui::IsItemClicked();
							}
							else {
								ImGui::Button(tileLabel.c_str(), ImVec2(100.0f, 100.0f));
								tilePressed = ImGui::IsItemClicked();
							}

							if (isDirectoryEntry && tilePressed) {
								s_CurrentDirectory = normalizedPath;
							}

							if (!isDirectoryEntry && IsModelAssetPath(entryPath) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) //拖拽模型文件时才设定拖拽源 Only set the drag source for model files to avoid confusion when dragging non-model assets
							{
								ImGui::SetDragDropPayload("CONTENT_BROWSER_MODEL", normalizedPath.c_str(), normalizedPath.size() + 1); //model drag-and-drop into the scene.
								ImGui::Text("Drop %s to Scene", filename.c_str());
								if (previewDescriptor) {
									ImGui::Image((ImTextureID)previewDescriptor, ImVec2(40.0f, 40.0f));
								}
								ImGui::EndDragDropSource();
							}

							if (ImGui::IsItemHovered()) //停留时显示完整路径 Hovering shows the full path as a tooltip to help distinguish similarly named assets.
							{
								ImGui::SetTooltip("%s", normalizedPath.c_str()); //Show the full path on hover for similarly named assets.
							}

							ImGui::TextWrapped("%s", filename.c_str());//显示文件名 Display the filename below the thumbnail, wrapped if it's too long.
							ImGui::PopID();
							};

						//绘制所有文件夹和文件
						for (const auto& directoryEntry : directoryEntries) {
							drawAssetEntry(directoryEntry.path(), true);
						}
						for (const auto& fileEntry : fileEntries) {
							drawAssetEntry(fileEntry.path(), false);
						}

						ImGui::EndTable();
					}

					ImGui::EndChild();
					ImGui::EndTable();
				}
			}
		}
		ImGui::End();//结束绘制窗口// End the window
	}


	void EngineUi::DrawViewportDropTarget(RenderSystem* renderSys, SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj) {

		bool isDraggingModel = false;
		// 如果没有在拖拽，才清理模型// Only clear the model preview if we're not currently dragging a model
		if (const ImGuiPayload* p = ImGui::GetDragDropPayload()) {
			if (p->IsDataType("CONTENT_BROWSER_MODEL")) isDraggingModel = true;
		}
		else {
			if (renderSys) renderSys->ClearModelPreview();
		}

		ImGuiWindowFlags viewportFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (!isDraggingModel) viewportFlags |= ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		if (ImGui::Begin("ViewportDropTarget", nullptr, viewportFlags)) {
			ImGui::Dummy(ImGui::GetContentRegionAvail());

			if (isDraggingModel && ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL", ImGuiDragDropFlags_AcceptBeforeDelivery)) {

					const char* currentDragPath = (const char*)payload->Data;

					ImVec2 mousePos = ImGui::GetMousePos();
					ImVec2 screenSize = ImGui::GetIO().DisplaySize;
					float ndcX = (2.0f * mousePos.x) / screenSize.x - 1.0f;
					float ndcY = 1.0f - (2.0f * mousePos.y) / screenSize.y;

					glm::mat4 invProjView = glm::inverse(proj * view);
					glm::vec4 nearP = invProjView * glm::vec4(ndcX, ndcY, 0.001f, 1.0f);
					glm::vec4 farP = invProjView * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
					nearP /= nearP.w;
					farP /= farP.w;

					glm::vec3 rayOrigin = glm::vec3(nearP);
					glm::vec3 rayDir = glm::normalize(glm::vec3(farP - nearP));

					glm::vec3 targetPos = rayOrigin + rayDir * 15.0f;
					if (std::abs(rayDir.y) > 0.0001f) {
						float t = -rayOrigin.y / rayDir.y;
						if (t > 0.0f) targetPos = rayOrigin + rayDir * t;
					}

					glm::mat4 previewTransform = glm::translate(glm::mat4(1.0f), targetPos);
					if (strstr(currentDragPath, "Car")) previewTransform = glm::scale(previewTransform, glm::vec3(0.1f));
					else if (strstr(currentDragPath, "Helicopter")) previewTransform = glm::scale(previewTransform, glm::vec3(0.3f));

					if (!payload->IsDelivery()) {
						// 正在拖拽// Still dragging, show the preview
						if (renderSys) {
							renderSys->SetModelPreview(currentDragPath, previewTransform);
							// std::print("[UI] Requesting preview for {}\n", currentDragPath);
						}
					}
					else {
						//松手// Dropped, create the actual model in the scene
						if (renderSys) {
							renderSys->ClearModelPreview();
							float mass = 50.0f;
							/*if (strstr(currentDragPath, "Car")) mass = 1500.0f;
							else if (strstr(currentDragPath, "Helicopter")) mass = 3000.0f;
							else if (strstr(currentDragPath, "BaseballBat")) mass = 1.5f;*/

							//renderSys->load_additional_model(currentDragPath, false, mass, previewTransform);
							EngineUi::ShowToast("[ Real Asset Deployed ]");
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}



    bool EngineUi::SyncTransformCache(flecs::entity_t id, const glm::mat4& matrix) {
        glm::mat4 cached(1.0f);
        ImGuizmo::RecomposeMatrixFromComponents(m_ui_translation, m_ui_rotation, m_ui_scale, glm::value_ptr(cached));
        bool changed = id != m_current_inspected_id;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                changed = changed || std::abs(matrix[col][row] - cached[col][row]) > 0.0001f;
        if (changed) {
            if (!editor_transform::Decompose(matrix, m_ui_translation, m_ui_rotation, m_ui_scale)) return false;
            m_current_inspected_id = id;
        }
        return true;
    }

    void EngineUi::DrawEditorWorkspace() {
        editor_layout::DrawDockspace(s_ResetEditorLayout);
        s_ResetEditorLayout = false;
    }

    void EngineUi::DrawConsole(EditorState& editor) {
        if (editor.showConsole)
            s_Console.Draw(PanelTitle("Console", "OutputConsole").c_str(), &editor.showConsole);
    }

    void EngineUi::DrawRenderSettings(RenderSettings& render, const RenderOverrides& overrides, EditorState& editor) {
        if (!editor.showRenderSettings) return;
        if (ImGui::Begin(PanelTitle("Render Settings", "RenderSettings").c_str(), &editor.showRenderSettings)) {
            ImGui::TextDisabled("Changes apply to the current session.");
            if (editor.renderMode != 0) ImGui::TextWrapped("%s", _SL("Diagnostic views display their rendering data directly. Return to Default to adjust scene effects."));
            ImGui::BeginDisabled(editor.renderMode != 0);
            ImGui::SeparatorText("Rendering");
            ImGui::Checkbox("Image-based Lighting (IBL)", &render.iblEnabled);
            if (overrides.iblEnabled.has_value())
                ImGui::TextDisabled("Scene IBL override: %s", *overrides.iblEnabled ? "On" : "Off");
            ImGui::Checkbox("Screen-space Reflections (SSR)", &render.ssrEnabled);
            ImGui::Checkbox("Ambient Occlusion (SSAO)", &render.ssaoEnabled);
            ImGui::Checkbox(_SL("Particle System"), &render.particlesEnabled);
            ImGui::SeparatorText("Post-processing");
            ImGui::Checkbox(_SL("Enable Bloom"), &render.bloomEnabled);
            ImGui::BeginDisabled(!render.bloomEnabled);
            ImGui::SliderFloat(_SL("Bloom Strength"), &render.bloomStrength, 0.0f, 5.0f, "%.2f");
            ImGui::EndDisabled();
            ImGui::SliderFloat(_SL("Exposure"), &render.bloomExposure, 0.1f, 5.0f, "%.2f");
            ImGui::Checkbox(_SL("Enable Mosaic Post-Process"), &render.mosaicEnabled);
            ImGui::EndDisabled();
            ImGui::SeparatorText("Visibility & detail");
            ImGui::Checkbox("Frustum Culling", &render.frustumCullingEnabled);
            ImGui::BeginDisabled(!render.frustumCullingEnabled);
            ImGui::SliderFloat("Frustum Padding", &render.frustumCullingPadding, 0.0f, 10.0f, "%.2f");
            ImGui::EndDisabled();

        }
        ImGui::End();
    }

    void EngineUi::DrawParticlePanel(RenderSettings& render, EditorState& editor, RenderSystem* renderSys, flecs::entity_t& selected_id) {
        if (!editor.showParticlePanel) return;
        if (!ImGui::Begin(PanelTitle("Particles", "Particles").c_str(), &editor.showParticlePanel)) {
            ImGui::End();
            return;
        }
        if (!renderSys) {
            ImGui::TextDisabled("Renderer unavailable.");
            ImGui::End();
            return;
        }
        ImGui::Checkbox(_SL("Particle System"), &render.particlesEnabled);
        if (!render.particlesEnabled)
            ImGui::TextWrapped("Simulation and rendering are disabled. Parameters can still be edited.");

        static const ParticleSystem* countOwner = nullptr;
        static uint32_t previousCapacity = 0;
        static int newCount = 0;
        auto& particles = renderSys->GetParticles();
        int& selectedParticle = editor.activeParticleIndex;
        if (selectedParticle >= static_cast<int>(particles.size())) selectedParticle = -1;
        if (ImGui::Button(_SL("Add Group"))) {
            renderSys->AddParticleGroup();
            selected_id = 0;
            countOwner = nullptr;
            selectedParticle = static_cast<int>(particles.size()) - 1;
            // A new editor emitter starts independent of the demo's sphere-follow behavior.
            particles[selectedParticle]->setEmitterShape(EmitterShape::Cone);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedParticle < 0);
        if (ImGui::Button(_SL("Delete Group"))) {
            renderSys->RemoveParticleGroup(selectedParticle);
            countOwner = nullptr;
            selectedParticle = -1;
        }
        ImGui::EndDisabled();

        const char* preview = selectedParticle >= 0 ? particles[selectedParticle]->config.name : "Select a particle group";
        if (ImGui::BeginCombo(_SL("Select Particle"), preview)) {
            for (int i = 0; i < static_cast<int>(particles.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(particles[i]->config.name, selectedParticle == i)) { selectedParticle = i; selected_id = 0; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (selectedParticle < 0) {
            ImGui::TextWrapped("Select a group here or click its icon in the scene viewport.");
            ImGui::End();
            return;
        }

        auto& config = particles[selectedParticle]->config;
        ImGui::InputText(_SL("Name"), config.name, IM_ARRAYSIZE(config.name));
        const bool triggerOwned = renderSys->GetTriggerSystem().HasParticleBinding(selectedParticle);
        const bool sceneOwned = renderSys->IsParticleGroupSceneControlled(selectedParticle);
        ImGui::BeginDisabled(triggerOwned || sceneOwned);
        ImGui::Checkbox(_SL("Visible"), &config.isVisible);
        ImGui::EndDisabled();
        if (triggerOwned) ImGui::TextWrapped("Visibility is controlled by a scene trigger.");
        if (sceneOwned) ImGui::TextWrapped("The scene controls this emitter's visibility, shape, position and direction.");
        ImGui::TextWrapped("Initial size, speed, rotation and lifetime apply to newly spawned particles.");

        // Tie the pending count to the selected group and its current capacity.
        const auto* group = particles[selectedParticle].get();
        if (countOwner != group || previousCapacity != group->count()) {
            countOwner = group;
            previousCapacity = group->count();
            newCount = static_cast<int>(previousCapacity);
        }
        ImGui::InputInt(_SL("Max Particles"), &newCount);
        ImGui::BeginDisabled(newCount < 1 || newCount > 100000 || newCount == static_cast<int>(group->count()));
        if (ImGui::Button(_SL("Apply Changes")))
            renderSys->ResizeParticleGroup(selectedParticle, static_cast<uint32_t>(newCount));
        ImGui::EndDisabled();
        if (newCount < 1 || newCount > 100000) ImGui::TextDisabled("Capacity must be 1 - 100000.");

        if (ImGui::CollapsingHeader(_SL("Emitter Settings"), ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool diskEmitter = particles[selectedParticle]->getEmitterShape() == EmitterShape::Disk;
            ImGui::BeginDisabled(diskEmitter);
            ImGui::Checkbox(_SL("Show Debug Wireframe"), &config.particleDebug);
            ImGui::EndDisabled();
            if (diskEmitter) ImGui::TextDisabled("Disk wireframe is not implemented.");
            ImGui::BeginDisabled(sceneOwned);
            const char* shapeNames[] = { "Cone", "Sphere", "Box", "Disk" };
            int currentShape = static_cast<int>(particles[selectedParticle]->getEmitterShape());
            if (ImGui::Combo(_SL("Emitter Shape"), &currentShape, shapeNames, IM_ARRAYSIZE(shapeNames)))
                particles[selectedParticle]->setEmitterShape(static_cast<EmitterShape>(currentShape));
            const bool followsDemoEntity = currentShape == static_cast<int>(EmitterShape::Sphere) && !config.triggerControlled;
            ImGui::BeginDisabled(followsDemoEntity);
            ImGui::DragFloat3(_SL("Emitter Pos"), &config.emitterPos.x, 0.1f);
            ImGui::EndDisabled();
            if (followsDemoEntity)
                ImGui::TextWrapped("This sphere emitter is driven by the demo's bat. Use Cone, Box or Disk for an independent emitter.");
            if (currentShape == static_cast<int>(EmitterShape::Cone))
                ImGui::SliderFloat(_SL("Cone Spread"), &config.coneSpread, 0.01f, 3.14f);
            if (currentShape == static_cast<int>(EmitterShape::Sphere) || currentShape == static_cast<int>(EmitterShape::Disk))
                ImGui::SliderFloat(_SL("Sphere Radius"), &config.sphereRadius, 0.01f, 10.0f);
            if (currentShape == static_cast<int>(EmitterShape::Box))
                ImGui::DragFloat3(_SL("Box Area"), &config.boxArea.x, 0.1f, 0.1f, 100.0f);
            if (currentShape == static_cast<int>(EmitterShape::Cone) || currentShape == static_cast<int>(EmitterShape::Disk)) {
                if (ImGui::DragFloat3("Emission Direction", &config.emitDir.x, 0.01f, -1.0f, 1.0f))
                    config.emitDir = NormalizeOrFallback(config.emitDir, glm::vec3(0, 1, 0));
            }
            ImGui::EndDisabled();
        }
        if (ImGui::CollapsingHeader(_SL("Physics & Movement"))) {
            ImGui::DragFloat3(_SL("Gravity"), &config.gravity.x, 0.001f);
            ImGui::DragFloatRange2("Speed (min / max)", &config.speedMin, &config.speedMax, 0.1f, 0.0f, 20.0f);
            ImGui::DragFloatRange2("Rotation (min / max)", &config.rotationMin, &config.rotationMax, 1.0f, -360.0f, 360.0f);
        }
        if (ImGui::CollapsingHeader(_SL("Appearance & Color"))) {
            ImGui::DragFloatRange2("Size (min / max)", &config.sizeMin, &config.sizeMax, 1.0f, 1.0f, 1000.0f);
            ImGui::SliderFloat(_SL("Start Size Scale"), &config.startSizeScale, 0.0f, 10.0f);
            ImGui::SliderFloat(_SL("End Size Scale"), &config.endSizeScale, 0.0f, 10.0f);
            ImGui::ColorEdit4(_SL("Start Color"), &config.startColor.x, ImGuiColorEditFlags_AlphaBar);
            ImGui::ColorEdit4(_SL("End Color"), &config.endColor.x, ImGuiColorEditFlags_AlphaBar);
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
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
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
                        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + 64.0f + ImGui::GetStyle().FramePadding.x * 2.0f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x && i != texNames.size() - 1) {
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



        }
        if (ImGui::CollapsingHeader(_SL("Life Cycle"))) {
            ImGui::DragFloatRange2("Lifetime (min / max)", &config.lifeMin, &config.lifeMax, 0.1f, 0.1f, 10.0f);
        }
        ImGui::End();
    }

	void EngineUi::DrawLightPanel(SceneManager* sceneManager, EditorState& editor)
	{
		if (!editor.showLightPanel) return;



		if (!ImGui::Begin(PanelTitle("Lighting", "Lighting").c_str(), &editor.showLightPanel)) {
			ImGui::End();
			return;
		}

		if (!sceneManager) {
			ImGui::TextDisabled("SceneManager unavailable.");
			ImGui::End();
			return;
		}

		auto& world = sceneManager->get_world();
		std::vector<flecs::entity_t> lightIds;

		world.each([&](flecs::entity entity, LightComponent&) {
			lightIds.push_back(entity.id());
			});

		if (lightIds.empty()) {
			ImGui::TextDisabled("No light entities found.");
			m_selected_light_id = 0;
			ImGui::End();
			return;
		}

		flecs::entity selectedLight = world.entity(m_selected_light_id);
		if (m_selected_light_id == 0 || !selectedLight.is_alive() || !selectedLight.has<LightComponent>()) {
			m_selected_light_id = lightIds.front();
			selectedLight = world.entity(m_selected_light_id);
		}

		const char* currentLightName = selectedLight.name();
		if (!currentLightName || currentLightName[0] == '\0') currentLightName = "Unnamed Light";

		if (ImGui::BeginCombo("Light Entity", currentLightName)) {
			for (flecs::entity_t id : lightIds) {
				flecs::entity lightEntity = world.entity(id);
				const char* lightName = lightEntity.name();
				if (!lightName || lightName[0] == '\0') lightName = "Unnamed Light";

				bool isSelected = (m_selected_light_id == id);
				if (ImGui::Selectable(lightName, isSelected)) {
					m_selected_light_id = id;
					selectedLight = lightEntity;
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if (!selectedLight.is_alive() || !selectedLight.has<LightComponent>()) {
			ImGui::TextDisabled("Selected light is no longer valid.");
			ImGui::End();
			return;
		}

		LightComponent* lightComponent = &selectedLight.get_mut<LightComponent>();
		EntityStatus* entityStatus = selectedLight.has<EntityStatus>() ? &selectedLight.get_mut<EntityStatus>() : nullptr;
		LocalTransform* localTransform = selectedLight.has<LocalTransform>() ? &selectedLight.get_mut<LocalTransform>() : nullptr;

		ImGui::Separator();
		ImGui::TextColored(editor_theme::Color(editor_theme::kText), "[ %s ]", selectedLight.name().c_str());

		if (entityStatus) {
			ImGui::Checkbox("Visible", &entityStatus->should_render);
		}

		const char* lightTypeNames[] = { "Directional", "Point", "Spot" };
		int lightTypeIndex = static_cast<int>(lightComponent->type);
		if (ImGui::Combo("Type", &lightTypeIndex, lightTypeNames, IM_ARRAYSIZE(lightTypeNames))) {
			lightComponent->type = static_cast<LightType>(lightTypeIndex);
			selectedLight.modified<LightComponent>();
		}

		bool lightChanged = false;
		lightChanged |= ImGui::ColorEdit3("Color", &lightComponent->color.x);
		lightChanged |= ImGui::DragFloat("Intensity", &lightComponent->intensity, 0.1f, 0.0f, 100.0f, "%.2f");

		if (lightComponent->type != LightType::Directional) {
			lightChanged |= ImGui::DragFloat("Range", &lightComponent->range, 0.1f, 0.0f, 500.0f, "%.2f");
		}

		if (localTransform && lightComponent->type == LightType::Directional) {
			//light UI（备注）方向光把方向存放在 transform 的第 4 列
			ImGui::TextWrapped("Direction changes direct lighting. Cascade shadows currently use a fixed sun direction.");
            glm::vec3 dir = NormalizeOrFallback(glm::vec3(localTransform->matrix[3]), glm::vec3(0.0f, 1.0f, 0.0f));
			if (ImGui::DragFloat3("Direction", &dir.x, 0.01f, -1.0f, 1.0f, "%.3f")) {
				dir = NormalizeOrFallback(dir, glm::vec3(0.0f, 1.0f, 0.0f));
				localTransform->matrix[3] = glm::vec4(dir, 0.0f);
				selectedLight.modified<LocalTransform>();
			}
		}
		else if (localTransform) {
			//light UI（备注）点光和聚光灯直接调位置
			glm::vec3 position = glm::vec3(localTransform->matrix[3]);
			if (ImGui::DragFloat3("Position", &position.x, 0.1f)) {
				localTransform->matrix[3] = glm::vec4(position, 1.0f);
				selectedLight.modified<LocalTransform>();
			}
		}

		if (lightComponent->type == LightType::Spot) {
			//light UI（备注）聚光灯额外暴露方向和锥角参数
			glm::vec3 direction = lightComponent->direction;
			if (ImGui::DragFloat3("Spot Direction", &direction.x, 0.01f, -1.0f, 1.0f, "%.3f")) {
				lightComponent->direction = NormalizeOrFallback(direction, glm::vec3(0.0f, 0.0f, -1.0f));
				lightChanged = true;
			}

			float innerCutOff = lightComponent->innerCutOff;
			float outerCutOff = lightComponent->outerCutOff;
			if (ImGui::SliderFloat("Inner Cutoff", &innerCutOff, 0.0f, 89.0f, "%.1f deg")) {
				lightComponent->innerCutOff = std::min(innerCutOff, lightComponent->outerCutOff);
				lightChanged = true;
			}
			if (ImGui::SliderFloat("Outer Cutoff", &outerCutOff, 0.0f, 89.0f, "%.1f deg")) {
				lightComponent->outerCutOff = std::max(outerCutOff, lightComponent->innerCutOff);
				lightChanged = true;
			}
		}

		if (lightChanged) {
			selectedLight.modified<LightComponent>();
		}

		ImGui::Separator();

		ImGui::End();
	}

	//camera UI相机调节面板
    void EngineUi::DrawCameraPanel(CameraController& controller, const PlayerState& player, EditorState& editor)
    {
        if (!editor.showCameraPanel) return;
        if (!ImGui::Begin(PanelTitle("Camera", "Camera").c_str(), &editor.showCameraPanel)) {
            ImGui::End();
            return;
        }
        const CameraState& camera = controller.State();
        const bool takenOver = camera.mode == CameraMode::Portal || camera.mode == CameraMode::Cinematic;
        if (camera.mode == CameraMode::Cinematic) ImGui::TextDisabled("Cinematic camera controls the view.");
        else if (camera.mode == CameraMode::Portal) ImGui::TextDisabled("Portal transition controls the view.");
        ImGui::BeginDisabled(takenOver);
        bool follow = camera.thirdPersonMode;
        if (ImGui::Checkbox("Third Person Mode", &follow)) {
            if (follow) controller.RequestFollow();
            else controller.RequestFree();
        }
        bool automaticFov = camera.automaticFov;
        if (ImGui::Checkbox("Automatic FOV", &automaticFov)) controller.UseAutomaticFov(automaticFov);
        float fov = camera.targetFov;
        if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f, "%.1f deg")) controller.SetFov(fov);
        ImGui::TextDisabled("Current FOV: %.1f degrees", camera.cameraFov);
        ImGui::TextWrapped("Adjusting FOV selects a manual lens. Enable Automatic FOV to follow camera distance and speed.");
        ImGui::EndDisabled();

        const glm::vec3 cameraPos(camera.camera2world[3]);
        ImGui::Text("Camera Pos: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy##cam")) {
            char text[128];
            snprintf(text, sizeof(text), "glm::vec3(%.2ff, %.2ff, %.2ff)", cameraPos.x, cameraPos.y, cameraPos.z);
            ImGui::SetClipboardText(text);
        }
        ImGui::Text("Player Pos: %.2f, %.2f, %.2f", player.position.x, player.position.y, player.position.z);
        if (camera.thirdPersonMode) {
            ImGui::Text("Follow target: %.2f, %.2f, %.2f", camera.followTargetPos.x, camera.followTargetPos.y, camera.followTargetPos.z);
            ImGui::BeginDisabled(takenOver || player.isExtremeSpeed);
            float yaw = camera.targetYaw, pitch = camera.targetPitch, distance = camera.targetDistance;
            bool changed = ImGui::SliderAngle("Yaw", &yaw, -180.0f, 180.0f);
            changed |= ImGui::SliderAngle("Pitch", &pitch, -85.0f, 85.0f);
            changed |= ImGui::SliderFloat("Distance", &distance, 2.0f, 70.0f, "%.2f");
            if (changed) controller.SetOrbit(yaw, pitch, distance);
            if (ImGui::Button("Reset Third Person Camera")) controller.SetOrbit(0.0f, 0.0f, 5.0f, true);
            if (changed || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) controller.HoldAutoAlign();
            ImGui::EndDisabled();
        }
        else if (camera.mode == CameraMode::Free) {
            float position[3], rotation[3], scale[3];
            if (editor_transform::Decompose(camera.camera2world, position, rotation, scale)) {
                bool changed = ImGui::DragFloat3("Position", position, 0.1f);
                changed |= ImGui::DragFloat3("Rotation", rotation, 0.5f);
                if (changed) {
                    const float unitScale[3] = {1.0f, 1.0f, 1.0f};
                    glm::mat4 transform;
                    ImGuizmo::RecomposeMatrixFromComponents(position, rotation, unitScale, glm::value_ptr(transform));
                    controller.SetFreeTransform(transform);
                }
            }
        }
        ImGui::End();
    }

	void EngineUi::DrawDebugPanel(EditorState& editor, const RenderStatistics& statistics, const RenderCapabilities& capabilities)
	{
		if (!editor.showDebugPanel) return;



		if (!ImGui::Begin(PanelTitle("Diagnostics", "Diagnostics").c_str(), &editor.showDebugPanel)) {
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted("Selection Debug");
        if (editor.renderMode != 0) ImGui::TextDisabled("%s", _SL("Selection wireframes require Default view."));
        ImGui::BeginDisabled(editor.renderMode != 0);
		ImGui::Checkbox("Selection Bounds (AABB)", &editor.debugSelectionBounds);
		ImGui::Checkbox("Collision Shapes", &editor.debugCollisionShapes);
        ImGui::EndDisabled();
		ImGui::Separator();
		ImGui::TextWrapped("Selection Bounds draws the selected body's world-space AABB. Collision Shapes draws the selected body's physics shape wireframe.");
		ImGui::Separator();

        ImGui::SeparatorText("Frame & visibility");
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("%.1f FPS / %.2f ms", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
        ImGui::Text("Visible static batches: %u / %u", statistics.frustumCullingVisibleCandidates, statistics.frustumCullingTotalCandidates);
        const char* modes[] = { _SL("Default"), _SL("Mipmaps"), _SL("Depth"), _SL("Derivatives"), _SL("Overdraw"), _SL("Overshading"),
            "SSAO", "SSR", _SL("Normals"), _SL("Wireframe"), "Albedo", _SL("Shadow") };
        static_assert(IM_ARRAYSIZE(modes) == view_mode::Count);
        editor.renderMode = std::clamp(editor.renderMode, 0, view_mode::Count - 1);
        if (ImGui::BeginCombo(_SL("View Mode"), modes[editor.renderMode])) {
            for (int mode = 0; mode < view_mode::Count; ++mode) {
                const bool available = mode != view_mode::Wireframe || capabilities.wireframeSupported;
                ImGui::BeginDisabled(!available);
                if (ImGui::Selectable(modes[mode], editor.renderMode == mode)) editor.renderMode = mode;
                if (editor.renderMode == mode) ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
                if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", _SL("Wireframe is not supported by this graphics device."));
            }
            ImGui::EndCombo();
        }
        const char* modeHelp[] = {
            "Lit scene with the current rendering settings.",
            "Texture mip level: 0 red, 1 green, 2 blue, 3 yellow, 4 cyan, 5 magenta; colors repeat every 6 levels.",
            "Camera distance on a logarithmic scale: near is black, far is white; empty background is black.",
            "Relative depth change per pixel: horizontal red, vertical green; surfaces parallel to the camera plane are dark.",
            "Covered layers without depth rejection: brighter means more overlap, white at 20 layers.",
            "Layers passing depth in draw order: brighter means more shading, white at 20 layers. This is an estimate, not GPU timing.",
            "SSAO buffer: white is unoccluded, darker areas are occluded. Computed even when SSAO is disabled in Default view.",
            "SSR buffer: reflection color weighted by hit confidence. Black means no valid reflection; rough or off-screen surfaces may have no hit. Computed even when SSR is disabled in Default view.",
            "World-space shading normals, including normal maps: X/Y/Z map to R/G/B. Empty pixels are black.",
            "Triangle edges for static and animated meshes. Surfaces are unfilled, so edges behind them may also be visible.",
            "Base-color texture multiplied by the material color, without lighting, AO, shadows or tone mapping.",
            "Directional-light cascaded shadow visibility: white is lit, black is shadowed. Includes cascade blending; spotlight shadows are not shown."
        };
        static_assert(IM_ARRAYSIZE(modeHelp) == view_mode::Count);
        if (editor.renderMode >= 0 && editor.renderMode < IM_ARRAYSIZE(modeHelp))
            ImGui::TextWrapped("%s", _SL(modeHelp[editor.renderMode]));
        if (view_mode::IsBuffer(editor.renderMode))
            ImGui::TextWrapped("%s", _SL("Buffer views use opaque and alpha-cutout geometry, including animated meshes. Transparent blending and display post-processing are omitted."));
        else if (editor.renderMode != 0)
            ImGui::TextWrapped("%s", _SL("Debug views include static and animated meshes; sky, particles, portal views and post-processing are omitted."));

		ImGui::End();
	}

	void EngineUi::DrawSceneHierarchy(RenderSystem* renderSys, SceneManager* sceneManager, EditorSceneAdapter& editorScene, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id, EditorState& editor)
	{
		// 获取当前屏幕分辨率 (Current screen resolution)



		// 🪟 1. Scene Hierarchy Panel (场景层级面板)
		if (editor.showSceneHierarchy)
		{
			// 设置面板的初始位置和大小// Set initial position and size of the panel



			// &editor.showSceneHierarchy，ImGui 会自动在右上角生成关闭按钮 [X]// ImGui will automatically generate a close button [X] in the top right corner when we pass &editor.showSceneHierarchy
			if (ImGui::Begin(PanelTitle("Scene Hierarchy", "SceneHierarchy").c_str(), &editor.showSceneHierarchy))
			{
				if (editorScene.Scene()) {
					// 显示实体总数// Display total entity count
					ImGui::Text(_SL("Total Entities: %d"), editorScene.EntityCount());
					ImGui::Separator();

					const auto entities = editorScene.ListEntities();

					// 滚动子窗口来容纳实体列表// Use a child window to contain the entity list and allow scrolling
					if (ImGui::BeginChild("EntityList", ImVec2(0, 0), true)) {
						// 遍历所有带有 MeshComponent 的实体 (Iterate all entities with MeshComponent)
						for (const auto& entity : entities) {
							const std::string& name = entity.name;

							// 渲染可选项Selectable
							bool is_selected = (selected_id == entity.id);
							if (ImGui::Selectable(name.c_str(), is_selected)) {
								selected_id = entity.id; // 更新当前选中的实体 ID
								editor.activeParticleIndex = -1;
							}
						}
					}
					ImGui::EndChild();


					// 拖放目标接收区 (Drag & Drop Target)
					// 从 Content Browser 直接拖拽模型到这个列表中生成// This is the drag & drop target area where we can drop models directly from the Content Browser to spawn them in the scene
					if (ImGui::BeginDragDropTarget()) {
						// 接收Content Browser 设定标签
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL")) {

							const char* droppedPath = (const char*)payload->Data;

							//计算生成位置：摄像机正前方 15 米处// Calculate spawn position: 15 units in front of the camera
							glm::mat4 invView = glm::inverse(view);
							glm::vec3 camPos = glm::vec3(invView[3]);        // 相机位置
							glm::vec3 camForward = -glm::vec3(invView[2]);   // 相机前向向量

							glm::vec3 spawnPosVec = camPos + camForward * 15.0f;
							spawnPosVec.y = std::max(spawnPosVec.y, 0.0f);   // 强制最低生成在地面，防止掉入虚空

							glm::mat4 spawnTransform = BuildDroppedModelTransform(droppedPath, spawnPosVec);

							// 调用渲染系统加载实体// Call the render system to load the model as a new entity in the scene
							if (renderSys) {
								LogPrint("[DragDrop] Loading model: %s at (%.1f, %.1f, %.1f)\n", droppedPath, spawnPosVec.x, spawnPosVec.y, spawnPosVec.z);
								//renderSys->load_additional_model(droppedPath, false, 100.0f, spawnTransform);
								flecs::entity spawnedEntity = SpawnDroppedModel(renderSys, sceneManager, droppedPath, spawnTransform);
								if (spawnedEntity.is_valid()) {
									EngineUi::ShowToast("Model Imported Successfully!");
								}
								else {
									EngineUi::ShowToast("Model Import Failed!");
								}
							}
						}
						ImGui::EndDragDropTarget();
					}

				}
			}
			ImGui::End();
		}



		// 🪟 2. Entity Inspector & ImGuizmo (实体属性检查器 & 3D 交互坐标轴)

        const EditorEntitySnapshot inspected = editorScene.Inspect(selected_id);
        if (selected_id != 0 && !inspected.alive) selected_id = 0;
        if (editor.showEntityInspector && (selected_id == 0 || !editorScene.Scene())) {
            if (ImGui::Begin(PanelTitle("Entity Inspector", "EntityInspector").c_str(), &editor.showEntityInspector)) {
                ImGui::TextWrapped("Select an entity in the hierarchy or scene viewport to inspect its components.");
                if (editor.activeParticleIndex >= 0 && ImGui::Button("Open Particles")) editor.showParticlePanel = true;
            }
            ImGui::End();
        }

		if (selected_id != 0 && inspected.alive) {
			const EditorEntitySnapshot selectedEntity = inspected;

			// 安全检查：如果选中的实体在上一帧被销毁了，立刻清空选中状态并提前退出
			if (!selectedEntity.alive) {
				selected_id = 0;
				return;
			}

			//属性检查器面板 (Entity Inspector)

			if (editor.showEntityInspector)
			{
				// 设置初始位置在 Hierarchy 的下方// Set initial position below the Hierarchy panel



				if (ImGui::Begin(PanelTitle("Entity Inspector", "EntityInspector").c_str(), &editor.showEntityInspector)) {

					ImGui::TextColored(editor_theme::Color(editor_theme::kText), "[ %s ]", selectedEntity.name.c_str());
					ImGui::Separator();

					// 可见性切换组件// Visibility Toggle Component
					if (selectedEntity.alive) {
						bool visible = selectedEntity.visible;
						if (ImGui::Checkbox(_SL("Visible"), &visible))
							editorScene.SetVisible(selected_id, visible);
					}

					// 变换组件 (LocalTransform)
 					if (selectedEntity.hasTransform) {
                            glm::mat4 localTransform = selectedEntity.transform;
                            bool canEditTransform = m_current_inspected_id == selectedEntity.id;
                            if (!canEditTransform || !ImGui::IsAnyItemActive())
                                canEditTransform = SyncTransformCache(selectedEntity.id, localTransform);
                            if (!canEditTransform) {
                                ImGui::TextWrapped("This transform has zero scale, shear or invalid values and cannot be edited as TRS.");
                            }
                            if (canEditTransform) {

							bool is_modified = false;

							// UI 输入控件// UI input controls for Position, Rotation, Scale
							ImGui::Text(_SL("Position (XYZ)"));
							if (ImGui::DragFloat3("##Pos", m_ui_translation, 0.1f)) is_modified = true;

							ImGui::Text(_SL("Rotation (Pitch Yaw Roll)"));
							if (ImGui::DragFloat3("##Rot", m_ui_rotation, 1.0f)) is_modified = true;

							ImGui::Text(_SL("Scale (XYZ)"));
							if (ImGui::DragFloat3("##Scl", m_ui_scale, 0.1f)) is_modified = true;

							// 快速镜像按钮 (Mirror Buttons)
							ImGui::TextColored(editor_theme::Color(editor_theme::kMuted), "Mirror:");
							ImGui::SameLine();
							if (ImGui::Button("Flip X")) { m_ui_scale[0] = -m_ui_scale[0]; is_modified = true; }
							ImGui::SameLine();
							if (ImGui::Button("Flip Y")) { m_ui_scale[1] = -m_ui_scale[1]; is_modified = true; }
							ImGui::SameLine();
							if (ImGui::Button("Flip Z")) { m_ui_scale[2] = -m_ui_scale[2]; is_modified = true; }


							if (is_modified) {
								ImGuizmo::RecomposeMatrixFromComponents(
									m_ui_translation, m_ui_rotation, m_ui_scale,
									glm::value_ptr(localTransform)
								);
								editorScene.SetTransform(selected_id, localTransform);
							}

						}
					}

					ImGui::Separator();

					// 删除按钮// Delete Button
					if (ImGui::Button(_SL("Delete Entity"), ImVec2(-1, 0))) {
						editorScene.Destroy(selected_id);
					}
					// 键盘快捷键删除 (Delete 键)
					if (selectedEntity.alive &&
						ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
						!(editor.showGameUiEditor && UIEditorWindow::WantsKeyboardCapture()) &&
						ImGui::IsKeyPressed(ImGuiKey_Delete) &&
						!ImGui::GetIO().WantTextInput) {
						editorScene.Destroy(selected_id);
					}
				}
				ImGui::End();
			}



		}
	}

	void EngineUi::DrawAudioPanel(EditorState& editor, AudioSystem* audioSystem)
	{
		if (!editor.showAudioPanel) return;



		if (!ImGui::Begin(PanelTitle("Audio", "Audio").c_str(), &editor.showAudioPanel)) {
			ImGui::End();
			return;
		}

		if (!audioSystem) {
			ImGui::TextUnformatted("AudioSystem is not connected.");
			ImGui::End();
			return;
		}

		float masterVolume = audioSystem->GetMasterVolume();
		if (ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f, "%.2f")) {
			audioSystem->SetMasterVolume(masterVolume);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Loaded Sounds");

		const auto soundNames = audioSystem->GetSoundNames();
		if (soundNames.empty()) {
			ImGui::TextDisabled("No sounds loaded.");
			ImGui::End();
			return;
		}

		for (const std::string& name : soundNames) {
			ImGui::PushID(name.c_str());

			if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				float volume = audioSystem->GetVolume(name);
				if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f")) {
					audioSystem->SetVolume(name, volume);
				}

				float pitch = audioSystem->GetPitch(name);
				if (ImGui::SliderFloat("Pitch", &pitch, 0.25f, 3.0f, "%.2f")) {
					audioSystem->SetPitch(name, pitch);
				}

				if (ImGui::Button("Play Loop")) {
					audioSystem->PlayLoop(name);
				}
				ImGui::SameLine();
				if (ImGui::Button("Stop")) {
					audioSystem->Stop(name);
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		ImGui::End();
	}

    void EngineUi::DrawMainMenuBar(RenderSystem* renderSys, SceneManager* sceneManager, EditorState& editor, const RenderCapabilities& capabilities, bool& appRunning) {
        if (!editor.showEngineUi) return;
        const auto snapshot = [&](bool save) {
            try {
                const bool ok = save ? SaveProject(sceneManager, renderSys, "Assets/MySceneSave.json")
                                     : LoadProject(sceneManager, renderSys, "Assets/MySceneSave.json");
                ShowToast(ok ? (save ? "Scene snapshot saved" : "Scene snapshot loaded") : "Snapshot failed - see Console");
                if (!ok) editor.showConsole = true;
            }
            catch (const std::exception& error) {
                LogPrintf("[Error] Scene snapshot failed: %s\n", error.what());
                ShowToast("Snapshot failed - see Console");
                editor.showConsole = true;
            }
        };
        // UI authoring owns its own save shortcut. Do not save the scene while typing.
        if (!ImGui::GetIO().WantTextInput && !(editor.showGameUiEditor && UIEditorWindow::WantsKeyboardCapture()) && ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) snapshot(true);
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) snapshot(false);
        }
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu(_SL("File"))) {
            if (ImGui::MenuItem(_SL("Save Scene Snapshot"), "Ctrl+S")) snapshot(true);
            if (ImGui::MenuItem(_SL("Load Scene Snapshot"), "Ctrl+O")) snapshot(false);
            ImGui::TextDisabled("Assets/MySceneSave.json");
            ImGui::Separator();
            ImGui::TextWrapped("Snapshots store named entity transforms and particle settings. They do not save a complete project.");
            ImGui::Separator();
            if (ImGui::MenuItem(_SL("Exit Engine"), "F4")) appRunning = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(_SL("Window"))) {
            ImGui::MenuItem(_SL("Scene Hierarchy"), nullptr, &editor.showSceneHierarchy);
            ImGui::MenuItem(_SL("Entity Inspector"), nullptr, &editor.showEntityInspector);
            ImGui::MenuItem(_SL("Assets"), nullptr, &editor.showContentBrowser);
            ImGui::MenuItem(_SL("Console"), nullptr, &editor.showConsole);
            ImGui::Separator();
            ImGui::MenuItem(_SL("Render Settings"), nullptr, &editor.showRenderSettings);
            ImGui::MenuItem(_SL("Lighting"), nullptr, &editor.showLightPanel);
            ImGui::MenuItem(_SL("Camera"), nullptr, &editor.showCameraPanel);
            ImGui::MenuItem(_SL("Particles"), nullptr, &editor.showParticlePanel);
            ImGui::MenuItem(_SL("Audio"), nullptr, &editor.showAudioPanel);
            ImGui::MenuItem(_SL("Diagnostics"), nullptr, &editor.showDebugPanel);
            ImGui::Separator();
            if (ImGui::BeginMenu(_SL("Game UI Tools"))) {
                ImGui::MenuItem(_SL("Game UI Editor"), nullptr, &editor.showGameUiEditor);
                ImGui::MenuItem(_SL("Runtime UI Debug"), nullptr, &editor.showRuntimeUiDebugPanel);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(_SL("View"))) {
            ImGui::MenuItem(_SL("Engine UI"), "F1", &editor.showEngineUi);
            ImGui::MenuItem(_SL("Neutral Viewport Background"), nullptr, &editor.editorViewportBackdrop, editor.renderMode == 0);
            ImGui::MenuItem(_SL("Viewport Grid"), nullptr, &editor.editorViewportGrid, editor.renderMode == 0);
            if (editor.renderMode != 0) ImGui::TextDisabled("%s", _SL("Available in default render mode"));
            ImGui::Separator();
            if (ImGui::MenuItem(_SL("Reset Editor Layout"))) {
                editor.showSceneHierarchy = editor.showEntityInspector = editor.showContentBrowser = true;
                editor.showConsole = editor.showRenderSettings = true;
                editor.showLightPanel = editor.showCameraPanel = editor.showAudioPanel = false;
                editor.showDebugPanel = editor.showParticlePanel = editor.showRuntimeUiDebugPanel = false;
                editor.showGameUiEditor = false;
                s_ResetEditorLayout = true;
            }
            if (ImGui::BeginMenu(_SL("Language"))) {
                if (ImGui::MenuItem("English", nullptr, Translator::CurrentLanguage == Language::English)) Translator::SetLanguage(Language::English);
                if (ImGui::MenuItem("Chinese", nullptr, Translator::CurrentLanguage == Language::Chinese)) Translator::SetLanguage(Language::Chinese);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(_SL("Help"))) {
            ImGui::TextUnformatted("Steer Engine Editor");
            ImGui::Separator();
            ImGui::TextUnformatted("F1: switch editor / game view");
            ImGui::TextUnformatted("W / E / R: translate / rotate / scale in viewport");
            ImGui::TextUnformatted("Drag models from Assets into the scene.");
            ImGui::TextUnformatted("Window: open panels. View: reset docking layout.");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

	VkDescriptorSet EngineUi::s_ParticleIconTexId = VK_NULL_HANDLE;



} // namespace engine


#pragma execution_character_set("utf-8")

#include "EngineUi.hpp"

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
#include "../Particle/ParticleSystem.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Renderer/RenderUtilities/light.hpp"

#include "../AudioSystem/AudioSystem.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;



namespace engine {

	namespace {
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
			ImGui::Begin(title, p_open);

			// 清空日志按钮//Clear logs button
			if (ImGui::Button("Clear Logs")) {
				Items.clear();
			}
			ImGui::Separator();

			for (const auto& item : Items) {
				if (item.find("[Error]") != std::string::npos) {
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", item.c_str());
				}
				else if (item.find("[Warning]") != std::string::npos) {
					ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", item.c_str());
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

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);// 设置圆角
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, alpha)); // 纯黑色文字

		if (ImGui::Begin("ToastWindow", nullptr, flags)) {
			ImGui::Text("  %s  ", s_ToastMessage.c_str());
		}
		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}




	// ========== project save&load==========
	void EngineUi::SaveProject(SceneManager* sceneManager, RenderSystem* renderSys, const std::string& filepath) {
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
			LogPrintf("Project saved to %s\n", filepath.c_str());
		}
		else {
			LogPrintf("[Error] Could not open %s for saving.\n", filepath.c_str());
		}
	}

	void EngineUi::LoadProject(SceneManager* sceneManager, RenderSystem* renderSys, const std::string& filepath) {
		std::ifstream file(filepath);
		if (!file.is_open()) {
			LogPrintf("[Serialization] No save file found at %s\n", filepath.c_str());
			return;
		}

		json root;
		try {
			file >> root;
		}
		catch (json::parse_error& e) {
			LogPrintf("[Error] JSON parsing failed: %s\n", e.what());
			return;
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

		LogPrint("Project loaded from %s\n", filepath.c_str());
	}



	// ========== Scene Viewport 场景视口==========
	static ImVec2 s_SceneViewportSize = ImVec2(1280.0f, 720.0f); // 默认尺寸
	static ImVec2 s_SceneViewportPos = ImVec2(0.0f, 0.0f);//默认位置
	ImVec2 EngineUi::GetSceneViewportSize() {
		return s_SceneViewportSize;
	}
	ImVec2 EngineUi::GetSceneViewportPos() {
		return s_SceneViewportPos;
	}


	void EngineUi::DrawSceneViewport(VkDescriptorSet sceneTexId, RenderSystem* renderSys, SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id, UserState& state) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

		const char* viewportWindowName = state.showEngineUi ? "Scene Viewport###SceneViewportEditor": "Game View###SceneViewportFullscreen";
		ImGuiWindowFlags viewportFlags = 0;
		if (!state.showEngineUi) {
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

		ImGui::Begin(viewportWindowName, nullptr, viewportFlags);

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

		if (state.showEngineUi && ImGui::BeginDragDropTarget()) {
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
		if (state.showEngineUi && renderSys && state.particlesEnabled) {
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

				bool is_selected = (state.activeParticleIndex == i);
				ImU32 iconColor = is_selected ? IM_COL32(255, 255, 100, 255) : IM_COL32(255, 80, 80, 255);

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
					drawList->AddText(ImVec2(screenPos.x - 4, screenPos.y - 7), IM_COL32(0, 0, 0, 255), "P");
				}


				//InvisibleButton

				// 光标移动到图标左上角
				ImGui::SetCursorScreenPos(ImVec2(screenPos.x - hitRadius, screenPos.y - hitRadius));

				ImGui::PushID(i);

				//InvisibleButton
				if (ImGui::InvisibleButton("ParticleIcon", ImVec2(hitRadius * 2, hitRadius * 2))) {
					state.activeParticleIndex = i;
					selected_id = 0; // clear
					LogPrint("[BillboardPick] Hit Particle Group %d\n", i + 1);
				}

				// 悬停状态//hover tooltip
				if (ImGui::IsItemHovered()) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					ImGui::BeginTooltip();
					ImGui::Text("[ Particle Group %d ]", i + 1);
					ImGui::Text("Click to Select & Enable Gizmo");
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
		if (state.showEngineUi && renderSys && state.particlesEnabled && state.activeParticleIndex >= 0 && state.activeParticleIndex < renderSys->GetParticles().size()) {
			drawGizmo = true;
			gizmoMatrix = glm::translate(glm::mat4(1.0f), renderSys->GetParticles()[state.activeParticleIndex]->config.emitterPos);
		}
		else if (state.showEngineUi && selected_id != 0 && sceneManager) {
			flecs::entity selectedEntity = sceneManager->get_world().entity(selected_id);
			if (selectedEntity.is_alive() && selectedEntity.has<LocalTransform>()) {
				drawGizmo = true;
				gizmoMatrix = selectedEntity.get<LocalTransform>().matrix;
			}
		}

		if (drawGizmo) {
			ImGuizmo::BeginFrame();

			ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetRect(s_SceneViewportPos.x, s_SceneViewportPos.y, s_SceneViewportSize.x, s_SceneViewportSize.y);

			static ImGuizmo::OPERATION currentOp = ImGuizmo::TRANSLATE;
			if (ImGui::IsKeyPressed(ImGuiKey_W)) currentOp = ImGuizmo::TRANSLATE;
			if (ImGui::IsKeyPressed(ImGuiKey_E)) currentOp = ImGuizmo::ROTATE;
			if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOp = ImGuizmo::SCALE;

			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), currentOp, ImGuizmo::LOCAL, glm::value_ptr(gizmoMatrix));

			if (ImGuizmo::IsUsing()) {
				if (state.activeParticleIndex >= 0) {
					renderSys->GetParticles()[state.activeParticleIndex]->config.emitterPos = glm::vec3(gizmoMatrix[3]);
				}
				else if (selected_id != 0 && sceneManager) {
					flecs::entity selectedEntity = sceneManager->get_world().entity(selected_id);
					LocalTransform* lt = &selectedEntity.get_mut<LocalTransform>();
					float t[3], r[3], s[3];
					ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);
					if (currentOp == ImGuizmo::SCALE) {
						for (int i = 0; i < 3; ++i) m_ui_scale[i] = (std::abs(r[i] - m_ui_rotation[i]) > 90.f) ? -s[i] : std::abs(s[i]);
					}
					else {
						for (int i = 0; i < 3; ++i) { m_ui_translation[i] = t[i]; m_ui_rotation[i] = r[i]; }
					}
					ImGuizmo::RecomposeMatrixFromComponents(m_ui_translation, m_ui_rotation, m_ui_scale, glm::value_ptr(lt->matrix));
					selectedEntity.modified<LocalTransform>();
				}
			}
		}

		state.isSceneViewportHovered = ImGui::IsWindowHovered();

		ImGui::End(); //  Viewport 闭合
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		ImGui::PopStyleVar();
	}


	//void EngineUi::DrawContentBrowser(RenderSystem* renderSys, SceneManager* sceneManager) {
	//	// 设置初始窗口大小和位置
	//	ImGui::SetNextWindowPos(ImVec2(20, ImGui::GetIO().DisplaySize.y - 300), ImGuiCond_FirstUseEver);
	//	ImGui::SetNextWindowSize(ImVec2(800, 250), ImGuiCond_FirstUseEver);

	//	if (ImGui::Begin(_SL("Content Browser"))) {
	//		// 设定格子的宽度// Set the width of each cell
	//		float cellSize = 110.0f;
	//		float panelWidth = ImGui::GetContentRegionAvail().x;
	//		int columnCount = std::max(1, (int)(panelWidth / cellSize));// 计算数量Calculate the number of columns based on available width

	//		if (ImGui::BeginTable("ContentGrid", columnCount)) {
	//			// 遍历 Assets/Models
	//			std::string path = "Assets/Models";
	//			if (fs::exists(path) && fs::is_directory(path)) {
	//				for (const auto& entry : fs::directory_iterator(path)) {
	//					// 显示show .glb 模型
	//					if (entry.path().extension() == ".glb") {
	//						ImGui::TableNextColumn();

	//						std::string filename = entry.path().filename().string();
	//						std::string relativePath = entry.path().string();
	//						// Windows 的 \ 替换为 /
	//						std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

	//						VkDescriptorSet fileIcon = renderSys->GetModelThumbnail(relativePath);

	//						// 1. 画图标按钮// Draw the icon button
	//						ImGui::PushID(filename.c_str());
	//						if (fileIcon) {
	//							ImGui::ImageButton(filename.c_str(), (ImTextureID)fileIcon, ImVec2(100, 100));// 显示缩略图按钮
	//						}
	//						else {
	//							ImGui::Button("MODEL\nICON", ImVec2(70, 70));
	//						}

	//						// 2.设定拖拽源 (Drag Source)
	//						if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
	//							
	//							ImGui::SetDragDropPayload("CONTENT_BROWSER_MODEL", relativePath.c_str(), relativePath.size() + 1);

	//							// 拖拽时悬浮在鼠标旁边的提示
	//							ImGui::Text("Drop %s to Scene", filename.c_str());
	//							if (fileIcon) ImGui::Image((ImTextureID)fileIcon, ImVec2(40, 40));

	//							ImGui::EndDragDropSource();
	//						}

	//						// 3.图标下方的文字// The text below the icon
	//						ImGui::TextWrapped("%s", filename.c_str());
	//						ImGui::PopID();
	//					}
	//				}
	//			}
	//			else {
	//				ImGui::TextColored(ImVec4(1, 0, 0, 1), "Folder Assets/Models not found!");
	//			}
	//			ImGui::EndTable();
	//		}
	//	}
	//	ImGui::End();
	//}
	void EngineUi::DrawContentBrowser(RenderSystem* renderSys, SceneManager* sceneManager) {
		(void)sceneManager; //The browser only needs asset browsing and drag/drop here.
		static std::string s_CurrentDirectory = "Assets"; //当前浏览的文件夹路径//Track the active folder inside Assets.
		static char s_NewFolderName[128] = "NewFolder"; //Buffer for the create-folder popup.
		static std::string s_CreateFolderError; //Show inline folder-creation errors in the popup.
		const fs::path assetsRoot("Assets");
		const std::string folderIconPath = "Assets/Textures/Folder.png"; //文件夹图标// Use Folder.png as the shared folder thumbnail source.

		// 设置初始窗口大小和位置
		ImGui::SetNextWindowPos(ImVec2(20, ImGui::GetIO().DisplaySize.y - 300), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(980, 320), ImGuiCond_FirstUseEver);

		if (ImGui::Begin(_SL("Content Browser"))) //开始绘制窗口// Begin drawing the window
		{
			if (!fs::exists(assetsRoot) || !fs::is_directory(assetsRoot)) { //Fail clearly when the Assets root is missing.
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "Folder Assets not found!");
			}
			else {
				if (!fs::exists(fs::path(s_CurrentDirectory)) || !fs::is_directory(fs::path(s_CurrentDirectory))) { //Reset to Assets if the current folder was removed.
					s_CurrentDirectory = NormalizeAssetBrowserPath(assetsRoot);
				}

				if (ImGui::BeginTable("ContentBrowserLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
					ImGui::TableSetupColumn("FolderTree", ImGuiTableColumnFlags_WidthFixed, 240.0f);
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
							ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", s_CreateFolderError.c_str());
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
								ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));//悬停背景顔色
								ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.18f));//点击背景颜色

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


	void EngineUi::DrawControlPanel(UserState& state, RenderSystem* renderSys, SceneManager* sceneManager)
	{

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
			//project data 项目数据
			//float btnWidth = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;

			//          // 1. 保存按钮
			//          if (ImGui::Button(_SL("Save Project"), ImVec2(btnWidth, 30))) {
			//              SaveProject(sceneManager, renderSys, "Assets/MySceneSave.json");
			//          }

			//          ImGui::SameLine();

			//          // 2. 读取按钮
			//          if (ImGui::Button(_SL("Load Project"), ImVec2(btnWidth, 30))) {
			//              LoadProject(sceneManager, renderSys, "Assets/MySceneSave.json");
			//          }
			//ImGui::Separator();
			//project data 项目数据end

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

			//UI system for bike===============================
			ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.6f, 1.0f), _SL("[ Bicycle Tuning ]"));
			ImGui::Text(_SL("Speed: %.2f"), state.bikeSpeed);
			ImGui::Text(_SL("Steer Angle: %.1f deg"), glm::degrees(state.bikeSteerAngle));

			//reset button 重置按钮
			if (ImGui::Button(_SL("Reset Bike Tuning"))) {
				state.bikeTuning = BikeTuning{};
			}

			// sliders 滑动条
			ImGui::SliderFloat(_SL("Max Steer Angle (deg)"), &state.bikeTuning.maxSteerAngleDeg, 1.0f, 60.0f);
			ImGui::SliderFloat(_SL("Steer Speed (deg/s)"), &state.bikeTuning.steerSpeedDeg, 1.0f, 360.0f);
			ImGui::SliderFloat(_SL("Max Lean Angle (deg)"), &state.bikeTuning.maxLeanAngleDeg, 0.0f, 60.0f);
			ImGui::SliderFloat(_SL("Lean Speed (deg/s)"), &state.bikeTuning.leanSpeedDeg, 1.0f, 360.0f);
			ImGui::SliderFloat(_SL("Wheel Base"), &state.bikeTuning.wheelBase, 0.1f, 5.0f);
			ImGui::SliderFloat(_SL("Drive Force"), &state.bikeTuning.driveForce, 0.0f, 5000.0f);
			ImGui::SliderFloat(_SL("Brake Force"), &state.bikeTuning.brakeForce, 0.0f, 500.0f);
			ImGui::SliderFloat(_SL("Max Speed"), &state.bikeTuning.maxSpeed, 0.1f, 200.0f);
			ImGui::SliderFloat(_SL("Gravity Factor"), &state.bikeTuning.gravityFactor, 0.0f, 5.0f);

			ImGui::Separator();
			//UI system for bike===============================


			//generator 生成器
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), _SL("[ generator ]"));

			static float spawnHeight = 25.0f;
			ImGui::SliderFloat(_SL("Height"), &spawnHeight, 0.0f, 100.0f);

			// 物品选择下拉菜单// Item selection dropdown
			const char* itemNames[] = { "BaseballBat", "Car", "Missile", "Police Car","Animated Character Base","Helicopter","Roman Centurion" };
			static int selectedItem = 1;
			ImGui::Combo(_SL("select"), &selectedItem, itemNames, IM_ARRAYSIZE(itemNames));

			// spawn button 生成按钮
			if (ImGui::Button(_SL("Spawn!!!!"), ImVec2(200, 40)))
			{
				if (renderSys) {
					LogPrint("Spawning %s at Y = %.1f\n", itemNames[selectedItem], spawnHeight);

					glm::mat4 spawnPos = glm::translate(glm::mat4(1.0f), glm::vec3(20.0f, spawnHeight, 20.0f));

					if (selectedItem == 0) {
						//renderSys->load_additional_model("Assets/Models/BaseballBat.glb", false, 1.5f, spawnPos);
					}
					else if (selectedItem == 1) {
						glm::mat4 carPos = spawnPos * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
						//renderSys->load_additional_model("Assets/Models/Car.glb", false, 1500.0f, carPos);
					}
					else if (selectedItem == 2) {
						//renderSys->load_additional_model("Assets/Models/Missile.glb", false, 50.0f, spawnPos);
					}
					else if (selectedItem == 3) {
						//renderSys->load_additional_model("Assets/Models/Police Car.glb", false, 1600.0f, spawnPos);
					}
					else if (selectedItem == 4) {
						//renderSys->load_additional_model("Assets/Models/Animated Character Base.glb", false, 1600.0f, spawnPos);
					}
					else if (selectedItem == 5) {
						//renderSys->load_additional_model("Assets/Models/Helicopter.glb", false, 1600.0f, spawnPos);
					}
					else if (selectedItem == 6) {
						//renderSys->load_additional_model("Assets/Models/Roman Centurion.glb", false, 1600.0f, spawnPos);
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

		}
		ImGui::End();
		// 控制台
		s_Console.Draw("Output Console", &state.showConsole);
	}

	//light UI灯光调节面板
	void EngineUi::DrawLightPanel(SceneManager* sceneManager, UserState& state)
	{
		if (!state.showLightPanel) return;

		ImGui::SetNextWindowPos(ImVec2(40, 140), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Light Panel", &state.showLightPanel)) {
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
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "[ %s ]", currentLightName);

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
	void EngineUi::DrawCameraPanel(UserState& state)
	{
		if (!state.showCameraPanel) return;

		ImGui::SetNextWindowPos(ImVec2(420, 140), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(340, 360), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Camera Panel", &state.showCameraPanel)) {
			ImGui::End();
			return;
		}

		//UI基础相机模式与镜头参数// Basic camera mode and lens parameters
		ImGui::Checkbox("Third Person Mode", &state.thirdPersonMode);
		if (ImGui::SliderFloat("FOV", &state.cameraFov, 10.0f, 120.0f, "%.1f deg")) {
			state.targetFov = state.cameraFov;
		}

		glm::vec3 cameraPos = glm::vec3(state.camera2world[3]);
		ImGui::Text("Camera Pos: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);

		if (state.thirdPersonMode) {
			//第三人称相机参数// Third-person camera parameters
			ImGui::DragFloat3("Follow Target", &state.followTargetPos.x, 0.1f);
			ImGui::SliderAngle("Yaw", &state.Yaw, -180.0f, 180.0f);
			ImGui::SliderAngle("Pitch", &state.Pitch, -85.0f, 85.0f);
			ImGui::SliderFloat("Distance", &state.Distance, 1.5f, 30.0f, "%.2f");

			if (ImGui::Button("Reset Third Person Camera")) {
				state.Yaw = 0.0f;
				state.Pitch = 0.0f;
				state.Distance = 5.0f;
			}
		}
		else {
			//自由相机直接编辑世界矩阵的位移和旋转// Free camera directly edits the translation and rotation of the world matrix
			float cameraTranslation[3];
			float cameraRotation[3];
			float cameraScale[3];

			ImGuizmo::DecomposeMatrixToComponents(
				glm::value_ptr(state.camera2world),
				cameraTranslation,
				cameraRotation,
				cameraScale
			);

			bool cameraTransformChanged = false;
			cameraTransformChanged |= ImGui::DragFloat3("Position", cameraTranslation, 0.1f);
			cameraTransformChanged |= ImGui::DragFloat3("Rotation", cameraRotation, 0.5f);

			if (cameraTransformChanged) {
				cameraScale[0] = 1.0f;
				cameraScale[1] = 1.0f;
				cameraScale[2] = 1.0f;
				ImGuizmo::RecomposeMatrixFromComponents(
					cameraTranslation,
					cameraRotation,
					cameraScale,
					glm::value_ptr(state.camera2world)
				);
			}
		}

		ImGui::Separator();

		ImGui::End();
	}

	void EngineUi::DrawDebugPanel(UserState& state)
	{
		if (!state.showDebugPanel) return;

		ImGui::SetNextWindowPos(ImVec2(780, 140), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(320, 160), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Debug Panel", &state.showDebugPanel)) {
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted("Selection Debug");
		ImGui::Checkbox("Selection Bounds (AABB)", &state.debugSelectionBounds);
		ImGui::Checkbox("Collision Shapes", &state.debugCollisionShapes);
		ImGui::Separator();
		ImGui::TextWrapped("Selection Bounds draws the selected body's world-space AABB. Collision Shapes draws the selected body's physics shape wireframe.");
		ImGui::Separator();

		ImGui::TextUnformatted("Frustum Culling");
		ImGui::Checkbox("Enable Frustum Culling", &state.frustumCullingEnabled);
		ImGui::SliderFloat("Frustum Padding", &state.frustumCullingPadding, 0.0f, 10.0f, "%.2f"); // new frustum culling
		ImGui::Text("Visible Batches: %u / %u", state.frustumCullingVisibleCandidates, state.frustumCullingTotalCandidates);
		ImGui::Text("FPS (Culling Off): %.1f", state.frustumCullingOffFps);
		ImGui::Text("FPS (Culling On): %.1f", state.frustumCullingOnFps);
		if (state.frustumCullingOffFps > 0.0f && state.frustumCullingOnFps > 0.0f) {
			float fpsDelta = state.frustumCullingOnFps - state.frustumCullingOffFps;
			float fpsDeltaPercent = state.frustumCullingOffFps > 0.0f ? (fpsDelta / state.frustumCullingOffFps) * 100.0f : 0.0f;
			ImGui::Text("Delta: %+0.1f FPS (%+0.1f%%)", fpsDelta, fpsDeltaPercent);
		}
		ImGui::End();
	}

	void EngineUi::DrawSceneHierarchy(RenderSystem* renderSys, SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id, UserState& state)
	{
		// 获取当前屏幕分辨率 (Current screen resolution)
		float screenWidth = ImGui::GetIO().DisplaySize.x;
		float screenHeight = ImGui::GetIO().DisplaySize.y;

		// 🪟 1. Scene Hierarchy Panel (场景层级面板)
		if (state.showSceneHierarchy)
		{
			// 设置面板的初始位置和大小// Set initial position and size of the panel
			ImGui::SetNextWindowPos(ImVec2(screenWidth - 320, 20), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

			// &state.showSceneHierarchy，ImGui 会自动在右上角生成关闭按钮 [X]// ImGui will automatically generate a close button [X] in the top right corner when we pass &state.showSceneHierarchy
			if (ImGui::Begin(_SL("Scene Hierarchy"), &state.showSceneHierarchy))
			{
				if (sceneManager && &sceneManager->get_world() != nullptr) {
					// 显示实体总数// Display total entity count
					ImGui::Text(_SL("Total Entities: %d"), sceneManager->get_entity_count());
					ImGui::Separator();

					auto& world = sceneManager->get_world();

					// 滚动子窗口来容纳实体列表// Use a child window to contain the entity list and allow scrolling
					if (ImGui::BeginChild("EntityList", ImVec2(0, 0), true)) {
						// 遍历所有带有 MeshComponent 的实体 (Iterate all entities with MeshComponent)
						world.each([&](flecs::entity entity, MeshComponent& meshComponent) {
							std::string name = entity.name().size() > 0 ? entity.name().c_str() : "ID: " + std::to_string(entity.id());

							// 渲染可选项Selectable
							bool is_selected = (selected_id == entity.id());
							if (ImGui::Selectable(name.c_str(), is_selected)) {
								selected_id = entity.id(); // 更新当前选中的实体 ID
								state.activeParticleIndex = -1;
							}
							});
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

		if (selected_id != 0 && sceneManager) {
			auto& world = sceneManager->get_world();
			flecs::entity selectedEntity = world.entity(selected_id); // 获取当前选中的实体

			// 安全检查：如果选中的实体在上一帧被销毁了，立刻清空选中状态并提前退出
			if (!selectedEntity.is_alive()) {
				selected_id = 0;
				return;
			}

			//属性检查器面板 (Entity Inspector)

			if (state.showEntityInspector)
			{
				// 设置初始位置在 Hierarchy 的下方// Set initial position below the Hierarchy panel
				ImGui::SetNextWindowPos(ImVec2(screenWidth - 320, 430), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

				if (ImGui::Begin(_SL("Entity Inspector"), &state.showEntityInspector)) {

					ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ %s ]", selectedEntity.name().c_str());
					ImGui::Separator();

					// 可见性切换组件// Visibility Toggle Component
					if (selectedEntity.has<EntityStatus>()) {
						EntityStatus* entityStatus = &selectedEntity.get_mut<EntityStatus>();
						if (entityStatus) {
							ImGui::Checkbox(_SL("Visible"), &entityStatus->should_render);
						}
					}

					// 变换组件 (LocalTransform)
					if (selectedEntity.has<LocalTransform>()) {
						LocalTransform* localTransform = &selectedEntity.get_mut<LocalTransform>();
						if (localTransform) {
							float matrixTranslation[3], matrixRotation[3], matrixScale[3];

							//  T/R/S (位置、旋转、缩放)
							ImGuizmo::DecomposeMatrixToComponents(
								glm::value_ptr(localTransform->matrix),
								matrixTranslation, matrixRotation, matrixScale
							);

							if (m_current_inspected_id != selectedEntity.id()) {
								m_current_inspected_id = selectedEntity.id();
								ImGuizmo::DecomposeMatrixToComponents(
									glm::value_ptr(localTransform->matrix),
									m_ui_translation, m_ui_rotation, m_ui_scale
								);
							}

							bool is_modified = false;

							// UI 输入控件// UI input controls for Position, Rotation, Scale
							ImGui::Text(_SL("Position (XYZ)"));
							if (ImGui::DragFloat3("##Pos", m_ui_translation, 0.1f)) is_modified = true;

							ImGui::Text(_SL("Rotation (Pitch Yaw Roll)"));
							if (ImGui::DragFloat3("##Rot", m_ui_rotation, 1.0f)) is_modified = true;

							ImGui::Text(_SL("Scale (XYZ)"));
							if (ImGui::DragFloat3("##Scl", m_ui_scale, 0.1f)) is_modified = true;

							// 快速镜像按钮 (Mirror Buttons)
							ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Mirror:");
							ImGui::SameLine();
							if (ImGui::Button("Flip X")) { m_ui_scale[0] = -m_ui_scale[0]; is_modified = true; }
							ImGui::SameLine();
							if (ImGui::Button("Flip Y")) { m_ui_scale[1] = -m_ui_scale[1]; is_modified = true; }
							ImGui::SameLine();
							if (ImGui::Button("Flip Z")) { m_ui_scale[2] = -m_ui_scale[2]; is_modified = true; }


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

					// 删除按钮// Delete Button
					if (ImGui::Button(_SL("Delete Entity"), ImVec2(-1, 0))) {
						selectedEntity.destruct();
						selected_id = 0;
					}
					// 键盘快捷键删除 (Delete 键)
					if (selectedEntity.is_alive() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput) {
						selectedEntity.destruct();
						selected_id = 0;
					}
				}
				ImGui::End();
			}



		}
	}

	void EngineUi::DrawAudioPanel(UserState& state, AudioSystem* audioSystem)
	{
		if (!state.showAudioPanel) return;

		ImGui::SetNextWindowPos(ImVec2(780, 320), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(340, 260), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Audio Panel", &state.showAudioPanel)) {
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

	void EngineUi::DrawMainMenuBar(RenderSystem* renderSys, SceneManager* sceneManager, UserState& state, bool& appRunning) {
		if (!state.showEngineUi) {
			return;
		}


		//绘制顶部主菜单栏 (Main Menu Bar)
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu(_SL("File"))) {
				if (ImGui::MenuItem(_SL("Save Project"), "Ctrl+S")) {
					SaveProject(sceneManager, renderSys, "Assets/MySceneSave.json");
					ShowToast("[ Project Saved ]");
				}
				if (ImGui::MenuItem(_SL("Load Project"), "Ctrl+O")) {
					LoadProject(sceneManager, renderSys, "Assets/MySceneSave.json");
					ShowToast("[ Project Loaded ]");
				}
				ImGui::Separator();
				if (ImGui::MenuItem(_SL("Exit Engine"), "Alt+F4")) {
					appRunning = false;
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(_SL("View"))) {
				ImGui::MenuItem("Engine UI", "F1", &state.showEngineUi);
				ImGui::Separator();
				ImGui::MenuItem(_SL("Control Panel"), NULL, &state.showControlPanel);
				ImGui::MenuItem(_SL("Content Browser"), NULL, &state.showContentBrowser);
				ImGui::MenuItem(_SL("Scene Hierarchy"), NULL, &state.showSceneHierarchy);
				ImGui::MenuItem(_SL("Entity Inspector"), NULL, &state.showEntityInspector);
				ImGui::MenuItem(_SL("Output Console"), NULL, &state.showConsole);
				ImGui::MenuItem(_SL("Light Panel"), NULL, &state.showLightPanel);
				ImGui::MenuItem(_SL("Camera Panel"), NULL, &state.showCameraPanel);
				ImGui::MenuItem("Audio Panel", NULL, &state.showAudioPanel);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(_SL("Help"))) {
				if (ImGui::MenuItem(_SL("About Engine"))) {
					LogPrint("6666 Engine Name!!!!!!!!!!!!!!!!!!!!!\n");
				}
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
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
				LogPrint("Game Started!\n");
			}

			ImGui::Spacing();
			if (ImGui::Button(_SL("Setting"), ImVec2(-1, 60))) {
				//未完成 not ffinished yet
				LogPrint("Setting...(coming soon)\n");
			}
			ImGui::Spacing();
			if (ImGui::Button(_SL("Exit Game"), ImVec2(-1, 60))) {
				appRunning = false;
				LogPrint("Exiting Game...\n");
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
				LogPrint("Restarting Level...\n");
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
				LogPrint("Restarting Level...\n");
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

	// 静态成员初始化
	VkDescriptorSet EngineUi::s_ParticleIconTexId = VK_NULL_HANDLE;



} // namespace engine


#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include "ThirdParty/json/json.hpp"
#include "SceneSnapshotValidation.hpp"
#include "SceneDocumentValidation.hpp"

namespace engine {

inline std::string ProjectPathText(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

// A project selects a generic scene or a registered native module and its data.
// Loading this document performs no scene mutation or working-directory change.
struct ProjectDescriptor {
    std::string name;
    std::string contentModule;
    std::string startupScene;
    std::filesystem::path filePath;
    std::filesystem::path assetRoot;
    std::filesystem::path snapshotPath;

    static ProjectDescriptor Load(const std::filesystem::path& path) {
        namespace fs = std::filesystem;
        ProjectDescriptor project;
        project.filePath = fs::weakly_canonical(fs::absolute(path));
        std::ifstream stream(project.filePath);
        if (!stream) throw std::runtime_error("Cannot open project: " + ProjectPathText(project.filePath));
        nlohmann::json document;
        try {
            document = nlohmann::json::parse(stream);
        } catch (const nlohmann::json::exception& error) {
            throw std::runtime_error("Invalid project JSON in " + ProjectPathText(project.filePath) + ": " + error.what());
        }
        if (!document.is_object() || !document.contains("schemaVersion") ||
            !document["schemaVersion"].is_number_integer() || document["schemaVersion"] != 1)
            throw std::runtime_error("Unsupported project format. Expected schemaVersion 1.");
        const auto text = [&](const char* key) {
            if (!document.contains(key) || !document[key].is_string())
                throw std::runtime_error(std::string("Project requires a string field: ") + key);
            const auto value = document[key].get<std::string>();
            if (value.find_first_not_of(" \t\r\n") == std::string::npos || value.find('\0') != std::string::npos)
                throw std::runtime_error(std::string("Project field cannot be empty: ") + key);
            return value;
        };
        project.name = text("name");
        project.contentModule = text("contentModule");
        project.startupScene = text("startupScene");
        const fs::path assets = fs::u8path(text("assetRoot"));
        if (assets.is_absolute() || assets.has_root_name())
            throw std::runtime_error("assetRoot must be relative to the project file.");
        const auto directory = project.filePath.parent_path();
        project.assetRoot = fs::weakly_canonical(directory / assets);
        if (project.assetRoot.filename() != "Assets" || !fs::is_directory(project.assetRoot / "Shaders"))
            throw std::runtime_error("Project assetRoot must point to an existing Assets directory with Shaders.");
        const fs::path snapshot = fs::u8path(document.contains("sceneSnapshot")
            ? text("sceneSnapshot") : std::string("Scenes/Main.snapshot.json"));
        if (snapshot.is_absolute() || snapshot.has_root_name())
            throw std::runtime_error("sceneSnapshot must be relative to the project file.");
        project.snapshotPath = fs::weakly_canonical(directory / snapshot);
        const auto relative = project.snapshotPath.lexically_relative(directory);
        if (relative.empty() || relative == "." || *relative.begin() == ".." || fs::is_directory(project.snapshotPath))
            throw std::runtime_error("sceneSnapshot must be a file inside the project directory.");
        if (fs::exists(project.snapshotPath)) {
            try {
                std::ifstream snapshotStream(project.snapshotPath);
                if (!snapshotStream) throw std::runtime_error("Cannot read the snapshot file.");
                const auto snapshotDocument = nlohmann::json::parse(snapshotStream);
                if (project.contentModule == "scene") ValidateSceneDocumentAssets(snapshotDocument, project.assetRoot);
                else ValidateSceneSnapshot(snapshotDocument);
            } catch (const std::exception& error) {
                throw std::runtime_error("Invalid scene snapshot " + ProjectPathText(project.snapshotPath) + ": " + error.what());
            }
        }
        else if (project.contentModule == "scene") {
            throw std::runtime_error("The generic project's scene document does not exist: " + ProjectPathText(project.snapshotPath));
        }
        if (project.contentModule == "scene") {
            if (project.startupScene != "main")
                throw std::runtime_error("Generic projects require startupScene main.");
            const auto relativeAssets = project.assetRoot.lexically_relative(directory);
            if (relativeAssets.empty() || *relativeAssets.begin() == "..")
                throw std::runtime_error("Generic project Assets must be inside its project directory.");
        }
        return project;
    }
};

} // namespace engine

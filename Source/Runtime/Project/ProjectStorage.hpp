#pragma once

#include "ProjectDescriptor.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace engine {
namespace project_storage {
namespace fs = std::filesystem;

inline bool Within(const fs::path& child, const fs::path& parent) {
    const auto relative = fs::weakly_canonical(child).lexically_relative(fs::weakly_canonical(parent));
    return !relative.empty() && *relative.begin() != "..";
}

inline void WriteJson(const fs::path& path, const nlohmann::json& value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Cannot write " + ProjectPathText(path));
    stream << value.dump(2) << '\n';
    stream.close();
    if (!stream) throw std::runtime_error("Could not finish writing " + ProjectPathText(path));
}

inline void ReplaceFile(const fs::path& source, const fs::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace " + ProjectPathText(target) +
            " (Windows error " + std::to_string(GetLastError()) + ").");
#else
    fs::rename(source, target);
#endif
}

// Files are staged before modifying the destination. Existing files are backed
// up, the descriptor is committed last, and failed commits restore prior files.
// Only our own newly-created staging/package directories are removed on error.
inline ProjectDescriptor Save(const fs::path& requested, const fs::path& activeAssets,
    const nlohmann::json& scene) {
    ValidateSceneDocument(scene);
    const auto descriptor = fs::weakly_canonical(fs::absolute(requested));
    auto extension = ProjectPathText(descriptor.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".steerproj") throw std::runtime_error("Choose a .steerproj filename.");
    if (!fs::is_directory(descriptor.parent_path()))
        throw std::runtime_error("The selected project directory does not exist.");
    const auto source = fs::weakly_canonical(fs::absolute(activeAssets));
    if (!fs::is_directory(source / "Shaders"))
        throw std::runtime_error("The active Assets directory has no Shaders directory.");
    ValidateSceneDocumentAssets(scene, source);
    if (Within(descriptor, source))
        throw std::runtime_error("Choose a project location outside the active Assets directory.");
    const auto packageName = descriptor.stem().native() + fs::path(".steer").native();
    const auto package = descriptor.parent_path() / packageName;
    const auto marker = package / ".steer-project.json";
    const nlohmann::json ownership = {{"schemaVersion", 1}, {"project", ProjectPathText(descriptor.filename())}};
    const bool newPackage = !fs::exists(package);
    if (!newPackage) {
        if (fs::is_symlink(package) || !fs::is_regular_file(marker))
            throw std::runtime_error("The companion .steer folder already exists and is not owned by this project. Choose another filename.");
        std::ifstream markerStream(marker);
        if (!markerStream || nlohmann::json::parse(markerStream) != ownership)
            throw std::runtime_error("The companion .steer folder belongs to another project. Choose another filename.");
    }
    if (fs::exists(descriptor)) {
        // Only establish ownership of the existing destination here. Loading
        // its old scene would prevent a valid edited scene from repairing a
        // deleted model reference, missing snapshot, or other stale disk data.
        std::ifstream existingStream(descriptor);
        if (!existingStream) throw std::runtime_error("Cannot read the existing project descriptor.");
        const auto existing = nlohmann::json::parse(existingStream);
        const auto pointsTo = [&](const char* key, const fs::path& expected) {
            if (!existing.is_object() || !existing.contains(key) || !existing[key].is_string()) return false;
            const auto value = existing[key].get<std::string>();
            if (value.empty() || value.find('\0') != std::string::npos) return false;
            const auto relative = fs::u8path(value);
            if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) return false;
            return fs::weakly_canonical(descriptor.parent_path() / relative) == fs::weakly_canonical(expected);
        };
        if (!existing.is_object() || !existing.contains("schemaVersion") || !existing["schemaVersion"].is_number_integer() ||
            existing["schemaVersion"] != 1 || existing.value("contentModule", nlohmann::json{}) != "scene" ||
            existing.value("startupScene", nlohmann::json{}) != "main" ||
            !pointsTo("assetRoot", package / "Assets") ||
            !pointsTo("sceneSnapshot", package / "Scenes/Main.scene.json") || newPackage)
            throw std::runtime_error("This file is not an editable scene project created by Save Project As. Choose another filename.");
    }
    else if (!newPackage) {
        throw std::runtime_error("A companion .steer folder already exists without its project file. Choose another filename.");
    }
    static std::atomic<unsigned long long> sequence{0};
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(sequence.fetch_add(1));
    const auto staging = descriptor.parent_path() / (".steer-save-" + token);
    if (!fs::create_directory(staging)) throw std::runtime_error("Could not create a unique project staging directory.");
    struct Replacement {
        fs::path staged;
        fs::path target;
        fs::path backup;
        bool installed = false;
        bool hadOriginal = false;
    };
    std::vector<Replacement> replacements;
    const auto stageJson = [&](const fs::path& relative, const fs::path& target, const nlohmann::json& value) {
        const auto file = staging / "files" / relative;
        WriteJson(file, value);
        replacements.push_back({file, target});
    };
    bool packageCreated = false;
    try {
        const auto destinationAssets = package / "Assets";
        if (fs::weakly_canonical(source) != fs::weakly_canonical(destinationAssets)) {
            for (const auto& item : fs::recursive_directory_iterator(source)) {
                if (item.is_symlink())
                    throw std::runtime_error("Project packaging requires regular asset files; symbolic link: " + ProjectPathText(item.path()));
                if (!item.is_regular_file()) continue;
                const auto relative = item.path().lexically_relative(source);
                const auto target = destinationAssets / relative;
                if (fs::exists(target) && (fs::is_symlink(target) || !fs::is_regular_file(target)))
                    throw std::runtime_error("Asset destination is not a regular file: " + ProjectPathText(target));
                // Copies retain timestamps, so later saves only stage changed/new assets.
                if (fs::is_regular_file(target) && fs::file_size(target) == item.file_size() &&
                    fs::last_write_time(target) == item.last_write_time()) continue;
                const auto staged = staging / "files/Assets" / relative;
                fs::create_directories(staged.parent_path());
                fs::copy_file(item.path(), staged);
                fs::last_write_time(staged, item.last_write_time());
                replacements.push_back({staged, target});
            }
        }
        stageJson("Main.scene.json", package / "Scenes/Main.scene.json", scene);
        stageJson("owner.json", marker, ownership);
        const nlohmann::json manifest = {
            {"schemaVersion", 1}, {"name", ProjectPathText(descriptor.stem())},
            {"contentModule", "scene"}, {"startupScene", "main"},
            {"assetRoot", ProjectPathText(fs::path(packageName) / "Assets")},
            {"sceneSnapshot", ProjectPathText(fs::path(packageName) / "Scenes/Main.scene.json")}
        };
        stageJson("project.steerproj", descriptor, manifest); // Commit the entry point last.
        if (newPackage) {
            packageCreated = fs::create_directory(package);
            if (!packageCreated) throw std::runtime_error("The project destination changed during save. Choose another filename.");
        }
        fs::create_directories(package / "Assets/Shaders");
        fs::create_directory(staging / "backup");
        for (std::size_t index = 0; index < replacements.size(); ++index) {
            auto& replacement = replacements[index];
            if (replacement.target != descriptor && !Within(replacement.target, package))
                throw std::runtime_error("An asset destination escapes the project directory.");
            fs::create_directories(replacement.target.parent_path());
            replacement.hadOriginal = fs::exists(replacement.target);
            if (replacement.hadOriginal) {
                if (fs::is_symlink(replacement.target) || !fs::is_regular_file(replacement.target))
                    throw std::runtime_error("Cannot overwrite a non-file project destination.");
                replacement.backup = staging / "backup" / std::to_string(index);
                fs::copy_file(replacement.target, replacement.backup);
                fs::last_write_time(replacement.backup, fs::last_write_time(replacement.target));
            }
            ReplaceFile(replacement.staged, replacement.target);
            replacement.installed = true;
        }
        const auto saved = ProjectDescriptor::Load(descriptor);
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        return saved;
    }
    catch (const std::exception& error) {
        std::string failure = error.what();
        bool restored = true;
        for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
            if (!it->installed) continue;
            try {
                if (it->hadOriginal) ReplaceFile(it->backup, it->target);
                else fs::remove(it->target);
            }
            catch (const std::exception& rollbackError) {
                restored = false;
                failure += " Restore failed: " + std::string(rollbackError.what());
            }
        }
        std::error_code ignored;
        if (restored) {
            if (packageCreated) fs::remove_all(package, ignored);
            fs::remove_all(staging, ignored);
        }
        else failure += " Recovery files were kept in " + ProjectPathText(staging);
        throw std::runtime_error(failure);
    }
}
} // namespace project_storage
} // namespace engine

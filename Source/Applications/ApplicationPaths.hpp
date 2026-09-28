#pragma once

#include <filesystem>
#include <stdexcept>

namespace engine::apps {

// Both executables can be launched from the repository, Visual Studio, or
// their configuration-specific Bin directory without losing relative assets.
inline void UseRepositoryAssets(const char* executable) {
    namespace fs = std::filesystem;
    const auto hasAssets = [](const fs::path& directory) {
        return fs::is_directory(directory / "Assets" / "Shaders");
    };
    // Prefer the installed engine beside the executable. The caller mounts an
    // explicitly selected project's assets only after resolving the project.
    fs::path directory = fs::absolute(executable).parent_path();
    while (!directory.empty()) {
        if (hasAssets(directory)) {
            fs::current_path(directory);
            return;
        }
        const auto parent = directory.parent_path();
        if (parent == directory) break;
        directory = parent;
    }
    if (hasAssets(fs::current_path())) return;
    throw std::runtime_error("Assets/Shaders not found beside the application or in its parent directories.");
}
}

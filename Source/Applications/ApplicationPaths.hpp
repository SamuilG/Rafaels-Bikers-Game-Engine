#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace engine::apps {

inline std::vector<std::filesystem::path> CommandLinePaths(int argc, char** argv) {
    std::vector<std::filesystem::path> arguments;
#ifdef _WIN32
    // The CRT narrow argv uses the active code page and can lose user-selected
    // project names. Keep the OS UTF-16 command line as native filesystem paths.
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) throw std::runtime_error("Cannot read the Unicode command line.");
    try {
        arguments.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) arguments.emplace_back(wide[index]);
    }
    catch (...) { LocalFree(wide); throw; }
    LocalFree(wide);
    (void)argc;
    (void)argv;
#else
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) arguments.emplace_back(argv[index]);
#endif
    return arguments;
}

// Both executables can be launched from the repository, Visual Studio, or
// their configuration-specific Bin directory without losing relative assets.
inline void UseRepositoryAssets(const std::filesystem::path& executable) {
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

#include "../ApplicationPaths.hpp"
#include "Projects/Bikers/BikersProject.hpp"
#include "Runtime/Core/Application.hpp"
#include "Runtime/Project/ProjectDescriptor.hpp"
#include "Runtime/LoadEngineSplash/StartupSplash.hpp"
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) try {
    namespace fs = std::filesystem;
    fs::path projectPath;
    // Resolve arguments before discovery or the project asset mount changes cwd.
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        if (argument == "--project") {
            if (++index >= argc) throw std::runtime_error("--project requires a project file.");
            argument = argv[index];
        }
        else if (argument.starts_with('-')) {
            throw std::runtime_error("Unknown Demo argument: " + std::string(argument));
        }
        if (argument.empty() || !projectPath.empty())
            throw std::runtime_error("Specify one project file: Demo.exe [--project] <file.steerproj>.");
        projectPath = fs::absolute(fs::path(argument));
    }

    engine::apps::UseRepositoryAssets(argv[0]);
    if (projectPath.empty())
        projectPath = fs::current_path() / "Projects/Bikers/Bikers.steerproj";
    const auto project = engine::ProjectDescriptor::Load(projectPath);
    auto options = engine::bikers::CreateOptions(project, false);
    fs::current_path(project.assetRoot.parent_path());

    engine::StartupSplash splash;
    splash.SetVideoPath(L"Assets/Splash/video.mp4");
    splash.Show();
    engine::Application app(std::move(options), [&splash](float progress, std::string_view stage) {
        splash.SetProgress(progress, stage);
    });
    splash.Close();
    app.ShowMainWindow();
    app.Run();
    return 0;
}
catch (const std::exception& error) {
    std::print(stderr, "Demo startup failed: {}\n", error.what());
    return 1;
}

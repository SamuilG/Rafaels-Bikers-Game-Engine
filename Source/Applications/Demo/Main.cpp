#include "../ApplicationPaths.hpp"
#include "Projects/Bikers/BikersProject.hpp"
#include "Runtime/Core/Application.hpp"
#include "Runtime/Project/ProjectDescriptor.hpp"
#include "Runtime/Project/GenericProject.hpp"
#include "Runtime/LoadEngineSplash/StartupSplash.hpp"
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) try {
    namespace fs = std::filesystem;
    const auto arguments = engine::apps::CommandLinePaths(argc, argv);
    fs::path projectPath;
    // Resolve arguments before discovery or the project asset mount changes cwd.
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        auto argument = arguments[index];
        if (argument == "--project") {
            if (++index >= arguments.size()) throw std::runtime_error("--project requires a project file.");
            argument = arguments[index];
        }
        else if (!argument.empty() && argument.native().front() == '-') {
            throw std::runtime_error("Unknown Demo argument: " + engine::ProjectPathText(argument));
        }
        if (argument.empty() || !projectPath.empty())
            throw std::runtime_error("Specify one project file: Demo.exe [--project] <file.steerproj>.");
        projectPath = fs::absolute(argument);
    }

    engine::apps::UseRepositoryAssets(arguments.front());
    if (projectPath.empty())
        projectPath = fs::current_path() / "Projects/Bikers/Bikers.steerproj";
    const auto project = engine::ProjectDescriptor::Load(projectPath);
    auto options = project.contentModule == "scene"
        ? engine::GenericProjectOptions(project, false)
        : engine::bikers::CreateOptions(project, false);
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

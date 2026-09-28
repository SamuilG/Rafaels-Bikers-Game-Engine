#include "../ApplicationPaths.hpp"
#include "Runtime/Core/Application.hpp"
#include "Runtime/Project/ProjectDescriptor.hpp"
#include "Runtime/Project/EditorProjectActions.hpp"
#include "Runtime/Renderer/RenderSystem.hpp"
#include "Runtime/Scene/blank.hpp"
#include "Projects/Bikers/BikersProject.hpp"
#include <optional>
#include <print>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;

engine::ApplicationOptions ProjectOptions(const engine::ProjectDescriptor& project) {
    // Executable-owned registry: Engine has no dependency on native game modules.
    if (project.contentModule == "bikers") return engine::bikers::CreateOptions(project, true);
    throw std::runtime_error("This editor has no registered content module: " + project.contentModule);
}

std::string RunProject(const fs::path& executable, const engine::ProjectDescriptor& project) {
    if (!fs::is_regular_file(executable))
        return "Build the Demo target for this configuration before running the project.";
#ifdef _WIN32
    // No shell is involved; project paths are passed directly to the executable.
    std::wstring command = L"\"" + executable.wstring() + L"\" --project \"" + project.filePath.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto directory = executable.parent_path().wstring();
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
        return "Could not start Demo (Windows error " + std::to_string(GetLastError()) + ").";
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
#else
    return "Run Demo --project <project file> from the command line on this platform.";
#endif
}
}

int main(int argc, char** argv) try {
    const auto executable = fs::absolute(argv[0]);
    std::optional<fs::path> initialProject;
    if (argc == 2) initialProject = fs::absolute(argv[1]);
    else if (argc == 3 && std::string(argv[1]) == "--project") initialProject = fs::absolute(argv[2]);
    else if (argc != 1) throw std::runtime_error("Usage: Editor [--project] <file.steerproj>");
    engine::apps::UseRepositoryAssets(argv[0]);
    const auto engineDirectory = fs::current_path();
    const auto exampleProject = engineDirectory / "Projects/Bikers/Bikers.steerproj";
#ifdef _WIN32
    const auto demoExecutable = executable.parent_path() / "Demo.exe";
#else
    const auto demoExecutable = executable.parent_path() / "Demo";
#endif
    std::optional<engine::ProjectDescriptor> currentProject;
    if (initialProject) {
        currentProject = engine::ProjectDescriptor::Load(*initialProject);
        ProjectOptions(*currentProject);
    }

    for (;;) {
        std::optional<engine::ProjectDescriptor> nextProject;
        bool closeProject = false;
        engine::ApplicationOptions options;
        if (currentProject) {
            options = ProjectOptions(*currentProject);
            fs::current_path(currentProject->assetRoot.parent_path());
        } else {
            fs::current_path(engineDirectory);
            options.createScene = [] { return std::make_unique<engine::blank>(); };
            options.windowTitle = "Steer Engine - Editor";
            options.imguiIniPath = (engineDirectory / "Intermediate/Layouts/Editor.ini").string();
            options.editorAvailable = true;
            options.showEditorInitially = true;
            options.editingProject = true;
        }
        auto configureContent = std::move(options.configureRenderer);
        options.configureRenderer = [&, configureContent = std::move(configureContent)](engine::RenderSystem& renderer) {
            if (configureContent) configureContent(renderer);
            engine::EditorProjectActions actions;
            if (fs::is_regular_file(exampleProject)) actions.exampleProjectFile = exampleProject.string();
            if (currentProject) {
                actions.projectName = currentProject->name;
                actions.projectFile = currentProject->filePath.string();
                actions.snapshotFile = currentProject->snapshotPath.string();
                actions.runProject = [&] { return RunProject(demoExecutable, *currentProject); };
                actions.closeProject = [&] { closeProject = true; };
            }
            actions.openProject = [&](const std::string& path) -> std::string {
                try {
                    auto candidate = engine::ProjectDescriptor::Load(fs::path(path));
                    ProjectOptions(candidate);
                    nextProject = std::move(candidate);
                    return {};
                } catch (const std::exception& error) {
                    return error.what();
                }
            };
            renderer.SetEditorProjectActions(std::move(actions));
        };
        {
            // Destroy device/world resources before switching asset directories.
            engine::Application app(std::move(options));
            app.ShowMainWindow();
            app.Run();
        }
        if (nextProject) currentProject = std::move(nextProject);
        else if (closeProject) currentProject.reset();
        else break;
    }
    return 0;
}
catch (const std::exception& error) {
    std::print(stderr, "Editor startup failed: {}\n", error.what());
#ifdef _WIN32
    MessageBoxA(nullptr, error.what(), "Steer Engine - Project Error", MB_OK | MB_ICONERROR);
#endif
    return 1;
}

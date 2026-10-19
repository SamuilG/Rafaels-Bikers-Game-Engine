#pragma once

#include "ProjectDescriptor.hpp"
#include "Runtime/Core/ApplicationOptions.hpp"
#include "Runtime/Scene/blank.hpp"

namespace engine {

inline ApplicationOptions GenericProjectOptions(const ProjectDescriptor& project, bool editor) {
    if (project.contentModule != "scene" || project.startupScene != "main")
        throw std::runtime_error("Generic projects require contentModule scene and startupScene main.");
    ApplicationOptions options;
    options.createScene = [] { return std::make_unique<blank>(); };
    options.windowTitle = editor ? "Steer Engine - " + project.name : project.name;
    options.imguiIniPath = ProjectPathText(project.assetRoot.parent_path() / (editor ? "Editor.ini" : "Runtime.ini"));
    options.editorAvailable = editor;
    options.showEditorInitially = editor;
    options.editingProject = editor;
    options.playInEditorAvailable = editor;
    options.genericScene = true;
    options.startPlaying = !editor;
    options.sceneSnapshotPath = ProjectPathText(project.snapshotPath);
    return options;
}

} // namespace engine

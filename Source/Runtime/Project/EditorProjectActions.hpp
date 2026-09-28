#pragma once

#include <functional>
#include <string>

namespace engine {

// The editor submits host actions; it never destroys/reloads an Application
// from inside a render frame. Empty callback results mean success.
struct EditorProjectActions {
    std::string projectName;
    std::string projectFile;
    std::string snapshotFile;
    std::string exampleProjectFile;
    std::string error;
    std::function<std::string(const std::string&)> openProject;
    std::function<void()> closeProject;
    std::function<std::string()> runProject;
};

} // namespace engine

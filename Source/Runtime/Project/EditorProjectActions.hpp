#pragma once

#include <functional>
#include <string>
#include "ThirdParty/json/json.hpp"

namespace engine {

// The editor submits host actions; it never destroys/reloads an Application
// from inside a render frame. Empty callback results mean success.
struct EditorProjectActions {
    std::string projectName;
    std::string projectFile;
    std::string snapshotFile;
    std::string exampleProjectFile;
    std::string error;
    bool genericScene = false;
    std::function<std::string(const std::string&)> openProject;
    // A successful save binds this session to the chosen descriptor without
    // destroying its authoring world or changing the active asset directory.
    std::function<std::string(const std::string&, const nlohmann::json&)> saveProject;
    std::function<void()> closeProject;
    std::function<std::string()> runProject;
};

} // namespace engine

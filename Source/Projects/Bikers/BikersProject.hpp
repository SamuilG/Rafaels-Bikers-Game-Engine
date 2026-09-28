#pragma once

#include "Runtime/Core/ApplicationOptions.hpp"

namespace engine {
struct ProjectDescriptor;
}

namespace engine::bikers {

// Both hosts use the same content composition, with distinct starting modes.
ApplicationOptions CreateOptions(const ProjectDescriptor& project, bool editor);

}

#pragma once

#include "ThirdParty/json/json.hpp"
#include "SceneDocumentValidation.hpp"

namespace engine {
class SceneManager;
class RenderSystem;

// Authoring document, not a snapshot of transient simulation/GPU state.
// These functions throw on malformed data or unsupported native-only content.
nlohmann::json CaptureSceneDocument(SceneManager& scene, RenderSystem& renderer);

// Decode model resources and check saved node references without touching a
// renderer/world. Hosts use this before closing the current editable project.
void PreflightSceneDocumentResources(const nlohmann::json& document,
    const std::filesystem::path& assetRoot);

// The caller supplies a fresh scene and owns rollback if resource loading fails.
// A pre-existing default PlayerCamera is reused. All other authored entities
// must be absent. Validation/resource preflight happens before world mutation.
void InstantiateSceneDocument(const nlohmann::json& document,
    SceneManager& scene, RenderSystem& renderer);
} // namespace engine

#pragma once

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include "Runtime/UserState/CameraSettings.hpp"
#include "ThirdParty/json/json.hpp"

namespace engine {

// Only authoring settings are persisted. Follow, portal and cinematic runtime
// state must never replace the settings that the editor will restore on Stop.
inline nlohmann::json SerializeCameraSettings(const CameraSettings& settings) {
    if (!settings.IsValid())
        throw std::runtime_error("Cannot save invalid camera settings.");
    return {
        {"Mode", settings.mode == CameraMode::Follow ? "Follow" : "Free"},
        {"AutomaticFov", settings.automaticFov},
        {"Fov", settings.fov},
        {"NearPlane", settings.nearPlane},
        {"FarPlane", settings.farPlane},
        {"Yaw", settings.yaw},
        {"Pitch", settings.pitch},
        {"Distance", settings.distance},
        {"OcclusionFadeEnabled", settings.occlusionFadeEnabled},
        {"OcclusionFadeCoverage", settings.occlusionFadeCoverage},
        {"OcclusionFadeSpeed", settings.occlusionFadeSpeed},
        {"OcclusionTargetHeight", settings.occlusionTargetHeight}
    };
}

inline CameraSettings DeserializeCameraSettings(const nlohmann::json& value) {
    if (!value.is_object())
        throw std::runtime_error("Camera must be an object.");
    CameraSettings settings;
    const auto boolean = [&](const char* field, bool& destination) {
        if (!value.contains(field)) return;
        if (!value[field].is_boolean())
            throw std::runtime_error(std::string("Camera.") + field + " must be a boolean.");
        destination = value[field].get<bool>();
    };
    const auto number = [&](const char* field, float& destination) {
        if (!value.contains(field)) return;
        if (!value[field].is_number())
            throw std::runtime_error(std::string("Camera.") + field + " must be a finite float value.");
        const double source = value[field].get<double>();
        if (!std::isfinite(source) || std::abs(source) > std::numeric_limits<float>::max())
            throw std::runtime_error(std::string("Camera.") + field + " must be a finite float value.");
        destination = static_cast<float>(source);
    };
    if (value.contains("Mode")) {
        if (!value["Mode"].is_string())
            throw std::runtime_error("Camera.Mode must be Follow or Free.");
        const auto& mode = value["Mode"].get_ref<const std::string&>();
        if (mode == "Follow") settings.mode = CameraMode::Follow;
        else if (mode == "Free") settings.mode = CameraMode::Free;
        else throw std::runtime_error("Camera.Mode must be Follow or Free.");
    }
    boolean("AutomaticFov", settings.automaticFov);
    number("Fov", settings.fov);
    number("NearPlane", settings.nearPlane);
    number("FarPlane", settings.farPlane);
    number("Yaw", settings.yaw);
    number("Pitch", settings.pitch);
    number("Distance", settings.distance);
    boolean("OcclusionFadeEnabled", settings.occlusionFadeEnabled);
    number("OcclusionFadeCoverage", settings.occlusionFadeCoverage);
    number("OcclusionFadeSpeed", settings.occlusionFadeSpeed);
    number("OcclusionTargetHeight", settings.occlusionTargetHeight);
    // Use the same bounds as the editor/controller; loading never silently
    // clamps malformed authoring data or accepts transient camera modes.
    if (!settings.IsValid())
        throw std::runtime_error("Camera settings are outside the supported ranges or have invalid clipping planes.");
    return settings;
}

} // namespace engine

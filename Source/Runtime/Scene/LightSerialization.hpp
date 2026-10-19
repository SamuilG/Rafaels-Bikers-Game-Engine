#pragma once
#include "../Renderer/RenderUtilities/light.hpp"
#include "ThirdParty/json/json.hpp"
#include <stdexcept>
#include <limits>
#include <string>

namespace engine {
inline nlohmann::json SerializeLight(const LightComponent& light) {
    if (!light.IsValid()) throw std::runtime_error("Cannot save invalid light settings.");
    const auto& s = light.shadow;
    return {{"type", uint32_t(light.type)}, {"color", {light.color.x, light.color.y, light.color.z}},
        {"intensity", light.intensity}, {"range", light.range}, {"specularMultiplier", light.specularMultiplier},
        {"direction", {light.direction.x, light.direction.y, light.direction.z}},
        {"innerCutOff", light.innerCutOff}, {"outerCutOff", light.outerCutOff},
        {"primaryDirectional", light.primaryDirectional}, {"castsShadows", light.castsShadows},
        {"shadow", {{"firstSplit", s.firstSplit}, {"secondSplit", s.secondSplit}, {"distance", s.distance},
            {"receiverBias", s.receiverBias}, {"filterRadius", s.filterRadius}, {"blendFraction", s.blendFraction},
            {"strength", s.strength}, {"constantBias", s.constantBias}, {"slopeBias", s.slopeBias},
            {"casterPadding", s.casterPadding}, {"depthPadding", s.depthPadding}}}};
}

inline LightComponent DeserializeLight(const nlohmann::json& value) {
    using Json = nlohmann::json;
    if (!value.is_object()) throw std::runtime_error("Light must be an object.");
    LightComponent light;
    const auto number = [](const Json& source, const char* key, float& field) {
        if (!source.contains(key)) return;
        if (!source[key].is_number()) throw std::runtime_error(std::string("Light.") + key + " must be a finite number.");
        const double v = source[key].get<double>();
        if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
            throw std::runtime_error(std::string("Light.") + key + " must be finite.");
        field = float(v);
    };
    const auto boolean = [&](const char* key, bool& field) {
        if (!value.contains(key)) return;
        if (!value[key].is_boolean()) throw std::runtime_error(std::string("Light.") + key + " must be a boolean.");
        field = value[key].get<bool>();
    };
    const auto vector = [&](const char* key, glm::vec3& field) {
        if (!value.contains(key)) return;
        const auto& v = value[key];
        if (!v.is_array() || v.size() != 3) throw std::runtime_error(std::string("Light.") + key + " must contain three numbers.");
        for (int i = 0; i < 3; ++i) {
            Json one{{"v", v[i]}};
            number(one, "v", field[i]);
        }
    };
    if (value.contains("type")) {
        if (!value["type"].is_number_integer() || value["type"].get<double>() < 0 || value["type"].get<double>() > 2)
            throw std::runtime_error("Light.type is invalid.");
        light.type = static_cast<LightType>(value["type"].get<uint32_t>());
    }
    vector("color", light.color); vector("direction", light.direction);
    number(value, "intensity", light.intensity); number(value, "range", light.range);
    number(value, "specularMultiplier", light.specularMultiplier);
    number(value, "innerCutOff", light.innerCutOff); number(value, "outerCutOff", light.outerCutOff);
    boolean("primaryDirectional", light.primaryDirectional); boolean("castsShadows", light.castsShadows);
    if (value.contains("shadow")) {
        const auto& s = value["shadow"];
        if (!s.is_object()) throw std::runtime_error("Light.shadow must be an object.");
        auto& target = light.shadow;
        number(s, "firstSplit", target.firstSplit); number(s, "secondSplit", target.secondSplit);
        number(s, "distance", target.distance); number(s, "receiverBias", target.receiverBias);
        number(s, "filterRadius", target.filterRadius); number(s, "blendFraction", target.blendFraction);
        number(s, "strength", target.strength); number(s, "constantBias", target.constantBias);
        number(s, "slopeBias", target.slopeBias); number(s, "casterPadding", target.casterPadding);
        number(s, "depthPadding", target.depthPadding);
    }
    if (!light.IsValid()) throw std::runtime_error("Light settings or shadow ranges are invalid.");
    return light;
}
} // namespace engine

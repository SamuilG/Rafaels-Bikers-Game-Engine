#pragma once

#include "PortalShardEmitter.hpp"
#include "ThirdParty/json/json.hpp"
#include <stdexcept>
#include <string>

namespace engine {

inline nlohmann::json SerializePortalShardEmitter(const PortalShardEmitter& e) {
    if (!e.IsValid()) throw std::runtime_error("Cannot save invalid portal shard effect settings.");
    const auto vec2 = [](const glm::vec2& v) { return nlohmann::json::array({v.x, v.y}); };
    const auto& m = e.material;
    const auto& filter = e.screenFilter;
    const auto& wave = e.crossingWave;
    return {{"followRider", e.followRider}, {"count", e.count}, {"duration", e.duration},
        {"emissionDuration", e.emissionDuration}, {"fadeIn", e.fadeIn}, {"fadeOut", e.fadeOut},
        {"spawnRadius", vec2(e.spawnRadius)}, {"travelDistance", vec2(e.travelDistance)},
        {"spawnHeight", vec2(e.spawnHeight)}, {"riseDistance", vec2(e.riseDistance)}, {"size", vec2(e.size)},
        {"expansionSpeed", e.expansionSpeed}, {"orbitSpeed", e.orbitSpeed}, {"spinSpeed", vec2(e.spinSpeed)},
        {"material", {{"opacity", m.opacity}, {"ior", m.ior}, {"refraction", m.refraction},
            {"dispersion", m.dispersion}}},
        {"screenFilter", {{"enabled", filter.enabled}, {"strength", filter.strength}, {"fadeOut", filter.fadeOut}}},
        {"crossingWave", {{"enabled", wave.enabled}, {"duration", wave.duration}, {"radius", wave.radius},
            {"width", wave.width}, {"strength", wave.strength}, {"textureScale", wave.textureScale},
            {"groundLift", wave.groundLift}, {"groundThickness", wave.groundThickness}}}};
}

inline PortalShardEmitter DeserializePortalShardEmitter(const nlohmann::json& value) {
    using Json = nlohmann::json;
    const auto fail = [](const char* key) {
        throw std::runtime_error(std::string("Invalid portal shard effect field: ") + key);
    };
    if (!value.is_object()) fail("effect");
    const auto number = [&](const Json& source, const char* key, float& field) {
        if (!source.contains(key)) return;
        if (!source[key].is_number()) fail(key);
        field = source[key].get<float>();
    };
    const auto vector = [&](const char* key, glm::vec2& field) {
        if (!value.contains(key)) return;
        const auto& v = value[key];
        if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) fail(key);
        field = {v[0].get<float>(), v[1].get<float>()};
    };
    PortalShardEmitter e;
    if (value.contains("followRider")) {
        if (!value["followRider"].is_boolean()) fail("followRider");
        e.followRider = value["followRider"].get<bool>();
    }
    if (value.contains("count")) {
        if (!value["count"].is_number_integer() || value["count"].get<double>() < 1.0 ||
            value["count"].get<double>() > PortalShardEmitter::kMaxCount) fail("count");
        e.count = value["count"].get<int>();
    }
    number(value, "duration", e.duration);
    number(value, "emissionDuration", e.emissionDuration);
    number(value, "fadeIn", e.fadeIn);
    number(value, "fadeOut", e.fadeOut);
    number(value, "expansionSpeed", e.expansionSpeed);
    number(value, "orbitSpeed", e.orbitSpeed);
    vector("spawnRadius", e.spawnRadius);
    vector("travelDistance", e.travelDistance);
    vector("spawnHeight", e.spawnHeight);
    vector("riseDistance", e.riseDistance);
    vector("size", e.size);
    vector("spinSpeed", e.spinSpeed);
    if (value.contains("material")) {
        const auto& m = value["material"];
        if (!m.is_object()) fail("material");
        number(m, "opacity", e.material.opacity);
        number(m, "ior", e.material.ior);
        number(m, "refraction", e.material.refraction);
        number(m, "dispersion", e.material.dispersion);
    }
    if (value.contains("screenFilter")) {
        const auto& filter = value["screenFilter"];
        if (!filter.is_object()) fail("screenFilter");
        if (filter.contains("enabled")) {
            if (!filter["enabled"].is_boolean()) fail("screenFilter.enabled");
            e.screenFilter.enabled = filter["enabled"].get<bool>();
        }
        number(filter, "strength", e.screenFilter.strength);
        number(filter, "fadeOut", e.screenFilter.fadeOut);
    }
    if (value.contains("crossingWave")) {
        const auto& wave = value["crossingWave"];
        if (!wave.is_object()) fail("crossingWave");
        if (wave.contains("enabled")) {
            if (!wave["enabled"].is_boolean()) fail("crossingWave.enabled");
            e.crossingWave.enabled = wave["enabled"].get<bool>();
        }
        number(wave, "duration", e.crossingWave.duration);
        number(wave, "radius", e.crossingWave.radius);
        number(wave, "width", e.crossingWave.width);
        number(wave, "strength", e.crossingWave.strength);
        number(wave, "textureScale", e.crossingWave.textureScale);
        number(wave, "groundLift", e.crossingWave.groundLift);
        number(wave, "groundThickness", e.crossingWave.groundThickness);
    }
    if (!e.IsValid()) fail("parameter range");
    return e;
}

} // namespace engine

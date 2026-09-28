#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include "ThirdParty/json/json.hpp"

namespace engine {

// Validate the entire document before applying any change to the live world.
// Unknown fields remain allowed for forwards compatibility; particle fields
// and the two top-level collections retain their legacy optional behavior.
inline void ValidateSceneSnapshot(const nlohmann::json& document) {
    using Json = nlohmann::json;
    const auto fail = [](const std::string& path, const char* expected) {
        throw std::runtime_error("Scene snapshot " + path + " must be " + expected + ".");
    };
    const auto finiteFloat = [&](const Json& value, const std::string& path) {
        if (!value.is_number()) fail(path, "a finite number");
        const double number = value.get<double>();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
            fail(path, "a finite float value");
    };
    const auto vector = [&](const Json& value, std::size_t count, const std::string& path) {
        if (!value.is_array() || value.size() != count)
            throw std::runtime_error("Scene snapshot " + path + " must be an array of " + std::to_string(count) + " numbers.");
        for (std::size_t index = 0; index < count; ++index)
            finiteFloat(value[index], path + "[" + std::to_string(index) + "]");
    };
    const auto text = [&](const Json& value, const std::string& path) {
        if (!value.is_string() || value.get_ref<const std::string&>().find('\0') != std::string::npos)
            fail(path, "a string without embedded null characters");
    };
    const auto integer = [&](const Json& value, double minimum, double maximum, const std::string& path) {
        if (!value.is_number_integer()) fail(path, "an integer");
        const double number = value.get<double>();
        if (number < minimum || number > maximum) fail(path, "an integer within the destination type's range");
    };
    if (!document.is_object()) fail("root", "an object");
    if (document.contains("Entities")) {
        const auto& entities = document["Entities"];
        if (!entities.is_array()) fail("Entities", "an array");
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const auto& entity = entities[index];
            const std::string path = "Entities[" + std::to_string(index) + "]";
            if (!entity.is_object()) fail(path, "an object");
            for (const auto* field : {"Name", "Position", "Rotation", "Scale"}) {
                if (!entity.contains(field))
                    throw std::runtime_error("Scene snapshot " + path + " is missing " + field + ".");
            }
            text(entity["Name"], path + ".Name");
            for (const auto* field : {"Position", "Rotation", "Scale"})
                vector(entity[field], 3, path + "." + field);
        }
    }
    if (document.contains("Particles")) {
        const auto& particles = document["Particles"];
        if (!particles.is_array()) fail("Particles", "an array");
        for (std::size_t index = 0; index < particles.size(); ++index) {
            const auto& particle = particles[index];
            const std::string path = "Particles[" + std::to_string(index) + "]";
            if (!particle.is_object()) fail(path, "an object");
            if (particle.contains("Name")) text(particle["Name"], path + ".Name");
            for (const auto* field : {"EmitterPos", "Gravity"})
                if (particle.contains(field)) vector(particle[field], 3, path + "." + field);
            for (const auto* field : {"StartColor", "EndColor"})
                if (particle.contains(field)) vector(particle[field], 4, path + "." + field);
            for (const auto* field : {"LifeMin", "LifeMax", "SpeedMin", "SpeedMax", "SizeMin", "SizeMax", "ConeSpread", "SphereRadius"})
                if (particle.contains(field)) finiteFloat(particle[field], path + "." + field);
            for (const auto* field : {"UseTexture", "AtlasCols", "AtlasRows"})
                if (particle.contains(field)) integer(particle[field], std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), path + "." + field);
            // EmitterShape currently defines Cone, Sphere, Box and Disk.
            if (particle.contains("Shape")) integer(particle["Shape"], 0, 3, path + ".Shape");
            if (particle.contains("AnimateAtlas") && !particle["AnimateAtlas"].is_boolean())
                fail(path + ".AnimateAtlas", "a boolean");
            if (particle.contains("MaxParticles"))
                integer(particle["MaxParticles"], 0, std::numeric_limits<std::uint32_t>::max(), path + ".MaxParticles");
        }
    }
}

} // namespace engine

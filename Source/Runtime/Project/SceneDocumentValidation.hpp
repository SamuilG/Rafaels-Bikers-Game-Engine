#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include "ThirdParty/json/json.hpp"
#include "Runtime/Scene/CameraSerialization.hpp"
#include "Runtime/Scene/LightSerialization.hpp"

namespace engine {
namespace scene_document_detail {
using Json = nlohmann::json;

[[noreturn]] inline void Invalid(const std::string& location, const std::string& message) {
    throw std::runtime_error("Scene document " + location + ": " + message);
}

inline std::string Text(const Json& value, const std::string& path, std::size_t maximum = 1024) {
    if (!value.is_string()) Invalid(path, "expected a string");
    const auto& result = value.get_ref<const std::string&>();
    if (result.size() > maximum || result.find('\0') != std::string::npos)
        Invalid(path, "invalid or excessively long string");
    return result;
}

inline std::string Id(const Json& value, const std::string& path) {
    auto result = Text(value, path, 128);
    if (result.empty() || !std::all_of(result.begin(), result.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_';
    })) Invalid(path, "expected a non-empty stable ID containing letters, digits, '-' or '_'");
    return result;
}

inline double Number(const Json& value, const std::string& path,
    double minimum = -std::numeric_limits<float>::max(), double maximum = std::numeric_limits<float>::max()) {
    if (!value.is_number()) Invalid(path, "expected a finite number");
    const double result = value.get<double>();
    if (!std::isfinite(result) || result < minimum || result > maximum)
        Invalid(path, "number is outside the supported range");
    return result;
}

inline std::uint32_t Integer(const Json& value, const std::string& path,
    std::uint32_t minimum, std::uint32_t maximum) {
    if (!value.is_number_integer()) Invalid(path, "expected an integer");
    return static_cast<std::uint32_t>(Number(value, path, minimum, maximum));
}

inline void Boolean(const Json& value, const std::string& path) {
    if (!value.is_boolean()) Invalid(path, "expected a boolean");
}

inline void Object(const Json& value, const std::string& path, std::initializer_list<const char*> fields) {
    if (!value.is_object()) Invalid(path, "expected an object");
    for (const auto* field : fields)
        if (!value.contains(field)) Invalid(path, std::string("missing '") + field + "'");
}

inline void Vector(const Json& value, std::size_t count, const std::string& path,
    double minimum = -std::numeric_limits<float>::max(), double maximum = std::numeric_limits<float>::max()) {
    if (!value.is_array() || value.size() != count) Invalid(path, "invalid vector length");
    for (const auto& number : value) Number(number, path, minimum, maximum);
}

inline glm::mat4 Matrix(const Json& value, const std::string& path, bool rigid = false) {
    Vector(value, 16, path, -100000000.0, 100000000.0);
    glm::mat4 result(1.0f);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) result[column][row] = value[column * 4 + row].get<float>();
    if (std::abs(result[0][3]) > 0.00001f || std::abs(result[1][3]) > 0.00001f ||
        std::abs(result[2][3]) > 0.00001f || std::abs(result[3][3] - 1.0f) > 0.00001f)
        Invalid(path, "transform must be affine");
    const float determinant = glm::determinant(glm::mat3(result));
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f)
        Invalid(path, "transform must be invertible");
    if (rigid) {
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 basis(result[axis]);
            if (std::abs(glm::length(basis) - 1.0f) > 0.001f)
                Invalid(path, "camera transform must have unit scale");
            if (std::abs(glm::dot(basis, glm::vec3(result[(axis + 1) % 3]))) > 0.001f)
                Invalid(path, "camera transform must not contain shear");
        }
        if (determinant < 0.0f) Invalid(path, "camera transform must not be reflected");
    }
    return result;
}

inline std::string Asset(const Json& value, const std::string& path) {
    const auto result = Text(value, path);
    if (result.empty() || result.find('\\') != std::string::npos || result.find(':') != std::string::npos)
        Invalid(path, "expected an Assets-relative path using '/' separators");
    const std::filesystem::path asset = std::filesystem::u8path(result);
    if (asset.is_absolute() || asset.has_root_name() || asset.begin() == asset.end() || *asset.begin() != "Assets")
        Invalid(path, "asset must be inside Assets");
    for (const auto& part : asset) if (part == ".." || part == "." || part.empty())
        Invalid(path, "asset path must not contain traversal components");
    const auto normalizedBytes = asset.lexically_normal().generic_u8string();
    const std::string normalized(reinterpret_cast<const char*>(normalizedBytes.data()), normalizedBytes.size());
    if (normalized != result || result == "Assets")
        Invalid(path, "asset must be a normalized path below Assets");
    return result;
}

inline void Particle(const Json& value, const std::string& path) {
    Object(value, path, {"name", "shape", "count", "visible", "useTexture", "texture", "icon",
        "emitterPos", "emitDir", "gravity", "boxArea", "lifeMin", "lifeMax", "speedMin", "speedMax",
        "sizeMin", "sizeMax", "startColor", "endColor", "coneSpread", "sphereRadius", "atlasCols", "atlasRows",
        "animateAtlas", "startSizeScale", "endSizeScale", "rotationMin", "rotationMax", "debug"});
    Text(value["name"], path + ".name", 63);
    Integer(value["shape"], path + ".shape", 0, 3);
    Integer(value["count"], path + ".count", 1, 1000000);
    Integer(value["useTexture"], path + ".useTexture", 0, 1);
    Integer(value["atlasCols"], path + ".atlasCols", 1, 256);
    Integer(value["atlasRows"], path + ".atlasRows", 1, 256);
    for (const auto* key : {"visible", "animateAtlas", "debug"}) Boolean(value[key], path + "." + key);
    for (const auto* key : {"texture", "icon"}) {
        const auto resource = Text(value[key], path + "." + key);
        if (!resource.empty()) Asset(value[key], path + "." + key);
    }
    if (value["useTexture"].get<int>() != 0 && value["texture"].get<std::string>().empty())
        Invalid(path, "textured emitter needs a texture resource");
    for (const auto* key : {"emitterPos", "emitDir", "gravity"}) Vector(value[key], 3, path + "." + key, -10000000, 10000000);
    Vector(value["boxArea"], 3, path + ".boxArea", 0, 100000);
    const auto& dir = value["emitDir"];
    if (dir[0].get<double>() * dir[0].get<double>() + dir[1].get<double>() * dir[1].get<double>() +
        dir[2].get<double>() * dir[2].get<double>() < 1e-12) Invalid(path, "emitter direction must be nonzero");
    for (const auto* key : {"startColor", "endColor"}) Vector(value[key], 4, path + "." + key, 0, 100000);
    for (const auto* key : {"lifeMin", "lifeMax"}) Number(value[key], path + "." + key, 0.001, 100000);
    for (const auto* key : {"speedMin", "speedMax", "rotationMin", "rotationMax"}) Number(value[key], path + "." + key, -100000, 100000);
    for (const auto* key : {"sizeMin", "sizeMax", "startSizeScale", "endSizeScale", "coneSpread", "sphereRadius"})
        Number(value[key], path + "." + key, 0, 100000);
    for (const auto& pair : {std::pair{"lifeMin", "lifeMax"}, {"speedMin", "speedMax"}, {"sizeMin", "sizeMax"}, {"rotationMin", "rotationMax"}})
        if (value[pair.first].get<double>() > value[pair.second].get<double>())
            Invalid(path, std::string(pair.first) + " exceeds " + pair.second);
}

// A GLB can refer to image/buffer files despite having a binary container.
// Inspect only its bounded JSON chunk; packaging the full Assets directory is
// sufficient exactly when every non-data URI stays within that directory.
inline void ModelDependencies(const std::filesystem::path& file,
    const std::filesystem::path& assetRoot, const std::string& assetName) {
    namespace fs = std::filesystem;
    std::ifstream stream(file, std::ios::binary);
    unsigned char header[20]{};
    if (!stream.read(reinterpret_cast<char*>(header), sizeof(header)))
        Invalid("model.asset", "incomplete GLB header: " + assetName);
    const auto word = [&](int offset) {
        return static_cast<std::uint32_t>(header[offset]) |
            (static_cast<std::uint32_t>(header[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(header[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(header[offset + 3]) << 24);
    };
    const auto jsonBytes = word(12);
    if (word(0) != 0x46546c67 || word(4) != 2 || word(8) != fs::file_size(file) || word(16) != 0x4e4f534a ||
        jsonBytes == 0 || jsonBytes > 64 * 1024 * 1024 || static_cast<std::uint64_t>(jsonBytes) + 20 > word(8))
        Invalid("model.asset", "invalid or excessively large GLB JSON chunk: " + assetName);
    std::string source(jsonBytes, '\0');
    if (!stream.read(source.data(), source.size())) Invalid("model.asset", "incomplete GLB JSON chunk: " + assetName);
    const auto model = Json::parse(source);
    if (!model.is_object()) Invalid("model.asset", "GLB JSON must be an object: " + assetName);
    const auto digit = [&](unsigned char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        Invalid("model.uri", "malformed percent escape in " + assetName);
    };
    for (const auto* kind : {"buffers", "images"}) {
        if (!model.contains(kind)) continue;
        if (!model[kind].is_array() || model[kind].size() > 100000) Invalid("model.asset", "invalid GLB resource list");
        for (const auto& resource : model[kind]) {
            if (!resource.is_object()) Invalid("model.asset", "invalid GLB resource record");
            if (!resource.contains("uri")) continue;
            const auto uri = Text(resource["uri"], "model.uri", 64 * 1024 * 1024);
            if (uri.starts_with("data:")) continue;
            if (uri.empty() || uri.size() > 32768) Invalid("model.uri", "empty or excessively long external URI");
            std::string decoded;
            for (std::size_t index = 0; index < uri.size(); ++index) {
                if (uri[index] == '%') {
                    if (index + 2 >= uri.size()) Invalid("model.uri", "incomplete percent escape");
                    decoded.push_back(static_cast<char>((digit(uri[index + 1]) << 4) | digit(uri[index + 2])));
                    index += 2;
                }
                else decoded.push_back(uri[index]);
            }
            if (decoded.find('\0') != std::string::npos || decoded.find(':') != std::string::npos ||
                decoded.find('\\') != std::string::npos)
                Invalid("model.uri", "external resources must use relative file URIs inside Assets: " + assetName);
            const auto dependency = fs::u8path(decoded);
            if (dependency.is_absolute() || dependency.has_root_name() || dependency.has_root_directory())
                Invalid("model.uri", "absolute external resource is not portable: " + assetName);
            const auto resolved = fs::weakly_canonical(file.parent_path() / dependency);
            const auto relative = resolved.lexically_relative(assetRoot);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                Invalid("model.uri", "external resource escapes Assets: " + assetName + " -> " + uri);
            if (!fs::is_regular_file(resolved) || fs::file_size(resolved) == 0)
                Invalid("model.uri", "external resource is missing or empty: " + assetName + " -> " + uri);
        }
    }
}
} // namespace scene_document_detail

// Pure validation is deliberately independent of Vulkan/Flecs and file IO.
inline void ValidateSceneDocument(const nlohmann::json& document) {
    using namespace scene_document_detail;
    Object(document, "root", {"schemaVersion", "models", "entities", "particles"});
    Integer(document["schemaVersion"], "schemaVersion", 1, 1);
    for (const auto* key : {"models", "entities", "particles"})
        if (!document[key].is_array() || document[key].size() > 100000) Invalid(key, "expected a bounded array");
    if (document["particles"].size() > 256) Invalid("particles", "at most 256 emitters are supported");
    std::unordered_set<std::string> modelIds;
    for (const auto& model : document["models"]) {
        Object(model, "models[]", {"id", "asset", "physicsType", "mass", "transform"});
        const auto id = Id(model["id"], "model.id");
        if (!modelIds.insert(id).second) Invalid("models", "duplicate import ID " + id);
        const auto asset = Asset(model["asset"], "model.asset");
        std::string extension = std::filesystem::path(asset).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".glb") Invalid("model.asset", "only GLB model resources are supported");
        Integer(model["physicsType"], "model.physicsType", 0, 1); // Static / Dynamic; compound belongs to native modules.
        const auto mass = Number(model["mass"], "model.mass", -1, 100000000);
        if (model["physicsType"].get<int>() == 1 && mass == 0) Invalid("model.mass", "dynamic mass must be positive, or -1 to disable physics");
        Matrix(model["transform"], "model.transform");
    }
    std::unordered_map<std::string, std::string> parents;
    std::unordered_map<std::string, glm::mat4> transforms;
    std::set<std::pair<std::string, std::string>> siblingNames;
    std::set<std::pair<std::string, std::uint32_t>> importedParts;
    std::unordered_set<std::string> usedImports;
    std::size_t cameraCount = 0;
    for (const auto& entity : document["entities"]) {
        Object(entity, "entities[]", {"id", "name", "parent", "transform", "status", "layer"});
        const auto id = Id(entity["id"], "entity.id");
        const auto name = Text(entity["name"], "entity.name");
        if (name.find("::") != std::string::npos) Invalid("entity.name", "names must not contain Flecs path separators");
        std::string parent;
        if (!entity["parent"].is_null()) parent = Id(entity["parent"], "entity.parent");
        if (!parents.emplace(id, parent).second) Invalid("entities", "duplicate entity ID " + id);
        if (!name.empty() && !siblingNames.emplace(parent, name).second) Invalid("entities", "duplicate sibling name " + name);
        transforms.emplace(id, Matrix(entity["transform"], "entity.transform", entity.contains("camera")));
        Object(entity["status"], "entity.status", {"visible", "physics"});
        Boolean(entity["status"]["visible"], "entity.status.visible");
        Boolean(entity["status"]["physics"], "entity.status.physics");
        Integer(entity["layer"], "entity.layer", 0, 2);
        if (entity.contains("model")) {
            Object(entity["model"], "entity.model", {"instance", "part"});
            const auto import = Id(entity["model"]["instance"], "entity.model.instance");
            const auto part = Integer(entity["model"]["part"], "entity.model.part", 0, 1000000);
            if (!modelIds.contains(import)) Invalid("entity.model", "unknown import ID " + import);
            if (!importedParts.emplace(import, part).second) Invalid("entity.model", "duplicate model part");
            usedImports.insert(import);
        }
        else if (entity["status"]["physics"].get<bool>()) Invalid("entity", "physics requires a referenced model");
        if (entity.contains("camera")) {
            if (++cameraCount > 1 || name != "PlayerCamera" || !parent.empty() || entity.contains("model"))
                Invalid("entity.camera", "the single game camera must be the root PlayerCamera entity");
            Object(entity["camera"], "entity.camera", {"settings", "freeTransform"});
            (void)DeserializeCameraSettings(entity["camera"]["settings"]);
            Matrix(entity["camera"]["freeTransform"], "entity.camera.freeTransform", true);
        }
        else if (name == "PlayerCamera" && parent.empty()) Invalid("entity.name", "PlayerCamera is reserved for the game camera");
        if (entity.contains("light")) {
            const auto& light = entity["light"];
            Object(light, "entity.light", {"type", "color", "intensity", "range", "specularMultiplier", "direction", "innerCutOff", "outerCutOff"});
            (void)DeserializeLight(light);
        }
    }
    if (usedImports.size() != modelIds.size()) Invalid("models", "unreferenced model import");
    std::unordered_map<std::string, unsigned> visiting;
    std::unordered_map<std::string, glm::mat4> worldTransforms;
    std::function<glm::mat4(const std::string&, unsigned)> visit = [&](const std::string& id, unsigned depth) {
        if (depth > 256) Invalid("entities", "hierarchy exceeds 256 levels");
        if (visiting[id] == 1) Invalid("entities", "parent cycle at " + id);
        if (visiting[id] == 2) return worldTransforms.at(id);
        visiting[id] = 1;
        glm::mat4 world = transforms.at(id);
        const auto& parent = parents.at(id);
        if (!parent.empty()) {
            if (!parents.contains(parent)) Invalid("entity.parent", "missing parent " + parent);
            world = visit(parent, depth + 1) * world;
        }
        for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
            if (!std::isfinite(world[column][row]) || std::abs(world[column][row]) > 100000000)
                Invalid("entity.transform", "hierarchy produces an invalid world transform");
        visiting[id] = 2;
        worldTransforms[id] = world;
        return world;
    };
    for (const auto& [id, parent] : parents) visit(id, 0);
    for (const auto& entity : document["entities"]) if (entity["status"]["physics"].get<bool>()) {
        const auto& transform = worldTransforms.at(entity["id"].get<std::string>());
        const auto axes = glm::mat3(transform);
        for (int axis = 0; axis < 3; ++axis) {
            const float axisLength = glm::length(axes[axis]);
            if (!std::isfinite(axisLength) || axisLength < 0.000001f)
                Invalid("entity.transform", "physics world scale is degenerate or too small");
            const auto first = glm::normalize(axes[axis]);
            const auto second = glm::normalize(axes[(axis + 1) % 3]);
            if (std::abs(glm::dot(first, second)) > 0.0001f)
                Invalid("entity.transform", "physics does not support shear; remove the non-uniformly scaled/rotated parent transform");
        }
        // Negative scale is retained: Jolt's ScaledShape and the dynamic convex
        // loader both support reflected mesh vertices.
    }
    std::uint64_t totalParticles = 0;
    for (const auto& particle : document["particles"]) {
        Particle(particle, "particles[]");
        totalParticles += particle["count"].get<std::uint32_t>();
    }
    if (totalParticles > 4000000) Invalid("particles", "total particle capacity exceeds 4000000");
}

inline void ValidateSceneDocumentAssets(const nlohmann::json& document,
    const std::filesystem::path& assetRoot) {
    ValidateSceneDocument(document);
    namespace fs = std::filesystem;
    using namespace scene_document_detail;
    const auto root = fs::weakly_canonical(assetRoot);
    if (!fs::is_directory(root)) Invalid("assets", "Assets directory does not exist: " + root.string());
    const auto resource = [&](const Json& reference) {
        const auto source = Asset(reference, "asset");
        const auto suffix = fs::u8path(source).lexically_relative("Assets");
        const auto file = fs::weakly_canonical(root / suffix);
        const auto relative = file.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            Invalid("asset", "resource escapes Assets (including symbolic links): " + source);
        if (!fs::is_regular_file(file) || fs::file_size(file) == 0)
            Invalid("asset", "resource is missing or empty: " + source);
        return file;
    };
    for (const auto& model : document["models"])
        ModelDependencies(resource(model["asset"]), root, model["asset"].get<std::string>());
    for (const auto& particle : document["particles"])
        for (const auto* field : {"texture", "icon"}) if (!particle[field].get<std::string>().empty()) resource(particle[field]);
}
} // namespace engine

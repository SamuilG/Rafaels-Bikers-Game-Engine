#include "SceneManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <print>

#include <flecs.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "../Input/InputSystem.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Renderer/RenderUtilities/light.hpp"

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains_wheel_keyword(const std::string& value) {
    const std::string lowered = to_lower_copy(value);
    return lowered.find("wheel") != std::string::npos
        || lowered.find("tire") != std::string::npos
        || lowered.find("tyre") != std::string::npos
        || lowered.find("rim") != std::string::npos;
}

float estimate_wheel_radius(const EngineMesh& mesh) {
    if (mesh.positions.empty()) {
        return 1.0f;
    }

    glm::vec3 minPos(std::numeric_limits<float>::max());
    glm::vec3 maxPos(std::numeric_limits<float>::lowest());
    for (const glm::vec3& pos : mesh.positions) {
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }

    const glm::vec3 extents = glm::max(maxPos - minPos, glm::vec3(0.001f));
    return std::max(0.05f, std::max(extents.y, extents.z) * 0.5f);
}

bool should_spin_as_wheel(const EngineInstance& instance, const EngineMesh& mesh) {
    if (mesh.hasSkinning) {
        return false;
    }
    return contains_wheel_keyword(instance.name) || contains_wheel_keyword(mesh.name);
}

bool should_skip_bicycle_cable(const EngineInstance& instance, const EngineMesh& mesh) {
    const std::string instanceName = to_lower_copy(instance.name);
    const std::string meshName = to_lower_copy(mesh.name);
    return instanceName.find("cable") != std::string::npos
        || meshName.find("cable") != std::string::npos
        || meshName.find("casing") != std::string::npos;
}

bool should_steer_with_front(const EngineInstance& instance, const EngineMesh& mesh) {
    const std::string instanceName = to_lower_copy(instance.name);
    const std::string meshName = to_lower_copy(mesh.name);

    const bool isFrontWheel = instanceName.find("fwheel") != std::string::npos
        || meshName.find("fwheel") != std::string::npos
        || instanceName.find("frontwheel") != std::string::npos
        || meshName.find("frontwheel") != std::string::npos;
    const bool isFork = instanceName.find("fork") != std::string::npos
        || meshName.find("fork") != std::string::npos;
    const bool isHandlebar = instanceName.find("handlebar") != std::string::npos
        || meshName.find("handlebar") != std::string::npos
        || instanceName.find("taped_handlebars") != std::string::npos
        || meshName.find("taped_handlebars") != std::string::npos;
    const bool isStem = instanceName.find("stem") != std::string::npos
        || meshName.find("stem") != std::string::npos
        || instanceName.find("headtube") != std::string::npos
        || meshName.find("headtube") != std::string::npos
        || instanceName.find("head_tube") != std::string::npos
        || meshName.find("head_tube") != std::string::npos
        || instanceName.find("headset") != std::string::npos
        || meshName.find("headset") != std::string::npos;
    return isFrontWheel || isFork || isHandlebar || isStem;
}

int steering_priority(const EngineInstance& instance, const EngineMesh& mesh) {
    const std::string instanceName = to_lower_copy(instance.name);
    const std::string meshName = to_lower_copy(mesh.name);

    if (instanceName.find("fork") != std::string::npos || meshName.find("fork") != std::string::npos) {
        return 0;
    }
    if (instanceName.find("headtube") != std::string::npos || meshName.find("headtube") != std::string::npos
        || instanceName.find("head_tube") != std::string::npos || meshName.find("head_tube") != std::string::npos
        || instanceName.find("headset") != std::string::npos || meshName.find("headset") != std::string::npos
        || instanceName.find("stem") != std::string::npos || meshName.find("stem") != std::string::npos) {
        return 1;
    }
    if (instanceName.find("handlebar") != std::string::npos || meshName.find("handlebar") != std::string::npos
        || instanceName.find("taped_handlebars") != std::string::npos || meshName.find("taped_handlebars") != std::string::npos) {
        return 2;
    }
    if (instanceName.find("fwheel") != std::string::npos || meshName.find("fwheel") != std::string::npos
        || instanceName.find("frontwheel") != std::string::npos || meshName.find("frontwheel") != std::string::npos) {
        return 3;
    }
    return 100;
}

glm::vec3 extract_translation(const glm::mat4& transform) {
    return glm::vec3(transform[3]);
}

glm::vec3 find_steering_pivot(const EngineModel& model, const glm::mat4& invBody) {
    int bestPriority = 100;
    glm::vec3 pivot(0.0f);

    for (const auto& instance : model.scenes) {
        const EngineMesh& mesh = model.meshes[instance.meshIndex];
        if (!should_steer_with_front(instance, mesh)) {
            continue;
        }

        const int priority = steering_priority(instance, mesh);
        if (priority < bestPriority) {
            bestPriority = priority;
            pivot = extract_translation(invBody * instance.transform);
        }
    }

    return pivot;
}

}

EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors)
{
    EngineMesh mesh;
    const float pi = glm::pi<float>();

    for (uint32_t r = 0; r <= rings; ++r) {
        const float phi = pi * r / rings;
        for (uint32_t s = 0; s <= sectors; ++s) {
            const float theta = 2.0f * pi * s / sectors;

            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::cos(phi);
            const float z = std::sin(phi) * std::sin(theta);

            mesh.positions.emplace_back(radius * x, radius * y, radius * z);
            mesh.normals.emplace_back(x, y, z);
            mesh.texcoords.emplace_back(
                static_cast<float>(s) / sectors,
                static_cast<float>(r) / rings);
        }
    }

    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            const uint32_t a = r * (sectors + 1) + s;
            const uint32_t b = (r + 1) * (sectors + 1) + s;
            mesh.indices.push_back(a);     mesh.indices.push_back(b);     mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);     mesh.indices.push_back(b + 1); mesh.indices.push_back(a + 1);
        }
    }

    return mesh;
}

namespace engine {

SceneManager::SceneManager(PhysicsSystem* physics_system)
    : m_physics_system(physics_system) {
}

SceneManager::~SceneManager() {
    Shutdown();
}

void SceneManager::Init() {
    m_world = new flecs::world();

    m_world->system<WorldTransform, const LocalTransform>("UpdateWorldTransform")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, WorldTransform& wt, const LocalTransform& lt) {
            auto parent = e.parent();
            if (parent.is_valid() && parent.has<WorldTransform>()) {
                const WorldTransform* pwt = &parent.get<WorldTransform>();
                wt.matrix = pwt->matrix * lt.matrix;
            }
            else {
                wt.matrix = lt.matrix;
            }
        });
}

void SceneManager::Shutdown() {
    if (m_world) {
        delete m_world;
        m_world = nullptr;
    }
}

void SceneManager::load_static_model(const EngineModel& model, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
    std::print("Loading static model with {} instances\n", model.scenes.size());
    std::print("[Debug] load_static_model called on SceneManager at: {}\n", (void*)this);
    int counter = 0;
    for (const auto& instance : model.scenes) {
        std::string name = instance.name.empty() ? "StaticObject" : instance.name;
        name += "_" + std::to_string(counter++);

        auto e = m_world->entity(name.c_str())
            .add<StaticObject>()
            .set<EntityStatus>({ true, true })
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex + baseMeshIdx });

        const uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
        e.set<MaterialComponent>({ matIdx });

        if (m_physics_system) {
            JPH::BodyID bodyID = m_physics_system->create_static_mesh_body(model.meshes[instance.meshIndex], instance.transform);
            if (!bodyID.IsInvalid()) {
                e.set<PhysicsBody>({ bodyID.GetIndexAndSequenceNumber() });
            }
        }
    }
}

void SceneManager::load_dynamic_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
    std::print("Loading dynamic model with {} instances\n", model.scenes.size());
    std::print("[Debug] load_dynamic_model called on SceneManager at: {}\n", (void*)this);
    int counter = 0;
    for (const auto& instance : model.scenes) {
        std::string name = instance.name.empty() ? "DynamicObject" : instance.name;
        name += "_" + std::to_string(counter++);

        auto e = m_world->entity(name.c_str())
            .add<DynamicObject>()
            .set<EntityStatus>({ true, true })
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex + baseMeshIdx });

        const uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
        e.set<MaterialComponent>({ matIdx });

        if (m_physics_system) {
            JPH::BodyID bodyID = m_physics_system->create_dynamic_convex_body(model.meshes[instance.meshIndex], instance.transform, mass);
            if (!bodyID.IsInvalid()) {
                e.set<PhysicsBody>({ bodyID.GetIndexAndSequenceNumber() });
            }
        }
    }
}

void SceneManager::load_compound_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
    std::print("Loading compound model with {} instances\n", model.scenes.size());

    JPH::BodyID compoundBodyID;
    glm::mat4 bodyWorldTransform = model.scenes.empty() ? glm::mat4(1.0f) : model.scenes[0].transform;

    if (m_physics_system && !model.scenes.empty()) {
        std::vector<EngineMesh> meshes;
        std::vector<glm::mat4> meshTransforms;
        for (const auto& instance : model.scenes) {
            const EngineMesh& mesh = model.meshes[instance.meshIndex];
            if (should_skip_bicycle_cable(instance, mesh)) {
                continue;
            }
            meshes.push_back(mesh);
            meshTransforms.push_back(instance.transform);
        }
        if (!meshes.empty()) {
            compoundBodyID = m_physics_system->create_dynamic_compound_body(meshes, meshTransforms, bodyWorldTransform, mass);
        }
    }

    const glm::mat4 invBody = glm::inverse(bodyWorldTransform);

    const glm::vec3 steeringPivot = find_steering_pivot(model, invBody);

    int counter = 0;
    for (const auto& instance : model.scenes) {
        const EngineMesh& mesh = model.meshes[instance.meshIndex];
        if (should_skip_bicycle_cable(instance, mesh)) {
            continue;
        }

        std::string name = instance.name.empty() ? "CompoundPart" : instance.name;
        name += "_" + std::to_string(counter++);

        const glm::mat4 localOffset = invBody * instance.transform;

        auto e = m_world->entity(name.c_str())
            .add<DynamicObject>()
            .set<EntityStatus>({ true, false })
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex + baseMeshIdx })
            .set<CompoundParent>({ compoundBodyID.GetIndexAndSequenceNumber(), localOffset });

        const uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
        e.set<MaterialComponent>({ matIdx });

        if (should_spin_as_wheel(instance, mesh)) {
            e.set<WheelSpin>({ localOffset, glm::vec3(1.0f, 0.0f, 0.0f), estimate_wheel_radius(mesh), 0.0f });
        }
        if (should_steer_with_front(instance, mesh)) {
            e.set<SteeringVisual>({ localOffset, glm::vec3(0.0f, 1.0f, 0.0f), steeringPivot });
        }
    }
}

void SceneManager::load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
    std::print("Loading compound model with {} instances\n", model.scenes.size());

    glm::mat4 bodyWorldTransform = glm::mat4(1.0f);
    if (!model.scenes.empty()) {
        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;
        glm::decompose(model.scenes[0].transform, scale, rotation, translation, skew, perspective);
        rotation = glm::normalize(rotation);
        bodyWorldTransform = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
    }

    JPH::BodyID compoundBodyID;
    if (m_physics_system && !model.scenes.empty()) {
        std::vector<EngineMesh> meshes;
        std::vector<glm::mat4> meshTransforms;
        for (const auto& instance : model.scenes) {
            const EngineMesh& mesh = model.meshes[instance.meshIndex];
            if (should_skip_bicycle_cable(instance, mesh)) {
                continue;
            }
            meshes.push_back(mesh);
            meshTransforms.push_back(instance.transform);
        }
        if (!meshes.empty()) {
            compoundBodyID = m_physics_system->create_dynamic_compound_body(meshes, meshTransforms, bodyWorldTransform, mass);
        }
    }

    const glm::mat4 invBody = glm::inverse(bodyWorldTransform);

    const glm::vec3 steeringPivot = find_steering_pivot(model, invBody);

    int counter = 0;
    for (const auto& instance : model.scenes) {
        const EngineMesh& mesh = model.meshes[instance.meshIndex];
        if (should_skip_bicycle_cable(instance, mesh)) {
            continue;
        }

        std::string name = instance.name.empty() ? "CompoundPart" : instance.name;
        name += "_" + std::to_string(counter++);

        const glm::mat4 localOffset = invBody * instance.transform;

        auto e = m_world->entity(name.c_str())
            .add<DynamicObject>()
            .set<EntityStatus>({ true, true })
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex + baseMeshIdx })
            .set<CompoundParent>({ compoundBodyID.GetIndexAndSequenceNumber(), localOffset });

        const uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
        e.set<MaterialComponent>({ matIdx });

        if (should_spin_as_wheel(instance, mesh)) {
            e.set<WheelSpin>({ localOffset, glm::vec3(1.0f, 0.0f, 0.0f), estimate_wheel_radius(mesh), 0.0f });
        }
        if (should_steer_with_front(instance, mesh)) {
            e.set<SteeringVisual>({ localOffset, glm::vec3(0.0f, 1.0f, 0.0f), steeringPivot });
        }
    }
}

flecs::entity SceneManager::find_entity(const char* name) {
    return m_world->lookup(name);
}

void SceneManager::set_local_transform(flecs::entity e, const glm::mat4& transform) {
    if (e.is_valid()) {
        e.set<LocalTransform>({ transform });
    }
}

std::string SceneManager::get_entity_name_from_body_id(uint32_t bodyID) {
    std::string result = "Unknown Entity";
    m_world->query<const PhysicsBody>()
        .each([&](flecs::entity e, const PhysicsBody& pb) {
            if (pb.bodyID == bodyID) {
                result = e.name() ? e.name() : "unnamed";
            }
        });
    return result;
}

uint32_t SceneManager::find_compound_body_near(const glm::vec3& worldPos, float maxDistance) const {
    if (!m_world) {
        return ~0u;
    }

    uint32_t bestBodyID = ~0u;
    float bestDistanceSq = maxDistance * maxDistance;

    m_world->query<const WorldTransform, const CompoundParent>()
        .each([&](const WorldTransform& wt, const CompoundParent& cp) {
            const glm::vec3 entityPos = glm::vec3(wt.matrix[3]);
            const float distSq = glm::dot(entityPos - worldPos, entityPos - worldPos);
            if (distSq <= bestDistanceSq) {
                bestDistanceSq = distSq;
                bestBodyID = cp.bodyID;
            }
        });

    return bestBodyID;
}

glm::vec3 SceneManager::get_compound_body_world_position(uint32_t bodyID) const {
    if (!m_world || bodyID == ~0u) {
        return glm::vec3(0.0f);
    }

    glm::vec3 result(0.0f);
    bool found = false;

    m_world->query<const WorldTransform, const CompoundParent>()
        .each([&](const WorldTransform& wt, const CompoundParent& cp) {
            if (!found && cp.bodyID == bodyID) {
                result = glm::vec3(wt.matrix[3]);
                found = true;
            }
        });

    return result;
}

void SceneManager::Update(float dt) {
    if (m_physics_system) {
        JPH::BodyInterface& bodyInterface = m_physics_system->get_body_interface();

        m_world->query<LocalTransform, const PhysicsBody, const EntityStatus>()
            .each([&](flecs::entity e, LocalTransform& lt, const PhysicsBody& pb, const EntityStatus& status) {
                if (!status.has_physics) return;
                JPH::BodyID bodyID(pb.bodyID);
                if (bodyInterface.IsActive(bodyID)) {
                    const JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                    const JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), toGlm(pos));
                    const glm::mat4 rotation = glm::mat4_cast(toGlm(rot));
                    const glm::vec3 scale(
                        glm::length(glm::vec3(lt.matrix[0])),
                        glm::length(glm::vec3(lt.matrix[1])),
                        glm::length(glm::vec3(lt.matrix[2])));
                    lt.matrix = translation * rotation * glm::scale(glm::mat4(1.0f), scale);
                }
            });

        m_world->query<LocalTransform, const CompoundParent>()
            .each([&](flecs::entity e, LocalTransform& lt, const CompoundParent& cp) {
                JPH::BodyID bodyID(cp.bodyID);
                if (!bodyInterface.IsActive(bodyID)) {
                    return;
                }

                const JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                const JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                const glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), toGlm(pos)) * glm::mat4_cast(toGlm(rot));

                glm::mat4 localOffset = cp.localOffset;
                glm::vec3 scale(1.0f);
                glm::vec3 translation(0.0f);
                glm::vec3 skew(0.0f);
                glm::quat baseRotation(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec4 perspective(0.0f);
                bool hasBaseOffset = false;
                float wheelAngle = 0.0f;
                glm::vec3 wheelAxis(1.0f, 0.0f, 0.0f);
                float steeringAngle = 0.0f;
                glm::vec3 steeringAxis(0.0f, 1.0f, 0.0f);

                if (e.has<SteeringVisual>()) {
                    const SteeringVisual& steering = e.get<SteeringVisual>();
                    glm::decompose(steering.baseLocalOffset, scale, baseRotation, translation, skew, perspective);
                    baseRotation = glm::normalize(baseRotation);
                    localOffset = steering.baseLocalOffset;
                    steeringAxis = steering.localAxis;
                    translation -= steering.pivotLocalPosition;
                    hasBaseOffset = true;
                }

                if (e.has<WheelSpin>()) {
                    auto& wheel = e.get_mut<WheelSpin>();
                    if (!hasBaseOffset) {
                        glm::decompose(wheel.baseLocalOffset, scale, baseRotation, translation, skew, perspective);
                        baseRotation = glm::normalize(baseRotation);
                        localOffset = wheel.baseLocalOffset;
                        hasBaseOffset = true;
                    }

                    const glm::vec3 bodyForward = glm::normalize(glm::vec3(glm::mat4_cast(toGlm(rot)) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
                    const JPH::Vec3 linearVelocity = bodyInterface.GetLinearVelocity(bodyID);
                    const glm::vec3 velocity(linearVelocity.GetX(), linearVelocity.GetY(), linearVelocity.GetZ());
                    const float forwardSpeed = glm::dot(velocity, bodyForward);
                    wheel.angle -= (forwardSpeed * dt) / std::max(0.05f, wheel.radius);
                    wheelAngle = wheel.angle;
                    wheelAxis = wheel.localAxis;
                    scale *= wheel.visualScale;
                }

                if (e.has<SteeringVisual>()) {
                    auto& steering = e.get_mut<SteeringVisual>();
                    float steerInput = 0.0f;
                    if (m_input_system) {
                        if (m_input_system->IsActionHeld("StrafeLeft")) {
                            steerInput -= 1.0f;
                        }
                        if (m_input_system->IsActionHeld("StrafeRight")) {
                            steerInput += 1.0f;
                        }
                    }

                    const float targetAngle = -steerInput * steering.maxAngleRadians;
                    const float blend = glm::clamp(dt * steering.response, 0.0f, 1.0f);
                    steering.angle = glm::mix(steering.angle, targetAngle, blend);
                    steeringAngle = steering.angle;
                    steeringAxis = steering.localAxis;
                    localOffset = steering.baseLocalOffset;
                }

                if (hasBaseOffset) {
                    const glm::vec3 steeringPivot = e.has<SteeringVisual>()
                        ? e.get<SteeringVisual>().pivotLocalPosition
                        : glm::vec3(0.0f);
                    localOffset =
                        glm::translate(glm::mat4(1.0f), steeringPivot) *
                        glm::rotate(glm::mat4(1.0f), steeringAngle, steeringAxis) *
                        glm::translate(glm::mat4(1.0f), translation) *
                        glm::mat4_cast(baseRotation) *
                        glm::rotate(glm::mat4(1.0f), wheelAngle, wheelAxis) *
                        glm::scale(glm::mat4(1.0f), scale);
                }

                lt.matrix = bodyWorld * localOffset;
            });

        m_world->query<LocalTransform, const AttachedToCompoundBody>()
            .each([&](flecs::entity e, LocalTransform& lt, const AttachedToCompoundBody& attachment) {
                JPH::BodyID bodyID(attachment.bodyID);
                if (bodyInterface.IsActive(bodyID)) {
                    const JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                    const JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                    const glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), toGlm(pos)) * glm::mat4_cast(toGlm(rot));
                    lt.matrix = bodyWorld * attachment.localOffset;
                }
            });
    }

    m_world->progress(dt);
}

void SceneManager::print_all_entities() {
    std::print("\n--- [Entity List] ---\n");

    m_world->each([&](flecs::entity e, EntityStatus& status) {
        const char* name = e.name() ? e.name() : "unnamed";
        std::string typeStr = "Unknown";

        if (e.has<MeshComponent>()) {
            typeStr = "Mesh [" + std::to_string(e.get<MeshComponent>().meshIndex) + "]";
        }
        else if (e.has<LightComponent>()) {
            const auto& lc = e.get<LightComponent>();
            typeStr = (lc.type == LightType::Directional) ? "DirLight" : "PointLight";
        }

        std::print("Entity: {:<15} | Type: {:<10} | Render: {} | Physics: {}\n",
            name, typeStr,
            status.should_render ? "ON" : "OFF",
            status.has_physics ? "ON" : "OFF");
    });
    std::print("--------------------------------------\n\n");
}

flecs::entity SceneManager::create_light_entity(
    const char* name,
    LightType type,
    glm::vec3 color,
    float intensity,
    const glm::mat4& transform,
    float range,
    glm::vec3 direction,
    float innerCutOff,
    float outerCutOff)
{
    return m_world->entity(name)
        .set<EntityStatus>({ true, false })
        .set<LocalTransform>({ transform })
        .set<WorldTransform>({ transform })
        .set<LightComponent>({ color, intensity, type, range, direction, innerCutOff, outerCutOff });
}

void SceneManager::get_light_data(std::vector<GpuLight>& outLights) {
    outLights.clear();

    m_world->query<const WorldTransform, const LightComponent, const EntityStatus>()
        .each([&](const WorldTransform& wt, const LightComponent& lc, const EntityStatus& status) {
            if (!status.should_render) return;
            if (outLights.size() >= 16) return;

            GpuLight gpuLight{};
            gpuLight.position = glm::vec4(glm::vec3(wt.matrix[3]), static_cast<float>(lc.type));
            gpuLight.color = glm::vec4(lc.color, lc.intensity);

            const glm::vec3 worldDir = glm::normalize(glm::vec3(wt.matrix * glm::vec4(lc.direction, 0.0f)));
            gpuLight.direction = glm::vec4(worldDir, lc.range);

            const float cosInner = glm::cos(glm::radians(lc.innerCutOff));
            const float cosOuter = glm::cos(glm::radians(lc.outerCutOff));
            gpuLight.params = glm::vec4(cosInner, cosOuter, 0.0f, 0.0f);

            outLights.push_back(gpuLight);
        });
}

std::vector<RenderBatch> SceneManager::get_render_batches() {
    std::vector<RenderBatch> batches;

    m_world->query<const WorldTransform, const MeshComponent, const MaterialComponent, const EntityStatus>()
        .each([&](const WorldTransform& wt, const MeshComponent& mc, const MaterialComponent& matc, const EntityStatus& status) {
            if (status.should_render) {
                batches.push_back({ mc.meshIndex, matc.materialIndex, wt.matrix });
            }
        });

    return batches;
}

flecs::entity SceneManager::create_dynamic_entity(
    const char* name, uint32_t meshIndex, uint32_t matIndex,
    const glm::mat4& transform, uint32_t physicsBodyID)
{
    auto e = m_world->entity(name)
        .add<DynamicObject>()
        .set<EntityStatus>({ true, true })
        .set<LocalTransform>({ transform })
        .set<WorldTransform>({ transform })
        .set<MeshComponent>({ meshIndex })
        .set<MaterialComponent>({ matIndex });

    if (physicsBodyID != ~0u) {
        e.set<PhysicsBody>({ physicsBodyID });
    }

    return e;
}

flecs::entity SceneManager::raycast_entity(const glm::vec3& origin, const glm::vec3& direction, float max_distance)
{
    if (!m_physics_system) return flecs::entity::null();

    const uint32_t hitBodyID = m_physics_system->cast_ray(origin, direction, max_distance);
    if (hitBodyID == JPH::BodyID::cInvalidBodyID) {
        return flecs::entity::null();
    }

    m_physics_system->get_body_interface().ActivateBody(JPH::BodyID(hitBodyID));

    flecs::entity hitEntity = flecs::entity::null();
    m_world->query<const PhysicsBody>().each([&](flecs::entity e, const PhysicsBody& pb) {
        if (pb.bodyID == hitBodyID) {
            hitEntity = e;
        }
    });

    return hitEntity;
}

} // namespace engine

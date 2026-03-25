#include "SceneManager.hpp"

#include <flecs.h>
#include    <print>
#include "../Physics/PhysicsSystem.hpp"




#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include"../Renderer/RenderUtilities/light.hpp"
#include <glm/gtx/matrix_decompose.hpp> 



namespace engine {
    
SceneManager::SceneManager(PhysicsSystem* physics_system) 
    : m_physics_system(physics_system) {
}

SceneManager::~SceneManager() {
    Shutdown();
}

void SceneManager::Init() {
    m_world = new flecs::world();


    // Robust hierarchical transform system
    m_world->system<WorldTransform, const LocalTransform>("UpdateWorldTransform")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, WorldTransform& wt, const LocalTransform& lt) {
            auto parent = e.parent();

            // Fix: First check if the parent entity exists and has the component
            if (parent.is_valid() && parent.has<WorldTransform>()) {
                // Use .get<T>() to get a reference, and receive its address with a pointer
                const WorldTransform* pwt = &parent.get<WorldTransform>();
                wt.matrix = pwt->matrix * lt.matrix;
            }
            else {
                // If it is a root node, the world matrix is directly equal to the local matrix
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

        uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
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

        uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
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

    // 1. Create compound body
    JPH::BodyID compoundBodyID;
    glm::mat4 bodyWorldTransform = model.scenes.empty() ? glm::mat4(1.0f) : model.scenes[0].transform;

    if (m_physics_system && !model.scenes.empty()) {
        std::vector<EngineMesh> meshes;
        std::vector<glm::mat4> meshTransforms;
        for (const auto& instance : model.scenes) {
            meshes.push_back(model.meshes[instance.meshIndex]);
            meshTransforms.push_back(instance.transform);
        }
        compoundBodyID = m_physics_system->create_dynamic_compound_body(
            meshes, meshTransforms, bodyWorldTransform, mass);
    }

    // Inverse of initial body transform, to compute each part's local offset
    glm::mat4 invBody = glm::inverse(bodyWorldTransform);

    // 2. Create render entities
    int counter = 0;
    for (const auto& instance : model.scenes) {
        std::string name = instance.name.empty() ? "CompoundPart" : instance.name;
        name += "_" + std::to_string(counter++);

        // This part's offset relative to the body origin
        glm::mat4 localOffset = invBody * instance.transform;

        auto e = m_world->entity(name.c_str())
            .add<DynamicObject>()
            .set<EntityStatus>({ true, false })  // No individual physics
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex + baseMeshIdx })
            .set<CompoundParent>({ compoundBodyID.GetIndexAndSequenceNumber(), localOffset });

        uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
        e.set<MaterialComponent>({ matIdx });
    }
}

void SceneManager::load_C_model(const EngineModel& model, float mass, uint32_t baseMeshIdx, uint32_t baseMatIdx) {
    std::print("Loading compound model with {} instances\n", model.scenes.size());

    // 1. Create compound body

    glm::mat4 bodyWorldTransform = glm::mat4(1.0f);

    if (!model.scenes.empty()) {
        glm::vec3 scale, translation, skew;
        glm::quat rotation;
        glm::vec4 perspective;

        // 分解第一个网格的变换矩阵
        glm::decompose(model.scenes[0].transform, scale, rotation, translation, skew, perspective);
        rotation = glm::normalize(rotation);

        // 重建一个纯净的矩阵：只保留 平移 (Translation) 和 旋转 (Rotation)，丢弃缩放！
        bodyWorldTransform = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
    }

    JPH::BodyID compoundBodyID;
    if (m_physics_system && !model.scenes.empty()) {
        std::vector<EngineMesh> meshes;
        std::vector<glm::mat4> meshTransforms;
        for (const auto& instance : model.scenes) {
            meshes.push_back(model.meshes[instance.meshIndex]);
            meshTransforms.push_back(instance.transform);
        }
        compoundBodyID = m_physics_system->create_dynamic_compound_body(
            meshes, meshTransforms, bodyWorldTransform, mass);
    }

    // Inverse of initial body transform (现在它是没有缩放的了)
    glm::mat4 invBody = glm::inverse(bodyWorldTransform);

    // 2. Create render entities
    int counter = 0;
    for (const auto& instance : model.scenes) {
        std::string name = instance.name.empty() ? "CPart" : instance.name;
        name += "_" + std::to_string(counter++);

        // 这里的 localOffset 将完美保留每个零件自己原本的缩放比例
        glm::mat4 localOffset = invBody * instance.transform;

        auto e = m_world->entity(name.c_str())
            .add<DynamicObject>()
            .set<EntityStatus>({ true, true })
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex + baseMeshIdx })
            .set<CompoundParent>({ compoundBodyID.GetIndexAndSequenceNumber(), localOffset });

        uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex + baseMatIdx;
        e.set<MaterialComponent>({ matIdx });
    }
}



flecs::entity SceneManager::find_entity(const char* name) {
    return m_world->lookup(name); // Flecs' efficient lookup interface
}

void SceneManager::set_local_transform(flecs::entity e, const glm::mat4& transform) {
    if (e.is_valid()) {
        e.set<LocalTransform>({ transform }); // Update component, which automatically triggers the UpdateWorldTransform system
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

void SceneManager::Update(float dt) {
    if (m_physics_system) {
        JPH::BodyInterface& bodyInterface = m_physics_system->get_body_interface();


        m_world->query<LocalTransform, const PhysicsBody, const EntityStatus>()
            .each([&](flecs::entity e, LocalTransform& lt, const PhysicsBody& pb, const EntityStatus& status) {
            if (!status.has_physics) return;
            JPH::BodyID bodyID(pb.bodyID);
            if (bodyInterface.IsActive(bodyID)) {
                JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                glm::mat4 translation = glm::translate(glm::mat4(1.0f), toGlm(pos));
                glm::mat4 rotation = glm::mat4_cast(toGlm(rot));
                glm::vec3 scale(
                    glm::length(glm::vec3(lt.matrix[0])),
                    glm::length(glm::vec3(lt.matrix[1])),
                    glm::length(glm::vec3(lt.matrix[2]))
                );
                lt.matrix = translation * rotation * glm::scale(glm::mat4(1.0f), scale);
            }
                });


        m_world->query<LocalTransform, const CompoundParent>()
            .each([&](flecs::entity e, LocalTransform& lt, const CompoundParent& cp) {
            JPH::BodyID bodyID(cp.bodyID);
            if (bodyInterface.IsActive(bodyID)) {
                JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), toGlm(pos))
                    * glm::mat4_cast(toGlm(rot));
 
                lt.matrix = bodyWorld * cp.localOffset;
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
            auto& lc = e.get<LightComponent>();
            if (lc.type == LightType::Directional) typeStr = "DirLight";
            else if (lc.type == LightType::Spot) typeStr = "SpotLight";
            else typeStr = "PointLight";
        }

        // --- 【新增】获取物理 BodyID 字符串 ---
        std::string bodyIdStr = "None";
        if (e.has<PhysicsBody>()) {
            bodyIdStr = std::to_string(e.get<PhysicsBody>().bodyID);
        }
        else if (e.has<CompoundParent>()) {
            // 如果是复合模型的子零件，也会把主刚体的 ID 打印出来，并标记为 (Sub)
            bodyIdStr = std::to_string(e.get<CompoundParent>().bodyID) + " (Sub)";
        }

        // 扩展了打印格式，加上了 BodyID
        std::print("Entity: {:<18} | Type: {:<12} | Render: {} | Physics: {} | BodyID: {}\n",
            name, typeStr,
            status.should_render ? "ON" : "OFF",
            status.has_physics ? "ON" : "OFF",
            bodyIdStr);
        });
    std::print("--------------------------------------\n\n");
}

// 在 SceneManager.cpp 中：
flecs::entity SceneManager::create_light_entity(
    const char* name,
    LightType type,
    glm::vec3 color,
    float intensity,
    const glm::mat4& transform,
    float range,
    // --- 【新增参数】 ---
    glm::vec3 direction,
    float innerCutOff,
    float outerCutOff)
{
    return m_world->entity(name)
        .set<EntityStatus>({ true, false }) // 光源通常不需要参与物理同步
        .set<LocalTransform>({ transform })
        .set<WorldTransform>({ transform })
        // --- 【关键修改】：按顺序把新参数全部塞进去 ---
        .set<LightComponent>({ color, intensity, type, range, direction, innerCutOff, outerCutOff });
}


void SceneManager::get_light_data(std::vector<GpuLight>& outLights) {
    outLights.clear();

    // 查询所有具有 LightComponent 和 WorldTransform 的实体
    m_world->query<const WorldTransform, const LightComponent, const EntityStatus>()
        .each([&](const WorldTransform& wt, const LightComponent& lc, const EntityStatus& status) {
        if (!status.should_render) return; // 如果关闭了渲染，则不贡献光照
        if (outLights.size() >= 16) return; // 假设 Shader 最大支持 16 个光源

        GpuLight gpuLight{};

        // 提取世界坐标：矩阵第4列 (wt.matrix[3])
        // 对于定向光，w=0；点光源，w=1；聚光灯，w=2
        gpuLight.position = glm::vec4(glm::vec3(wt.matrix[3]), static_cast<float>(lc.type));

        // 颜色与强度
        gpuLight.color = glm::vec4(lc.color, lc.intensity);

        // --- 【新增计算】光照方向与范围 ---
        // 使用矩阵将局部朝向 (lc.direction) 转换到世界空间（忽略平移，只取旋转，所以 w=0.0f）
        glm::vec3 worldDir = glm::normalize(glm::vec3(wt.matrix * glm::vec4(lc.direction, 0.0f)));

        // 把 range 移到了 direction 的 w 通道
        gpuLight.direction = glm::vec4(worldDir, lc.range);

        // --- 【新增计算】预计算光锥角度的 cos 值 ---
        // 这样 Shader 里就不用算昂贵的三角函数了
        float cosInner = glm::cos(glm::radians(lc.innerCutOff));
        float cosOuter = glm::cos(glm::radians(lc.outerCutOff));
        gpuLight.params = glm::vec4(cosInner, cosOuter, 0.0f, 0.0f);

        outLights.push_back(gpuLight);
            });
}
std::vector<RenderBatch> SceneManager::get_render_batches() {
    std::vector<RenderBatch> batches;

    // Query all entities with WorldTransform, MeshComponent, and MaterialComponent
    m_world->query<const WorldTransform, const MeshComponent, const MaterialComponent, const EntityStatus>()
        .each([&](const WorldTransform& wt, const MeshComponent& mc, const MaterialComponent& matc, const EntityStatus& status) {
        // 只有 should_render 为 true 时才加入渲染批次
        if (status.should_render) {
            batches.push_back({ mc.meshIndex, matc.materialIndex, wt.matrix });
        }
            });
    return batches;
}

} // namespace engine



EngineMesh generate_uv_sphere(float radius, uint32_t rings, uint32_t sectors)
{
    EngineMesh mesh;
    const float pi = glm::pi<float>();

    for (uint32_t r = 0; r <= rings; ++r) {
        float const phi = pi * r / rings; // 0 .. pi
        for (uint32_t s = 0; s <= sectors; ++s) {
            float const theta = 2.f * pi * s / sectors; // 0 .. 2pi

            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);

            mesh.positions.emplace_back(radius * x, radius * y, radius * z);
            mesh.normals.emplace_back(x, y, z);
            mesh.texcoords.emplace_back(
                static_cast<float>(s) / sectors,
                static_cast<float>(r) / rings);
        }
    }

    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint32_t a = r       * (sectors + 1) + s;
            uint32_t b = (r + 1) * (sectors + 1) + s;
            mesh.indices.push_back(a);     mesh.indices.push_back(b);     mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);     mesh.indices.push_back(b + 1); mesh.indices.push_back(a + 1);
        }
    }

    return mesh;
}

namespace engine {

	
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

//==========UI System======================
// Raycast from origin in direction, return first hit entity射线检测，返回第一个被击中的实体
flecs::entity SceneManager::raycast_entity(const glm::vec3& origin, const glm::vec3& direction, float max_distance)
{
    if (!m_physics_system) return flecs::entity::null();

    uint32_t hitBodyID = m_physics_system->cast_ray(origin, direction, max_distance);

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
//==========UI System======================


} // namespace enginea
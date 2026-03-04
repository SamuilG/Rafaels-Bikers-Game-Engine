#include "SceneManager.hpp"

#include <flecs.h>
#include    <print>
#include "../Physics/PhysicsSystem.hpp"




#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>




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

flecs::entity SceneManager::find_entity(const char* name) {
    return m_world->lookup(name); // Flecs' efficient lookup interface
}

void SceneManager::set_local_transform(flecs::entity e, const glm::mat4& transform) {
    if (e.is_valid()) {
        e.set<LocalTransform>({ transform }); // Update component, which automatically triggers the UpdateWorldTransform system
    }
}

void SceneManager::Update(float dt) {
    if (m_physics_system) {
        m_world->query<LocalTransform, const PhysicsBody, const EntityStatus>()
            .each([&](flecs::entity e, LocalTransform& lt, const PhysicsBody& pb, const EntityStatus& status) {
            // 只有 has_physics 为 true 时才同步
            if (!status.has_physics) return;

                JPH::BodyID bodyID(pb.bodyID);
                JPH::BodyInterface& bodyInterface = m_physics_system->get_body_interface();
                
                // read from body whether it is active or asleep
                if (bodyInterface.IsActive(bodyID)) {
                    JPH::Vec3 pos = bodyInterface.GetPosition(bodyID);
                    JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                    
                    glm::mat4 translation = glm::translate(glm::mat4(1.0f), toGlm(pos));
                    glm::mat4 rotation = glm::mat4_cast(toGlm(rot));
                    
                    // extract original scale using column vectors instead of rows
                    glm::vec3 scale(
                        glm::length(glm::vec3(lt.matrix[0])),
                        glm::length(glm::vec3(lt.matrix[1])),
                        glm::length(glm::vec3(lt.matrix[2]))
                    );
                    
                    lt.matrix = translation * rotation * glm::scale(glm::mat4(1.0f), scale);
                }
            });
    }
    m_world->progress(dt);
}

void SceneManager::print_all_entities() {
    std::print("\n--- [Entity List] ---\n");
    std::print("[Debug] SceneManager Address: {}\n", (void*)this);

    int count = 0;
    // Use each with component arguments, which is the most reliable query method
    m_world->each([&](flecs::entity e, MeshComponent& m, EntityStatus& status) {
        count++;
        const char* name = e.name();
        std::print("Entity: {:<15}       | Render: {} | Physics: {} | MeshIdx: {}\n",
            name ? name : "unnamed",
            status.should_render ? "ON" : "OFF",
            status.has_physics ? "ON" : "OFF",
            m.meshIndex);
        });

    if (count == 0) {
        std::print("Warning: No entities with MeshComponent found!\n");
    }
    std::print("Total Renderable Entities Found: {}\n", count);
    std::print("--------------------------------------\n\n");
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

    // diagnostic: print once
    //static bool once = false;
    //if (!once) {
    //    once = true;
    //    std::printf("[RenderBatches] total=%zu\n", batches.size());
    //    for (size_t i = 0; i < batches.size(); ++i) {
    //        std::printf("  batch[%zu] meshIdx=%u matIdx=%u pos=(%.1f,%.1f,%.1f)\n", i,
    //            batches[i].meshIndex, batches[i].materialIndex,
    //            batches[i].transform[3][0], batches[i].transform[3][1], batches[i].transform[3][2]);
    //    }
    //}

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

} // namespace enginea
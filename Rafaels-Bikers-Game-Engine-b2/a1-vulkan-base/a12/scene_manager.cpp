#include "scene_manager.hpp"
#include "engine_model.hpp" // 仅在此处包含资源定义
#include "../flecs-4.1.4/include/flecs.h"
#include    <print>

SceneManager::SceneManager() {
    m_world = new flecs::world();

    // 健壮的层级变换系统
    m_world->system<WorldTransform, const LocalTransform>("UpdateWorldTransform")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, WorldTransform& wt, const LocalTransform& lt) {
        auto parent = e.parent();

        // 修正点：先检查父实体是否存在且拥有该组件
        if (parent.is_valid() && parent.has<WorldTransform>()) {
            // 使用 .get<T>() 获取引用，并用指针接收其地址
            const WorldTransform* pwt = &parent.get<WorldTransform>();
            wt.matrix = pwt->matrix * lt.matrix;
        }
        else {
            // 如果是根节点，世界矩阵直接等于局部矩阵
            wt.matrix = lt.matrix;
        }
            });
}

SceneManager::~SceneManager() {
    delete m_world;
}

void SceneManager::load_model(const EngineModel& model) {
    std::print("Loading model with {} instances\n", model.scenes.size());
    std::print("[Debug] load_model called on SceneManager at: {}\n", (void*)this);
    int counter = 0;
    for (const auto& instance : model.scenes) {
        // 核心修改：如果名字为空或为了防止重复，加上序号
        std::string name = instance.name.empty() ? "Object" : instance.name;
        name += "_" + std::to_string(counter++);

        auto e = m_world->entity(name.c_str())
            .add<StaticObject>()
            .set<LocalTransform>({ instance.transform })
            .set<WorldTransform>({ instance.transform })
            .set<MeshComponent>({ instance.meshIndex });

        uint32_t matIdx = model.meshes[instance.meshIndex].materialIndex;
        e.set<MaterialComponent>({ matIdx });
    }
}

flecs::entity SceneManager::find_entity(const char* name) {
    return m_world->lookup(name); // Flecs 的高效查找接口
}

void SceneManager::set_local_transform(flecs::entity e, const glm::mat4& transform) {
    if (e.is_valid()) {
        e.set<LocalTransform>({ transform }); // 更新组件，这将自动触发 UpdateWorldTransform 系统
    }
}
void SceneManager::update(float dt) {
    m_world->progress(dt);
}

void SceneManager::print_all_entities() {
    std::print("\n--- [Entity List] ---\n");
    std::print("[Debug] SceneManager Address: {}\n", (void*)this);

    int count = 0;
    // 使用带有组件参数的 each，这是最可靠的查询方式
    m_world->each([&](flecs::entity e, MeshComponent& m) {
        count++;
        const char* name = e.name();
        std::print("Entity Found: {:<20} | ID: {} | MeshIdx: {}\n",
            name ? name : "(unnamed)", e.id(), m.meshIndex);
        });

    if (count == 0) {
        std::print("Warning: No entities with MeshComponent found!\n");
    }
    std::print("Total Renderable Entities Found: {}\n", count);
    std::print("--------------------------------------\n\n");
}


std::vector<RenderBatch> SceneManager::get_render_batches() {
    std::vector<RenderBatch> batches;

    // 查询所有具有 WorldTransform、Mesh 和 Material 的实体
    m_world->query<const WorldTransform, const MeshComponent, const MaterialComponent>()
        .each([&](const WorldTransform& wt, const MeshComponent& mc, const MaterialComponent& matc) {
        batches.push_back({ mc.meshIndex, matc.materialIndex, wt.matrix });
            });

    return batches;
}
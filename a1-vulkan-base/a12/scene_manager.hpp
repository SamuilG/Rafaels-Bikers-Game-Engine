#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "../flecs-4.1.4/include/flecs.h"
// 前置声明 EngineModel，避免在此处包含 engine_model.hpp

// 1. 避免循环依赖：如果 engine_model.hpp 也包含 scene_manager.hpp，会导致编译错误
// 2. 加快编译速度：前置声明比包含整个头文件更轻量，减少编译时间
// 3. 隐藏实现细节：SceneManager 只需要知道 EngineModel 的存在，而不需要了解其内部结构

struct EngineModel;

// 基础组件定义 (保留在头文件，因为可能被其他系统查询)
struct LocalTransform { glm::mat4 matrix; };
struct WorldTransform { glm::mat4 matrix; };
struct MeshComponent { uint32_t meshIndex; };
struct MaterialComponent { uint32_t materialIndex; };

struct PhysicsBody {
    uint32_t bodyID;
};

// Tag 组件

struct StaticObject {};
struct DynamicObject {};

// 渲染批次数据
struct RenderBatch {
    uint32_t meshIndex;
    uint32_t materialIndex;
    glm::mat4 transform;
};

// 预声明 flecs::world 以便在指针中使用
namespace flecs { class world; }

class SceneManager {
public:
    SceneManager();
    ~SceneManager(); // 分离编译模式下需要显式析构指针

    // 从 EngineModel 加载并实例化所有实体
    void load_model(const EngineModel& model);

    // 推进 ECS 世界
    void update(float dt, class PhysicsSystem* physics_system = nullptr);

    // 获取当前帧的渲染批次
    std::vector<RenderBatch> get_render_batches();


    // 1. 通过名称查找实体（Flecs 支持实体命名）
    flecs::entity find_entity(const char* name);

    // 2. 更新实体的局部变换（位置、旋转、缩放）
    void set_local_transform(flecs::entity e, const glm::mat4& transform);

    // 3. 辅助功能：获取 Flecs 世界句柄以便进行底层操作
    flecs::world& get_world() { return *m_world; }

    void print_all_entities();

private:
    flecs::world* m_world; // 使用指针隐藏 Flecs 内部实现细节
};
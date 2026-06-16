# Rafael's Bikers Game Engine 项目架构梳理

这份文档按“面试讲得清楚”的顺序整理：先讲项目是什么，再讲启动流程、模块划分、主数据流、核心玩法、渲染管线、UI 系统，最后给出面试追问清单。

## 1. 项目一句话

这是一个 C++23 桌面 3D 自行车游戏/游戏引擎项目。底层使用 Vulkan + GLFW 做窗口和渲染，Jolt Physics 做物理，Flecs 做 ECS 场景实体管理，ImGui 做编辑器和调试 UI，另外有一套 JSON 驱动的运行时游戏 UI。当前主游戏关卡是 `level1.cpp`，核心玩法围绕自行车控制、收集能力、复活点、传送门、火箭事件、胜利/失败流程展开。

## 2. 工程和依赖

### 构建系统

- `premake5.lua` 定义工作区 `EngineWorkspace`。
- 配置有 `Debug`、`Release`、`Game`。
- 主工程是 `Engine`，输出到 `Bin`。
- 还有静态库工程：`GLFW`、`JoltPhysics`、`ImGui`。
- `Shaders` 是 Utility 项目，会把 `Assets/Shaders/*.vert/*.frag/*.comp/*.geom` 编译到 `Assets/Shaders/spirv/*.spv`。

### 主要第三方库

- Vulkan SDK headers + Volk：Vulkan 函数加载和渲染 API。
- GLFW：窗口、输入、手柄。
- VulkanMemoryAllocator：GPU 内存分配。
- Jolt Physics：刚体、碰撞、射线检测。
- Flecs：ECS 世界、实体、组件、系统。
- ImGui + ImGuizmo：编辑器 UI、场景面板、Gizmo。
- miniaudio：音频播放。
- GLM：数学库。
- tinygltf/stb/nlohmann json：模型、贴图、JSON 解析。
- zstd、shaderc、rapidobj、ozz-animation 等作为辅助或预留依赖。

## 3. 顶层目录职责

- `Source/Runtime/Core`：应用生命周期，系统注册和主循环。
- `Source/Runtime/Rhi`：Vulkan RAII 封装、窗口、swapchain、buffer、image、descriptor、同步等。
- `Source/Runtime/Renderer`：大渲染系统，Vulkan 资源初始化、每帧渲染、后处理、粒子、UI 集成。
- `Source/Runtime/Scene`：Flecs ECS 场景管理、模型加载、关卡代码。
- `Source/Runtime/Physics`：Jolt Physics 封装和自行车控制。
- `Source/Runtime/Input`：键盘、鼠标、手柄统一成 action。
- `Source/Runtime/Event`：事件订阅、同步派发、队列派发。
- `Source/Runtime/Animation`：glTF 骨骼动画、骑手 IK。
- `Source/Runtime/UI`：ImGui 编辑器 UI + JSON 驱动运行时 UI。
- `Source/Runtime/Particle`：CPU 粒子模拟，GPU 顶点上传。
- `Source/Runtime/Trigger`：盒体/球体/胶囊触发器，支持回调和粒子联动。
- `Assets`：模型、贴图、声音、UI JSON、shader、skybox、启动视频。
- `ThirdParty`：第三方依赖源码或预编译工具。

## 4. 启动流程

入口在 `main.cpp`：

1. 创建 `StartupSplash`。
2. 设置启动视频 `Assets/Splash/video.mp4`。
3. 创建 `Application`，把 splash 进度条回调传进去。
4. 关闭 splash。
5. 显示主窗口。
6. 进入 `Application::Run()` 主循环。

`Application` 构造时按顺序创建系统：

1. `InputSystem`
2. `EventSystem`
3. `AudioSystem`
4. `PhysicsSystem`
5. `SceneManager`
6. `AnimationSystem`
7. `RenderSystem`

然后调用所有系统的 `Init()`，把 GLFW 窗口交给输入系统，把输入系统交给渲染和物理，预加载运行时 UI，加载音效，最后创建当前关卡：

```cpp
m_currentScene = std::make_unique<level>();
m_currentScene->Init(renderSystem, sceneManager, physicsSystem, inputSystem, eventSystem, &mState, animationSystem, audioSystem);
```

## 5. 主循环

`Application::Run()` 每帧做：

1. 计算 `dt`，最大钳制到 `0.05f`，避免卡顿后一帧物理爆炸。
2. 更新当前关卡 `m_currentScene->Update(dt)`。
3. 按系统注册顺序更新所有系统：
   - 输入
   - 事件
   - 音频
   - 物理
   - 场景
   - 动画
   - 渲染
4. 根据自行车速度调整链条循环音效音量和 pitch。
5. 如果请求重开或回主菜单，调用 `ReloadCurrentScene()`。

这个顺序意味着：关卡逻辑先读上一帧/当前输入和状态，然后系统统一推进；场景在物理后同步 transform，动画在场景后算骨骼，渲染最后拿最新批次画出来。

## 6. Application 和 UserState

`Application` 是总装配层，不直接写具体玩法。它的核心职责：

- 创建和销毁所有系统。
- 持有全局 `UserState`。
- 持有当前 `GameScene`。
- 控制主循环和场景重载。

`UserState` 继承 `GameplayState`，是跨系统共享的运行状态：

- 游戏流程：主菜单、Playing、Paused、GameOver、Victory。
- 相机状态：第三人称、yaw、pitch、FOV、portal camera。
- 玩家状态：是否存活、速度、倾斜、转向、死亡计数、能力解锁。
- 画面开关：IBL、Bloom、SSR、SSAO、LOD、frustum culling、debug render mode。
- UI 开关：引擎 UI、运行时 UI、各编辑器面板。

面试可以这样说：这个项目没有复杂的全局服务定位器，而是用 `Application` 明确把系统指针传给场景和渲染层，跨系统状态集中在 `UserState` 中，便于 UI、渲染、玩法共享。

## 7. ECS 场景系统

`SceneManager` 使用 Flecs 管理实体和组件。

### 核心组件

- `LocalTransform`：本地 transform。
- `WorldTransform`：世界 transform。
- `MeshComponent`：引用全局 mesh index。
- `MaterialComponent`：引用全局 material index。
- `PhysicsBody`：普通实体对应的 Jolt BodyID。
- `CompoundParent`：复合刚体子部件共享一个 Jolt BodyID，并带 local offset。
- `EntityStatus`：控制是否渲染、是否启用物理同步。
- `LayerComponent`：渲染层，如 default/emissive/transparent。
- `OpacityComponent`：第三人称遮挡物 X-Ray 淡化。
- `LODComponent`：距离 LOD。
- `RiderBinding`：角色跟随自行车座位。
- `AnimationComponent` / `SkinComponent`：骨骼动画。
- `LightComponent`：灯光组件。

### 模型加载路径

`SceneManager::LoadModel()` 做：

1. `load_engine_model_glb(path)` 解析 glTF/GLB 到 CPU 侧 `EngineModel`。
2. 把初始 transform 应用到每个 instance。
3. 调 `RenderSystem::RegisterModelAssets()` 把 mesh/material/texture 注册到渲染系统。
4. 根据 `ModelPhysicsType` 创建 ECS 实体和 Jolt 刚体：
   - `Static`：静态三角网格碰撞。
   - `Dynamic`：动态凸包。
   - `Compound`：多个 mesh 合成一个动态复合刚体。
   - `CustomC`：类似复合刚体，但对自行车这类复杂对象做了定制同步。

### 每帧同步

`SceneManager::Update()` 做：

- 从 Jolt 读取 `PhysicsBody` 的位置/旋转，写回 `LocalTransform`。
- 对 `CompoundParent` 子部件，根据复合 body 的 transform 和 local offset 更新部件位置。
- 对自行车部件做额外动画：前轮/后轮转动，车把转向，踏板联动。
- 做第三人称相机遮挡检测：相机到车之间的物体被加 `OpacityComponent`，渲染时半透明。
- 更新骑手 `RiderBinding` 和 `RiderIKComponent` 的目标点。
- 调 `m_world->progress(dt)` 运行 Flecs 内部系统，比如 WorldTransform 继承。

## 8. 关卡系统

抽象基类是 `GameScene`：

- `Init(...)`
- `Update(float dt)`
- `Shutdown()`

当前实际使用的是 `level`，文件在 `Source/Runtime/Scene/level1.cpp`。

`level::Init()` 负责搭建关卡：

- 绑定系统指针。
- 初始化运行时 UI 显示状态。
- 加载 `Level.glb` 作为主关卡。
- 加载火箭、角色、自行车、收集品、能力道具、新闻纸、卫星、复活点等模型。
- 创建 `BikeController`。
- 绑定骑手角色到自行车，并设置手脚 IK 链。
- 注册碰撞事件：高速度正面撞击会死亡。
- 创建灯光：太阳、车灯、点光源。
- 创建触发器：收集、能力解锁、checkpoint、清除 checkpoint、IBL 开关、火箭发射等。
- 加载音效和电台音乐。

`level::Update()` 负责运行时玩法：

- 死亡后的复活提示和复活逻辑。
- 收集物、能力道具旋转/隐藏/挂载到车上。
- 喇叭、弹簧、火箭、卫星动画。
- 电台切歌、静音、广播延迟。
- UFO 新闻 UI 显示计时。
- 火箭发射和火箭镜头。
- 传送门部署、预览、传送、镜头过渡。
- 胜利 UI 延迟显示。
- 最后更新 `BikeController`。

面试可以把 `level1.cpp` 讲成“关卡脚本层”：它不是通用引擎设施，而是把引擎能力编排成具体游戏体验。

## 9. 输入系统

`InputSystem` 把不同设备统一成 action：

- 键盘：WASD、方向键、空格、Shift、Ctrl、F1、1-8、Esc 等。
- 鼠标：左右键踩踏、中键捕获。
- 手柄：左摇杆、右摇杆、肩键、A/Y/Back 等。

常用接口：

- `IsActionPressed(name)`：这一帧刚按下。
- `IsActionHeld(name)`：持续按住。
- `GetActionValue(name)`：模拟量或数字键值。
- `GetMouseDelta()` / `GetMousePosition()`。

典型 action：

- `MoveForward` / `MoveBackward`
- `StrafeLeft` / `StrafeRight`
- `Jump`
- `DEPLOY`
- `pedal0` / `pedal1`
- `ToggleEngineUi`
- `BloomToggle` / `IBLToggle` / `SSRToggle` / `SSAOToggle`

## 10. 物理系统

`PhysicsSystem` 封装 Jolt：

- 初始化 Jolt allocator、job system、physics world。
- 设置 BroadPhase layer：`NON_MOVING` 和 `MOVING`。
- 创建静态 mesh body。
- 创建动态 convex body。
- 创建动态 compound body。
- 射线检测：普通 raycast、忽略单个 body、忽略多个 body、返回 hit point。
- 设置 body transform、scale、damping、impulse。
- 注册 contact listener，把 Jolt 碰撞转换为引擎 `CollisionEvent`。

物理和 ECS 的关键桥接是：

- Jolt body 只存在于物理世界。
- Flecs 实体持有 `PhysicsBody` 或 `CompoundParent` 中的 raw BodyID。
- 每帧 `SceneManager` 根据 BodyID 把 Jolt transform 同步回 ECS。

## 11. 自行车控制

`BikeController` 是核心玩法手感层。

它不是使用 Jolt 现成 vehicle controller，而是自定义控制：

- 读取 action：
  - W/S 或手柄轴控制前进后退。
  - A/D 控制转向。
  - 鼠标左右键交替踩踏增加 `engineForce`。
  - 空格在落地时跳跃。
- 根据当前 Jolt body rotation 计算 yaw 和 forward。
- 根据速度计算 signed speed。
- 低速主要靠转向角，高速更多靠倾斜角。
- 用 wheelbase 和 steer angle 算 yaw rate。
- 用 lateral grip force 抑制侧滑。
- 用 `engineForce` 给车身施力，踩踏增加、自然衰减、刹车可反向。
- 根据坡度做惩罚。
- 速度、车把角、倾斜角写回 `GameplayState`，供渲染、UI、音频和动画使用。

这是非常适合面试重点讲的部分：它体现了“物理模拟 + 游戏手感调参”的结合。

## 12. 事件系统

`EventSystem` 是轻量事件总线：

- `Subscribe(EventType, callback)` 注册监听。
- `Dispatch(Event&)` 立即派发。
- `QueueEvent(unique_ptr<Event>)` 入队。
- `Update()` 里交换队列并派发，避免回调中继续加事件导致死循环。

已有事件：

- WindowResize
- Collision
- ItemCollected
- AllItemsCollected
- Custom

典型用途：

- Jolt contact listener 创建 `CollisionEvent`。
- 收集品触发器创建 `ItemCollectedEvent` 和 `AllItemsCollectedEvent`。
- 关卡订阅这些事件更新玩法状态和音效。

## 13. 音频系统

`AudioSystem` 使用 miniaudio。

能力：

- `LoadSound(name, filePath)`
- `PlayOneShot(name)`
- `PlayLoop(name)`
- `SetVolume(name, volume)`
- `SetPitch(name, pitch)`
- `SetMasterVolume(volume)`
- `SetRuntimeVolume(name, volume)`
- `Stop(name)`

实现细节：

- 循环音效保存在 `sounds` map。
- 一次性音效用 `ma_sound_init_copy` 复制实例，播放完成后在 `Update()` 清理。
- 自行车链条声音是循环播放，通过 `bikeSpeed` 动态调 runtime volume 和 pitch。

## 14. 动画和 IK

`AnimationSystem` 管理 glTF 动画：

- 注册模型节点、skin、animation。
- 采样 animation sampler。
- 根据节点层级计算 global transform。
- 输出每个 skin 的 bone matrix。
- 支持 rest pose 下仍然做 IK。

骑手 IK 的数据流：

1. 关卡找到角色 skinned entity 和自行车部件实体。
2. 给角色实体加 `RiderBinding` 和 `RiderIKComponent`。
3. `SceneManager::Update()` 根据车身、车把、踏板更新 IK world target/pole。
4. `AnimationSystem::Update()` 应用 spine lean，然后做 2-bone analytic IK。
5. `RenderSystem` 把 bone matrices 上传到 SSBO，skinned pipeline 渲染角色。

面试亮点：角色不是简单 parent 到车上，而是通过 IK 让手脚贴住车把/踏板，提升真实感。

## 15. 渲染系统概览

`RenderSystem` 是全项目最大类。它集成：

- Vulkan window/swapchain。
- VMA allocator。
- command pool、descriptor pool、同步对象。
- scene/object/post descriptors。
- PBR mesh pipeline。
- alpha pipeline。
- skinned mesh pipeline。
- shadow/skinned shadow pipeline。
- skybox。
- particle pipeline。
- debug line pipeline。
- bloom、blur、composite。
- SSR。
- SSAO。
- speed/death post-process。
- portal surface 和递归 portal render target。
- ImGui 和运行时 UI。
- 缩略图生成。

### 每帧渲染大致流程

1. 更新 ImGui frame 和编辑器 UI。
2. 根据输入切换 debug render mode。
3. 更新相机 `update_user_state()`。
4. 写入 `SceneUniform`，包含相机、projection、灯光、shadow、renderMode、IBL、portal clip。
5. 处理触发器和粒子可见性。
6. 获取 render batches：
   - 普通 mesh：`SceneManager::get_render_batches()`。
   - skinned mesh：`SceneManager::get_skinned_batches()`。
7. 获取灯光数据。
8. 如果传送门可见，构造 portal camera uniform 和递归 portal uniform。
9. 调 `record_commands()` 录制 Vulkan commands。
10. submit command buffer。
11. present swapchain。

### RenderBatch

渲染层不是直接遍历 Flecs 实体，而是场景系统把实体压成 `RenderBatch`：

- mesh index
- material index
- transform
- alpha
- entity id
- compound body id
- shadow flag
- skinning 信息
- portal clip 信息

这样 ECS 和 Vulkan 渲染细节解耦了一层。

## 16. RHI 层

`Source/Runtime/Rhi` 是 Vulkan 基础封装：

- `VulkanContext`：instance、physical device、device、graphics queue、debug messenger。
- `VulkanWindow`：继承 context，增加 GLFW window、surface、present queue、swapchain、swap image/view。
- `Allocator`：VMA allocator RAII。
- `Buffer`：VMA buffer RAII。
- `Image` / `ImageWithView`：Vulkan image 和 image view。
- `UniqueHandle`：descriptor pool、pipeline、pipeline layout、sampler、fence、semaphore 等 RAII。
- `commands`：command pool。
- `synch`：image/buffer barrier、fence、semaphore。
- `load`：读取 SPIR-V 文件。
- `textures` / `descriptors`：采样器、descriptor 辅助。

面试可以说：项目有一层比较薄的 Vulkan RAII 工具库，真正的渲染管线组织在 `RenderSystem` 和 `RenderUtilities`。

## 17. Shader 和后处理

Shader 源文件在 `Assets/Shaders`，编译产物在 `Assets/Shaders/spirv`。

主要 shader：

- `default.vert/frag`：PBR 主渲染。
- `alpha.frag`：透明/alpha。
- `skinned.vert`：骨骼动画。
- `shadowmap.vert/frag`、`shadowmap_skinned.vert`：阴影。
- `particles.vert/frag`：粒子。
- `skybox.vert/frag`：天空盒。
- `blur.frag`、`composite.frag`：Bloom。
- `ssr.frag`：屏幕空间反射。
- `ssao.frag`、`ssao_blur.frag`：屏幕空间环境光遮蔽。
- `speed_postprocess.frag`：高速/死亡后处理。
- `portal.vert/frag`：传送门表面。
- debug shaders：mip/depth/derivative/overdraw/debug line。

## 18. UI 架构

项目有两套 UI。

### 18.1 引擎编辑器 UI

`EngineUi` 基于 ImGui，主要用于开发和调试：

- 顶部菜单。
- 场景 viewport。
- 内容浏览器。
- 控制面板。
- 场景层级。
- Inspector。
- 灯光面板。
- 相机面板。
- Debug 面板。
- 音频面板。
- Toast 和 Console。
- Gizmo 操作。

`showEngineUi` 控制显示，一般 F1 切换。

### 18.2 运行时 UI

运行时 UI 是 JSON 驱动：

- UI 文件在 `Assets/ui/*.ui.json`。
- 主题在 `Assets/ui/BicycleSim_DarkTheme.ui.theme.json`。
- `UIScreen` 表示一个屏幕。
- `UIElement` 是树形节点，具体类型包括：
  - Canvas
  - Panel
  - Image
  - Text
  - Button
  - Slider
  - Toggle
  - ProgressBar
  - RadialProgressBar
  - InputField
- `UISerializer` 从 JSON 加载/保存。
- `UIManager` 管多屏幕栈、输入、命中测试、事件、数据绑定、动画。
- `RuntimeUiController` 是给游戏代码用的简化入口。
- `GameUIEventRouter` 把按钮事件映射到游戏状态变化。

主要 UI：

- `MainMenu.ui.json`
- `HUD.ui.json`
- `PauseMenu.ui.json`
- `Settings.ui.json`
- `GameOver.ui.json`
- `Win.ui.json`
- `AbilityUnlock.ui.json`
- `RespawnPrompt.ui.json`
- `UFONews.ui.json`

### 18.3 UI 数据绑定

`RenderSystem::RefreshRuntimeUiDataContext()` 每帧写数据：

- `bike.speedKmh`
- `bike.energy`
- `bike.stamina`
- `bike.distance`
- `race.lapTime`
- `ui.globalScale`
- 等等

`UIManager` 根据 UI JSON 的 binding，把这些值写入文本、进度条、可见性、透明度、位置、旋转等属性。

## 19. 触发器和粒子

`TriggerSystem` 支持：

- Box
- Sphere
- Capsule

每个 trigger 有：

- 位置/尺寸/半径。
- 是否可见。
- 是否启用。
- 是否 one-shot。
- 进入回调。
- 离开回调。
- 可绑定一个粒子组，进入时显示粒子，离开时隐藏。

`level1.cpp` 用它实现：

- 收集品。
- 能力解锁。
- checkpoint。
- 清 checkpoint。
- IBL 开关区域。
- 火箭发射区域。
- 新闻纸触发 UFO UI。

粒子系统在 CPU 侧更新粒子位置、生命周期、颜色、大小、atlas 帧，然后上传到 Vulkan vertex buffer 绘制。

## 20. 传送门系统

传送门是这个项目的一个复杂亮点。

关卡层负责：

- 判断长按 `DEPLOY`。
- 判断是否高速、是否有 checkpoint。
- 创建两个 portal surface transform。
- 记录入口/出口位置和朝向。
- 调 `RenderSystem::SetPortalPreviewPair()` 设置双向 portal 预览相机。
- 在 `UpdatePortalTeleport()` 中判断自行车是否穿过 portal 平面。
- 传送时设置 Jolt body 的位置、旋转、速度，并更新相机过渡状态。

渲染层负责：

- 单独创建 portal render targets。
- 根据 portal surface 构建 portal camera。
- 设置 portal clip plane。
- 支持双 portal 和递归层。
- 在主画面中把 portal surface 画成另一个相机的画面。

面试可以概括为：玩法层管“什么时候开门、传哪里、物理状态怎么迁移”，渲染层管“门里看见什么”。

## 21. 当前游戏流程

状态大致是：

1. 启动后显示 MainMenu。
2. StartGame 后进入 Playing，显示 HUD。
3. 游戏中可暂停、打开 settings、恢复或回主菜单。
4. 高速撞击或低速大坡度失衡会死亡。
5. 死亡后显示复活提示，可满足条件后原地复活。
6. 收集能力：
   - Spring/Jump
   - Horn
   - Radio
   - Gas tanks / speed progression
7. 触发 checkpoint 后可以部署 portal。
8. 特定触发器触发火箭发射和镜头。
9. 达成条件后显示 Win UI。

## 22. 适合面试重点讲的亮点

1. 自研 ECS + 物理桥接  
   Flecs 管实体和组件，Jolt 管物理，组件保存 BodyID，每帧同步 transform。

2. Vulkan 渲染管线较完整  
   PBR、shadow、skinning、particles、skybox、bloom、SSR、SSAO、debug render mode、portal render target 都有。

3. 自行车手感不是简单刚体推力  
   结合转向角、倾斜角、侧向抓地力、踩踏蓄力、坡度惩罚、跳跃和失衡检测。

4. 骑手绑定和 IK  
   角色跟随车身，手脚通过 IK 追踪把手和踏板，而不是死板动画。

5. 数据驱动 UI  
   UI 从 JSON 载入，支持屏幕栈、事件路由、数据绑定、动画、运行时修改。

6. 传送门系统  
   包含玩法部署、物理传送、相机过渡、渲染预览和递归 portal。

7. 编辑器能力  
   ImGui 面板、内容浏览器、场景层级、Inspector、Gizmo、UI 编辑器、缩略图生成。

## 23. 可被追问的问题和回答思路

### Q: 这个项目的系统之间怎么解耦？

答：基础系统统一继承 `System`，由 `Application` 创建和更新。场景实体用 Flecs 组件表达，渲染层不直接操作游戏对象，而是从 `SceneManager` 拿 `RenderBatch`。物理层只暴露 BodyID 和创建/查询接口，ECS 保存 BodyID 做同步。运行时 UI 通过 `RuntimeUiController` 和事件名路由到游戏状态，避免 UI 文件直接依赖游戏代码。

### Q: 每帧从输入到画面发生了什么？

答：输入系统先更新 action 状态；关卡和 `BikeController` 读 action 修改 Jolt body 和 `UserState`；物理系统推进模拟；`SceneManager` 从 Jolt 同步 transform 并更新 ECS；动画系统根据 ECS 的 IK 目标生成骨骼矩阵；渲染系统从场景取 batch、上传 uniform/bone buffer、录制 Vulkan command，最后 present。

### Q: 为什么用 `UserState`？

答：很多系统需要共享轻量运行时状态，比如相机、速度、UI 状态、渲染开关、游戏流程。集中在 `UserState` 可以减少系统间交叉引用。但它也有代价：字段很多，长期看可以拆成 GameState、RenderSettings、CameraState、UiState。

### Q: 如何加载一个新模型？

答：关卡调用 `SceneManager::LoadModel(render, path, physicsType, mass, transform, layer)`。内部用 tinygltf 解析为 `EngineModel`，渲染系统注册 mesh/material/texture 到 GPU，场景系统为每个 scene instance 创建 Flecs entity，并按 physics type 创建 Jolt body。

### Q: 静态物体和动态物体区别？

答：静态物体用 Jolt mesh shape，layer 是 `NON_MOVING`，不会被物理模拟移动；动态物体用 convex hull 或 compound body，layer 是 `MOVING`，每帧由物理驱动 transform。

### Q: 自行车为什么用 compound body？

答：自行车模型由多个部件组成，如果每个部件独立刚体会难以控制。这里用一个复合 Jolt body 作为真实物理车身，Flecs 子实体通过 `CompoundParent` 的 local offset 跟随主 body，视觉部件还能额外做轮子转动、车把转向、踏板联动。

### Q: UI 点击 Start Game 怎么影响游戏？

答：UI JSON 里按钮定义事件名，`UIManager` 命中并触发事件，`GameUIEventRouter` 注册了这些事件名。比如 `StartGame` 会设置 `UserState` 为 Playing，隐藏 MainMenu，显示 HUD。

### Q: 渲染模式有哪些？

答：`UserState::renderMode` 控制，输入 1-8 切换。包括默认、mipmap debug、depth、derivatives、overdraw、overshading、shadow debug 等。渲染时根据 mode 选择 pipeline 和 descriptor。

### Q: 传送门如何避免只是一张贴图？

答：渲染层为 portal 单独创建 offscreen render target，用 portal camera 从出口视角渲染场景，再把结果作为 portal surface 的材质绘制到主画面。同时用 clip plane 避免错误穿帮，并支持双向 portal 和递归层。

## 24. 技术债和风险点

1. `RenderSystem.hpp` 过大  
   接近把渲染器、资源管理、UI、后处理、portal、缩略图都放在一个类里。面试时可以说后续会拆成 `RenderGraph/ResourceManager/Pass` 或至少拆 pass 类。

2. `level1.cpp` 过大  
   当前关卡脚本承担太多逻辑。可以按道具系统、checkpoint、portal、rocket event、radio system 拆分。

3. `UserState` 字段很多  
   容易变成“万能状态包”。可以拆成更小的状态结构，或者用 ECS singleton component。

4. `WindowSystem.hpp` 像是未使用的早期残留  
   当前真正窗口由 `RenderSystem` 持有 `mWindow = lut::make_vulkan_window(false,false)`。`WindowSystem.hpp` 里还存在明显拼写/变量问题，说明它不在主编译包含路径中。

5. 字符编码有损坏痕迹  
   很多中文注释显示乱码，说明文件可能经历过编码不一致。面试可避免强调注释质量，重点讲架构和实现。

6. 缺少自动化测试  
   这类图形项目更多靠运行验证，但物理/数学/序列化/UI binding 可以补单元测试。

7. 有些系统耦合仍偏高  
   比如 `RenderSystem` 同时知道 `SceneManager`、`InputSystem`、`AudioSystem`、`RuntimeUiController`。可以用更明确的 subsystem 或事件减少交叉依赖。

## 25. 面试讲解模板

可以按 5 分钟版本讲：

1. 这是一个 C++23 Vulkan 自行车游戏引擎项目，使用 GLFW/Vulkan/Jolt/Flecs/ImGui/miniaudio。
2. 启动时 `main` 显示 splash，然后 `Application` 创建输入、事件、音频、物理、场景、动画、渲染系统，并加载当前 `level`。
3. 每帧先更新关卡，再更新所有系统。输入变成 action，自行车控制器读 action 给 Jolt body 施力，物理推进后场景系统把 body transform 同步到 Flecs，动画系统算骨骼，渲染系统取 batch 画 Vulkan frame。
4. 场景实体用 Flecs 组件组织，模型通过 tinygltf 解析为 `EngineModel`，渲染资源注册到 GPU，物理 body 通过 BodyID 和实体关联。
5. 渲染管线包含 PBR、shadow、skinning、particles、skybox、bloom、SSR、SSAO、portal 和 debug 模式。
6. UI 分两层：ImGui 编辑器 UI 和 JSON 驱动运行时 UI。运行时 UI 支持屏幕栈、数据绑定、动画和事件路由。
7. 我重点做/理解的亮点可以讲自行车控制、Jolt/ECS 同步、骑手 IK、传送门和运行时 UI。

## 26. 快速文件索引

- 入口：`main.cpp`
- 应用生命周期：`Source/Runtime/Core/Application.cpp`
- 系统接口：`Source/Runtime/Core/System.h`
- 全局状态：`Source/Runtime/UserState/GameplayState.hpp`、`Source/Runtime/UserState/UserState.hpp`
- 当前关卡：`Source/Runtime/Scene/level1.cpp`
- 场景/ECS：`Source/Runtime/Scene/SceneManager.hpp/.cpp`
- 模型加载：`Source/Runtime/Scene/model_loader/engine_model.hpp/.cpp`
- 渲染系统：`Source/Runtime/Renderer/RenderSystem.hpp`
- 渲染工具：`Source/Runtime/Renderer/RenderUtilities`
- Vulkan RHI：`Source/Runtime/Rhi`
- 物理：`Source/Runtime/Physics/PhysicsSystem.hpp/.cpp`
- 自行车控制：`Source/Runtime/Physics/bikeController.hpp/.cpp`
- 输入：`Source/Runtime/Input/InputSystem.hpp/.cpp`
- 事件：`Source/Runtime/Event`
- 音频：`Source/Runtime/AudioSystem`
- 动画/IK：`Source/Runtime/Animation`
- 粒子：`Source/Runtime/Particle`
- 触发器：`Source/Runtime/Trigger`
- 编辑器 UI：`Source/Runtime/UI/EngineUi.hpp/.cpp`
- 运行时 UI：`Source/Runtime/UI/VisualUIEditor`
- UI 文件：`Assets/ui`
- Shader：`Assets/Shaders`

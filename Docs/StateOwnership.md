# 状态所有权与生命周期清单

## 范围与读法

本清单保留状态拆分第 1 步的历史盘点。第 3、4 步现已实施，当前分组、权限与控制器接口见 [StateArchitecture.md](StateArchitecture.md)。下文基线字段表用于追溯迁移来源。

**“当前事实”固定指实施前的 Git 提交 `b7ff397`（2026-09-18 审计）**。本轮第 2 步会收拢流程写入口，因此完成后的源码可能已不再保留表中列出的旧写法。表中的旧路径和行号用于定位迁移来源，不能作为新接口仍允许直接写字段的承诺。**“目标约定”是边界设计；当前实现与完成边界以 StateArchitecture.md 的第 5、6 节为准。**

审计范围是 `Source/Runtime/UserState/GameplayState.hpp`、`UserState.hpp` 的全部数据成员，以及历史版本中内嵌 `BikeTuning` 的全部 9 个成员。检索了 `Source` 内的引用，并区分活跃路径、仅定义/仅写入以及遗留未调用的 UI。表中“未发现读取”指本仓库源码消费者，不推断外部工具或未来功能。

初稿对应第 1、2 步：状态盘点、流程入口统一及必要的模拟开关。第 3–6 步的状态分组、玩家/相机控制器、输入调度和生命周期清理已经接入。下文未接通字段和旧写者均描述初始基线，当前实现以 StateArchitecture.md 为准。

所有成员都有头文件默认初始化；此外，当前 `Application::ReloadCurrentScene` 的 `mState = UserState{}` 是表中所有字段的共同写入者。以下各表只额外列业务写入点，避免反复列出默认初始化和整份赋值。

### 文件简称

| 简称 | 路径 |
| --- | --- |
| App | `Source/Runtime/Core/Application.cpp` |
| Router | `Source/Runtime/UI/VisualUIEditor/GameUIEventRouter.cpp` |
| UI Controller | `Source/Runtime/UI/VisualUIEditor/RuntimeUiController.hpp` |
| Renderer | `Source/Runtime/Renderer/RenderSystem.hpp` |
| Camera | `Source/Runtime/Renderer/RenderUtilities/camera.cpp` |
| Editor UI | `Source/Runtime/UI/EngineUi.cpp` |
| Runtime UI | `Source/Runtime/UI/RenderSystemRuntimeUi.inl` |
| UI Debug | `Source/Runtime/UI/VisualUIEditor/RuntimeUIDebugPanel.cpp` |
| Level | `Source/Runtime/Scene/level1.cpp` |
| TestScene | `Source/Runtime/Scene/TestScene.cpp` |
| Scene | `Source/Runtime/Scene/SceneManager.cpp` |
| Bike | `Source/Runtime/Physics/bikeController.cpp` |

## 1. 先确定控制权

1. **游戏流程**：游戏流程控制器是阶段、阶段转换与重载请求的唯一写入者。UI、关卡、快捷键发送命令或领域事件；App 执行重载并报告完成。查询不暴露可写引用，也不保留能任意设置所有阶段的通用公共 setter。兼容布尔若暂时保留，只能由流程控制器同步，外界只读。
2. **设置面板与编辑器**：设置是界面叠层，编辑器是否显示是独立工具状态。它们不能机械并入游戏阶段枚举。“打开设置是否暂停”由显式策略决定；关闭设置不能清掉用户原有的暂停原因。F1 本身不启动、结束或重新开始游戏。
3. **玩家与关卡**：角色存活、物理反馈、解锁和收集进度由游戏侧维护。`isAlive == false` 不自动等于 `GameOver`，现有关卡有倒地后原地复活流程。Renderer 不应通过切相机来复活角色。
4. **相机**：相机控制器写最终姿态、平滑结果及计时；输入、编辑器、传送门与过场提交意图。编辑器可请求改目标或重置相机，不直接与每帧算法争写 current 字段。
5. **渲染与编辑器**：渲染设置经校验接口提交；硬件能力和统计只读。编辑器拥有面板、选中项与工具状态。各模块只接收所需只读视图或命令接口，不能在拆成多个 struct 后仍把全部可写对象传给所有系统。

现有 UI Controller 的显示设置 `QueryDisplaySettings` / `ApplyDisplaySettings`（137–150 行）已使用查询/应用回调，可作为以后收窄写接口的参考；这不要求通用 UI 系统拥有游戏流程。

## 2. 生命周期：当前事实与目标约定

### 当前事实

| 操作 | 实际行为 |
| --- | --- |
| 重新开始 | Router 494–500 行先写 Playing 和多个布尔，再置 `restartRequested`。App 153–155 行在帧末执行重载；232–241 行整份重置 `UserState`。只有 `showEngineUi`、`showRuntimeUi` 被显式恢复；AudioSystem 的主音量单独保存恢复。玩家、相机、画质、面板开关、调试能力缓存等其余字段都会回到默认值，随后被初始化/更新再次写入。 |
| 返回主菜单 | Router 440–453 行只改流程字段和界面，不发重载请求，不重置其余状态。`returnToMainMenuRequested` 在基线没有赋值为 true 的生产者。App 虽有 `ReloadCurrentScene(true)` 分支，但不能把该分支等同于当前主菜单按钮行为。 |
| 切 F1 | Renderer 1564–1566 行只翻转 `showEngineUi`；不重置玩家、相机、画质和面板选择。实际输入捕获还依赖视口悬停与 Camera 67–81 行，不能把 F1 当成完整的暂停/输入策略。 |
| OpenEditor 事件 | 与 F1 不同：Router 250–268 行强制写 Playing、清暂停/结算标志并显示编辑器。这个差异必须由流程策略明确处理。 |
| 打开/关闭设置 | Router 287–324 行主要增删 Settings widget、刷新设置草稿，未写 `gameFlowState = Settings`。枚举中存在 Settings 不代表它已经是实际阶段。 |
| 游戏继续推进 | App 135–142 行无条件更新关卡和各系统，因此菜单、暂停标志以及 F1 本身都不等于完整停止模拟。是否修改调度属于后续执行策略工作，不能仅因状态入口统一就宣称已解决。 |

### 各字段组使用的目标生命周期代码

| 代码 | 重新开始 | 返回主菜单 | 切 F1 |
| --- | --- | --- | --- |
| **F：流程** | 发起重载；完成后进入 Playing；等待期间不提前对外宣称新局已就绪 | 经流程命令进入 MainMenu，明确是否释放本局；不得留下冲突重载请求 | 不改游戏阶段和重载请求 |
| **P：本局数据** | 重建/重置本局玩家与关卡状态 | 本局退出后失效；若暂存结算快照，不能继续被当作活跃玩家更新 | 不因显示工具界面而重置 |
| **C：游戏相机** | 按新场景/出生点初始化，清临时接管与平滑历史 | 清游戏临时接管，菜单视图自行配置；不沿用旧传送门过渡 | 不重置；是否切编辑器自由相机另有显式命令 |
| **I：输入瞬态** | 清捕获、边沿和悬停，按当前窗口/界面重新计算 | 清捕获与悬停，再计算界面输入归属 | 更新焦点/捕获；不得沿用已失效的点击/拖拽状态 |
| **R：用户设置** | 保留已应用设置 | 保留已应用设置 | 保留 |
| **E：编辑器工作区** | 保留面板、布局和可复用工具偏好 | 保留工作区 | 只改变整体可见性；重新显示时保留面板选择 |
| **S：场景相关选择** | 清除旧场景选中项，防止索引指向新对象 | 当前场景释放时清除 | 保留仍有效的选择；不因隐藏界面而删除对象 |
| **D：派生结果/能力** | 按来源重新计算；设备能力不因关卡重载归零 | 统计重置或标为无场景；设备能力保留 | 不重置硬件能力；可按当前视图更新统计 |
| **U：无消费者** | 不赋予新生命周期语义；确认用途后删除或接通 | 同左 | 同左 |

“保留设置”不等于无限保留旧场景运行效果。关卡对画面施加的临时覆盖，应与用户设置分层；退出关卡清掉覆盖，保留用户偏好。F1 无直接重置也不意味着上述值每帧不变，原来的合法所有者仍可能更新它们。

## 3. GameplayState 完整字段清单

### 鼠标与相机

| 字段 | 当前读取者 | 当前写入者 | 目标归属与写入约束 | 生命周期 |
| --- | --- | --- | --- | --- |
| `mouseX`, `mouseY`, `mouseLastX`, `mouseLastY`, `wasMousing` | 未发现头文件外引用 | 无业务写者 | 输入迁移遗留；不要把它们重新当作 UI/游戏共享坐标 | U |
| `previousMouseState` | Camera 76–81 | Camera 70、76；同时调用 InputSystem 捕获接口 | 输入捕获状态；最终只保留一个权威来源，不能与 InputSystem 两份布尔漂移 | I |
| `Yaw`, `Pitch`, `Distance` | Camera 构建轨道姿态；Renderer 跟随/传送计算；Level 传送 | Camera 平滑及过渡；Level 传送；Editor UI 重置按钮 1628–1630 | 相机最终输出；由相机控制器写，重置/传送经相机命令 | C |
| `camera2world` | Renderer/渲染 uniform、Scene 遮挡、Level、Editor UI 视口/相机面板 | Camera 通过 `cam` 引用更新；Level 火箭镜头 2149；Editor UI 自由相机 1644–1662 | 相机最终输出；跟随、自由编辑、过场需要明确当前控制者 | C |
| `cameraFov` | Camera 投影 377；Renderer 视口/Portal 投影；Editor UI | Camera 223；Editor UI 自由相机滑块 1594 | 相机最终镜头参数，只允许控制器写 | C |
| `targetFov` | Camera 223 | Camera 缩放/极速/距离算法 99–167；Editor UI 1595 | 相机意图；自由镜头请求与自动跟随镜头策略分开，自动策略不得悄悄覆盖“已接受”的手动请求 | C |
| `targetYaw`, `targetPitch`, `targetDistance` | Camera 平滑/轨道/自动对齐；Level 传送 | Camera 输入/算法；Level 传送；Editor UI 1623–1630 | 相机意图，由相机命令入口仲裁手动、自动、过场来源 | C |
| `cameraIdleTimer` | Camera 自动对齐 192 | Camera 176、185；Level 1819；Editor UI 1634 | 相机内部计时；外部提交“用户操作/重置”事件，不直接清计时器 | C |
| `cameraRoll`, `targetCameraRoll` | Camera 240、333 | Camera 231–240 | 相机内部平滑结果/目标 | C |
| `followTargetPos` | Camera；Renderer 的触发器探针 1665；Level；Editor UI | Renderer 跟随目标 1542；Level 传送 1763、1820 | 相机跟随请求；触发器应使用玩家位置，而不是借相机目标充当游戏位置 | C；触发器来源随后归 P |
| `thirdPersonMode` | Camera；Bike 40/128；Scene 遮挡；Renderer；Editor UI | Editor UI 1592；Renderer T 键 1558；Bike 死亡；Level/TestScene 死亡、复活和火箭镜头 | 相机模式。它目前还被用作骑行控制条件，后续必须让角色控制使能独立于镜头模式 | C |

### 传送门与相机临时接管

| 字段 | 当前读取者 | 当前写入者 | 目标归属与写入约束 | 生命周期 |
| --- | --- | --- | --- | --- |
| `portalCameraActive`, `portalCameraTimer`, `portalCameraBoomLength`, `portalCameraStartSide` | Camera；其中 Active 还被 Renderer、Scene、Editor UI 读取 | Level 建立/重置接管；Camera 更新并结束过渡 | 游戏侧申请传送镜头，Camera 持有执行状态/计时并报告完成 | C |
| `portalCameraPosition`, `portalCameraTargetPosition`, `portalCameraBoomOffset` | Camera 和 Level 的传送构造计算 | Level 初始化/传送/复活重置；Camera 插值/回收 | 相机临时接管的目标与运行结果，禁止双方任意改同一输出 | C |
| `portalCameraEntrySurface`, `portalCameraExitSurface` | Camera；Renderer 跟随方向 | Level 初始化/传送 1537–1538、1798–1799 | 一次传送镜头请求的输入快照；执行期间只读 | C |
| `portalCameraInverseExitSurface` | 未发现业务读取 | Level 重置/赋值 1539、1800 | 当前仅写入缓存；若不需要则后续删除，不能当作已生效功能 | U |
| `portalTransitionVisualActive`, `portalTransitionRealAtExit` | Renderer 1849、1872；Level 传送逻辑 | Level 1540–1584、1648–1651、1807–1809 | 关卡传送表现状态；Renderer 只消费快照 | P |
| `portalTransitionVisualTimer` | Level 1581–1582 | Level 复位、倒计时、开始过渡 | 关卡传送表现计时；以后按游戏/表现时钟策略推进 | P |
| `portalTransitionVisualDuration` | Level 1650、1808 | 只有默认值，没有业务写者 | 传送效果配置，不是每帧运行结果 | 新局读取关卡配置；F1 保留 |
| `portalTransitionEntrySurface`, `portalTransitionExitSurface`, `portalTransitionExitCorrection` | Renderer 1864–1877 | Level 复位和启动过渡 | 传送表现请求的输入快照；Renderer 不反向修改 | P |

### 游戏流程与玩家

| 字段 | 当前读取者 | 当前写入者 | 目标归属与写入约束 | 生命周期 |
| --- | --- | --- | --- | --- |
| `gameFlowState` | Level 隐藏胜利界面；UI Debug 341 | Router；Level 胜利/隐藏胜利；App 重载 | 流程控制器唯一写。Settings 不作为互斥游戏阶段；阶段变化同时产生确定的 UI 呈现请求 | F |
| `isGameStarted`, `isGamePause`, `isGameOver`, `isGameWon` | Renderer 菜单分支；Runtime UI HUD 80–89；UI Debug；Level/TestScene 事件；Router 调试输出/内部逻辑 | Router；App；Level 胜利与复活；TestScene 复活；Renderer G/H；遗留 EngineUi 菜单实现 | 不再独立可写。改成阶段/重载状态的只读查询或过渡兼容视图；不能只增加 setter 但保留公开可写字段 | F |
| `restartRequested`, `returnToMainMenuRequested` | App 153–155 | Router Restart 499–500；App 239–240；后者没有 true 写入者 | 流程控制器的待执行重载命令，使用互斥请求而非两个可同时为真的布尔；App 消费并报告完成 | F |
| `isAlive` | Bike、Camera、Renderer 死亡效果、Level/TestScene | Bike 判死；Level/TestScene 碰撞/复活；Renderer T 键 1560 还会置 true | 玩家存活/倒地状态；死亡/复活由游戏侧控制，不由相机或 Renderer 写入。不要与 GameOver 强行等同 | P |
| `isExtremeSpeed` | Camera；Scene 遮挡；Level 传送/音效；Editor UI | Camera 112 按 `bikeSpeed` 计算，且只在 third-person 分支内执行 | 玩家运动状态/规则输出，由游戏侧按速度计算；相机只读，避免换镜头后游戏判断停更 | P |
| `bikeYaw`, `bikeLeanAngle` | Camera 自动对齐/倾斜 | Bike 270–271；Level 传送/复活；TestScene 复活 | 玩家/载具姿态快照，物理或角色控制器发布；相机不拥有物理姿态 | P |
| `bikeSpeed`, `bikeSteerAngle` | Scene 车轮/踏板；Camera；Renderer 特效；App 音效；Runtime UI 与旧 GameUi；Level/TestScene | Bike 268–269；Level/TestScene 事件或复活 | 玩家运动快照；由载具控制器发布，复活通过游戏重置入口完成 | P |
| `engineForce`, `lastPedal` | Bike 自身的踩踏/力计算 | Bike 208–253；Level 传送/复活复位 | 载具控制器内部运行状态，不向 UI 暴露任意写权限 | P |
| `jumpEnabled`, `hornEnabled`, `radioEnabled` | Bike 跳跃；Level 音效/提示/能力；Router HUD 提示 | Level 对应拾取触发器 1021、1100、1192 | 玩家能力/本局解锁；UI 只读并发送使用动作，不能直接改解锁值 | P |
| `showHints` | Router 和 Level 提示刷新 | Router Apply Settings 348 | 玩家偏好设置；与解锁状态组合决定 UI 是否显示，不属于本局能力数据 | R |
| `deathCount` | Level 胜利结算 226 | Level 存活边沿 1918 自增 | 本局统计；胜利屏读取快照 | P |
| `deathTimer` | Renderer 死亡特效 | Renderer 905–937；Bike、Level、TestScene 判死/复活清零 | 死亡表现的计时，由表现控制器接收死亡/复活事件后独占维护 | P；明确表现时钟策略 |
| `deathFactor` | Renderer 向 rendering.cpp 1696 传后处理参数 | Renderer 918–942 | 死亡表现输出；Renderer 消费最终系数，角色不直接调曲线 | P |
| `radioMuted` | Level 电台播放 1992、2040、2065 | Level M 动作 2042 翻转 | 当前是本局电台开关，归游戏音频控制；若以后作为用户偏好持久化，需要单独产品约定 | P |

### 载具配置与收集

| 字段 | 当前读取者 | 当前写入者 | 目标归属与写入约束 | 生命周期 |
| --- | --- | --- | --- | --- |
| `bikeTuning`；其成员 `maxSteerAngleDeg`, `steerSpeedDeg`, `maxLeanAngleDeg`, `leanSpeedDeg`, `wheelBase`, `driveForce`, `brakeForce`, `maxSpeed`, `gravityFactor` | 没有载具控制器读取这些成员；`maxSpeed` 只有 TestScene 365 的读改写。Bike 内存在同名局部常量，不是该配置的消费者 | `maxSpeed` 仅 TestScene 全收集奖励乘二；其他成员无业务写者 | 载具配置资源 + 本局升级修正；当前属于未接通配置，不能仅迁移字段就宣称调参/奖励生效 | 配置按资源生命周期；升级按 P；未接通部分按 U |
| `collectedItems`, `allCollected` | 未发现这两个字段的业务读取；收集事件回调使用事件局部计数执行逻辑 | Level 922、957；TestScene 356、364 | 本局进度快照；若保留，应让 HUD/规则显式消费，避免另一份权威计数漂移 | P |
| `totalCollectibles` | Level/TestScene 收集日志与提示 | 只有默认值 15，没有按关卡实际生成量写入 | 关卡目标配置/初始化结果，不能作为全局通用常量 | 新局按关卡初始化；F1 保留 |

### 图形开关与运行时 UI

| 字段 | 当前读取者 | 当前写入者 | 目标归属与写入约束 | 生命周期 |
| --- | --- | --- | --- | --- |
| `iblEnabled` | Renderer 1631 | Editor UI；Renderer 快捷键 1306；Level 两个区域触发器 1388、1412 | 用户渲染偏好与关卡环境覆盖必须分开；统一渲染设置入口，明确覆盖优先级 | 用户值 R；关卡覆盖 P |
| `bloomEnabled`, `ssrEnabled`, `ssaoEnabled` | Renderer 1799、2181、2183 及相应渲染通道 | Editor UI 1240–1244；Renderer 快捷键 1301、1311、1316 | 渲染设置；多个 UI 可提交同一设置命令，Renderer 读取已应用值 | R |
| `showRuntimeUi` | Runtime UI 显示、调试、渲染入口 | Runtime UI Debug checkbox 579；App 保存/恢复 | 运行时 UI 显示策略/调试覆盖；不能用它代表游戏已开始或允许玩家输入 | 显示偏好保留；重载后重新投影实际屏幕；F1 不改该值 |

## 4. UserState 自有字段完整清单

| 字段 | 当前读取者 | 当前写入者 | 目标归属与写入约束 | 生命周期 |
| --- | --- | --- | --- | --- |
| `renderMode` | Renderer 管线/通道选择；Camera uniform；Editor UI | Editor UI 视图菜单/面板；Renderer 调试快捷键；设备不支持 Wireframe 时回退 | 编辑器视口显示模式，通过合法 mode/capability 校验入口设置；不属于玩家状态 | E |
| `wireframeSupported` | Renderer 管线创建/过滤；Editor UI 1703 | Renderer `CreateDebugViewPipelines` 2625 | 设备能力只读。当前整份 UserState 重载会清为 false，SetUserState 只赋指针，不能靠字段默认值恢复真实能力 | D |
| `showEngineUi` | Renderer、Camera、Editor UI、Runtime UI、旧 GameUi、UIEditorWindow 入口 | Renderer F1；Router OpenEditor；App 保存/恢复；Editor UI View 菜单 | 编辑器整体可见性；独立于游戏阶段。改变它不隐式启动/结束本局 | E；F1 唯一直接切换的工作区字段 |
| `editorViewportBackdrop`, `editorViewportGrid` | Renderer 1776–1777 | Editor UI View 菜单 2051–2052 | 编辑器视口偏好，不写场景环境或游戏渲染配置 | E |
| `particlesEnabled` | Renderer 粒子更新/绘制；Editor UI 粒子显示和拾取 | Editor UI 1241、1272；Renderer ToggleParticles 分支 1553 | 粒子系统/渲染调试开关。当前同时控制模拟和绘制，目标需保留这个明确语义或拆成独立命令，不能误当全局暂停 | R/调试偏好 |
| `showRenderSettings`, `showContentBrowser`, `showSceneHierarchy`, `showEntityInspector`, `showConsole`, `showLightPanel`, `showCameraPanel`, `showDebugPanel`, `showAudioPanel`, `showParticlePanel`, `showRuntimeUiDebugPanel`, `showGameUiEditor` | Renderer 面板分发；各 Editor UI 面板；Runtime UI Debug；UIEditorWindow/RenderSystemUiEditor | Editor UI Window 菜单与布局重置 2030–2060；窗口关闭按钮；打开资源/选择粒子等入口；UIEditorWindow 自身的开关 | 编辑器工作区。UI 可以修改自己管理的窗口状态，但游戏流程不应重置整组开关；可作为布局配置保存 | E |
| `debugSelectionBounds`, `debugCollisionShapes` | Renderer 1203–1211 选择对象调试绘制 | Editor UI Debug 1686–1687 | 编辑器可视化工具设置；不会更改物理碰撞开关 | E |
| `frustumCullingEnabled`, `frustumCullingPadding` | Renderer 主视图/Portal 批次获取；Scene 静态网格裁剪 | Editor UI 1251–1253 | 渲染设置；padding 应经范围校验，统计是另一组只读数据 | R |
| `frustumCullingOffFps`, `frustumCullingOnFps` | Renderer 各自平滑计算读取上一值；当前 UI 不再展示二者 | Renderer 1639–1643 | 性能统计，非用户设置；不能把非同场景/同负载样本当严格性能对照 | D |
| `frustumCullingTotalCandidates`, `frustumCullingVisibleCandidates` | Editor UI Debug 1696 | Renderer 1734–1735 从 Scene 结果复制 | 只读渲染统计，当前含义为静态批次候选/可见量 | D |
| `lodEnabled`, `lodDebugDistance` | Scene 557、584–585 | 无业务写者，也没有当前 UI 控件 | `lodEnabled` 是渲染设置；`lodDebugDistance` 是调试覆盖。基线没有 SetupEntityLOD 调用/对象 LOD 配置，故不能宣称当前场景已有可观察效果 | 前者 R；后者 E，重建场景可清调试覆盖 |
| `activeParticleIndex` | Editor UI 粒子列表/Inspector/Gizmo | Editor UI 选择/删除；Renderer 点击选中其他对象时清除 1184、1189 | 编辑器场景选择；删除、清场或重载时检查/修正，不能把旧索引沿用到新容器 | S |
| `isSceneViewportHovered` | Camera 捕获与滚轮 | Editor UI 702、906；Renderer 1094 | 当帧视口输入上下文，单点生成后供输入仲裁使用，不是持久编辑器偏好 | I |
| `mosaicEnabled` | Renderer 1706 上传 mosaic uniform；合成 shader | Editor UI 1248；Renderer 调试快捷键 1592 | 后处理/调试设置，统一设置入口 | R/调试偏好 |
| `bloomExposure`, `bloomStrength` | Renderer 2192、1799 → rendering.cpp → composite.frag | Editor UI 1245、1247 | 已接通的后处理设置；曝光是场景合成曝光，不应因 Bloom 关闭而被当作无效配置 | R |

## 5. 第 2 步必须覆盖的流程写入来源

不能只改 Router：

- **Router**：开始、暂停、恢复、失败、返回菜单、重开、OpenEditor，以及 `RestoreSettingsReturnScreen` 这类旧返回辅助路径，均不能绕过流程入口。设置草稿/叠层可以保留自己的状态，不写游戏阶段充当返回栈。
- **App**：重载完成写阶段与清请求必须走同一流程接口；重载指令需要完成/失败反馈，不能靠散布的两个 bool 猜执行结果。
- **Renderer 活跃调试键**：G 在 949 行直接翻转 `isGameOver`；H 在 964 行翻转 `isGamePause` 并自行增删 Pause/HUD。这是活跃路径，不是注释，应转换为合法流程命令。
- **Level**：`ShowWinScreen` / `HideWinScreen`（230–245 行）直接写多个流程字段；复活 2478 行清 `isGameOver`。胜利/关闭胜利界面不能通过 widget 可见性反向推定阶段。复活只有在允许的上下文中恢复角色，不能顺便把 Paused/Victory 强制改成 Playing。
- **TestScene**：424 行复活清 `isGameOver`；当前 App 默认使用 Level，但 TestScene 仍参与编译并是可替换场景，不能留下后门。
- **遗留 EngineUi 菜单实现**：`DrawMainMenu(bool& isGameStarted)`、`DrawGamePause`、`DrawGameOver` 在 2178、2267–2277、2360–2370 行仍能改流程变量。当前 Renderer 调用已注释；应删除、适配命令接口或明确废弃，不能为使旧函数编译而重新暴露可写字段。
- **只读消费者**：Runtime UI HUD、UI Debug、Renderer 菜单条件、关卡事件 guard 都要切换到查询接口，避免一部分读旧 bool、另一部分读新阶段。

历史基线中 Renderer T 键曾通过 `isAlive = true` 复活玩家；当前实现已由 PlayerController 与 CameraController 分离，T 键只提交相机模式请求。

## 6. 第 2 步实施后的流程接口

`Source/Runtime/UserState/GameFlowController.hpp` 定义流程控制器，`GameplayState` 只持有 `gameFlow`。基线的 `gameFlowState`、`isGameStarted`、`isGamePause`、`isGameOver`、`isGameWon`、`restartRequested`、`returnToMainMenuRequested` 从状态结构删除，不能再直接赋值。

| 新接口/状态 | 使用者与权限 |
| --- | --- |
| `gameFlow.Request(GameFlowCommand)` | UI、游戏逻辑及调试快捷键请求 Start、Pause、Resume、OpenSettings、CloseSettings、GameOver、Victory、Restart、ReturnToMainMenu；返回 bool 表示命令是否接受。不能绕过它写内部阶段。 |
| `State()` | 只读阶段：MainMenu、Playing、Paused、GameOver、Victory、Loading、LoadFailed。Settings 不是互斥阶段，编辑器显示也不是阶段。 |
| `IsSettingsOpen()` | 只读设置叠层状态。设置打开造成的阻塞与 Paused 阶段分开记录；关闭设置不把原先暂停的游戏恢复成 Playing。 |
| `CanSimulate()` | 读取当前阶段及设置阻塞后的模拟意图。已接入下述关卡、物理、动画、事件与渲染侧更新门禁；它不等于输入/呈现/游戏时钟已完成全面拆分。 |
| `Revision()` | UI 检测流程变化，统一投影屏幕；不能从某个 widget 是否可见反向恢复游戏阶段。 |
| `TakeReloadRequest()`, `ReloadTarget()`, `CompleteReload(bool)` | App 是约定的唯一重载执行者。进入 Loading 后只能取一次请求；完成后进入目标阶段，失败则进入 LoadFailed。该约束由接口行为和 App 的接线共同保证，不能把公开方法理解为任意模块都可消费重载。 |

Runtime UI 的 `SyncGameFlowUi()` 负责按状态投影流程屏幕。Level 的胜利入口请求 Victory 后调用同步并更新结算文本；隐藏胜利 widget 只清理显示，不再写 Playing。Level/TestScene 的复活仅恢复角色，不再清某个会话终态布尔。

为让流程状态产生实际行为，本轮还接入了以下门禁：

- App 在不允许模拟时停止关卡 Update、PhysicsSystem、AnimationSystem、EventSystem 的推进；Input、UI、Renderer 与编辑器继续运行。自行车链条运行音量在暂停期间归零，不能据此宣称所有音频都已暂停。
- SceneManager 暂停物理姿态同步、车轮/踏板推进与游戏相机遮挡逻辑，保留 rider/IK 目标及世界变换传播，以支持编辑器修改。
- Renderer 暂停游戏触发器、粒子模拟、游戏相机推进与死亡效果计时；编辑器自由相机仍允许操作，冻结粒子与场景仍可渲染。
- Level 在当帧胜利命令被处理后重新检查 `CanSimulate()`，必要时立即退出 Update，避免本次函数尾部继续执行载具控制和复活动作。
- 初次加载与重载场景后，执行一次 SceneManager/Animation 的零时长更新，生成世界变换、IK 目标与初始骨骼姿态；菜单停模拟时显示这份有效姿态，不依赖先点击开始才能完成初始化。

重载失败的恢复边界也需要与阶段控制分开：子系统重建失败时停止主循环，不能只写 LoadFailed 后继续访问可能为空的 ECS 世界或物理系统。只有关卡初始化失败且有效子系统仍在时，才清理半成品场景、动画、触发器、粒子与事件；成功恢复为可渲染的空世界后显示 LoadFailed 并允许重试。恢复清理自身失败同样停止主循环。清理 GPU 资源前等待设备完成在途工作。这是本轮重载错误处理的边界，并非任何异常都能无条件返回菜单。

这些是流程接入所需的模拟门禁。当前 App 已按“输入采样 → 重载/流程命令 → 模拟 → 呈现”固定阶段运行：暂停时不推进游戏模拟，输入捕获只屏蔽玩家控制，不改变编辑器相机与呈现。编辑器主动修改变换、属性或相机不等同于游戏模拟自行推进。

本接口统一流程写入和 UI 投影；玩家数据、相机结果、渲染参数、编辑器工作区按第 3–6 节的状态分组、输入阶段和生命周期策略运行。历史表格仍用于追溯旧字段，不表示当前实现保留旧入口。

## 7. 完成边界与验收

第 1 步完成意味着：上面每个字段都有明确的当前消费者、目标所有者和生命周期；尚无读取的字段没有被包装成新功能。

第 2 步完成应能证明：所有活跃及仍编译的流程写入路径都经过受限命令；非法转换不改变阶段；连续/重复命令不产生相互矛盾的请求；重载完成才公布目标阶段；只读查询与 UI 呈现一致；Settings 和 F1 不被硬塞进游戏阶段。测试至少覆盖主菜单设置开关、暂停后设置开关、胜利后重开、菜单与游玩期间 F1、失败/胜利后的非法 Resume、重复 Restart、重载中的返回菜单请求。

后续按上述归属拆出状态并缩小模块参数时，需另外验证：重开保留已应用画质与编辑器布局；旧场景选择失效；玩家存活与流程失败分开；相机手动/自动接管不争写；UI 输入捕获与模拟暂停分别生效。这些是后续边界，不能仅凭完成本清单或新增流程控制器就标记完成。

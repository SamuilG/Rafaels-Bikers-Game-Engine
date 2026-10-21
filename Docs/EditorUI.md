# 引擎编辑器 UI

编辑器默认采用固定分区：左侧约 18% 为场景层级，中间为场景视口，右侧约 25% 为实体属性及渲染设置等工具标签；左侧与中间下方约 22% 为资源浏览器和日志控制台。右侧默认显示实体属性，底部默认显示资源浏览器。1280×720 的无窗口布局测试中，场景内容仍占屏幕约 41%。

通过 **窗口（Window）** 菜单打开灯光、相机、粒子、音频及诊断面板；**游戏 UI 工具（Game UI Tools）** 子菜单提供游戏 UI 编辑器和运行时 UI 诊断。面板关闭按钮与菜单开关保持一致。**视图 → 恢复编辑器布局（View → Reset Editor Layout）** 恢复默认分区和面板开关。日常拖动和停靠布局由 ImGui 保存；同次运行中手动选择的标签不会被默认布局重设。切换中英文只改变显示标题，`###` 后的稳定 ID 不变，不会创建另一套窗口布局。

本次整理移除了没有实际效果的菜单和调节项。渲染参数保留已接入的开关与数值；Bloom 关闭时其强度、视锥剔除关闭时其边距会灰显。受玩法控制的相机 FOV、特殊镜头期间的轨道参数，以及触发器控制的粒子可见性会禁用直接修改并显示原因，避免数值改完又被下一帧覆盖。

## Scene Gizmos

参照 [Unity 的变换工具](https://docs.unity3d.com/Manual/PositioningGameObjects.html)、
[Gizmos 显示菜单](https://docs.unity3d.com/Manual/GizmosMenu.html) 和
[Godot 的 3D 编辑操作](https://docs.godotengine.org/en/stable/tutorials/3d/introduction_to_3d.html)，
场景视口现在将变换工具与组件辅助图形分开控制：

| 操作 | 当前实现 |
| --- | --- |
| 变换工具 | `Q` 选择、`W` 移动、`E` 旋转、`R` 缩放；工具栏显示当前模式 |
| 坐标空间 | World / Local 切换；缩放使用 Local，并明确显示这一限制 |
| 吸附 | Snap 常开，或拖动期间按住 Ctrl；Steps 分别设置位移米数、旋转角度和缩放步长 |
| 聚焦 | `F` 或 Frame 对齐选中对象；按照变换后的完整网格包围盒和视口比例取景 |
| 组件标识 | 相机、灯光、粒子使用固定屏幕尺寸的小图标，点击可选中；相机和灯光选中后图标偏离操作轴中心 |
| 辅助线 | 相机视锥、平行光方向、点光范围球、聚光内外锥；默认仅选中对象显示 |
| 显示控制 | Gizmos 按钮和 View → Scene Gizmos 共用开关、分类、图标及操作轴大小设置 |

相机的预览距离只控制辅助线，不修改真实相机远裁剪面。灯光辅助线读取与渲染相同的局部方向、实体变换、光照范围及聚光角度；方向光的短箭头只表示方向。标识和辅助线作为 Scene 上的覆盖层显示，不进行场景深度遮挡，也不会进入 Game 画面。关闭 Show Gizmos 隐藏组件标识和辅助线；选择工具、变换工具仍可独立使用。

受控制器接管的相机、跟随模式相机及场景控制的粒子位置禁用变换；相机和灯光不提供缩放。未捕获鼠标时快捷键操作编辑工具，捕获鼠标后才启用场景相机的键盘飞行移动。所有设置目前属于编辑器会话。

`Tests/EditorUI/run-scene-gizmo-tests.ps1` 使用真实 ImGui 和 ImGuizmo 验证工具按钮、禁用状态、设置菜单、鼠标隔离、World / Local 拖动及实际吸附，另外验证灯光几何和不同视口比例下的聚焦。测试不启动 Vulkan；日志位于 `Intermediate/Diagnostics/scene-gizmos`。

## 保存与限制

### 实体组合与父子层级

- 从 Assets 拖入 GLB 时，为这次导入创建以文件名命名的根节点，例如 `sponza`；网格部件保留为子实体，层级默认折叠。选中根节点可整体移动、旋转、缩放、隐藏或删除，展开后仍可编辑单个部件。
- 层级面板中 Ctrl+单击多选，再点击 **Group** 或按 **Ctrl+G** 创建组合。**New Group** 创建空容器；将实体或多选项拖到另一个节点上，可设置父对象。拖到列表空白处，或右键 **Move to scene root**，可移回场景根层级。
- **Shift+左键** 从上次普通／Ctrl 点击的实体开始，按当前展开的树状列表顺序连续选择；反向点击或重复 Shift 点击可调整范围。**Ctrl+Shift+左键** 追加范围并保留已有选择。折叠组内的子对象不参与范围选择；锚点被删除或折叠后，下一次范围选择从点击的对象重新开始。
- 右键组合选择 **Ungroup**，或按 **Ctrl+Shift+G**，将子实体移回组合的父层级并删除空容器。组合、重新设置父对象和取消组合均保持对象的世界位置不变。已有平铺模型可右键其中任一部件，选择 **Group imported model**，一次收拢同次导入的所有部件。
- Inspector 的 Name 可重命名对象；有父对象时，属性面板编辑局部变换，视口 Gizmo 使用世界变换。F 聚焦组合时计算所有子网格的合并包围盒。父节点的 Visible 影响整个子树，子节点自身的可见开关仍保留。
- 选中组合后按 **F2**，或右键选择 **Rename Group**，打开重命名对话框；**Enter** 确认、**Esc** 取消。名称自动去除首尾空白，空名称及 `::` 会被拒绝，同级重名自动追加后缀。层级面板和 Inspector 名称同步更新，子对象、变换及稳定 ID 保留。
- 通用项目的场景文档保存根节点、父子关系、局部变换及稳定实体 ID，重开和 Play 时恢复层级。原生游戏的旧式变换快照仍依赖已存在的对象，按完整层级路径匹配，不负责重建导入资源。
- 为保留现有游戏控制器的约束，PlayerCamera 保持根节点，原生车辆与骑手绑定不参与重新组合；父子循环和不可逆变换会被拒绝。

`Tests/EditorUI/run-entity-grouping-tests.ps1` 链接真实引擎，使用隐藏的 Vulkan 窗口导入 `Assets/Models/sponza.glb`，检查父子变换、Jolt 碰撞体、可见性、真实层级控件、场景保存重开和递归删除。先构建 Release；诊断保存在 `Intermediate/Diagnostics/entity-groups`。

- **文件 → 保存／载入场景快照** 使用 `Assets/MySceneSave.json`。它保存命名实体变换和粒子配置，依赖关卡先创建这些对象；这不是完整工程保存，也不包含所有资源、组件和玩法状态。
- Demo 原有的 Sphere 粒子发射器跟随 `BaseballBat.nails_0` 的行为保留。此类发射器的位置会灰显；需要独立发射器时使用 Cone、Box 或 Disk。删除粒子组会解除对应触发器绑定，并修正后续组的索引。
- 编辑器仍运行在游戏应用中，本次整理未完成引擎、编辑器与游戏模块的构建隔离。渲染设置主要作用于当前会话。
- 跨启动保存停靠位置和分区比例，工具窗口开关仍使用启动默认值，首次窗口焦点由 ImGui 管理；不保证重启后恢复上次打开或激活的可选工具。
- 无窗口测试验证布局、变换与触发器逻辑，不验证 Vulkan 画面、字体实际显示效果、GPU 资源释放或鼠标拖拽的视觉反馈。

## 回归验证

在仓库根目录使用安装了 Visual Studio C++ 工具的 PowerShell 运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-layout-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-editor-transform-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/EditorUI/run-trigger-binding-tests.ps1
```

布局测试运行真实 ImGui 帧，覆盖 1280×720、1920×1080、默认活动标签、同次运行的标签选择、可选工具、旧布局迁移、自定义布局保存／加载／重置及真实翻译切换。变换测试使用真实 ImGuizmo，覆盖负缩放、镜像、旋转和连续属性刷新。触发器测试编译真实 `trigger.cpp`，覆盖删除绑定组、索引左移、无绑定标记，以及禁用或已完成的一次性触发器；测试的图形边界适配器若被调用会立即失败，不替代触发器逻辑。

编译和结果日志写入 `Intermediate/Diagnostics` 对应测试目录；测试不启动整个引擎，也不读取或覆盖用户的 `imgui.ini`。

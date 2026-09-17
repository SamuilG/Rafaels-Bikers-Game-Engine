# 引擎编辑器 UI

编辑器默认采用固定分区：左侧约 18% 为场景层级，中间为场景视口，右侧约 25% 为实体属性及渲染设置等工具标签；左侧与中间下方约 22% 为资源浏览器和日志控制台。右侧默认显示实体属性，底部默认显示资源浏览器。1280×720 的无窗口布局测试中，场景内容仍占屏幕约 41%。

通过 **窗口（Window）** 菜单打开灯光、相机、粒子、音频及诊断面板；**游戏 UI 工具（Game UI Tools）** 子菜单提供游戏 UI 编辑器和运行时 UI 诊断。面板关闭按钮与菜单开关保持一致。**视图 → 恢复编辑器布局（View → Reset Editor Layout）** 恢复默认分区和面板开关。日常拖动和停靠布局由 ImGui 保存；同次运行中手动选择的标签不会被默认布局重设。切换中英文只改变显示标题，`###` 后的稳定 ID 不变，不会创建另一套窗口布局。

本次整理移除了没有实际效果的菜单和调节项。渲染参数保留已接入的开关与数值；Bloom 关闭时其强度、视锥剔除关闭时其边距会灰显。受玩法控制的相机 FOV、特殊镜头期间的轨道参数，以及触发器控制的粒子可见性会禁用直接修改并显示原因，避免数值改完又被下一帧覆盖。

## 保存与限制

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

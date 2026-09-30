# 单车 Demo 工程

工程入口是 `Projects/Bikers/Bikers.steerproj`。它通过 `contentModule: bikers` 选择单车内容模块，通过 `startupScene: level1` 选择完整游戏关卡，并记录资源根和场景快照位置。

## 打开与运行

1. 构建解决方案并启动 `Bin/Debug-x64/Editor.exe`。
2. 选择 File → Open Example Project，或者 Open Project… / Ctrl+O 选择 `.steerproj`。
3. 场景层级、车辆、关卡与资源会一起加载。编辑态停止玩法、物理和游戏动画，保留场景变换传播、自由相机和编辑器渲染。编辑相机初始定位在单车出生点附近。
4. File → Save Scene Snapshot / Ctrl+S 保存已有命名实体的变换及粒子参数；Load Scene Snapshot / Ctrl+Shift+O 重新读取。
5. 顶部 **Play** 在同级的 **Game Viewport** 标签中运行游戏；**Pause / Resume** 暂停或继续模拟，**Stop** 恢复进入运行前的编辑场景和未保存改动。运行时可切回 Scene Viewport，使用独立的编辑相机查看场景。只有激活 Game Viewport 后，游戏才接收玩家输入。
6. **Project → Run Saved Project in Separate Window** 保留独立 Demo 窗口启动方式，使用磁盘上的场景快照。
7. 停止运行后，File → Close Project 返回空工作区。

Scene / Game 目前共用渲染目标，以同级标签切换，不能拆成同时显示的两个视口。已有布局会自动加入 Game 标签，保留其他面板的布局。运行期间禁止保存/加载场景快照及切换工程，避免把运行时变化写入编辑工程。

Scene Viewport 默认显示玩家相机图标和 5 米视锥预览线框，分别通过 View → Player Camera Icon / Player Camera Frustum 开关控制。预览保持真实位置、方向、FOV、宽高比和近平面，标有 `Preview 5.0 m` 的末端是显示用截面，不是游戏相机的远裁剪面。View → Camera Frustum Display 可调整预览距离，或开启 Use Camera Far Clip 检查完整裁剪范围。此设置不改变游戏渲染距离。悬停图标可查看真实相机参数。

线框与粒子图标一样属于编辑器叠加层，隔着模型也可观察，不出现在 Game Viewport 或独立 Demo 中。其宽高比随同级视口的尺寸变化。完整范围模式保留真正的齐次裁剪；由于实际远端可达 1000 米，在近距离观察时仍可能只看到被 Scene 视口裁剪后的部分棱线。

编辑态会根据单车位置刷新玩家相机预览，不推进物理、触发器或相机动画；Play 时跟随实际游戏相机，Pause 时保持暂停姿态，Stop 后恢复编辑态预览。图标资源为 `Assets/Textures/CameraIcon.png`，打包工程时会随 Assets 一起复制；旧外部资源包缺少该图片时显示字母 C 作为替代。

Play 重新构造游戏实例，同步已有同名实体的变换、启用状态、物理形状/位置和粒子设置。未保存的新增/删除实体及任意组件结构变更目前不会完整复制到运行实例。Stop 恢复的是原编辑世界本身，因此这些编辑改动仍保留；这与完整的场景复制是两个不同范围。

两个程序均支持 `--project <工程文件>`，相对命令行参数在切换资源目录之前解析。工程内部路径始终相对工程文件，不依赖启动时的工作目录。

## 工程内容与打包

仓库中的工程引用 `../../Assets`，便于复用现有资源。运行 `Tools/Package-BikersProject.ps1` 后，`Intermediate/Packages/Bikers` 包含：

- `Bikers.steerproj`：资源根改为本目录下的 `Assets`。
- `Assets/`：完整资源树，包括模型、贴图、声音、UI、字体及已编译着色器。
- `gamecontrollerdb.txt`：输入系统使用的手柄映射。
- `Source/`：单车内容模块、关卡、车辆控制器与 HUD 源文件。
- 已保存的 `Scenes/Main.snapshot.json` 和使用说明。

整个目录可移动到其他位置，由本版 Editor 打开。脚本遇到已存在的输出目录会停止；可通过 `-Destination` 指定新目录。包不携带引擎程序或完整引擎源代码。

## 构建和生命周期

`BikersProject.lib` 只编译一次单车内容，Editor 和 Demo 均链接该库与 Engine；Engine 不依赖具体内容模块。Editor 宿主注册 `bikers`，未知模块或启动场景会明确报错。

打开项目先验证描述文件和关键资源；验证失败保留当前工作区。成功的打开/关闭请求在当前帧结束后退出旧 Application，销毁世界及 GPU/ImGui 资源，再按新资源根创建应用。UI 静态缓存、字体指针、旧回调和场景实体命名计数随生命周期重置，避免跨项目引用旧资源。项目布局单独存放在 `.steer/Editor.ini` 和 `.steer/Demo.ini`。

## 当前边界

这是原生 C++ 内容工程。关卡构造和玩法仍由 BikersProject 编译提供，打开工程不会编译代码或热加载 DLL；修改 C++ 后需重新构建内容模块和宿主。

场景快照仍只覆盖既有命名实体的变换及粒子设置，不是完整场景序列化。新增/删除模型、完整组件与复合物理配置尚不能通过快照往返保存。项目打开、资源定位和游戏启动已独立于这项后续工作。

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "../../UserState/UserState.hpp"

namespace engine {

    // Game UI Editor 的 ImGui 宿主窗口。
    // 这里只负责编辑器侧的可视化编辑，不参与真正的运行时逻辑。
    class UIEditorWindow {
    public:
        static void Draw(UserState& state);
        // 复用引擎现有的缩略图 / 贴图解析能力，让编辑器画布能直接显示 Assets 里的图片。
        static void SetTexturePreviewResolver(std::function<void*(const std::string&)> resolver);
        // 当编辑器保存 / 编译 UI 文件后，通过回调通知运行时 UI 热重载。
        static void SetRuntimeUiFileChangedCallback(std::function<void(const std::filesystem::path&)> callback);
        // 告诉其他编辑器模块当前是否应该让出鼠标 / 键盘输入。
        static bool WantsMouseCapture();
        static bool WantsKeyboardCapture();
    };

} // namespace engine

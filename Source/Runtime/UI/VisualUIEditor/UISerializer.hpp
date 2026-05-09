#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "UIScreen.hpp"

namespace engine {

    // UIScreen 的 JSON 序列化入口。
    // 当前保存的是可编辑源文件，同时也可作为运行时直接加载的数据格式。
    class UISerializer {
    public:
        // 将 UIScreen 序列化为 JSON 并写入 .ui.json 文件，成功返回 true
        static bool SaveToFile(const UIScreen& screen, const std::filesystem::path& path);

        // 从 .ui.json 文件反序列化并构建 UIScreen，失败返回 nullptr
        static std::unique_ptr<UIScreen> LoadFromFile(const std::filesystem::path& path);

        // 内存版序列化 —— 把 UIScreen 整棵树打包成 JSON 字符串。
        // 失败返回空字符串。主要用途：编辑器的 Undo/Redo 历史栈，
        // 比临时文件干净，比写自定义二进制可控。
        static std::string SaveToString(const UIScreen& screen);

        // 从 JSON 字符串反序列化构建 UIScreen，失败返回 nullptr。
        static std::unique_ptr<UIScreen> LoadFromString(const std::string& json);
    };

} // namespace engine

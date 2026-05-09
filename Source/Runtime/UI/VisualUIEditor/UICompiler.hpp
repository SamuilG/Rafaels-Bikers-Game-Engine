#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace engine {

    class UIScreen;
    class UIManager;

    // UICompiler 负责把可编辑的 .ui.json 屏幕进行结构性校验，并写出
    // 一份 "compiled" 副本供运行时直接加载。
    //
    // 它的职责严格限制在 校验 + 导出，保持与编辑器界面解耦：
    //   * 编辑器 (UIEditorWindow) 调用它来一键编译当前屏幕；
    //   * 运行时调试面板 (RuntimeUIDebugPanel) 也可以复用同一个 Compile()，
    //     在 PR 之前快速校验场上加载的所有屏幕。
    //
    // 校验规则：
    //   - 重复的元素 ID
    //   - 元素名 / Transform / Style 字段非法（NaN、负尺寸等）
    //   - 引用的纹理 / 字体 / 图片资源不存在
    //   - 按钮缺少 onClick 事件
    //   - 数据绑定指向不存在的目标元素，或目标属性与元素类型不匹配
    //   - 动画轨道指向不存在的目标元素
    //   - 屏幕引用的 enter/exit 动画在 clip 列表里找不到
    //   - 屏幕引用的主题路径不存在，或 preset 在主题里找不到
    //   - 可选：检查 UI 中使用的事件名是否已被 UIManager 注册
    class UICompiler {
    public:
        // 单次编译的输出。warnings 不阻断成功，errors 会让 success = false。
        struct Result {
            bool success = false;
            int warningCount = 0;
            int errorCount = 0;
            std::filesystem::path outputPath;          // 实际写入的 compiled 文件
            std::vector<std::string> warnings;
            std::vector<std::string> errors;
        };

        // 校验 + 导出 compiled 文件。
        // sourcePath 用来推断输出文件名；可以为空（取屏幕名）。
        // optionalUiManager 不为空时，会额外校验 UI 元素事件名是否已注册到运行时。
        static Result Compile(const UIScreen& screen,
                              const std::filesystem::path& sourcePath,
                              const UIManager* optionalUiManager = nullptr);

        // 仅做校验，不写文件 —— 给调试面板调用。
        static Result Validate(const UIScreen& screen,
                               const UIManager* optionalUiManager = nullptr);
    };

} // namespace engine

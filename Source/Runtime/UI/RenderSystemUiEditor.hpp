#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

#include "../UI/VisualUIEditor/UIEditorWindow.hpp"

namespace engine::render_system_ui_editor {

    using TexturePreviewResolver = std::function<void*(const std::string&)>;
    using RuntimeUiFileChangedCallback = std::function<void(const std::filesystem::path&)>;

    inline void Configure(TexturePreviewResolver texturePreviewResolver,
                          RuntimeUiFileChangedCallback fileChangedCallback) {
        UIEditorWindow::SetTexturePreviewResolver(std::move(texturePreviewResolver));
        UIEditorWindow::SetRuntimeUiFileChangedCallback(std::move(fileChangedCallback));
    }

    inline void Draw(UserState& state) {
        if (state.showEngineUi && state.showGameUiEditor) {
            UIEditorWindow::Draw(state);
        }
    }

    inline bool WantsMouseCapture() {
        return UIEditorWindow::WantsMouseCapture();
    }

} // namespace engine::render_system_ui_editor

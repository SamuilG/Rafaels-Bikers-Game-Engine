#include "UIEditorWindow.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "../../../../ThirdParty/imgui/misc/cpp/imgui_stdlib.h"
#include "../../../../ThirdParty/imgui/misc/cpp/imgui_stdlib.cpp"

#include "../EngineUi.hpp"
#include "UICompiler.hpp"
#include "VisualUIRuntime.hpp"

namespace engine {

    namespace {

        // 画布缩放控制柄的 8 个方向。
        enum class ResizeHandle : std::uint8_t {
            None = 0,
            TopLeft,
            Top,
            TopRight,
            Right,
            BottomRight,
            Bottom,
            BottomLeft,
            Left
        };

        enum class UiFileDialogMode : std::uint8_t {
            None = 0,
            Open,
            SaveAs
        };

        enum class AnimationPreset : std::uint8_t {
            FadeIn = 0,
            FadeOut,
            SlideInLeft,
            SlideInRight,
            Pop,
            Pulse,
            Shake
        };

        struct AnimatedPropertySnapshot {
            UIElementId elementId = 0;
            UIAnimationProperty property = UIAnimationProperty::Opacity;
            UIValue value;
        };

        struct AnimationKeySelection {
            int trackIndex = -1;
            int keyframeIndex = -1;
        };

        // 编辑器的 Undo / Redo 历史状态。
        // 设计要点：
        //   * 每条历史记录就是一份完整的 UIScreen JSON 序列化字符串 ——
        //     做法粗暴但 100% 正确，省掉为每种命令写 inverse 的成本；
        //     UI 屏幕规模小（~50KB），栈深 64 也只有几 MB。
        //   * 提交策略：基于 ImGui::IsAnyItemActive() 的"非活动帧"提交。
        //     拖滑块、输入文本时活动 → 不提交；松手/失焦后下一帧检测到差异 → 一次入栈。
        //     菜单/按钮触发的操作也走这条路径，因为点击释放后该项即变为非活动。
        //   * 选中状态不进栈 —— 经典编辑器都是只 undo 数据；undo 后若选中元素已不存在则清空。
        struct UiEditorHistoryState {
            std::deque<std::string> undoStack;            // 旧状态：执行 Undo 时弹出
            std::deque<std::string> redoStack;            // 新状态：执行 Redo 时弹出
            std::string lastCommittedJson;                // 最近一次入栈的基线，用来检测变化
            bool initialized = false;                     // 第一次有屏幕之前不要尝试比较
            bool hadActiveItemLastFrame = false;          // 仅作 telemetry / 未来扩展
            bool suppressNextCommit = false;              // ApplySnapshot 后跳过一次比较，避免回环
            static constexpr std::size_t kMaxHistory = 64;
        };

        // Game UI Editor 的整窗口会话状态。
        // 这里集中保存当前编辑的屏幕、选中节点、拖拽/缩放状态和文件对话框状态。
        struct UiEditorSession {
            std::unique_ptr<UIScreen> screen;
            std::filesystem::path currentPath;
            UIElementId selectedElementId = 0;
            UIElementId renameElementId = 0;
            UIElementId pendingDeleteElementId = 0;
            UIElementId draggedElementId = 0;
            UIElementId resizedElementId = 0;
            char renameBuffer[128] = {};
            char filePathBuffer[260] = "Assets/ui/";
            int selectedResolution = 0;
            float zoomPercent = 100.0f;
            float gridSize = 10.0f;
            bool showGrid = true;
            bool enableSnapping = true;
            bool previewMode = false;
            bool isDraggingElement = false;
            bool isResizingElement = false;
            bool layoutInitialized = false;
            glm::vec2 dragPointerOffset = glm::vec2(0.0f);
            glm::vec2 resizeDragStartPoint = glm::vec2(0.0f);
            UIRect resizeInitialRect;
            UIRect resizeInitialParentRect;
            UITransform resizeInitialTransform;
            ResizeHandle activeResizeHandle = ResizeHandle::None;
            UiFileDialogMode fileDialogMode = UiFileDialogMode::None;
            UIElementId previewHoveredElementId = 0;
            UIElementId previewPressedElementId = 0;
            int selectedAnimationClipIndex = -1;
            int selectedAnimationTrackIndex = -1;
            int selectedAnimationKeyframeIndex = -1;
            int newAnimationTrackPropertyIndex = 0;
            int selectedAnimationPresetIndex = 0;
            float animationPreviewTime = 0.0f;
            float animationTimelineZoom = 1.0f;
            bool isAnimationPreviewPlaying = false;
            bool isDraggingTimelineKeys = false;
            bool isMarqueeSelectingKeys = false;
            UIAnimator previewAnimator;
            std::vector<AnimatedPropertySnapshot> animationPreviewSnapshots;
            std::vector<AnimationKeySelection> selectedAnimationKeys;
            std::vector<float> draggedKeyframeOriginalTimes;
            ImVec2 timelineDragStartMouse = ImVec2(0.0f, 0.0f);
            ImVec2 marqueeSelectionStart = ImVec2(0.0f, 0.0f);
            ImVec2 marqueeSelectionEnd = ImVec2(0.0f, 0.0f);
            std::unordered_set<std::uint64_t> collapsedAnimationTracks;
            int contextAnimationTrackIndex = -1;
            int contextAnimationKeyframeIndex = -1;
            float contextAnimationTime = 0.0f;
            bool isDraggingRootWindow = false;
            ImVec2 rootWindowDragStartMouse = ImVec2(0.0f, 0.0f);
            ImVec2 rootWindowDragStartPos = ImVec2(0.0f, 0.0f);
            bool wantsMouseCapture = false;
            bool wantsKeyboardCapture = false;
            UiEditorHistoryState history;
        };

        UiEditorSession& GetSession() {
            static UiEditorSession session;
            return session;
        }

        // 由 RenderSystem 注入，用于把 Assets 中的图片解析成 ImGui 可直接绘制的纹理。
        std::function<void*(const std::string&)>& GetTexturePreviewResolver() {
            static std::function<void*(const std::string&)> resolver;
            return resolver;
        }

        // 保存 / 编译 UI 文件后，通知运行时 UI 尝试热重载对应文件。
        std::function<void(const std::filesystem::path&)>& GetRuntimeUiFileChangedCallback() {
            static std::function<void(const std::filesystem::path&)> callback;
            return callback;
        }

        void TrackWindowInputCapture(UiEditorSession& session) {
            // 用统一的“谁获得焦点谁吃输入”策略，避免 UI Editor 和场景视口互相抢输入。
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
                session.wantsMouseCapture = true;
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                session.wantsKeyboardCapture = true;
            }
        }

        void DrawRootWindowMoveHandles(UiEditorSession& session) {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (!window) {
                return;
            }

            constexpr float handleSize = 18.0f;
            constexpr std::array<const char*, 4> handleIds = {
                "##MoveHandleTopLeft",
                "##MoveHandleTopRight",
                "##MoveHandleBottomLeft",
                "##MoveHandleBottomRight"
            };
            const ImVec2 windowPos = ImGui::GetWindowPos();
            const ImVec2 windowSize = ImGui::GetWindowSize();
            const std::array<ImVec2, 4> handlePositions = {
                ImVec2(windowPos.x + 6.0f, windowPos.y + 6.0f),
                ImVec2(windowPos.x + windowSize.x - handleSize - 6.0f, windowPos.y + 6.0f),
                ImVec2(windowPos.x + 6.0f, windowPos.y + windowSize.y - handleSize - 6.0f),
                ImVec2(windowPos.x + windowSize.x - handleSize - 6.0f, windowPos.y + windowSize.y - handleSize - 6.0f)
            };

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 fillColor = IM_COL32(58, 66, 82, 120);
            const ImU32 borderColor = IM_COL32(180, 190, 210, 180);

            for (std::size_t index = 0; index < handlePositions.size(); ++index) {
                const ImVec2 handlePos = handlePositions[index];
                const ImVec2 handleMax(handlePos.x + handleSize, handlePos.y + handleSize);
                ImGui::SetCursorScreenPos(handlePos);
                ImGui::InvisibleButton(handleIds[index], ImVec2(handleSize, handleSize));

                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    session.wantsMouseCapture = true;
                }

                if (ImGui::IsItemActivated()) {
                    session.isDraggingRootWindow = true;
                    session.rootWindowDragStartMouse = ImGui::GetMousePos();
                    session.rootWindowDragStartPos = windowPos;
                }

                drawList->AddRectFilled(handlePos, handleMax, fillColor, 4.0f);
                drawList->AddRect(handlePos, handleMax, borderColor, 4.0f, 0, 1.0f);
            }

            if (session.isDraggingRootWindow) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const ImVec2 mouseDelta(
                        ImGui::GetMousePos().x - session.rootWindowDragStartMouse.x,
                        ImGui::GetMousePos().y - session.rootWindowDragStartMouse.y);
                    ImGui::SetWindowPos(ImVec2(
                        session.rootWindowDragStartPos.x + mouseDelta.x,
                        session.rootWindowDragStartPos.y + mouseDelta.y));
                }
                else {
                    session.isDraggingRootWindow = false;
                }
            }
        }

        void ResetPreviewInteraction(UiEditorSession& session);
        bool IsPreviewInteractiveType(UIElementType type);
        void SyncAnimationSelection(UiEditorSession& session);
        void StopAnimationPreview(UiEditorSession& session, bool restoreAnimatedProperties);
        void StartAnimationPreview(UiEditorSession& session);
        void ScrubAnimationPreview(UiEditorSession& session, float sampleTime);
        void DrawAnimationPanel(UiEditorSession& session);

        const std::array<const char*, 4>& GetResolutionLabels() {
            static const std::array<const char*, 4> labels = {
                "1280 x 720",
                "1600 x 900",
                "1920 x 1080",
                "2560 x 1440"
            };
            return labels;
        }

        glm::vec2 ResolutionFromIndex(int index) {
            switch (index) {
            case 0: return glm::vec2(1280.0f, 720.0f);
            case 1: return glm::vec2(1600.0f, 900.0f);
            case 2: return glm::vec2(1920.0f, 1080.0f);
            case 3: return glm::vec2(2560.0f, 1440.0f);
            default: return glm::vec2(1920.0f, 1080.0f);
            }
        }

        // 把当前屏幕序列化为字符串。失败 / 空屏幕时返回 ""。
        std::string SnapshotCurrentScreen(const UiEditorSession& session) {
            if (!session.screen) {
                return {};
            }
            return UISerializer::SaveToString(*session.screen);
        }

        // 在加载 / 替换屏幕之后调用：把当前快照作为新基线，丢弃所有历史。
        void ResetHistory(UiEditorSession& session) {
            session.history.undoStack.clear();
            session.history.redoStack.clear();
            session.history.lastCommittedJson = SnapshotCurrentScreen(session);
            session.history.initialized = !session.history.lastCommittedJson.empty();
            session.history.suppressNextCommit = false;
        }

        // 用一份 JSON 快照替换当前屏幕。保留 selection 若元素仍存在。
        void ApplyHistorySnapshot(UiEditorSession& session, const std::string& snapshot) {
            if (snapshot.empty()) {
                return;
            }
            auto restored = UISerializer::LoadFromString(snapshot);
            if (!restored) {
                EngineUi::LogPrint("[UIEditor] Failed to restore history snapshot\n");
                return;
            }

            // 暂停动画预览，避免它对刚被替换走的元素继续写入。
            StopAnimationPreview(session, false);

            const UIElementId previousSelection = session.selectedElementId;
            session.screen = std::move(restored);

            // 选中状态仅在元素仍存在时保留，否则回退到根画布。
            if (previousSelection == 0 || !session.screen->FindById(previousSelection)) {
                session.selectedElementId = session.screen->GetRootCanvas() ? session.screen->GetRootCanvas()->GetId() : 0;
            }

            // 清掉指向已删除元素的瞬态状态。
            session.renameElementId = 0;
            session.pendingDeleteElementId = 0;
            session.draggedElementId = 0;
            session.resizedElementId = 0;
            session.previewHoveredElementId = 0;
            session.previewPressedElementId = 0;
            session.isDraggingElement = false;
            session.isResizingElement = false;
            session.activeResizeHandle = ResizeHandle::None;

            SyncAnimationSelection(session);
            session.history.lastCommittedJson = snapshot;
            // 防止本帧末尾的 commit 把刚还原的状态又当成"新变更"再次入栈。
            session.history.suppressNextCommit = true;
        }

        // 在每帧末尾调用：发现差异且当前没有任何 ImGui 项处于活动状态时入栈一次。
        // 这天然把"一次拖拽"折叠成一次 undo step。
        void CommitHistoryIfChanged(UiEditorSession& session) {
            if (!session.screen || !session.history.initialized) {
                return;
            }
            if (session.history.suppressNextCommit) {
                session.history.suppressNextCommit = false;
                session.history.hadActiveItemLastFrame = ImGui::IsAnyItemActive();
                return;
            }
            // 任意 ImGui 项处于活动状态意味着用户正在拖拽 / 输入 —— 等他松手再合并入栈。
            if (ImGui::IsAnyItemActive()) {
                session.history.hadActiveItemLastFrame = true;
                return;
            }

            std::string snapshot = SnapshotCurrentScreen(session);
            if (snapshot.empty() || snapshot == session.history.lastCommittedJson) {
                session.history.hadActiveItemLastFrame = false;
                return;
            }

            session.history.undoStack.push_back(std::move(session.history.lastCommittedJson));
            while (session.history.undoStack.size() > UiEditorHistoryState::kMaxHistory) {
                session.history.undoStack.pop_front();
            }
            session.history.redoStack.clear();
            session.history.lastCommittedJson = std::move(snapshot);
            session.history.hadActiveItemLastFrame = false;
        }

        bool CanUndo(const UiEditorSession& session) {
            return !session.history.undoStack.empty();
        }
        bool CanRedo(const UiEditorSession& session) {
            return !session.history.redoStack.empty();
        }

        void PerformUndo(UiEditorSession& session) {
            if (!CanUndo(session)) {
                return;
            }
            // 当前状态 -> redo 栈；undo 栈顶 -> 当前。
            session.history.redoStack.push_back(SnapshotCurrentScreen(session));
            std::string snapshot = std::move(session.history.undoStack.back());
            session.history.undoStack.pop_back();
            ApplyHistorySnapshot(session, snapshot);
            EngineUi::LogPrint("[UIEditor] Undo (undo stack: {}, redo stack: {})\n",
                session.history.undoStack.size(), session.history.redoStack.size());
        }

        void PerformRedo(UiEditorSession& session) {
            if (!CanRedo(session)) {
                return;
            }
            session.history.undoStack.push_back(SnapshotCurrentScreen(session));
            std::string snapshot = std::move(session.history.redoStack.back());
            session.history.redoStack.pop_back();
            ApplyHistorySnapshot(session, snapshot);
            EngineUi::LogPrint("[UIEditor] Redo (undo stack: {}, redo stack: {})\n",
                session.history.undoStack.size(), session.history.redoStack.size());
        }

        void EnsureScreen(UiEditorSession& session) {
            if (session.screen) {
                if (session.selectedElementId != 0 && session.screen->FindById(session.selectedElementId) == nullptr) {
                    session.selectedElementId = 0;
                }
                SyncAnimationSelection(session);
                if (!session.history.initialized) {
                    ResetHistory(session);
                }
                return;
            }

            session.selectedResolution = 2;
            // 编辑器首次打开且没有加载文件时，自动创建一张默认空 UIScreen。
            session.screen = std::make_unique<UIScreen>("Untitled UI", ResolutionFromIndex(session.selectedResolution));
            session.currentPath.clear();
            session.screen->SetThemePath("Assets/ui/BicycleSim_DarkTheme.ui.theme.json");

            UIElement* canvas = session.screen->GetRootCanvas();
            if (!canvas) {
                return;
            }

            session.selectedElementId = canvas->GetId();
            SyncAnimationSelection(session);
            ResetHistory(session);
        }

        std::string CurrentPathLabel(const UiEditorSession& session) {
            return session.currentPath.empty() ? std::string("Assets/ui/<unsaved>.ui.json") : session.currentPath.string();
        }

        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        std::string MakeAssetRelativePath(const std::filesystem::path& absolutePath) {
            std::error_code errorCode;
            const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical("Assets", errorCode);
            const std::filesystem::path filePath = std::filesystem::weakly_canonical(absolutePath, errorCode);
            if (!errorCode && !assetsRoot.empty() && !filePath.empty()) {
                const std::filesystem::path relativePath = std::filesystem::relative(filePath, assetsRoot, errorCode);
                if (!errorCode) {
                    return (std::filesystem::path("Assets") / relativePath).generic_string();
                }
            }

            return absolutePath.generic_string();
        }

        std::vector<std::string> CollectAssetPaths(
            const std::filesystem::path& rootPath,
            const std::unordered_set<std::string>& extensions) {
            // Inspector 的贴图 / 字体下拉框都从这里扫描 Assets 目录。
            std::vector<std::string> assetPaths;
            if (!std::filesystem::exists(rootPath)) {
                return assetPaths;
            }

            std::error_code errorCode;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath, errorCode)) {
                if (errorCode) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }

                const std::string extension = ToLowerCopy(entry.path().extension().string());
                if (!extensions.contains(extension)) {
                    continue;
                }

                assetPaths.push_back(MakeAssetRelativePath(entry.path()));
            }

            std::sort(assetPaths.begin(), assetPaths.end());
            assetPaths.erase(std::unique(assetPaths.begin(), assetPaths.end()), assetPaths.end());
            return assetPaths;
        }

        std::vector<std::string>& GetTextureAssetPathCache() {
            static std::vector<std::string> texturePaths;
            return texturePaths;
        }

        std::vector<std::string>& GetFontAssetPathCache() {
            static std::vector<std::string> fontPaths;
            return fontPaths;
        }

        std::vector<std::string>& GetThemeAssetPathCache() {
            static std::vector<std::string> themePaths;
            return themePaths;
        }

        bool& AreAssetPathCachesInitialized() {
            static bool initialized = false;
            return initialized;
        }

        void RefreshAssetPathCaches() {
            static const std::unordered_set<std::string> textureExtensions = {
                ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif", ".dds", ".ktx", ".ktx2", ".webp"
            };
            static const std::unordered_set<std::string> fontExtensions = {
                ".ttf", ".otf", ".ttc", ".woff", ".woff2"
            };

            GetTextureAssetPathCache() = CollectAssetPaths("Assets", textureExtensions);
            GetFontAssetPathCache() = CollectAssetPaths(
                std::filesystem::exists("Assets/Fonts") ? std::filesystem::path("Assets/Fonts") : std::filesystem::path("Assets"),
                fontExtensions);

            auto& themePaths = GetThemeAssetPathCache();
            themePaths.clear();
            const std::filesystem::path rootPath = std::filesystem::path("Assets") / "ui";
            if (std::filesystem::exists(rootPath)) {
                std::error_code errorCode;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath, errorCode)) {
                    if (errorCode || !entry.is_regular_file()) {
                        continue;
                    }
                    const std::string filename = ToLowerCopy(entry.path().filename().string());
                    if (filename.ends_with(".ui.theme.json")) {
                        themePaths.push_back(MakeAssetRelativePath(entry.path()));
                    }
                }

                std::sort(themePaths.begin(), themePaths.end());
                themePaths.erase(std::unique(themePaths.begin(), themePaths.end()), themePaths.end());
            }

            AreAssetPathCachesInitialized() = true;
        }

        void EnsureAssetPathCaches() {
            if (!AreAssetPathCachesInitialized()) {
                RefreshAssetPathCaches();
            }
        }

        const std::vector<std::string>& GetTextureAssetPaths() {
            EnsureAssetPathCaches();
            return GetTextureAssetPathCache();
        }

        const std::vector<std::string>& GetFontAssetPaths() {
            EnsureAssetPathCaches();
            return GetFontAssetPathCache();
        }

        const std::vector<std::string>& GetThemeAssetPaths() {
            EnsureAssetPathCaches();
            return GetThemeAssetPathCache();
        }

        std::shared_ptr<const UITheme> LoadScreenTheme(const UIScreen& screen) {
            if (screen.GetThemePath().empty()) {
                return nullptr;
            }
            return LoadUiTheme(screen.GetThemePath());
        }

        void ApplyResolvedPresetStyle(UIElement& element, const UIStylePreset& preset) {
            // 选择样式预设时，把当前有效样式写回本地字段，
            // 这样即使后续渲染后端暂时不做动态主题解析，视觉效果也会立刻更新。
            element.style.backgroundColor = preset.style.backgroundColor;
            element.style.tintColor = preset.style.tintColor;
            element.style.textColor = preset.style.textColor;
            element.style.texturePath = preset.style.texturePath;
            element.style.fontPath = preset.style.fontPath;
            element.style.fontSize = preset.style.fontSize;
            element.style.opacity = preset.style.opacity;
            element.style.padding = preset.style.padding;
            element.style.margin = preset.style.margin;
            element.style.borderColor = preset.style.borderColor;
            element.style.borderWidth = preset.style.borderWidth;
            element.style.borderRadius = preset.style.borderRadius;
            SetAllStyleOverrides(element.style, false);

            if (auto* image = dynamic_cast<UIImage*>(&element)) {
                if (!element.style.texturePath.empty()) {
                    image->imagePath = element.style.texturePath;
                }
            }

            if (auto* button = dynamic_cast<UIButton*>(&element); button && preset.buttonStyle.enabled) {
                button->usePresetTransitionStyle = true;
                button->transitionMode = preset.buttonStyle.transitionMode;
                button->normalColor = preset.buttonStyle.normalColor;
                button->hoverColor = preset.buttonStyle.hoverColor;
                button->pressedColor = preset.buttonStyle.pressedColor;
                button->disabledColor = preset.buttonStyle.disabledColor;
                button->normalScale = preset.buttonStyle.normalScale;
                button->hoverScale = preset.buttonStyle.hoverScale;
                button->pressedScale = preset.buttonStyle.pressedScale;
                button->transitionDuration = preset.buttonStyle.transitionDuration;
                button->runtimeVisualColor = button->normalColor;
                button->runtimeVisualScale = button->normalScale;
                button->runtimeVisualInitialized = false;
            }
        }

        bool DrawAssetPathCombo(
            const char* label,
            std::string& currentPath,
            const std::vector<std::string>& options,
            const char* emptyLabel = "<None>") {
            // 统一的资源路径下拉框，避免 texturePath / fontPath 继续手写字符串。
            bool changed = false;
            const char* previewValue = currentPath.empty() ? emptyLabel : currentPath.c_str();
            if (!ImGui::BeginCombo(label, previewValue)) {
                return false;
            }

            const bool isEmptySelected = currentPath.empty();
            if (ImGui::Selectable(emptyLabel, isEmptySelected)) {
                currentPath.clear();
                changed = true;
            }
            if (isEmptySelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (const std::string& option : options) {
                const bool isSelected = currentPath == option;
                if (ImGui::Selectable(option.c_str(), isSelected)) {
                    currentPath = option;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
            return changed;
        }

        ImTextureID ResolvePreviewTexture(const std::string& assetPath) {
            if (assetPath.empty()) {
                return static_cast<ImTextureID>(0);
            }

            const auto& resolver = GetTexturePreviewResolver();
            if (!resolver) {
                return static_cast<ImTextureID>(0);
            }

            return reinterpret_cast<ImTextureID>(resolver(assetPath));
        }

        ImU32 ToImGuiColor(const glm::vec4& color, float opacity = 1.0f) {
            return IM_COL32(
                static_cast<int>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
                static_cast<int>(std::clamp(color.a * opacity, 0.0f, 1.0f) * 255.0f)
            );
        }

        float ComputeScaledBorderWidth(const UIElement& element, float scale);
        float ComputeScaledBorderRadius(const UIElement& element, float scale);

        ImFont* ResolvePreviewFont(const std::string& fontPath) {
            if (fontPath.empty()) {
                return nullptr;
            }

            static std::unordered_map<std::string, ImFont*> fontCache;
            std::filesystem::path resolvedPath(fontPath);
            if (!resolvedPath.is_absolute()) {
                const std::string normalized = resolvedPath.generic_string();
                if (normalized.starts_with("assets/")) {
                    resolvedPath = std::filesystem::path("Assets") / normalized.substr(7);
                }
                else if (!normalized.starts_with("Assets/")) {
                    resolvedPath = std::filesystem::path(normalized);
                }
            }
            const std::string cacheKey = resolvedPath.generic_string();

            if (const auto iterator = fontCache.find(cacheKey); iterator != fontCache.end()) {
                return iterator->second;
            }

            if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath)) {
                fontCache[cacheKey] = nullptr;
                return nullptr;
            }

            ImGuiIO& io = ImGui::GetIO();
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                cacheKey.c_str(),
                18.0f,
                nullptr,
                io.Fonts->GetGlyphRangesChineseFull());
            if (font) {
                io.Fonts->Build();
            }

            fontCache[cacheKey] = font;
            return font;
        }

        ImVec2 RotatePoint(const ImVec2& point, const ImVec2& pivot, float rotationRadians) {
            const float cosine = std::cos(rotationRadians);
            const float sine = std::sin(rotationRadians);
            const ImVec2 translated(point.x - pivot.x, point.y - pivot.y);
            return ImVec2(
                pivot.x + translated.x * cosine - translated.y * sine,
                pivot.y + translated.x * sine + translated.y * cosine);
        }

        std::array<ImVec2, 4> BuildRotatedQuad(const UIElement& element, const ImVec2& min, const ImVec2& max) {
            const ImVec2 pivot(
                min.x + (max.x - min.x) * element.transform.pivot.x,
                min.y + (max.y - min.y) * element.transform.pivot.y);
            const float rotationRadians = glm::radians(element.transform.rotation);
            return {
                RotatePoint(min, pivot, rotationRadians),
                RotatePoint(ImVec2(max.x, min.y), pivot, rotationRadians),
                RotatePoint(max, pivot, rotationRadians),
                RotatePoint(ImVec2(min.x, max.y), pivot, rotationRadians)
            };
        }

        bool HasVisibleRotation(const UIElement& element) {
            return std::abs(element.transform.rotation) > 0.01f;
        }

        ImVec2 ComputeRotatedQuadMin(const std::array<ImVec2, 4>& quad) {
            ImVec2 result = quad[0];
            for (const ImVec2& point : quad) {
                result.x = std::min(result.x, point.x);
                result.y = std::min(result.y, point.y);
            }
            return result;
        }

        ImVec2 ComputeRotatedQuadMax(const std::array<ImVec2, 4>& quad) {
            ImVec2 result = quad[0];
            for (const ImVec2& point : quad) {
                result.x = std::max(result.x, point.x);
                result.y = std::max(result.y, point.y);
            }
            return result;
        }

        void DrawPreviewBorder(
            ImDrawList* drawList,
            const UIElement& element,
            const ImVec2& min,
            const ImVec2& max,
            float scale,
            ImU32 color) {
            const float scaledBorderWidth = ComputeScaledBorderWidth(element, scale);
            if (scaledBorderWidth <= 0.0f) {
                return;
            }

            const float scaledBorderRadius = ComputeScaledBorderRadius(element, scale);
            if (HasVisibleRotation(element)) {
                const auto quad = BuildRotatedQuad(element, min, max);
                drawList->AddQuad(quad[0], quad[1], quad[2], quad[3], color, scaledBorderWidth);
            }
            else {
                drawList->AddRect(min, max, color, scaledBorderRadius, 0, scaledBorderWidth);
            }
        }

        float NormalizeWidgetValue(float value, float minValue, float maxValue);

        void DrawPreviewText(
            ImDrawList* drawList,
            const std::string& text,
            const ImVec2& min,
            const ImVec2& max,
            ImU32 color,
            float fontSizePixels,
            const std::string& alignment,
            ImFont* font,
            bool wrapText);

        void DrawPreviewSlider(
            ImDrawList* drawList,
            const UIElement& element,
            const UISlider& slider,
            const ImVec2& min,
            const ImVec2& max,
            float scale) {
            const float normalized = NormalizeWidgetValue(slider.value, slider.minValue, slider.maxValue);
            const float radius = std::max(6.0f, (max.y - min.y) * 0.5f);
            const float trackHeight = std::max(6.0f, (max.y - min.y) * 0.28f);
            const float trackY = min.y + (max.y - min.y - trackHeight) * 0.5f;
            const float handleCenterX = min.x + (max.x - min.x) * normalized;
            const float handleCenterY = (min.y + max.y) * 0.5f;

            drawList->AddRectFilled(
                ImVec2(min.x, trackY),
                ImVec2(max.x, trackY + trackHeight),
                ToImGuiColor(element.style.backgroundColor, element.style.opacity),
                trackHeight * 0.5f);
            drawList->AddRectFilled(
                ImVec2(min.x, trackY),
                ImVec2(handleCenterX, trackY + trackHeight),
                ToImGuiColor(slider.fillColor, element.style.opacity),
                trackHeight * 0.5f);
            drawList->AddCircleFilled(
                ImVec2(handleCenterX, handleCenterY),
                radius,
                ToImGuiColor(slider.handleColor, element.style.opacity),
                24);
            DrawPreviewBorder(drawList, element, min, max, scale, ToImGuiColor(element.style.borderColor, element.style.opacity));
        }

        void DrawPreviewToggle(
            ImDrawList* drawList,
            const UIElement& element,
            const UIToggle& toggle,
            const ImVec2& min,
            const ImVec2& max,
            float scale,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            const float switchWidth = std::min(max.x - min.x, (max.y - min.y) * 1.8f);
            const ImVec2 switchMin(min.x, min.y + (max.y - min.y) * 0.15f);
            const ImVec2 switchMax(min.x + switchWidth, max.y - (max.y - min.y) * 0.15f);
            const float knobRadius = std::max(4.0f, (switchMax.y - switchMin.y) * 0.42f);
            const float knobCenterY = (switchMin.y + switchMax.y) * 0.5f;
            const float knobCenterX = toggle.isOn
                ? (switchMax.x - knobRadius - 3.0f)
                : (switchMin.x + knobRadius + 3.0f);

            drawList->AddRectFilled(
                switchMin,
                switchMax,
                ToImGuiColor(toggle.isOn ? toggle.onColor : toggle.offColor, element.style.opacity),
                (switchMax.y - switchMin.y) * 0.5f);
            drawList->AddCircleFilled(ImVec2(knobCenterX, knobCenterY), knobRadius, ToImGuiColor(toggle.knobColor, element.style.opacity), 24);
            DrawPreviewBorder(drawList, element, min, max, scale, ToImGuiColor(element.style.borderColor, element.style.opacity));

            if (!toggle.label.empty()) {
                DrawPreviewText(drawList, toggle.label, ImVec2(switchMax.x + 8.0f, min.y), max, textColor, fontSize, "Left", font, false);
            }
        }

        void DrawPreviewProgressBar(
            ImDrawList* drawList,
            const UIElement& element,
            const UIProgressBar& progressBar,
            const ImVec2& min,
            const ImVec2& max,
            float scale,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            const float normalized = NormalizeWidgetValue(progressBar.value, progressBar.minValue, progressBar.maxValue);
            drawList->AddRectFilled(min, max, ToImGuiColor(element.style.backgroundColor, element.style.opacity), std::max(2.0f, ComputeScaledBorderRadius(element, scale)));
            drawList->AddRectFilled(
                min,
                ImVec2(min.x + (max.x - min.x) * normalized, max.y),
                ToImGuiColor(progressBar.fillColor, element.style.opacity),
                std::max(2.0f, ComputeScaledBorderRadius(element, scale)));
            DrawPreviewBorder(drawList, element, min, max, scale, ToImGuiColor(element.style.borderColor, element.style.opacity));

            if (progressBar.showPercentage) {
                const std::string percentage = std::format("{:.0f}%", normalized * 100.0f);
                DrawPreviewText(drawList, percentage, min, max, textColor, fontSize, "Center", font, false);
            }
        }

        void DrawPreviewInputField(
            ImDrawList* drawList,
            const UIElement& element,
            const UIInputField& inputField,
            const ImVec2& min,
            const ImVec2& max,
            float scale,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            drawList->AddRectFilled(min, max, ToImGuiColor(element.style.backgroundColor, element.style.opacity), ComputeScaledBorderRadius(element, scale));
            DrawPreviewBorder(drawList, element, min, max, scale, ToImGuiColor(element.style.borderColor, element.style.opacity));

            std::string displayText = inputField.text;
            ImU32 displayColor = textColor;
            if (displayText.empty()) {
                displayText = inputField.placeholder.empty() ? std::string("Input") : inputField.placeholder;
                displayColor = IM_COL32(180, 184, 196, 190);
            }
            else if (inputField.password) {
                displayText.assign(inputField.text.size(), '*');
            }

            DrawPreviewText(drawList, displayText, min, max, displayColor, fontSize, "Left", font, false);
        }

        bool DrawPreviewTexture(
            ImDrawList* drawList,
            const UIElement& element,
            const ImVec2& min,
            const ImVec2& max,
            const std::string& texturePath,
            bool useTint,
            float scale) {
            const ImTextureID textureId = ResolvePreviewTexture(texturePath);
            if (textureId == static_cast<ImTextureID>(0)) {
                return false;
            }

            const ImU32 tintColor = useTint
                ? ToImGuiColor(element.style.tintColor, element.style.opacity)
                : IM_COL32(255, 255, 255, static_cast<int>(std::clamp(element.style.opacity, 0.0f, 1.0f) * 255.0f));

            if (HasVisibleRotation(element)) {
                const auto quad = BuildRotatedQuad(element, min, max);
                drawList->AddImageQuad(textureId, quad[0], quad[1], quad[2], quad[3], ImVec2(0.0f, 1.0f), ImVec2(1.0f, 1.0f), ImVec2(1.0f, 0.0f), ImVec2(0.0f, 0.0f), tintColor);
            }
            else {
                drawList->AddImage(textureId, min, max, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tintColor);
            }
            DrawPreviewBorder(drawList, element, min, max, scale, ToImGuiColor(element.style.borderColor, element.style.opacity));
            return true;
        }

        float DegreesToRadians(float degrees) {
            return degrees * 0.01745329251994329577f;
        }

        ImVec2 RotateAroundPoint(const ImVec2& point, const ImVec2& center, float radians) {
            const float sine = std::sin(radians);
            const float cosine = std::cos(radians);
            const float dx = point.x - center.x;
            const float dy = point.y - center.y;
            return ImVec2(
                center.x + dx * cosine - dy * sine,
                center.y + dx * sine + dy * cosine);
        }

        ImVec2 MakeRadialPoint(const ImVec2& center, float radius, float angleRadians) {
            return ImVec2(
                center.x + std::cos(angleRadians) * radius,
                center.y + std::sin(angleRadians) * radius);
        }

        ImVec2 MakeRadialUv(const ImVec2& point, const ImVec2& min, const ImVec2& max) {
            const float width = std::max(1.0f, max.x - min.x);
            const float height = std::max(1.0f, max.y - min.y);
            return ImVec2(
                std::clamp((point.x - min.x) / width, 0.0f, 1.0f),
                std::clamp(1.0f - ((point.y - min.y) / height), 0.0f, 1.0f));
        }

        void DrawSolidRadialArc(
            ImDrawList* drawList,
            const ImVec2& center,
            float innerRadius,
            float outerRadius,
            float startAngle,
            float endAngle,
            ImU32 color) {
            const float thickness = std::max(1.0f, outerRadius - innerRadius);
            const float centerRadius = innerRadius + thickness * 0.5f;
            const int segmentCount = std::clamp(
                static_cast<int>(std::ceil(std::abs(endAngle - startAngle) * std::max(centerRadius, 1.0f) / 10.0f)),
                12,
                180);
            drawList->PathClear();
            for (int segmentIndex = 0; segmentIndex <= segmentCount; ++segmentIndex) {
                const float t = static_cast<float>(segmentIndex) / static_cast<float>(segmentCount);
                const float angle = startAngle + (endAngle - startAngle) * t;
                drawList->PathLineTo(MakeRadialPoint(center, centerRadius, angle));
            }
            drawList->PathStroke(color, false, thickness);
        }

        bool DrawTexturedRadialArc(
            ImDrawList* drawList,
            const UIElement& element,
            const ImVec2& min,
            const ImVec2& max,
            const std::string& texturePath,
            float innerRadius,
            float outerRadius,
            float startAngle,
            float endAngle,
            ImU32 tintColor) {
            const ImTextureID textureId = ResolvePreviewTexture(texturePath);
            if (textureId == static_cast<ImTextureID>(0)) {
                return false;
            }

            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const float rotationRadians = DegreesToRadians(element.transform.rotation);
            const int segmentCount = std::clamp(
                static_cast<int>(std::ceil(std::abs(endAngle - startAngle) * std::max(outerRadius, 1.0f) / 10.0f)),
                12,
                180);
            const float step = (endAngle - startAngle) / static_cast<float>(segmentCount);

            for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const float angleA = startAngle + step * static_cast<float>(segmentIndex);
                const float angleB = startAngle + step * static_cast<float>(segmentIndex + 1);

                const ImVec2 outerA = MakeRadialPoint(center, outerRadius, angleA);
                const ImVec2 outerB = MakeRadialPoint(center, outerRadius, angleB);
                const ImVec2 innerA = MakeRadialPoint(center, innerRadius, angleA);
                const ImVec2 innerB = MakeRadialPoint(center, innerRadius, angleB);

                const ImVec2 uvOuterA = MakeRadialUv(outerA, min, max);
                const ImVec2 uvOuterB = MakeRadialUv(outerB, min, max);
                const ImVec2 uvInnerA = MakeRadialUv(innerA, min, max);
                const ImVec2 uvInnerB = MakeRadialUv(innerB, min, max);

                const ImVec2 drawOuterA = HasVisibleRotation(element) ? RotateAroundPoint(outerA, center, rotationRadians) : outerA;
                const ImVec2 drawOuterB = HasVisibleRotation(element) ? RotateAroundPoint(outerB, center, rotationRadians) : outerB;
                const ImVec2 drawInnerA = HasVisibleRotation(element) ? RotateAroundPoint(innerA, center, rotationRadians) : innerA;
                const ImVec2 drawInnerB = HasVisibleRotation(element) ? RotateAroundPoint(innerB, center, rotationRadians) : innerB;

                drawList->AddImageQuad(
                    textureId,
                    drawOuterA,
                    drawOuterB,
                    drawInnerB,
                    drawInnerA,
                    uvOuterA,
                    uvOuterB,
                    uvInnerB,
                    uvInnerA,
                    tintColor);
            }

            return true;
        }

        void DrawPreviewRadialProgressBar(
            ImDrawList* drawList,
            const UIElement& element,
            const UIRadialProgressBar& radialProgressBar,
            const ImVec2& min,
            const ImVec2& max,
            float fontSize,
            ImFont* font,
            ImU32 textColor) {
            const float normalized = NormalizeWidgetValue(radialProgressBar.value, radialProgressBar.minValue, radialProgressBar.maxValue);
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const float maxOuterRadius = std::max(2.0f, std::min(max.x - min.x, max.y - min.y) * 0.5f - 1.0f);
            const float outerRadius = maxOuterRadius * std::clamp(radialProgressBar.outerRadiusRatio, 0.05f, 1.0f);
            const float innerRadius = outerRadius * std::clamp(radialProgressBar.innerRadiusRatio, 0.05f, 0.98f);
            const float fullSweepRadians = DegreesToRadians(std::max(0.0f, radialProgressBar.sweepAngleDegrees));
            const float startAngle = DegreesToRadians(radialProgressBar.startAngleDegrees);
            const float direction = radialProgressBar.clockwise ? 1.0f : -1.0f;
            const float fullEndAngle = startAngle + direction * fullSweepRadians;
            const float fillEndAngle = startAngle + direction * fullSweepRadians * normalized;

            const ImU32 backgroundTint = radialProgressBar.tintBackgroundImage
                ? ToImGuiColor(radialProgressBar.backgroundFillColor, radialProgressBar.style.opacity)
                : IM_COL32(255, 255, 255, static_cast<int>(std::clamp(radialProgressBar.style.opacity, 0.0f, 1.0f) * 255.0f));
            if (!radialProgressBar.backgroundImagePath.empty()) {
                DrawTexturedRadialArc(
                    drawList,
                    element,
                    min,
                    max,
                    radialProgressBar.backgroundImagePath,
                    innerRadius,
                    outerRadius,
                    startAngle,
                    fullEndAngle,
                    backgroundTint);
            }
            else {
                DrawSolidRadialArc(
                    drawList,
                    center,
                    innerRadius,
                    outerRadius,
                    startAngle,
                    fullEndAngle,
                    ToImGuiColor(radialProgressBar.backgroundFillColor, radialProgressBar.style.opacity));
            }

            if (normalized > 0.0f) {
                if (!radialProgressBar.fillImagePath.empty()) {
                    const ImU32 fillTint = radialProgressBar.tintFillImage
                        ? ToImGuiColor(radialProgressBar.fillColor, radialProgressBar.style.opacity)
                        : IM_COL32(255, 255, 255, static_cast<int>(std::clamp(radialProgressBar.style.opacity, 0.0f, 1.0f) * 255.0f));
                    DrawTexturedRadialArc(
                        drawList,
                        element,
                        min,
                        max,
                        radialProgressBar.fillImagePath,
                        innerRadius,
                        outerRadius,
                        startAngle,
                        fillEndAngle,
                        fillTint);
                }
                else {
                    DrawSolidRadialArc(
                        drawList,
                        center,
                        innerRadius,
                        outerRadius,
                        startAngle,
                        fillEndAngle,
                        ToImGuiColor(radialProgressBar.fillColor, radialProgressBar.style.opacity));
                }
            }

            if (radialProgressBar.showPercentage) {
                const std::string percentage = std::format("{:.0f}%", normalized * 100.0f);
                DrawPreviewText(drawList, percentage, min, max, textColor, fontSize, "Center", font, false);
            }
        }

        int CountDescendants(const UIElement& element) {
            int count = 1;
            for (const auto& child : element.GetChildren()) {
                count += CountDescendants(*child);
            }
            return count;
        }

        std::string MakeElementLabel(UIElementType type) {
            switch (type) {
            case UIElementType::Canvas: return "Canvas";
            case UIElementType::Panel: return "Panel";
            case UIElementType::Image: return "Image";
            case UIElementType::Text: return "Text";
            case UIElementType::Button: return "Button";
            case UIElementType::Slider: return "Slider";
            case UIElementType::Toggle: return "Toggle";
            case UIElementType::ProgressBar: return "ProgressBar";
            case UIElementType::RadialProgressBar: return "RadialProgressBar";
            case UIElementType::ScrollView: return "ScrollView";
            case UIElementType::InputField: return "InputField";
            case UIElementType::HorizontalLayout: return "HorizontalLayout";
            case UIElementType::VerticalLayout: return "VerticalLayout";
            case UIElementType::GridLayout: return "GridLayout";
            }

            return "Element";
        }

        std::vector<const UIElement*> GetChildrenSortedForDraw(const UIElement& element) {
            std::vector<const UIElement*> children;
            children.reserve(element.GetChildren().size());
            for (const auto& child : element.GetChildren()) {
                children.push_back(child.get());
            }

            std::stable_sort(children.begin(), children.end(), [](const UIElement* lhs, const UIElement* rhs) {
                return lhs->zOrder < rhs->zOrder;
            });
            return children;
        }

        std::vector<UIElement*> GetChildrenSortedForDrawMutable(UIElement& element) {
            std::vector<UIElement*> children;
            children.reserve(element.GetChildren().size());
            for (auto& child : element.GetChildren()) {
                children.push_back(child.get());
            }

            std::stable_sort(children.begin(), children.end(), [](const UIElement* lhs, const UIElement* rhs) {
                return lhs->zOrder < rhs->zOrder;
            });
            return children;
        }

        enum class LayerMoveDirection : std::uint8_t {
            Backward = 0,
            Forward,
            ToBack,
            ToFront
        };

        bool ReorderElementLayer(UIElement& element, LayerMoveDirection direction) {
            UIElement* parent = element.GetParent();
            if (parent == nullptr) {
                return false;
            }

            auto orderedChildren = GetChildrenSortedForDrawMutable(*parent);
            auto iterator = std::find(orderedChildren.begin(), orderedChildren.end(), &element);
            if (iterator == orderedChildren.end()) {
                return false;
            }

            const std::size_t index = static_cast<std::size_t>(std::distance(orderedChildren.begin(), iterator));
            switch (direction) {
            case LayerMoveDirection::Backward:
                if (index == 0) {
                    return false;
                }
                std::iter_swap(orderedChildren.begin() + static_cast<std::ptrdiff_t>(index),
                    orderedChildren.begin() + static_cast<std::ptrdiff_t>(index - 1));
                break;
            case LayerMoveDirection::Forward:
                if (index + 1 >= orderedChildren.size()) {
                    return false;
                }
                std::iter_swap(orderedChildren.begin() + static_cast<std::ptrdiff_t>(index),
                    orderedChildren.begin() + static_cast<std::ptrdiff_t>(index + 1));
                break;
            case LayerMoveDirection::ToBack:
                if (index == 0) {
                    return false;
                }
                std::rotate(orderedChildren.begin(),
                    orderedChildren.begin() + static_cast<std::ptrdiff_t>(index),
                    orderedChildren.begin() + static_cast<std::ptrdiff_t>(index + 1));
                break;
            case LayerMoveDirection::ToFront:
                if (index + 1 >= orderedChildren.size()) {
                    return false;
                }
                std::rotate(orderedChildren.begin() + static_cast<std::ptrdiff_t>(index),
                    orderedChildren.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    orderedChildren.end());
                break;
            }

            for (std::size_t childIndex = 0; childIndex < orderedChildren.size(); ++childIndex) {
                orderedChildren[childIndex]->zOrder = static_cast<int>(childIndex);
            }

            return true;
        }

        UIElement* GetSelectedElement(UiEditorSession& session) {
            if (!session.screen || session.selectedElementId == 0) {
                return nullptr;
            }

            return session.screen->FindById(session.selectedElementId);
        }

        const std::array<const char*, 8>& GetAnimationPropertyLabels() {
            static const std::array<const char*, 8> labels = {
                "Position",
                "Size",
                "Scale",
                "Rotation",
                "Opacity",
                "BackgroundColor",
                "TintColor",
                "TextColor"
            };
            return labels;
        }

        const std::array<UIAnimationProperty, 8>& GetAnimationPropertyValues() {
            static const std::array<UIAnimationProperty, 8> values = {
                UIAnimationProperty::Position,
                UIAnimationProperty::Size,
                UIAnimationProperty::Scale,
                UIAnimationProperty::Rotation,
                UIAnimationProperty::Opacity,
                UIAnimationProperty::BackgroundColor,
                UIAnimationProperty::TintColor,
                UIAnimationProperty::TextColor
            };
            return values;
        }

        const std::array<const char*, 5>& GetAnimationEasingLabels() {
            static const std::array<const char*, 5> labels = {
                "Linear",
                "EaseIn",
                "EaseOut",
                "EaseInOut",
                "EaseOutStrong"
            };
            return labels;
        }

        const std::array<UIAnimationEasing, 5>& GetAnimationEasingValues() {
            static const std::array<UIAnimationEasing, 5> values = {
                UIAnimationEasing::Linear,
                UIAnimationEasing::EaseIn,
                UIAnimationEasing::EaseOut,
                UIAnimationEasing::EaseInOut,
                UIAnimationEasing::EaseOutStrong
            };
            return values;
        }

        const std::array<const char*, 7>& GetAnimationPresetLabels() {
            static const std::array<const char*, 7> labels = {
                "Fade In",
                "Fade Out",
                "Slide In Left",
                "Slide In Right",
                "Pop",
                "Pulse",
                "Shake"
            };
            return labels;
        }

        UIAnimationClip* GetSelectedAnimationClip(UiEditorSession& session) {
            if (!session.screen) {
                return nullptr;
            }

            auto& clips = session.screen->GetAnimationClips();
            if (session.selectedAnimationClipIndex < 0 ||
                session.selectedAnimationClipIndex >= static_cast<int>(clips.size())) {
                return nullptr;
            }

            return &clips[static_cast<std::size_t>(session.selectedAnimationClipIndex)];
        }

        UIAnimationTrack* GetSelectedAnimationTrack(UiEditorSession& session) {
            UIAnimationClip* clip = GetSelectedAnimationClip(session);
            if (!clip) {
                return nullptr;
            }

            if (session.selectedAnimationTrackIndex < 0 ||
                session.selectedAnimationTrackIndex >= static_cast<int>(clip->tracks.size())) {
                return nullptr;
            }

            return &clip->tracks[static_cast<std::size_t>(session.selectedAnimationTrackIndex)];
        }

        std::uint64_t MakeAnimationTrackKey(const UIAnimationTrack& track) {
            return (static_cast<std::uint64_t>(track.targetElementId) << 32) |
                static_cast<std::uint64_t>(track.property);
        }

        bool IsAnimationKeySelected(const UiEditorSession& session, int trackIndex, int keyframeIndex) {
            return std::any_of(session.selectedAnimationKeys.begin(), session.selectedAnimationKeys.end(), [&](const AnimationKeySelection& selection) {
                return selection.trackIndex == trackIndex && selection.keyframeIndex == keyframeIndex;
            });
        }

        void SetPrimaryAnimationKeySelection(UiEditorSession& session, int trackIndex, int keyframeIndex, bool additive = false) {
            if (!additive) {
                session.selectedAnimationKeys.clear();
            }
            if (!IsAnimationKeySelected(session, trackIndex, keyframeIndex)) {
                session.selectedAnimationKeys.push_back({ trackIndex, keyframeIndex });
            }
            session.selectedAnimationTrackIndex = trackIndex;
            session.selectedAnimationKeyframeIndex = keyframeIndex;
        }

        void DeleteSelectedAnimationKeys(UiEditorSession& session, UIAnimationClip& clip) {
            if (session.selectedAnimationKeys.empty()) {
                return;
            }

            StopAnimationPreview(session, true);

            std::vector<AnimationKeySelection> selections = session.selectedAnimationKeys;
            std::sort(selections.begin(), selections.end(), [](const AnimationKeySelection& lhs, const AnimationKeySelection& rhs) {
                if (lhs.trackIndex != rhs.trackIndex) {
                    return lhs.trackIndex > rhs.trackIndex;
                }
                return lhs.keyframeIndex > rhs.keyframeIndex;
            });

            for (const AnimationKeySelection& selection : selections) {
                if (selection.trackIndex < 0 ||
                    selection.trackIndex >= static_cast<int>(clip.tracks.size())) {
                    continue;
                }

                UIAnimationTrack& track = clip.tracks[static_cast<std::size_t>(selection.trackIndex)];
                if (selection.keyframeIndex < 0 ||
                    selection.keyframeIndex >= static_cast<int>(track.keyframes.size())) {
                    continue;
                }

                track.keyframes.erase(track.keyframes.begin() + selection.keyframeIndex);
            }

            session.selectedAnimationKeys.clear();
            session.selectedAnimationKeyframeIndex = -1;
            SyncAnimationSelection(session);
            ScrubAnimationPreview(session, session.animationPreviewTime);
        }

        UIKeyframe* GetSelectedAnimationKeyframe(UiEditorSession& session) {
            UIAnimationTrack* track = GetSelectedAnimationTrack(session);
            if (!track) {
                return nullptr;
            }

            if (session.selectedAnimationKeyframeIndex < 0 ||
                session.selectedAnimationKeyframeIndex >= static_cast<int>(track->keyframes.size())) {
                return nullptr;
            }

            return &track->keyframes[static_cast<std::size_t>(session.selectedAnimationKeyframeIndex)];
        }

        UIValue CaptureAnimatedPropertyValue(const UIElement& element, UIAnimationProperty property) {
            switch (property) {
            case UIAnimationProperty::Position:
                return UIValue(element.transform.position);
            case UIAnimationProperty::Size:
                return UIValue(element.transform.size);
            case UIAnimationProperty::Scale:
                return UIValue(element.transform.scale);
            case UIAnimationProperty::Rotation:
                return UIValue(element.transform.rotation);
            case UIAnimationProperty::Opacity:
                return UIValue(element.style.opacity);
            case UIAnimationProperty::BackgroundColor:
                return UIValue(element.style.backgroundColor);
            case UIAnimationProperty::TintColor:
                return UIValue(element.style.tintColor);
            case UIAnimationProperty::TextColor:
                return UIValue(element.style.textColor);
            }

            return UIValue();
        }

        void ApplyAnimatedPropertyValue(UIElement& element, UIAnimationProperty property, const UIValue& value) {
            switch (property) {
            case UIAnimationProperty::Position:
                if (const auto* vec2Value = std::get_if<glm::vec2>(&value.data)) {
                    element.transform.position = *vec2Value;
                }
                break;
            case UIAnimationProperty::Size:
                if (const auto* vec2Value = std::get_if<glm::vec2>(&value.data)) {
                    element.transform.size = *vec2Value;
                }
                break;
            case UIAnimationProperty::Scale:
                if (const auto* vec2Value = std::get_if<glm::vec2>(&value.data)) {
                    element.transform.scale = *vec2Value;
                }
                break;
            case UIAnimationProperty::Rotation:
                if (const auto* floatValue = std::get_if<float>(&value.data)) {
                    element.transform.rotation = *floatValue;
                }
                break;
            case UIAnimationProperty::Opacity:
                if (const auto* floatValue = std::get_if<float>(&value.data)) {
                    element.style.opacity = *floatValue;
                }
                break;
            case UIAnimationProperty::BackgroundColor:
                if (const auto* colorValue = std::get_if<glm::vec4>(&value.data)) {
                    element.style.backgroundColor = *colorValue;
                }
                break;
            case UIAnimationProperty::TintColor:
                if (const auto* colorValue = std::get_if<glm::vec4>(&value.data)) {
                    element.style.tintColor = *colorValue;
                }
                break;
            case UIAnimationProperty::TextColor:
                if (const auto* colorValue = std::get_if<glm::vec4>(&value.data)) {
                    element.style.textColor = *colorValue;
                }
                break;
            }
        }

        void RestoreAnimationPreviewSnapshot(UiEditorSession& session) {
            if (!session.screen) {
                return;
            }

            for (const AnimatedPropertySnapshot& snapshot : session.animationPreviewSnapshots) {
                if (UIElement* element = session.screen->FindById(snapshot.elementId)) {
                    ApplyAnimatedPropertyValue(*element, snapshot.property, snapshot.value);
                }
            }
        }

        void CaptureAnimationPreviewSnapshot(UiEditorSession& session, const UIAnimationClip& clip) {
            session.animationPreviewSnapshots.clear();
            if (!session.screen) {
                return;
            }

            std::unordered_set<std::uint64_t> seenKeys;
            for (const UIAnimationTrack& track : clip.tracks) {
                const std::uint64_t compositeKey =
                    (static_cast<std::uint64_t>(track.targetElementId) << 32) |
                    static_cast<std::uint64_t>(track.property);
                if (!seenKeys.insert(compositeKey).second) {
                    continue;
                }

                if (UIElement* element = session.screen->FindById(track.targetElementId)) {
                    session.animationPreviewSnapshots.push_back({
                        track.targetElementId,
                        track.property,
                        CaptureAnimatedPropertyValue(*element, track.property)
                        });
                }
            }
        }

        void SortTrackKeyframes(UIAnimationTrack& track) {
            std::sort(track.keyframes.begin(), track.keyframes.end(), [](const UIKeyframe& lhs, const UIKeyframe& rhs) {
                return lhs.time < rhs.time;
            });
        }

        void SyncAnimationSelection(UiEditorSession& session) {
            if (!session.screen) {
                session.selectedAnimationClipIndex = -1;
                session.selectedAnimationTrackIndex = -1;
                session.selectedAnimationKeyframeIndex = -1;
                session.animationPreviewTime = 0.0f;
                return;
            }

            auto& clips = session.screen->GetAnimationClips();
            if (clips.empty()) {
                session.selectedAnimationClipIndex = -1;
                session.selectedAnimationTrackIndex = -1;
                session.selectedAnimationKeyframeIndex = -1;
                session.animationPreviewTime = 0.0f;
                return;
            }

            session.selectedAnimationClipIndex = std::clamp(
                session.selectedAnimationClipIndex,
                0,
                static_cast<int>(clips.size()) - 1);

            UIAnimationClip& clip = clips[static_cast<std::size_t>(session.selectedAnimationClipIndex)];
            session.animationPreviewTime = std::clamp(session.animationPreviewTime, 0.0f, std::max(0.0f, clip.duration));

            if (clip.tracks.empty()) {
                session.selectedAnimationTrackIndex = -1;
                session.selectedAnimationKeyframeIndex = -1;
                return;
            }

            session.selectedAnimationTrackIndex = std::clamp(
                session.selectedAnimationTrackIndex,
                0,
                static_cast<int>(clip.tracks.size()) - 1);

            UIAnimationTrack& track = clip.tracks[static_cast<std::size_t>(session.selectedAnimationTrackIndex)];
            if (track.keyframes.empty()) {
                session.selectedAnimationKeyframeIndex = -1;
                session.selectedAnimationKeys.clear();
                return;
            }

            session.selectedAnimationKeyframeIndex = std::clamp(
                session.selectedAnimationKeyframeIndex,
                0,
                static_cast<int>(track.keyframes.size()) - 1);

            session.selectedAnimationKeys.erase(
                std::remove_if(
                    session.selectedAnimationKeys.begin(),
                    session.selectedAnimationKeys.end(),
                    [&](const AnimationKeySelection& selection) {
                        return selection.trackIndex < 0 ||
                            selection.trackIndex >= static_cast<int>(clip.tracks.size()) ||
                            selection.keyframeIndex < 0 ||
                            selection.keyframeIndex >= static_cast<int>(clip.tracks[static_cast<std::size_t>(selection.trackIndex)].keyframes.size());
                    }),
                session.selectedAnimationKeys.end());

            if (session.selectedAnimationKeyframeIndex >= 0 &&
                !IsAnimationKeySelected(session, session.selectedAnimationTrackIndex, session.selectedAnimationKeyframeIndex)) {
                session.selectedAnimationKeys.clear();
                session.selectedAnimationKeys.push_back({ session.selectedAnimationTrackIndex, session.selectedAnimationKeyframeIndex });
            }
        }

        std::string MakeUniqueAnimationClipName(const UIScreen& screen, std::string_view baseName) {
            const auto& clips = screen.GetAnimationClips();
            int suffix = 1;
            while (true) {
                const std::string candidate = std::format("{}_{:02d}", baseName, suffix);
                const auto duplicate = std::find_if(clips.begin(), clips.end(), [&](const UIAnimationClip& clip) {
                    return clip.name == candidate;
                });
                if (duplicate == clips.end()) {
                    return candidate;
                }
                ++suffix;
            }
        }

        UIAnimationClip CreateAnimationPresetClip(const UIElement& element, AnimationPreset preset) {
            UIAnimationClip clip{};
            clip.duration = 0.6f;
            clip.loopCount = 1;
            clip.name = std::format("{}_{}", element.GetName(), GetAnimationPresetLabels()[static_cast<int>(preset)]);

            auto addTrack = [&](UIAnimationProperty property, std::initializer_list<UIKeyframe> keyframes) {
                UIAnimationTrack track{};
                track.targetElementId = element.GetId();
                track.property = property;
                track.keyframes.assign(keyframes.begin(), keyframes.end());
                SortTrackKeyframes(track);
                clip.tracks.push_back(std::move(track));
            };

            switch (preset) {
            case AnimationPreset::FadeIn:
                clip.duration = 0.45f;
                clip.playOnShow = true;
                addTrack(UIAnimationProperty::Opacity, {
                    UIKeyframe{ 0.0f, UIValue(0.0f), UIAnimationEasing::EaseOut },
                    UIKeyframe{ clip.duration, UIValue(element.style.opacity), UIAnimationEasing::EaseOut }
                    });
                break;
            case AnimationPreset::FadeOut:
                clip.duration = 0.45f;
                clip.playOnShow = true;
                addTrack(UIAnimationProperty::Opacity, {
                    UIKeyframe{ 0.0f, UIValue(element.style.opacity), UIAnimationEasing::Linear },
                    UIKeyframe{ clip.duration, UIValue(0.0f), UIAnimationEasing::EaseIn }
                    });
                break;
            case AnimationPreset::SlideInLeft:
                clip.duration = 0.5f;
                clip.playOnShow = true;
                addTrack(UIAnimationProperty::Position, {
                    UIKeyframe{ 0.0f, UIValue(element.transform.position + glm::vec2(-220.0f, 0.0f)), UIAnimationEasing::EaseOutStrong },
                    UIKeyframe{ clip.duration, UIValue(element.transform.position), UIAnimationEasing::EaseOutStrong }
                    });
                break;
            case AnimationPreset::SlideInRight:
                clip.duration = 0.5f;
                clip.playOnShow = true;
                addTrack(UIAnimationProperty::Position, {
                    UIKeyframe{ 0.0f, UIValue(element.transform.position + glm::vec2(220.0f, 0.0f)), UIAnimationEasing::EaseOutStrong },
                    UIKeyframe{ clip.duration, UIValue(element.transform.position), UIAnimationEasing::EaseOutStrong }
                    });
                break;
            case AnimationPreset::Pop:
                clip.duration = 0.4f;
                clip.playOnShow = true;
                addTrack(UIAnimationProperty::Scale, {
                    UIKeyframe{ 0.0f, UIValue(glm::vec2(0.82f)), UIAnimationEasing::EaseOut },
                    UIKeyframe{ 0.22f, UIValue(glm::vec2(1.08f)), UIAnimationEasing::EaseOut },
                    UIKeyframe{ clip.duration, UIValue(element.transform.scale), UIAnimationEasing::EaseInOut }
                    });
                break;
            case AnimationPreset::Pulse:
                clip.duration = 0.9f;
                clip.loopCount = 0;
                addTrack(UIAnimationProperty::Scale, {
                    UIKeyframe{ 0.0f, UIValue(element.transform.scale), UIAnimationEasing::EaseInOut },
                    UIKeyframe{ clip.duration * 0.5f, UIValue(element.transform.scale * 1.08f), UIAnimationEasing::EaseInOut },
                    UIKeyframe{ clip.duration, UIValue(element.transform.scale), UIAnimationEasing::EaseInOut }
                    });
                break;
            case AnimationPreset::Shake:
                clip.duration = 0.45f;
                addTrack(UIAnimationProperty::Position, {
                    UIKeyframe{ 0.0f, UIValue(element.transform.position), UIAnimationEasing::Linear },
                    UIKeyframe{ 0.08f, UIValue(element.transform.position + glm::vec2(-18.0f, 0.0f)), UIAnimationEasing::EaseOut },
                    UIKeyframe{ 0.16f, UIValue(element.transform.position + glm::vec2(18.0f, 0.0f)), UIAnimationEasing::EaseOut },
                    UIKeyframe{ 0.24f, UIValue(element.transform.position + glm::vec2(-12.0f, 0.0f)), UIAnimationEasing::EaseOut },
                    UIKeyframe{ 0.32f, UIValue(element.transform.position + glm::vec2(12.0f, 0.0f)), UIAnimationEasing::EaseOut },
                    UIKeyframe{ clip.duration, UIValue(element.transform.position), UIAnimationEasing::EaseInOut }
                    });
                break;
            }

            return clip;
        }

        bool IsRootCanvas(const UiEditorSession& session, const UIElement& element) {
            const UIElement* root = session.screen ? session.screen->GetRootCanvas() : nullptr;
            return root && root->GetId() == element.GetId();
        }

        int FindNextNameIndex(const UIElement& root, const std::string& baseName) {
            const std::string prefix = baseName + "_";
            int highestIndex = 0;

            root.Traverse([&](const UIElement& element) {
                if (!element.GetName().starts_with(prefix)) {
                    return;
                }

                const std::string suffix = element.GetName().substr(prefix.size());
                if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                    return;
                }

                try {
                    highestIndex = std::max(highestIndex, std::stoi(suffix));
                }
                catch (...) {
                }
            });

            return highestIndex + 1;
        }

        std::string MakeReadableName(const UIScreen& screen, UIElementType type) {
            const UIElement* canvas = screen.GetRootCanvas();
            const std::string baseName = MakeElementLabel(type);
            const int nextIndex = canvas ? FindNextNameIndex(*canvas, baseName) : 1;
            return std::format("{}_{:03d}", baseName, nextIndex);
        }

        glm::vec2 DefaultSizeForType(UIElementType type) {
            switch (type) {
            case UIElementType::Canvas: return glm::vec2(1920.0f, 1080.0f);
            case UIElementType::Panel: return glm::vec2(320.0f, 180.0f);
            case UIElementType::Image: return glm::vec2(256.0f, 256.0f);
            case UIElementType::Text: return glm::vec2(280.0f, 48.0f);
            case UIElementType::Button: return glm::vec2(220.0f, 64.0f);
            case UIElementType::Slider: return glm::vec2(260.0f, 36.0f);
            case UIElementType::Toggle: return glm::vec2(140.0f, 40.0f);
            case UIElementType::ProgressBar: return glm::vec2(280.0f, 32.0f);
            case UIElementType::RadialProgressBar: return glm::vec2(256.0f, 256.0f);
            case UIElementType::ScrollView: return glm::vec2(320.0f, 220.0f);
            case UIElementType::InputField: return glm::vec2(260.0f, 44.0f);
            case UIElementType::HorizontalLayout: return glm::vec2(360.0f, 96.0f);
            case UIElementType::VerticalLayout: return glm::vec2(220.0f, 240.0f);
            case UIElementType::GridLayout: return glm::vec2(320.0f, 240.0f);
            }

            return glm::vec2(200.0f, 60.0f);
        }

        glm::vec2 DefaultPositionForParent(const UIElement* parent) {
            const std::size_t childCount = parent ? parent->GetChildren().size() : 0;
            const float offset = 24.0f * static_cast<float>(childCount);
            return glm::vec2(40.0f + offset, 40.0f + offset);
        }

        void ApplyDefaultStyle(UIElement& element) {
            element.style.opacity = 1.0f;
            element.style.fontSize = 16.0f;
            element.style.tintColor = glm::vec4(1.0f);
            element.style.textColor = glm::vec4(0.95f, 0.95f, 0.98f, 1.0f);
            element.style.backgroundColor = glm::vec4(0.14f, 0.16f, 0.22f, 0.92f);
            element.style.borderColor = glm::vec4(0.35f, 0.40f, 0.52f, 1.0f);
            element.style.borderWidth = 1.0f;
            element.style.borderRadius = 8.0f;

            switch (element.GetType()) {
            case UIElementType::Image:
                element.style.backgroundColor = glm::vec4(0.18f, 0.20f, 0.26f, 1.0f);
                break;
            case UIElementType::Text:
                element.style.backgroundColor = glm::vec4(0.0f);
                element.style.textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                element.style.borderWidth = 0.0f;
                break;
            case UIElementType::Button:
                element.style.backgroundColor = glm::vec4(0.24f, 0.31f, 0.55f, 1.0f);
                element.style.borderColor = glm::vec4(0.65f, 0.74f, 0.96f, 1.0f);
                break;
            case UIElementType::Slider:
            case UIElementType::ProgressBar:
            case UIElementType::RadialProgressBar:
                element.style.backgroundColor = glm::vec4(0.18f, 0.22f, 0.30f, 1.0f);
                break;
            case UIElementType::Toggle:
                element.style.backgroundColor = glm::vec4(0.18f, 0.28f, 0.22f, 1.0f);
                break;
            case UIElementType::ScrollView:
            case UIElementType::InputField:
                element.style.backgroundColor = glm::vec4(0.12f, 0.14f, 0.18f, 1.0f);
                break;
            default:
                break;
            }
        }

        void InitializeNewElement(UIElement& element, UIElementType type, const UIElement* parent) {
            element.transform.position = DefaultPositionForParent(parent);
            element.transform.size = DefaultSizeForType(type);
            element.transform.anchorMin = glm::vec2(0.0f);
            element.transform.anchorMax = glm::vec2(0.0f);
            element.transform.pivot = glm::vec2(0.0f, 0.0f);
            element.transform.rotation = 0.0f;
            element.transform.scale = glm::vec2(1.0f);
            ApplyDefaultStyle(element);

            if (auto* text = dynamic_cast<UIText*>(&element)) {
                text->text = "New Text";
                text->alignment = "Left";
                text->wrapText = true;
            }
            else if (auto* button = dynamic_cast<UIButton*>(&element)) {
                button->label = "Button";
                button->normalColor = element.style.backgroundColor;
                button->hoverColor = glm::vec4(0.32f, 0.40f, 0.66f, 1.0f);
                button->pressedColor = glm::vec4(0.18f, 0.24f, 0.42f, 1.0f);
                button->disabledColor = glm::vec4(0.25f, 0.25f, 0.28f, 0.8f);
                button->transitionMode = UIButtonTransitionMode::Animation;
                button->normalScale = 1.0f;
                button->hoverScale = 1.04f;
                button->pressedScale = 0.96f;
                button->transitionDuration = 0.12f;
                button->runtimeVisualColor = button->normalColor;
                button->runtimeVisualScale = button->normalScale;
                button->runtimeVisualInitialized = false;
                element.events.onClick = "UI.Button.Click";
            }
            else if (auto* slider = dynamic_cast<UISlider*>(&element)) {
                slider->minValue = 0.0f;
                slider->maxValue = 1.0f;
                slider->value = 0.5f;
                slider->wholeNumbers = false;
            }
            else if (auto* toggle = dynamic_cast<UIToggle*>(&element)) {
                toggle->label = "Toggle";
                toggle->isOn = true;
            }
            else if (auto* radialProgressBar = dynamic_cast<UIRadialProgressBar*>(&element)) {
                radialProgressBar->minValue = 0.0f;
                radialProgressBar->maxValue = 1.0f;
                radialProgressBar->value = 0.65f;
                radialProgressBar->showPercentage = false;
                radialProgressBar->startAngleDegrees = 135.0f;
                radialProgressBar->sweepAngleDegrees = 270.0f;
                radialProgressBar->outerRadiusRatio = 1.0f;
                radialProgressBar->innerRadiusRatio = 0.72f;
                radialProgressBar->clockwise = true;
                radialProgressBar->tintBackgroundImage = false;
                radialProgressBar->tintFillImage = false;
                radialProgressBar->backgroundFillColor = element.style.backgroundColor;
            }
            else if (auto* progressBar = dynamic_cast<UIProgressBar*>(&element)) {
                progressBar->minValue = 0.0f;
                progressBar->maxValue = 1.0f;
                progressBar->value = 0.65f;
                progressBar->showPercentage = true;
            }
            else if (auto* inputField = dynamic_cast<UIInputField*>(&element)) {
                inputField->text.clear();
                inputField->placeholder = "Enter text...";
                inputField->readOnly = false;
                inputField->password = false;
            }
            else if (auto* image = dynamic_cast<UIImage*>(&element)) {
                image->imagePath = "Assets/textures/ui/placeholder.png";
                image->preserveAspectRatio = true;
                element.style.texturePath = image->imagePath;
            }
        }

        bool DrawVec2Editor(const char* label, glm::vec2& value) {
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);

            float values[2] = { value.x, value.y };
            bool changed = false;
            ImGui::SetNextItemWidth(-1.0f);
			//changed |= ImGui::InputFloat2("##Input", values, "%.3f");//输入框
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::DragFloat2("##Drag", values, 1.0f, 0.0f, 0.0f, "%.3f");//拖拽框（可输入）

            if (changed) {
                value = glm::vec2(values[0], values[1]);
            }

            ImGui::PopID();
            return changed;
        }

        bool DrawNormalizedVec2Editor(const char* label, glm::vec2& value) {
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);

            float values[2] = { value.x, value.y };
            bool changed = false;
            ImGui::SetNextItemWidth(-1.0f);
            //changed |= ImGui::InputFloat2("##Input", values, "%.3f");
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::SliderFloat2("##Slider", values, 0.0f, 1.0f, "%.3f");

            if (changed) {
                value = glm::clamp(glm::vec2(values[0], values[1]), glm::vec2(0.0f), glm::vec2(1.0f));
            }

            ImGui::PopID();
            return changed;
        }

        bool DrawFloatInputAndSlider(
            const char* label,
            float& value,
            float inputStep,
            float sliderMin,
            float sliderMax,
            const char* format = "%.3f") {
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);

            bool changed = false;
            float localValue = value;
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::InputFloat("##Input", &localValue, inputStep, inputStep * 10.0f, format);
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::SliderFloat("##Slider", &localValue, sliderMin, sliderMax, format);

            if (changed) {
                value = localValue;
            }

            ImGui::PopID();
            return changed;
        }

        float ComputeScaledBorderWidth(const UIElement& element, float scale) {
            if (element.style.borderWidth <= 0.0f) {
                return 0.0f;
            }

            return std::max(1.0f, element.style.borderWidth * scale);
        }

        float ComputeScaledBorderRadius(const UIElement& element, float scale) {
            if (element.style.borderRadius <= 0.0f) {
                return 0.0f;
            }

            return std::max(0.0f, element.style.borderRadius * scale);
        }

        bool DrawColorEditor(const char* label, glm::vec4& value) {
            float values[4] = { value.x, value.y, value.z, value.w };
            if (ImGui::ColorEdit4(label, values)) {
                value = glm::vec4(values[0], values[1], values[2], values[3]);
                return true;
            }

            return false;
        }

        float NormalizeWidgetValue(float value, float minValue, float maxValue) {
            const float range = maxValue - minValue;
            if (std::abs(range) <= 0.0001f) {
                return 0.0f;
            }

            return std::clamp((value - minValue) / range, 0.0f, 1.0f);
        }

        ImVec2 ToCanvasPoint(const glm::vec2& point, const ImVec2& canvasMin, float scale) {
            return ImVec2(
                canvasMin.x + point.x * scale,
                canvasMin.y + point.y * scale
            );
        }

        ImVec2 ToCanvasSize(const glm::vec2& size, float scale) {
            return ImVec2(size.x * scale, size.y * scale);
        }

        bool ContainsPoint(const UIRect& rect, const glm::vec2& point) {
            const glm::vec2 min = rect.Min();
            const glm::vec2 max = rect.Max();
            return point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y;
        }

        float SnapToGrid(float value, float gridSize) {
            if (gridSize <= 0.0f) {
                return value;
            }

            return std::round(value / gridSize) * gridSize;
        }

        struct ResizeHandleRect {
            ResizeHandle handle = ResizeHandle::None;
            ImVec2 min;
            ImVec2 max;
        };

        std::array<ImVec2, 8> BuildResizeHandlePoints(const UIElement& element, const ImVec2& min, const ImVec2& max) {
            if (HasVisibleRotation(element)) {
                const auto quad = BuildRotatedQuad(element, min, max);
                const ImVec2 topMid((quad[0].x + quad[1].x) * 0.5f, (quad[0].y + quad[1].y) * 0.5f);
                const ImVec2 rightMid((quad[1].x + quad[2].x) * 0.5f, (quad[1].y + quad[2].y) * 0.5f);
                const ImVec2 bottomMid((quad[2].x + quad[3].x) * 0.5f, (quad[2].y + quad[3].y) * 0.5f);
                const ImVec2 leftMid((quad[3].x + quad[0].x) * 0.5f, (quad[3].y + quad[0].y) * 0.5f);
                return {
                    quad[0],
                    topMid,
                    quad[1],
                    rightMid,
                    quad[2],
                    bottomMid,
                    quad[3],
                    leftMid
                };
            }

            const float handleHalfExtent = 5.0f;
            const float centerX = (min.x + max.x) * 0.5f;
            const float centerY = (min.y + max.y) * 0.5f;

            return {
                ImVec2(min.x, min.y),
                ImVec2(centerX, min.y),
                ImVec2(max.x, min.y),
                ImVec2(max.x, centerY),
                ImVec2(max.x, max.y),
                ImVec2(centerX, max.y),
                ImVec2(min.x, max.y),
                ImVec2(min.x, centerY)
            };
        }

        std::array<ResizeHandleRect, 8> BuildResizeHandleRects(const UIElement& element, const ImVec2& min, const ImVec2& max) {
            constexpr std::array<ResizeHandle, 8> handleKinds = {
                ResizeHandle::TopLeft,
                ResizeHandle::Top,
                ResizeHandle::TopRight,
                ResizeHandle::Right,
                ResizeHandle::BottomRight,
                ResizeHandle::Bottom,
                ResizeHandle::BottomLeft,
                ResizeHandle::Left
            };

            const float handleHalfExtent = 5.0f;
            const auto points = BuildResizeHandlePoints(element, min, max);
            std::array<ResizeHandleRect, 8> rects{};
            for (std::size_t index = 0; index < rects.size(); ++index) {
                rects[index] = ResizeHandleRect{
                    handleKinds[index],
                    ImVec2(points[index].x - handleHalfExtent, points[index].y - handleHalfExtent),
                    ImVec2(points[index].x + handleHalfExtent, points[index].y + handleHalfExtent)
                };
            }
            return rects;
        }

        bool ContainsScreenPoint(const ImVec2& min, const ImVec2& max, const ImVec2& point) {
            return point.x >= min.x && point.x <= max.x &&
                point.y >= min.y && point.y <= max.y;
        }

        ResizeHandle HitTestResizeHandles(const UIElement& element, const ImVec2& min, const ImVec2& max, const ImVec2& point) {
            const auto handles = BuildResizeHandleRects(element, min, max);
            for (const ResizeHandleRect& handleRect : handles) {
                if (ContainsScreenPoint(handleRect.min, handleRect.max, point)) {
                    return handleRect.handle;
                }
            }

            return ResizeHandle::None;
        }

        struct UiHitCandidate {
            UIElementId elementId = 0;
            int zOrder = 0;
            int drawOrder = 0;
        };

        void CollectHitTestCandidates(
            const UIElement& element,
            const UIRect& parentRect,
            const glm::vec2& point,
            int& drawOrder,
            std::vector<UiHitCandidate>& candidates)
        {
            if (element.GetType() != UIElementType::Canvas && !element.visible) {
                return;
            }

            UIRect elementRect = parentRect;
            if (element.GetType() != UIElementType::Canvas) {
                elementRect = element.transform.ComputeRect(parentRect);
                if (ContainsPoint(elementRect, point)) {
                    candidates.push_back(UiHitCandidate{
                        element.GetId(),
                        element.zOrder,
                        drawOrder
                    });
                }
                ++drawOrder;
            }

            for (const auto& child : element.GetChildren()) {
                CollectHitTestCandidates(*child, elementRect, point, drawOrder, candidates);
            }
        }

        UIElementId HitTestCanvas(const UiEditorSession& session, const glm::vec2& point, const UIRect& rootRect) {
            if (!session.screen || !session.screen->GetRootCanvas()) {
                return 0;
            }

            int drawOrder = 0;
            std::vector<UiHitCandidate> candidates;
            candidates.reserve(32);
            CollectHitTestCandidates(*session.screen->GetRootCanvas(), rootRect, point, drawOrder, candidates);

            if (candidates.empty()) {
                return 0;
            }

            const auto topmost = std::max_element(candidates.begin(), candidates.end(), [](const UiHitCandidate& lhs, const UiHitCandidate& rhs) {
                if (lhs.zOrder != rhs.zOrder) {
                    return lhs.zOrder < rhs.zOrder;
                }
                return lhs.drawOrder < rhs.drawOrder;
            });

            return topmost != candidates.end() ? topmost->elementId : 0;
        }

        bool TryGetElementRects(
            const UIElement& element,
            const UIRect& parentRect,
            UIElementId targetId,
            UIRect& outParentRect,
            UIRect& outElementRect)
        {
            UIRect elementRect = parentRect;
            if (element.GetType() != UIElementType::Canvas) {
                elementRect = element.transform.ComputeRect(parentRect);
            }

            if (element.GetId() == targetId) {
                outParentRect = parentRect;
                outElementRect = elementRect;
                return true;
            }

            for (const auto& child : element.GetChildren()) {
                if (TryGetElementRects(*child, elementRect, targetId, outParentRect, outElementRect)) {
                    return true;
                }
            }

            return false;
        }


        std::filesystem::path NormalizeUiFilePath(const std::filesystem::path& inputPath) {
            std::filesystem::path path = inputPath;
            if (path.empty()) {
                return {};
            }

            if (!path.has_parent_path()) {
                path = std::filesystem::path("Assets") / "ui" / path;
            }
            else {
                const std::string parent = path.parent_path().generic_string();
                if (parent == "assets/ui") {
                    path = std::filesystem::path("Assets") / "ui" / path.filename();
                }
            }

            const std::string filename = path.filename().string();
            if (!filename.ends_with(".ui.json")) {
                if (path.extension() == ".json") {
                    path.replace_extension();
                }
                path += ".ui.json";
            }

            return path;
        }

        void SetFileDialogPath(UiEditorSession& session, const std::filesystem::path& path) {
            const std::string normalized = path.empty() ? std::string("Assets/ui/") : path.generic_string();
            std::snprintf(session.filePathBuffer, sizeof(session.filePathBuffer), "%s", normalized.c_str());
        }

        void OpenFileDialog(UiEditorSession& session, UiFileDialogMode mode, const std::filesystem::path& suggestedPath = {}) {
            session.fileDialogMode = mode;
            if (!suggestedPath.empty()) {
                SetFileDialogPath(session, suggestedPath);
            }
            else if (mode == UiFileDialogMode::SaveAs && !session.currentPath.empty()) {
                SetFileDialogPath(session, session.currentPath);
            }
            else {
                SetFileDialogPath(session, std::filesystem::path("Assets") / "ui");
            }
            ImGui::OpenPopup(mode == UiFileDialogMode::Open ? "Open UI File" : "Save UI File");
        }

        std::vector<std::filesystem::path> GetAvailableUiFiles() {
            std::vector<std::filesystem::path> files;
            const std::filesystem::path uiDirectory = std::filesystem::path("Assets") / "ui";
            std::error_code ec;
            if (!std::filesystem::exists(uiDirectory, ec)) {
                return files;
            }

            for (const auto& entry : std::filesystem::directory_iterator(uiDirectory, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::string filename = entry.path().filename().string();
                if (filename.ends_with(".ui.json")) {
                    files.push_back(entry.path());
                }
            }

            std::sort(files.begin(), files.end());
            return files;
        }

        bool SaveCurrentScreen(UiEditorSession& session, const std::filesystem::path& path) {
            EnsureScreen(session);
            if (!session.screen) {
                return false;
            }

            const std::filesystem::path normalizedPath = NormalizeUiFilePath(path);
            if (normalizedPath.empty()) {
                return false;
            }

            if (!UISerializer::SaveToFile(*session.screen, normalizedPath)) {
                return false;
            }

            session.currentPath = normalizedPath;
            SetFileDialogPath(session, normalizedPath);
            // 保存成功后顺带触发运行时热刷新，让 F1 视图立刻看到最新 UI。
            if (const auto& callback = GetRuntimeUiFileChangedCallback()) {
                callback(normalizedPath);
            }
            return true;
        }

        bool LoadScreenFromPath(UiEditorSession& session, const std::filesystem::path& path) {
            const std::filesystem::path normalizedPath = NormalizeUiFilePath(path);
            if (normalizedPath.empty()) {
                return false;
            }

            auto loaded = UISerializer::LoadFromFile(normalizedPath);
            if (!loaded) {
                return false;
            }

            StopAnimationPreview(session, true);
            session.screen = std::move(loaded);
            // 加载新文件后重置编辑期状态，避免拖拽 / 缩放残留到另一张 UI。
            session.currentPath = normalizedPath;
            session.selectedElementId = 0;
            session.draggedElementId = 0;
            session.resizedElementId = 0;
            session.previewMode = false;
            session.isDraggingElement = false;
            session.isResizingElement = false;
            session.activeResizeHandle = ResizeHandle::None;
            session.selectedAnimationClipIndex = -1;
            session.selectedAnimationTrackIndex = -1;
            session.selectedAnimationKeyframeIndex = -1;
            session.animationPreviewTime = 0.0f;
            ResetPreviewInteraction(session);
            SetFileDialogPath(session, normalizedPath);
            SyncAnimationSelection(session);
            // 新文件加载后重新建立历史基线，旧屏幕的 undo 记录对它无意义。
            ResetHistory(session);
            return true;
        }

        bool CompileCurrentScreen(UiEditorSession& session) {
            EnsureScreen(session);
            if (!session.screen) {
                return false;
            }

            const UICompiler::Result result = UICompiler::Compile(*session.screen, session.currentPath);
            if (result.success) {
                // 编译出的 compiled 文件如果当前正在运行时显示，也尝试同步热重载。
                if (const auto& callback = GetRuntimeUiFileChangedCallback()) {
                    callback(result.outputPath);
                }
            }
            return result.success;
        }

        void ResetPreviewInteraction(UiEditorSession& session) {
            session.previewHoveredElementId = 0;
            session.previewPressedElementId = 0;
        }

        bool IsPreviewInteractiveType(UIElementType type) {
            // 编辑器预览模式下，目前只让真正可点击的控件吞交互。
            return type == UIElementType::Button;
        }

        void StopAnimationPreview(UiEditorSession& session, bool restoreAnimatedProperties) {
            session.previewAnimator.StopAll();
            session.previewAnimator.SetScreen(nullptr);
            session.isAnimationPreviewPlaying = false;

            if (restoreAnimatedProperties) {
                RestoreAnimationPreviewSnapshot(session);
            }

            session.animationPreviewSnapshots.clear();
            session.animationPreviewTime = 0.0f;
        }

        void ApplyAnimationClipAtTime(UiEditorSession& session, UIAnimationClip& clip, float sampleTime) {
            if (!session.screen) {
                return;
            }

            RestoreAnimationPreviewSnapshot(session);
            session.previewAnimator.SetScreen(session.screen.get());
            if (session.previewAnimator.Play(clip.name)) {
                if (sampleTime > 0.0f) {
                    session.previewAnimator.Update(sampleTime);
                }
                session.previewAnimator.StopAll();
            }
            session.previewAnimator.SetScreen(nullptr);
        }

        void StartAnimationPreview(UiEditorSession& session) {
            UIAnimationClip* clip = GetSelectedAnimationClip(session);
            if (!clip || !session.screen) {
                return;
            }

            StopAnimationPreview(session, true);
            CaptureAnimationPreviewSnapshot(session, *clip);
            session.previewAnimator.SetScreen(session.screen.get());
            if (session.previewAnimator.Play(clip->name)) {
                session.isAnimationPreviewPlaying = true;
                session.animationPreviewTime = 0.0f;
            }
        }

        void ScrubAnimationPreview(UiEditorSession& session, float sampleTime) {
            UIAnimationClip* clip = GetSelectedAnimationClip(session);
            if (!clip || !session.screen) {
                return;
            }

            if (session.animationPreviewSnapshots.empty()) {
                CaptureAnimationPreviewSnapshot(session, *clip);
            }

            session.animationPreviewTime = std::clamp(sampleTime, 0.0f, std::max(0.0f, clip->duration));
            ApplyAnimationClipAtTime(session, *clip, session.animationPreviewTime);
        }

        void UpdateAnimationPreview(UiEditorSession& session, float deltaTime) {
            if (!session.isAnimationPreviewPlaying) {
                return;
            }

            UIAnimationClip* clip = GetSelectedAnimationClip(session);
            if (!clip || !session.screen) {
                StopAnimationPreview(session, true);
                return;
            }

            if (!session.previewAnimator.IsPlaying(clip->name)) {
                StopAnimationPreview(session, false);
                return;
            }

            session.previewAnimator.Update(std::max(0.0f, deltaTime));
            const auto& activeClips = session.previewAnimator.GetActiveClips();
            const auto iterator = std::find_if(activeClips.begin(), activeClips.end(), [&](const UIAnimator::ActiveClipState& state) {
                return state.clipName == clip->name;
            });

            if (iterator == activeClips.end()) {
                StopAnimationPreview(session, false);
                return;
            }

            const float duration = std::max(0.0f, clip->duration);
            if (clip->loopCount == 0 && duration > 0.0f) {
                session.animationPreviewTime = std::fmod(std::max(0.0f, iterator->currentTime), duration);
            }
            else {
                session.animationPreviewTime = std::clamp(iterator->currentTime, 0.0f, duration);
            }
        }

        glm::vec2 ComputeSerializedSizeFromFinalRect(const UITransform& transform, const UIRect& parentRect, const glm::vec2& finalSize) {
            const glm::vec2 anchorSpan = parentRect.size * (transform.anchorMax - transform.anchorMin);
            const glm::vec2 safeScale(
                std::max(transform.scale.x, 0.0001f),
                std::max(transform.scale.y, 0.0001f)
            );

            return glm::vec2(
                (finalSize.x / safeScale.x) - anchorSpan.x,
                (finalSize.y / safeScale.y) - anchorSpan.y
            );
        }

        glm::vec2 ComputeSerializedPositionFromFinalRect(const UITransform& transform, const UIRect& parentRect, const glm::vec2& finalPosition, const glm::vec2& finalSize) {
            const glm::vec2 anchorStart = parentRect.position + parentRect.size * transform.anchorMin;
            return finalPosition - anchorStart + finalSize * transform.pivot;
        }

        ImVec2 MeasurePreviewText(const std::string& text, ImFont* font, float fontSizePixels, float wrapWidth = 0.0f) {
            if (!font) {
                font = ImGui::GetFont();
            }
            if (!font) {
                return ImGui::CalcTextSize(text.c_str());
            }

            const float maxWidth = wrapWidth > 0.0f ? wrapWidth : FLT_MAX;
            return font->CalcTextSizeA(fontSizePixels, maxWidth, wrapWidth, text.c_str());
        }

        ImVec2 ComputeAlignedTextPosition(
            const std::string& alignment,
            const ImVec2& min,
            const ImVec2& max,
            const ImVec2& textSize,
            float padding = 8.0f) {
            const std::string alignmentLower = ToLowerCopy(alignment);
            float x = min.x + padding;
            if (alignmentLower == "center") {
                x = min.x + std::max(padding, (max.x - min.x - textSize.x) * 0.5f);
            }
            else if (alignmentLower == "right") {
                x = std::max(min.x + padding, max.x - textSize.x - padding);
            }

            const float y = std::clamp(
                min.y + padding,
                min.y + 2.0f,
                std::max(min.y + 2.0f, max.y - textSize.y - 2.0f));

            return ImVec2(x, y);
        }

        void DrawPreviewText(
            ImDrawList* drawList,
            const std::string& text,
            const ImVec2& min,
            const ImVec2& max,
            ImU32 color,
            float fontSizePixels = 0.0f,
            const std::string& alignment = "Left",
            ImFont* font = nullptr,
            bool wrapText = false) {
            const float effectiveFontSize = fontSizePixels > 0.0f ? fontSizePixels : ImGui::GetFontSize();
            const float wrapWidth = wrapText ? std::max(0.0f, (max.x - min.x) - 16.0f) : 0.0f;
            const ImVec2 textSize = MeasurePreviewText(text, font, effectiveFontSize, wrapWidth);
            const ImVec2 textPos = ComputeAlignedTextPosition(alignment, min, max, textSize);
            drawList->AddText(font, effectiveFontSize, textPos, color, text.c_str(), nullptr, wrapWidth);
        }

        void DrawCanvasGrid(ImDrawList* drawList, const ImVec2& canvasMin, const ImVec2& canvasMax, float scale) {
            // 可选网格仅用于编辑期对齐辅助，不会写入运行时 UI 数据。
            const float scaledStep = std::max(8.0f, 32.0f * scale);
            const ImU32 gridColor = IM_COL32(65, 72, 84, 120);

            for (float x = canvasMin.x + scaledStep; x < canvasMax.x; x += scaledStep) {
                drawList->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), gridColor, 1.0f);
            }
            for (float y = canvasMin.y + scaledStep; y < canvasMax.y; y += scaledStep) {
                drawList->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), gridColor, 1.0f);
            }
        }

        void DrawPreviewElement(
            const UIElement& element,
            const UIRect& parentRect,
            UiEditorSession& session,
            ImDrawList* drawList,
            const ImVec2& canvasMin,
            float scale)
        {
            // 编辑器画布直接递归绘制当前 UIScreen 的真实节点树，
            // 这样 Inspector、Hierarchy、Canvas 三者永远操作同一份数据。
            if (element.GetType() != UIElementType::Canvas && !element.visible) {
                return;
            }

            UIRect elementRect = parentRect;
            if (element.GetType() != UIElementType::Canvas) {
                elementRect = element.transform.ComputeRect(parentRect);
            }

            const ImVec2 min = ToCanvasPoint(elementRect.position, canvasMin, scale);
            const ImVec2 size = ToCanvasSize(elementRect.size, scale);
            const ImVec2 max(min.x + size.x, min.y + size.y);
            const ImU32 borderColor = ToImGuiColor(element.style.borderColor, element.style.opacity);
            const ImU32 fillColor = ToImGuiColor(element.style.backgroundColor, element.style.opacity);
            const ImU32 textColor = ToImGuiColor(element.style.textColor, element.style.opacity);
            const float scaledBorderRadius = ComputeScaledBorderRadius(element, scale);
            const float scaledFontSize = std::max(8.0f, element.style.fontSize * scale);
            ImFont* previewFont = ResolvePreviewFont(element.style.fontPath);

            switch (element.GetType()) {
            case UIElementType::Canvas:
                break;
            case UIElementType::Panel:
                if (!DrawPreviewTexture(drawList, element, min, max, element.style.texturePath, true, scale)) {
                    if (HasVisibleRotation(element)) {
                        const auto quad = BuildRotatedQuad(element, min, max);
                        drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], fillColor);
                    }
                    else {
                        drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    }
                    DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                }
                break;
            case UIElementType::Image: {
                const UIImage* image = dynamic_cast<const UIImage*>(&element);
                const std::string& texturePath = image && !image->imagePath.empty()
                    ? image->imagePath
                    : element.style.texturePath;
                if (DrawPreviewTexture(drawList, element, min, max, texturePath, true, scale)) {
                    break;
                }

                if (HasVisibleRotation(element)) {
                    const auto quad = BuildRotatedQuad(element, min, max);
                    drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], fillColor);
                    drawList->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(210, 210, 220, 180), 1.0f);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    drawList->AddRect(min, max, IM_COL32(210, 210, 220, 180), scaledBorderRadius, 0, 1.0f);
                }
                std::string placeholder = "Image";
                if (image && !image->imagePath.empty()) {
                    placeholder = std::format("Image\n{}", image->imagePath);
                }
                drawList->AddLine(min, max, IM_COL32(180, 180, 190, 150), 1.0f);
                drawList->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, min.y), IM_COL32(180, 180, 190, 150), 1.0f);
                DrawPreviewText(drawList, placeholder, min, max, textColor, scaledFontSize, "Left", previewFont, true);
                break;
            }
            case UIElementType::Text: {
                const UIText* text = dynamic_cast<const UIText*>(&element);
                const std::string previewText = text ? text->text : element.GetName();
                DrawPreviewText(
                    drawList,
                    previewText.empty() ? std::string("Text") : previewText,
                    min,
                    max,
                    textColor,
                    scaledFontSize,
                    text ? text->alignment : std::string("Left"),
                    previewFont,
                    text ? text->wrapText : false);
                break;
            }
            case UIElementType::Button: {
                const UIButton* button = dynamic_cast<const UIButton*>(&element);
                ImU32 buttonColor = fillColor;
                if (button) {
                    const glm::vec4* sourceColor = &button->normalColor;
                    if (!element.enabled || !element.interactable) {
                        sourceColor = &button->disabledColor;
                    }
                    else if (session.previewPressedElementId == element.GetId()) {
                        sourceColor = &button->pressedColor;
                    }
                    else if (session.previewHoveredElementId == element.GetId()) {
                        sourceColor = &button->hoverColor;
                    }
                    buttonColor = IM_COL32(
                        static_cast<int>(std::clamp(sourceColor->r, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(sourceColor->g, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(sourceColor->b, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(sourceColor->a * element.style.opacity, 0.0f, 1.0f) * 255.0f)
                    );
                }
                const bool drewTexture = !element.style.texturePath.empty() &&
                    DrawPreviewTexture(drawList, element, min, max, element.style.texturePath, false, scale);
                if (!drewTexture) {
                    if (HasVisibleRotation(element)) {
                        const auto quad = BuildRotatedQuad(element, min, max);
                        drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], buttonColor);
                    }
                    else {
                        drawList->AddRectFilled(min, max, buttonColor, scaledBorderRadius);
                    }
                }
                else if (buttonColor != IM_COL32(255, 255, 255, static_cast<int>(std::clamp(element.style.opacity, 0.0f, 1.0f) * 255.0f))) {
                    if (HasVisibleRotation(element)) {
                        const auto quad = BuildRotatedQuad(element, min, max);
                        drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], IM_COL32(
                            (buttonColor >> IM_COL32_R_SHIFT) & 0xFF,
                            (buttonColor >> IM_COL32_G_SHIFT) & 0xFF,
                            (buttonColor >> IM_COL32_B_SHIFT) & 0xFF,
                            96));
                    }
                    else {
                        drawList->AddRectFilled(min, max, IM_COL32(
                            (buttonColor >> IM_COL32_R_SHIFT) & 0xFF,
                            (buttonColor >> IM_COL32_G_SHIFT) & 0xFF,
                            (buttonColor >> IM_COL32_B_SHIFT) & 0xFF,
                            96), scaledBorderRadius);
                    }
                }
                DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                const std::string label = button && !button->label.empty() ? button->label : std::string("Button");
                const ImVec2 textSize = MeasurePreviewText(label, previewFont, scaledFontSize);
                const ImVec2 textPos(
                    min.x + std::max(8.0f, (size.x - textSize.x) * 0.5f),
                    min.y + std::max(6.0f, (size.y - textSize.y) * 0.5f)
                );
                drawList->AddText(previewFont, scaledFontSize, textPos, textColor, label.c_str());
                break;
            }
            case UIElementType::Slider: {
                if (const auto* slider = dynamic_cast<const UISlider*>(&element)) {
                    DrawPreviewSlider(drawList, element, *slider, min, max, scale);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                }
                break;
            }
            case UIElementType::Toggle: {
                if (const auto* toggle = dynamic_cast<const UIToggle*>(&element)) {
                    DrawPreviewToggle(drawList, element, *toggle, min, max, scale, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                }
                break;
            }
            case UIElementType::ProgressBar: {
                if (const auto* progressBar = dynamic_cast<const UIProgressBar*>(&element)) {
                    DrawPreviewProgressBar(drawList, element, *progressBar, min, max, scale, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                }
                break;
            }
            case UIElementType::RadialProgressBar: {
                if (const auto* radialProgressBar = dynamic_cast<const UIRadialProgressBar*>(&element)) {
                    DrawPreviewRadialProgressBar(drawList, element, *radialProgressBar, min, max, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                }
                break;
            }
            case UIElementType::InputField: {
                if (const auto* inputField = dynamic_cast<const UIInputField*>(&element)) {
                    DrawPreviewInputField(drawList, element, *inputField, min, max, scale, scaledFontSize, previewFont, textColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                    DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                }
                break;
            }
            default:
                if (HasVisibleRotation(element)) {
                    const auto quad = BuildRotatedQuad(element, min, max);
                    drawList->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], fillColor);
                }
                else {
                    drawList->AddRectFilled(min, max, fillColor, scaledBorderRadius);
                }
                DrawPreviewBorder(drawList, element, min, max, scale, borderColor);
                DrawPreviewText(drawList, std::string(ToString(element.GetType())), min, max, textColor, scaledFontSize, "Left", previewFont, false);
                break;
            }

            if (!session.previewMode && session.selectedElementId == element.GetId()) {
                if (HasVisibleRotation(element)) {
                    const auto quad = BuildRotatedQuad(element, min, max);
                    drawList->AddQuad(quad[0], quad[1], quad[2], quad[3], IM_COL32(255, 204, 96, 255), 2.5f);
                }
                else {
                    drawList->AddRect(min, max, IM_COL32(255, 204, 96, 255), std::max(2.0f, scaledBorderRadius), 0, 2.5f);
                }
            }

            for (const UIElement* child : GetChildrenSortedForDraw(element)) {
                DrawPreviewElement(*child, elementRect, session, drawList, canvasMin, scale);
            }
        }

        void DrawResizeHandles(ImDrawList* drawList, const UIElement& element, const ImVec2& min, const ImVec2& max) {
            const auto handles = BuildResizeHandleRects(element, min, max);
            for (const ResizeHandleRect& handleRect : handles) {
                drawList->AddRectFilled(handleRect.min, handleRect.max, IM_COL32(255, 204, 96, 255), 2.0f);
                drawList->AddRect(handleRect.min, handleRect.max, IM_COL32(20, 24, 30, 255), 2.0f, 0, 1.0f);
            }
        }

        void DrawBasicInspector(UIElement& element) {
            ImGui::SeparatorText("Basic");
            ImGui::InputText("Name", &element.name);
            ImGui::BeginDisabled();
            ImGui::InputScalar("ID", ImGuiDataType_U64, &element.id);
            const std::string typeName = std::string(ToString(element.type));
            ImGui::Text("Type: %s", typeName.c_str());
            ImGui::EndDisabled();
            ImGui::Checkbox("Visible", &element.visible);
            ImGui::Checkbox("Enabled", &element.enabled);
            ImGui::Checkbox("Interactable", &element.interactable);
            ImGui::Checkbox("Runtime Mutable", &element.runtimeMutable);
            ImGui::InputInt("Z Order", &element.zOrder);
        }

        void DrawLayerInspector(UIElement& element) {
            if (element.GetParent() == nullptr) {
                return;
            }

            ImGui::SeparatorText("Layer");
            if (ImGui::Button("Send To Back")) {
                ReorderElementLayer(element, LayerMoveDirection::ToBack);
            }
            ImGui::SameLine();
            if (ImGui::Button("Bring To Front")) {
                ReorderElementLayer(element, LayerMoveDirection::ToFront);
            }
            if (ImGui::Button("Move Backward")) {
                ReorderElementLayer(element, LayerMoveDirection::Backward);
            }
            ImGui::SameLine();
            if (ImGui::Button("Move Forward")) {
                ReorderElementLayer(element, LayerMoveDirection::Forward);
            }
        }

        void DrawTransformInspector(UIElement& element) {
            ImGui::SeparatorText("Rect Transform");
            DrawVec2Editor("Position", element.transform.position);
            DrawVec2Editor("Size", element.transform.size);
            DrawNormalizedVec2Editor("Anchor Min", element.transform.anchorMin);
            DrawNormalizedVec2Editor("Anchor Max", element.transform.anchorMax);
            DrawNormalizedVec2Editor("Pivot", element.transform.pivot);
            DrawFloatInputAndSlider("Rotation", element.transform.rotation, 0.1f, -360.0f, 360.0f, "%.1f");
            DrawVec2Editor("Scale", element.transform.scale);
        }

        void DrawStyleInspector(UIScreen& screen, UIElement& element) {
            ImGui::SeparatorText("Style");
            const auto theme = LoadScreenTheme(screen);

            if (theme != nullptr && !theme->presets.empty()) {
                std::string presetPreview = element.style.presetName.empty() ? std::string("<None>") : element.style.presetName;
                if (ImGui::BeginCombo("Style Preset", presetPreview.c_str())) {
                    const bool isNoneSelected = element.style.presetName.empty();
                    if (ImGui::Selectable("<None>", isNoneSelected)) {
                        element.style.presetName.clear();
                        SetAllStyleOverrides(element.style, true);
                    }
                    if (isNoneSelected) {
                        ImGui::SetItemDefaultFocus();
                    }

                    for (const UIStylePreset& preset : theme->presets) {
                        const bool isSelected = element.style.presetName == preset.name;
                        if (ImGui::Selectable(preset.name.c_str(), isSelected)) {
                            element.style.presetName = preset.name;
                            ApplyResolvedPresetStyle(element, preset);
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }
            }
            else {
                ImGui::TextDisabled("No theme loaded for this screen.");
            }

            auto drawOverrideHeader = [&](const char* label, bool& enabled) {
                ImGui::PushID(label);
                ImGui::Checkbox("##Override", &enabled);
                ImGui::SameLine();
                ImGui::TextUnformatted(label);
                ImGui::PopID();
            };

            drawOverrideHeader("Background Color", element.style.overrides.backgroundColor);
            ImGui::BeginDisabled(!element.style.overrides.backgroundColor && !element.style.presetName.empty());
            DrawColorEditor("##BackgroundColor", element.style.backgroundColor);
            ImGui::EndDisabled();

            drawOverrideHeader("Tint Color", element.style.overrides.tintColor);
            ImGui::BeginDisabled(!element.style.overrides.tintColor && !element.style.presetName.empty());
            DrawColorEditor("##TintColor", element.style.tintColor);
            ImGui::EndDisabled();

            drawOverrideHeader("Text Color", element.style.overrides.textColor);
            ImGui::BeginDisabled(!element.style.overrides.textColor && !element.style.presetName.empty());
            DrawColorEditor("##TextColor", element.style.textColor);
            ImGui::EndDisabled();

            drawOverrideHeader("Opacity", element.style.overrides.opacity);
            ImGui::BeginDisabled(!element.style.overrides.opacity && !element.style.presetName.empty());
            ImGui::SliderFloat("##Opacity", &element.style.opacity, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();

            drawOverrideHeader("Texture Path", element.style.overrides.texturePath);
            ImGui::BeginDisabled(!element.style.overrides.texturePath && !element.style.presetName.empty());
            if (DrawAssetPathCombo("##TexturePath", element.style.texturePath, GetTextureAssetPaths())) {
                if (auto* image = dynamic_cast<UIImage*>(&element)) {
                    image->imagePath = element.style.texturePath;
                }
            }
            ImGui::EndDisabled();

            drawOverrideHeader("Font Path", element.style.overrides.fontPath);
            ImGui::BeginDisabled(!element.style.overrides.fontPath && !element.style.presetName.empty());
            DrawAssetPathCombo("##FontPath", element.style.fontPath, GetFontAssetPaths());
            ImGui::EndDisabled();

            drawOverrideHeader("Font Size", element.style.overrides.fontSize);
            ImGui::BeginDisabled(!element.style.overrides.fontSize && !element.style.presetName.empty());
            DrawFloatInputAndSlider("##FontSize", element.style.fontSize, 0.5f, 4.0f, 128.0f, "%.1f");
            ImGui::EndDisabled();

            drawOverrideHeader("Border Width", element.style.overrides.borderWidth);
            ImGui::BeginDisabled(!element.style.overrides.borderWidth && !element.style.presetName.empty());
            DrawFloatInputAndSlider("##BorderWidth", element.style.borderWidth, 0.1f, 0.0f, 32.0f, "%.2f");
            ImGui::EndDisabled();

            drawOverrideHeader("Border Radius", element.style.overrides.borderRadius);
            ImGui::BeginDisabled(!element.style.overrides.borderRadius && !element.style.presetName.empty());
            DrawFloatInputAndSlider("##BorderRadius", element.style.borderRadius, 0.5f, 0.0f, 128.0f, "%.1f");
            ImGui::EndDisabled();

            drawOverrideHeader("Border Color", element.style.overrides.borderColor);
            ImGui::BeginDisabled(!element.style.overrides.borderColor && !element.style.presetName.empty());
            DrawColorEditor("##BorderColor", element.style.borderColor);
            ImGui::EndDisabled();
        }

        UIBindingTargetProperty GetDefaultBindingTargetProperty(const UIElement& element) {
            switch (element.GetType()) {
            case UIElementType::Text:
                return UIBindingTargetProperty::TextText;
            case UIElementType::ProgressBar:
            case UIElementType::RadialProgressBar:
                return UIBindingTargetProperty::ProgressBarValue;
            case UIElementType::Slider:
                return UIBindingTargetProperty::SliderValue;
            case UIElementType::Image:
                return UIBindingTargetProperty::ImageTintColor;
            default:
                return UIBindingTargetProperty::ElementVisible;
            }
        }

        const std::array<const char*, 8>& GetBindingTargetPropertyLabels() {
            static const std::array<const char*, 8> labels = {
                "Text.text",
                "ProgressBar.value",
                "Slider.value",
                "Image.tintColor",
                "Element.visible",
                "Element.opacity",
                "Element.position",
                "Element.rotation"
            };
            return labels;
        }

        const std::array<UIBindingTargetProperty, 8>& GetBindingTargetPropertyValues() {
            static const std::array<UIBindingTargetProperty, 8> values = {
                UIBindingTargetProperty::TextText,
                UIBindingTargetProperty::ProgressBarValue,
                UIBindingTargetProperty::SliderValue,
                UIBindingTargetProperty::ImageTintColor,
                UIBindingTargetProperty::ElementVisible,
                UIBindingTargetProperty::ElementOpacity,
                UIBindingTargetProperty::ElementPosition,
                UIBindingTargetProperty::ElementRotation
            };
            return values;
        }

        const std::array<const char*, 2>& GetBindingUpdateModeLabels() {
            static const std::array<const char*, 2> labels = {
                "EveryFrame",
                "OnChange"
            };
            return labels;
        }

        const std::array<UIBindingUpdateMode, 2>& GetBindingUpdateModeValues() {
            static const std::array<UIBindingUpdateMode, 2> values = {
                UIBindingUpdateMode::EveryFrame,
                UIBindingUpdateMode::OnChange
            };
            return values;
        }

        const std::array<const char*, 6>& GetSuggestedBindingKeys() {
            static const std::array<const char*, 6> keys = {
                "bike.speedKmh",
                "bike.gear",
                "bike.stamina",
                "race.lapTime",
                "bike.lowStamina",
                "bike.warningColor"
            };
            return keys;
        }

        void DrawBindingsInspector(UIScreen& screen, UIElement& element) {
            ImGui::SeparatorText("Bindings");

            auto& bindings = screen.GetBindings();
            int removeIndex = -1;
            bool hasBinding = false;

            for (std::size_t index = 0; index < bindings.size(); ++index) {
                UIPropertyBinding& binding = bindings[index];
                if (binding.targetElementId != element.GetId()) {
                    continue;
                }

                hasBinding = true;
                ImGui::PushID(static_cast<int>(index));

                const std::string header = std::format("Binding {}##binding{}", index + 1, index);
                if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto& targetPropertyLabels = GetBindingTargetPropertyLabels();
                    const auto& targetPropertyValues = GetBindingTargetPropertyValues();
                    int targetPropertyIndex = 0;
                    for (std::size_t propertyIndex = 0; propertyIndex < targetPropertyValues.size(); ++propertyIndex) {
                        if (binding.targetProperty == targetPropertyValues[propertyIndex]) {
                            targetPropertyIndex = static_cast<int>(propertyIndex);
                            break;
                        }
                    }
                    if (ImGui::Combo("Target Property", &targetPropertyIndex, targetPropertyLabels.data(), static_cast<int>(targetPropertyLabels.size()))) {
                        binding.targetProperty = targetPropertyValues[static_cast<std::size_t>(targetPropertyIndex)];
                    }

                    ImGui::InputText("Source Key", &binding.sourceKey);
                    if (ImGui::BeginCombo("Suggested Key", binding.sourceKey.empty() ? "<Pick>" : binding.sourceKey.c_str())) {
                        for (const char* key : GetSuggestedBindingKeys()) {
                            const bool selected = binding.sourceKey == key;
                            if (ImGui::Selectable(key, selected)) {
                                binding.sourceKey = key;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::InputText("Format", &binding.formatString);

                    const auto& updateModeLabels = GetBindingUpdateModeLabels();
                    const auto& updateModeValues = GetBindingUpdateModeValues();
                    int updateModeIndex = 0;
                    for (std::size_t modeIndex = 0; modeIndex < updateModeValues.size(); ++modeIndex) {
                        if (binding.updateMode == updateModeValues[modeIndex]) {
                            updateModeIndex = static_cast<int>(modeIndex);
                            break;
                        }
                    }
                    if (ImGui::Combo("Update Mode", &updateModeIndex, updateModeLabels.data(), static_cast<int>(updateModeLabels.size()))) {
                        binding.updateMode = updateModeValues[static_cast<std::size_t>(updateModeIndex)];
                    }

                    ImGui::Checkbox("Invert", &binding.invert);
                    ImGui::Checkbox("Use Min", &binding.hasMin);
                    if (binding.hasMin) {
                        ImGui::InputFloat("Min", &binding.minValue, 0.1f, 1.0f, "%.3f");
                    }
                    ImGui::Checkbox("Use Max", &binding.hasMax);
                    if (binding.hasMax) {
                        ImGui::InputFloat("Max", &binding.maxValue, 0.1f, 1.0f, "%.3f");
                    }

                    ImGui::TextDisabled("Target Element ID: %llu", static_cast<unsigned long long>(binding.targetElementId));
                    if (ImGui::Button("Remove Binding")) {
                        removeIndex = static_cast<int>(index);
                    }
                }

                ImGui::PopID();
            }

            if (!hasBinding) {
                ImGui::TextDisabled("No bindings on this element.");
            }

            if (ImGui::Button("Add Binding")) {
                UIPropertyBinding newBinding{};
                newBinding.targetElementId = element.GetId();
                newBinding.targetProperty = GetDefaultBindingTargetProperty(element);
                newBinding.sourceKey = "bike.speedKmh";
                if (newBinding.targetProperty == UIBindingTargetProperty::TextText) {
                    newBinding.formatString = "{0:.0f}";
                }
                screen.AddBinding(std::move(newBinding));
            }

            if (removeIndex >= 0) {
                screen.RemoveBinding(static_cast<std::size_t>(removeIndex));
            }
        }

        bool DrawAnimationValueEditor(const char* label, UIAnimationProperty property, UIValue& value) {
            switch (property) {
            case UIAnimationProperty::Position:
            case UIAnimationProperty::Size:
            case UIAnimationProperty::Scale: {
                glm::vec2 vec2Value = std::get_if<glm::vec2>(&value.data)
                    ? *std::get_if<glm::vec2>(&value.data)
                    : glm::vec2(0.0f);
                if (DrawVec2Editor(label, vec2Value)) {
                    value = UIValue(vec2Value);
                    return true;
                }
                return false;
            }
            case UIAnimationProperty::Rotation:
            case UIAnimationProperty::Opacity: {
                float floatValue = std::get_if<float>(&value.data)
                    ? *std::get_if<float>(&value.data)
                    : 0.0f;
                const float speed = property == UIAnimationProperty::Opacity ? 0.01f : 1.0f;
                const char* format = property == UIAnimationProperty::Opacity ? "%.3f" : "%.2f";
                if (ImGui::DragFloat(label, &floatValue, speed, -FLT_MAX, FLT_MAX, format)) {
                    value = UIValue(floatValue);
                    return true;
                }
                return false;
            }
            case UIAnimationProperty::BackgroundColor:
            case UIAnimationProperty::TintColor:
            case UIAnimationProperty::TextColor: {
                glm::vec4 colorValue = std::get_if<glm::vec4>(&value.data)
                    ? *std::get_if<glm::vec4>(&value.data)
                    : glm::vec4(1.0f);
                if (ImGui::ColorEdit4(label, &colorValue.x)) {
                    value = UIValue(colorValue);
                    return true;
                }
                return false;
            }
            }

            ImGui::TextDisabled("Unsupported animated value.");
            return false;
        }

        bool AddAnimationKeyframeAtTime(
            UiEditorSession& session,
            UIAnimationClip& clip,
            UIAnimationTrack& track,
            float sampleTime,
            bool selectNewKeyframe)
        {
            if (!session.screen) {
                return false;
            }

            UIElement* targetElement = session.screen->FindById(track.targetElementId);
            if (!targetElement) {
                return false;
            }

            StopAnimationPreview(session, true);

            UIKeyframe keyframe{};
            keyframe.time = std::clamp(sampleTime, 0.0f, std::max(0.0f, clip.duration));
            keyframe.value = CaptureAnimatedPropertyValue(*targetElement, track.property);
            keyframe.easing = UIAnimationEasing::EaseInOut;
            track.keyframes.push_back(std::move(keyframe));
            SortTrackKeyframes(track);

            if (selectNewKeyframe) {
                auto keyframeIt = std::find_if(track.keyframes.begin(), track.keyframes.end(), [&](const UIKeyframe& candidate) {
                    return std::abs(candidate.time - keyframe.time) <= 0.0001f &&
                        AreUIValuesEqual(candidate.value, keyframe.value);
                });
                if (keyframeIt != track.keyframes.end()) {
                    session.selectedAnimationKeyframeIndex = static_cast<int>(std::distance(track.keyframes.begin(), keyframeIt));
                }
            }

            ScrubAnimationPreview(session, session.animationPreviewTime);
            return true;
        }

        void DrawAnimationTimeline(UiEditorSession& session, UIAnimationClip& clip) {
            const float timelineHeaderHeight = 34.0f;
            const float timelineRowHeight = 42.0f;
            const float timelineLeftColumnWidth = 240.0f;
            const float collapsedRowHeight = 22.0f;
            const float duration = std::max(0.01f, clip.duration);

            float totalRowHeight = 0.0f;
            for (const UIAnimationTrack& track : clip.tracks) {
                totalRowHeight += session.collapsedAnimationTracks.contains(MakeAnimationTrackKey(track))
                    ? collapsedRowHeight
                    : timelineRowHeight;
            }
            totalRowHeight = std::max(totalRowHeight, timelineRowHeight);

            const float timelineHeight = std::max(220.0f, timelineHeaderHeight + totalRowHeight + 12.0f);
            ImGui::BeginChild("AnimationTimelineCanvas", ImVec2(0.0f, timelineHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
            const bool childHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (childHovered) {
                session.wantsMouseCapture = true;
            }

            if (childHovered && ImGui::GetIO().MouseWheel != 0.0f) {
                session.animationTimelineZoom = std::clamp(session.animationTimelineZoom + ImGui::GetIO().MouseWheel * 0.1f, 0.5f, 4.0f);
            }

            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float rightWidth = std::max(420.0f, (available.x - timelineLeftColumnWidth) * session.animationTimelineZoom);
            const float fullWidth = timelineLeftColumnWidth + rightWidth;
            const float fullHeight = timelineHeaderHeight + totalRowHeight;
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            drawList->AddRectFilled(origin, ImVec2(origin.x + fullWidth, origin.y + fullHeight), IM_COL32(16, 19, 26, 255), 8.0f);
            drawList->AddRect(origin, ImVec2(origin.x + fullWidth, origin.y + fullHeight), IM_COL32(74, 82, 98, 255), 8.0f, 0, 1.2f);
            drawList->AddRectFilled(origin, ImVec2(origin.x + fullWidth, origin.y + timelineHeaderHeight), IM_COL32(26, 30, 40, 255), 8.0f, ImDrawFlags_RoundCornersTop);
            drawList->AddLine(ImVec2(origin.x + timelineLeftColumnWidth, origin.y), ImVec2(origin.x + timelineLeftColumnWidth, origin.y + fullHeight), IM_COL32(84, 92, 110, 255), 1.0f);
            drawList->AddText(ImVec2(origin.x + 10.0f, origin.y + 8.0f), IM_COL32(228, 232, 240, 255), "Tracks");
            drawList->AddText(ImVec2(origin.x + timelineLeftColumnWidth + 10.0f, origin.y + 8.0f), IM_COL32(228, 232, 240, 255), "Timeline");

            const int majorTickCount = 10;
            for (int tick = 0; tick <= majorTickCount; ++tick) {
                const float normalized = static_cast<float>(tick) / static_cast<float>(majorTickCount);
                const float x = origin.x + timelineLeftColumnWidth + normalized * rightWidth;
                drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + fullHeight), IM_COL32(54, 58, 68, 180), 1.0f);
                drawList->AddText(ImVec2(x + 4.0f, origin.y + 7.0f), IM_COL32(195, 202, 214, 255), std::format("{:.2f}", duration * normalized).c_str());
                if (tick < majorTickCount) {
                    for (int minor = 1; minor < 5; ++minor) {
                        const float minorNormalized = (static_cast<float>(tick) + static_cast<float>(minor) / 5.0f) / static_cast<float>(majorTickCount);
                        const float minorX = origin.x + timelineLeftColumnWidth + minorNormalized * rightWidth;
                        drawList->AddLine(ImVec2(minorX, origin.y + timelineHeaderHeight), ImVec2(minorX, origin.y + fullHeight), IM_COL32(38, 42, 52, 120), 1.0f);
                    }
                }
            }

            const float currentTimeX = origin.x + timelineLeftColumnWidth + (std::clamp(session.animationPreviewTime, 0.0f, duration) / duration) * rightWidth;
            drawList->AddLine(ImVec2(currentTimeX, origin.y + 2.0f), ImVec2(currentTimeX, origin.y + fullHeight), IM_COL32(255, 210, 96, 255), 2.5f);
            drawList->AddTriangleFilled(
                ImVec2(currentTimeX - 6.0f, origin.y + 2.0f),
                ImVec2(currentTimeX + 6.0f, origin.y + 2.0f),
                ImVec2(currentTimeX, origin.y + 12.0f),
                IM_COL32(255, 210, 96, 255));

            const ImVec2 mousePos = ImGui::GetMousePos();
            const bool leftClicked = childHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            const bool leftDoubleClicked = childHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            const bool rightClicked = childHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            const bool ctrlHeld = ImGui::GetIO().KeyCtrl;

            int hitTrackIndex = -1;
            int hitKeyframeIndex = -1;
            float cursorY = origin.y + timelineHeaderHeight;

            if (leftReleased) {
                session.isDraggingTimelineKeys = false;
                session.isMarqueeSelectingKeys = false;
                session.draggedKeyframeOriginalTimes.clear();
            }

            for (std::size_t trackIndex = 0; trackIndex < clip.tracks.size(); ++trackIndex) {
                UIAnimationTrack& track = clip.tracks[trackIndex];
                const std::uint64_t trackKey = MakeAnimationTrackKey(track);
                const bool collapsed = session.collapsedAnimationTracks.contains(trackKey);
                const float rowHeight = collapsed ? collapsedRowHeight : timelineRowHeight;
                const float rowTop = cursorY;
                const float rowBottom = rowTop + rowHeight;
                cursorY += rowHeight;

                const bool isSelectedTrack = session.selectedAnimationTrackIndex == static_cast<int>(trackIndex);
                const bool alternateRow = (trackIndex % 2) == 1;
                const ImU32 rowColor = isSelectedTrack
                    ? IM_COL32(42, 52, 74, 255)
                    : (alternateRow ? IM_COL32(27, 31, 40, 255) : IM_COL32(24, 27, 35, 255));
                drawList->AddRectFilled(ImVec2(origin.x, rowTop), ImVec2(origin.x + fullWidth, rowBottom), rowColor);
                drawList->AddLine(ImVec2(origin.x, rowBottom), ImVec2(origin.x + fullWidth, rowBottom), IM_COL32(46, 50, 60, 255), 1.0f);
                drawList->AddLine(
                    ImVec2(origin.x + timelineLeftColumnWidth + 12.0f, rowTop + rowHeight * 0.5f),
                    ImVec2(origin.x + fullWidth - 10.0f, rowTop + rowHeight * 0.5f),
                    IM_COL32(68, 76, 92, 110),
                    1.0f);

                const UIElement* targetElement = session.screen ? session.screen->FindById(track.targetElementId) : nullptr;
                const std::string label = std::format("{}  {}", collapsed ? ">" : "v", targetElement ? targetElement->GetName() : std::format("Missing({})", track.targetElementId));
                const std::string propertyLabel = std::string(ToString(track.property));
                drawList->AddText(ImVec2(origin.x + 10.0f, rowTop + 5.0f), IM_COL32(228, 232, 240, 255), label.c_str());
                drawList->AddText(ImVec2(origin.x + 28.0f, rowTop + 22.0f), IM_COL32(154, 176, 230, 255), propertyLabel.c_str());

                const bool mouseInRow = childHovered && mousePos.y >= rowTop && mousePos.y <= rowBottom;
                if (mouseInRow) {
                    hitTrackIndex = static_cast<int>(trackIndex);
                    if (!collapsed && mousePos.x >= origin.x + timelineLeftColumnWidth) {
                        for (std::size_t keyframeIndex = 0; keyframeIndex < track.keyframes.size(); ++keyframeIndex) {
                            const UIKeyframe& keyframe = track.keyframes[keyframeIndex];
                            const float normalized = std::clamp(keyframe.time / duration, 0.0f, 1.0f);
                            const float keyX = origin.x + timelineLeftColumnWidth + normalized * rightWidth;
                            if (std::abs(mousePos.x - keyX) <= 10.0f) {
                                hitTrackIndex = static_cast<int>(trackIndex);
                                hitKeyframeIndex = static_cast<int>(keyframeIndex);
                                break;
                            }
                        }
                    }
                }

                if (leftClicked && mouseInRow && mousePos.x < origin.x + timelineLeftColumnWidth) {
                    session.selectedAnimationTrackIndex = static_cast<int>(trackIndex);
                    session.selectedAnimationKeyframeIndex = -1;
                    if (mousePos.x < origin.x + 26.0f) {
                        if (collapsed) {
                            session.collapsedAnimationTracks.erase(trackKey);
                        }
                        else {
                            session.collapsedAnimationTracks.insert(trackKey);
                        }
                    }
                }

                if (collapsed) {
                    continue;
                }

                for (std::size_t keyframeIndex = 0; keyframeIndex < track.keyframes.size(); ++keyframeIndex) {
                    const UIKeyframe& keyframe = track.keyframes[keyframeIndex];
                    const float normalized = std::clamp(keyframe.time / duration, 0.0f, 1.0f);
                    const float x = origin.x + timelineLeftColumnWidth + normalized * rightWidth;
                    const float y = rowTop + rowHeight * 0.5f;
                    const bool isSelected = IsAnimationKeySelected(session, static_cast<int>(trackIndex), static_cast<int>(keyframeIndex));
                    const ImU32 diamondColor = isSelected ? IM_COL32(255, 214, 112, 255) : IM_COL32(188, 202, 226, 255);
                    drawList->AddQuadFilled(ImVec2(x, y - 7.0f), ImVec2(x + 7.0f, y), ImVec2(x, y + 7.0f), ImVec2(x - 7.0f, y), diamondColor);
                    drawList->AddQuad(ImVec2(x, y - 7.0f), ImVec2(x + 7.0f, y), ImVec2(x, y + 7.0f), ImVec2(x - 7.0f, y), IM_COL32(18, 20, 24, 255), 1.0f);
                }
            }

            if (leftDoubleClicked &&
                hitTrackIndex >= 0 &&
                hitKeyframeIndex < 0 &&
                mousePos.x >= origin.x + timelineLeftColumnWidth) {
                session.selectedAnimationTrackIndex = hitTrackIndex;
                if (UIAnimationTrack* targetTrack = GetSelectedAnimationTrack(session)) {
                    const float normalized = std::clamp((mousePos.x - (origin.x + timelineLeftColumnWidth)) / rightWidth, 0.0f, 1.0f);
                    AddAnimationKeyframeAtTime(session, clip, *targetTrack, normalized * duration, true);
                }
            }

            if (leftClicked && childHovered && mousePos.y >= origin.y + timelineHeaderHeight) {
                if (hitKeyframeIndex >= 0 && hitTrackIndex >= 0) {
                    if (ctrlHeld) {
                        SetPrimaryAnimationKeySelection(session, hitTrackIndex, hitKeyframeIndex, true);
                    }
                    else {
                        SetPrimaryAnimationKeySelection(session, hitTrackIndex, hitKeyframeIndex, false);
                    }
                    StopAnimationPreview(session, true);
                    ScrubAnimationPreview(session, clip.tracks[static_cast<std::size_t>(hitTrackIndex)].keyframes[static_cast<std::size_t>(hitKeyframeIndex)].time);
                    session.isDraggingTimelineKeys = true;
                    session.timelineDragStartMouse = mousePos;
                    session.draggedKeyframeOriginalTimes.clear();
                    for (const AnimationKeySelection& selection : session.selectedAnimationKeys) {
                        session.draggedKeyframeOriginalTimes.push_back(
                            clip.tracks[static_cast<std::size_t>(selection.trackIndex)].keyframes[static_cast<std::size_t>(selection.keyframeIndex)].time);
                    }
                }
                else if (mousePos.x >= origin.x + timelineLeftColumnWidth && hitTrackIndex >= 0) {
                    session.selectedAnimationTrackIndex = hitTrackIndex;
                    session.selectedAnimationKeyframeIndex = -1;
                    session.selectedAnimationKeys.clear();
                    const float normalized = std::clamp((mousePos.x - (origin.x + timelineLeftColumnWidth)) / rightWidth, 0.0f, 1.0f);
                    StopAnimationPreview(session, true);
                    ScrubAnimationPreview(session, normalized * duration);
                    session.isMarqueeSelectingKeys = true;
                    session.marqueeSelectionStart = mousePos;
                    session.marqueeSelectionEnd = mousePos;
                }
            }

            if (rightClicked && childHovered && mousePos.y >= origin.y + timelineHeaderHeight && hitTrackIndex >= 0) {
                session.contextAnimationTrackIndex = hitTrackIndex;
                session.contextAnimationKeyframeIndex = hitKeyframeIndex;
                session.selectedAnimationTrackIndex = hitTrackIndex;
                if (hitKeyframeIndex >= 0) {
                    SetPrimaryAnimationKeySelection(session, hitTrackIndex, hitKeyframeIndex, ctrlHeld);
                }
                session.contextAnimationTime = std::clamp(
                    (mousePos.x - (origin.x + timelineLeftColumnWidth)) / std::max(1.0f, rightWidth),
                    0.0f,
                    1.0f) * duration;
                ImGui::OpenPopup("AnimationTimelineContext");
            }

            if (session.isDraggingTimelineKeys && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float deltaPixels = mousePos.x - session.timelineDragStartMouse.x;
                const float deltaTime = (deltaPixels / rightWidth) * duration;
                for (std::size_t index = 0; index < session.selectedAnimationKeys.size() && index < session.draggedKeyframeOriginalTimes.size(); ++index) {
                    const AnimationKeySelection& selection = session.selectedAnimationKeys[index];
                    UIKeyframe& keyframe = clip.tracks[static_cast<std::size_t>(selection.trackIndex)].keyframes[static_cast<std::size_t>(selection.keyframeIndex)];
                    keyframe.time = std::clamp(session.draggedKeyframeOriginalTimes[index] + deltaTime, 0.0f, duration);
                }
                for (UIAnimationTrack& track : clip.tracks) {
                    SortTrackKeyframes(track);
                }
                SyncAnimationSelection(session);
                ScrubAnimationPreview(session, session.animationPreviewTime);
            }

            if (session.isMarqueeSelectingKeys && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                session.marqueeSelectionEnd = mousePos;
            }
            if (session.isMarqueeSelectingKeys && leftReleased) {
                const ImVec2 minPoint(std::min(session.marqueeSelectionStart.x, session.marqueeSelectionEnd.x), std::min(session.marqueeSelectionStart.y, session.marqueeSelectionEnd.y));
                const ImVec2 maxPoint(std::max(session.marqueeSelectionStart.x, session.marqueeSelectionEnd.x), std::max(session.marqueeSelectionStart.y, session.marqueeSelectionEnd.y));
                session.selectedAnimationKeys.clear();
                float rowCursorY = origin.y + timelineHeaderHeight;
                for (std::size_t trackIndex = 0; trackIndex < clip.tracks.size(); ++trackIndex) {
                    UIAnimationTrack& track = clip.tracks[trackIndex];
                    const bool collapsed = session.collapsedAnimationTracks.contains(MakeAnimationTrackKey(track));
                    const float rowHeight = collapsed ? collapsedRowHeight : timelineRowHeight;
                    const float rowTop = rowCursorY;
                    rowCursorY += rowHeight;
                    if (collapsed) {
                        continue;
                    }
                    const float y = rowTop + rowHeight * 0.5f;
                    if (y < minPoint.y || y > maxPoint.y) {
                        continue;
                    }
                    for (std::size_t keyframeIndex = 0; keyframeIndex < track.keyframes.size(); ++keyframeIndex) {
                        const float normalized = std::clamp(track.keyframes[keyframeIndex].time / duration, 0.0f, 1.0f);
                        const float x = origin.x + timelineLeftColumnWidth + normalized * rightWidth;
                        if (x >= minPoint.x && x <= maxPoint.x) {
                            session.selectedAnimationKeys.push_back({ static_cast<int>(trackIndex), static_cast<int>(keyframeIndex) });
                        }
                    }
                }
                if (!session.selectedAnimationKeys.empty()) {
                    session.selectedAnimationTrackIndex = session.selectedAnimationKeys.front().trackIndex;
                    session.selectedAnimationKeyframeIndex = session.selectedAnimationKeys.front().keyframeIndex;
                }
            }

            if (session.isMarqueeSelectingKeys) {
                const ImVec2 minPoint(std::min(session.marqueeSelectionStart.x, session.marqueeSelectionEnd.x), std::min(session.marqueeSelectionStart.y, session.marqueeSelectionEnd.y));
                const ImVec2 maxPoint(std::max(session.marqueeSelectionStart.x, session.marqueeSelectionEnd.x), std::max(session.marqueeSelectionStart.y, session.marqueeSelectionEnd.y));
                drawList->AddRectFilled(minPoint, maxPoint, IM_COL32(88, 156, 255, 35.0f));
                drawList->AddRect(minPoint, maxPoint, IM_COL32(88, 156, 255, 220), 0.0f, 0, 1.2f);
            }

            if (ImGui::BeginPopup("AnimationTimelineContext")) {
                TrackWindowInputCapture(session);

                const int contextTrackIndex = session.contextAnimationTrackIndex;
                if (contextTrackIndex >= 0 && contextTrackIndex < static_cast<int>(clip.tracks.size())) {
                    UIAnimationTrack& contextTrack = clip.tracks[static_cast<std::size_t>(contextTrackIndex)];
                    if (ImGui::MenuItem("Add Key Here")) {
                        AddAnimationKeyframeAtTime(session, clip, contextTrack, session.contextAnimationTime, true);
                    }
                    if (ImGui::MenuItem("Delete Selected Keyframes", nullptr, false, !session.selectedAnimationKeys.empty())) {
                        DeleteSelectedAnimationKeys(session, clip);
                    }
                    if (ImGui::MenuItem("Delete Track")) {
                        StopAnimationPreview(session, true);
                        clip.tracks.erase(clip.tracks.begin() + contextTrackIndex);
                        session.selectedAnimationTrackIndex = -1;
                        session.selectedAnimationKeyframeIndex = -1;
                        session.selectedAnimationKeys.clear();
                        SyncAnimationSelection(session);
                    }

                    const std::uint64_t trackKey = MakeAnimationTrackKey(contextTrack);
                    const bool collapsed = session.collapsedAnimationTracks.contains(trackKey);
                    if (ImGui::MenuItem(collapsed ? "Expand Track" : "Collapse Track")) {
                        if (collapsed) {
                            session.collapsedAnimationTracks.erase(trackKey);
                        }
                        else {
                            session.collapsedAnimationTracks.insert(trackKey);
                        }
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::Dummy(ImVec2(fullWidth, fullHeight));
            ImGui::EndChild();
        }

        void DrawAnimationPanel(UiEditorSession& session) {
            EnsureScreen(session);
            SyncAnimationSelection(session);

            if (!ImGui::Begin("UI Animation###GameUIEditorAnimation")) {
                ImGui::End();
                return;
            }

            TrackWindowInputCapture(session);

            if (!session.screen) {
                ImGui::TextDisabled("No UI screen loaded.");
                ImGui::End();
                return;
            }

            auto& clips = session.screen->GetAnimationClips();
            UIElement* selectedElement = GetSelectedElement(session);
            UIAnimationClip* selectedClip = GetSelectedAnimationClip(session);
            ImGui::Columns(2, "AnimationEditorColumns", true);
            ImGui::SetColumnWidth(0, 280.0f);

            ImGui::Text("Animation Clips");
            if (ImGui::Button("New Clip")) {
                StopAnimationPreview(session, true);
                UIAnimationClip clip{};
                clip.name = MakeUniqueAnimationClipName(*session.screen, "Animation");
                clip.duration = 0.6f;
                clip.loopCount = 1;
                clip.playOnShow = true;
                session.screen->AddAnimationClip(std::move(clip));
                session.selectedAnimationClipIndex = static_cast<int>(clips.size()) - 1;
                session.selectedAnimationTrackIndex = -1;
                session.selectedAnimationKeyframeIndex = -1;
                session.animationPreviewTime = 0.0f;
                selectedClip = GetSelectedAnimationClip(session);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) {
                if (selectedClip && session.screen->RemoveAnimationClip(static_cast<std::size_t>(session.selectedAnimationClipIndex))) {
                    StopAnimationPreview(session, true);
                    session.selectedAnimationTrackIndex = -1;
                    session.selectedAnimationKeyframeIndex = -1;
                    SyncAnimationSelection(session);
                    selectedClip = GetSelectedAnimationClip(session);
                }
            }

            if (ImGui::BeginListBox("##AnimationClipList", ImVec2(-FLT_MIN, 120.0f))) {
                for (std::size_t index = 0; index < clips.size(); ++index) {
                    const bool isSelected = session.selectedAnimationClipIndex == static_cast<int>(index);
                    if (ImGui::Selectable(clips[index].name.c_str(), isSelected)) {
                        StopAnimationPreview(session, true);
                        session.selectedAnimationClipIndex = static_cast<int>(index);
                        session.selectedAnimationTrackIndex = clips[index].tracks.empty() ? -1 : 0;
                        session.selectedAnimationKeyframeIndex = -1;
                        session.animationPreviewTime = 0.0f;
                        selectedClip = &clips[index];
                    }
                }
                ImGui::EndListBox();
            }

            ImGui::SeparatorText("Presets");
            ImGui::Combo("##AnimationPreset", &session.selectedAnimationPresetIndex, GetAnimationPresetLabels().data(), static_cast<int>(GetAnimationPresetLabels().size()));
            if (selectedElement) {
                if (ImGui::Button("Create Preset")) {
                    StopAnimationPreview(session, true);
                    UIAnimationClip presetClip = CreateAnimationPresetClip(
                        *selectedElement,
                        static_cast<AnimationPreset>(session.selectedAnimationPresetIndex));
                    presetClip.name = MakeUniqueAnimationClipName(*session.screen, presetClip.name);
                    session.screen->AddAnimationClip(std::move(presetClip));
                    session.selectedAnimationClipIndex = static_cast<int>(clips.size()) - 1;
                    session.selectedAnimationTrackIndex = 0;
                    session.selectedAnimationKeyframeIndex = 0;
                    session.animationPreviewTime = 0.0f;
                    selectedClip = GetSelectedAnimationClip(session);
                }
            }
            else {
                ImGui::TextDisabled("Select an element to generate presets.");
            }

            if (selectedClip) {
                ImGui::SeparatorText("Selected Clip");
                ImGui::InputText("Name", &selectedClip->name);
                ImGui::DragFloat("Duration", &selectedClip->duration, 0.01f, 0.0f, 30.0f, "%.2f s");
                selectedClip->duration = std::max(0.0f, selectedClip->duration);
                bool loopForever = selectedClip->loopCount == 0;
                if (ImGui::Checkbox("Loop", &loopForever)) {
                    selectedClip->loopCount = loopForever ? 0 : std::max(1, selectedClip->loopCount);
                }
                if (!loopForever) {
                    ImGui::DragInt("Loop Count", &selectedClip->loopCount, 1.0f, 1, 99);
                }
                ImGui::Checkbox("Play On Show", &selectedClip->playOnShow);

                const auto& propertyLabels = GetAnimationPropertyLabels();
                const auto& propertyValues = GetAnimationPropertyValues();
                ImGui::SeparatorText("Tracks");
                ImGui::Combo("Track Property", &session.newAnimationTrackPropertyIndex, propertyLabels.data(), static_cast<int>(propertyLabels.size()));
                if (selectedElement) {
                    if (ImGui::Button("Add Track For Selected Element")) {
                        StopAnimationPreview(session, true);
                        UIAnimationTrack track{};
                        track.targetElementId = selectedElement->GetId();
                        track.property = propertyValues[static_cast<std::size_t>(session.newAnimationTrackPropertyIndex)];
                        selectedClip->tracks.push_back(std::move(track));
                        session.selectedAnimationTrackIndex = static_cast<int>(selectedClip->tracks.size()) - 1;
                        session.selectedAnimationKeyframeIndex = -1;
                    }
                }
                else {
                    ImGui::TextDisabled("Select an element to add a track.");
                }

                UIAnimationTrack* selectedTrack = GetSelectedAnimationTrack(session);
                if (selectedTrack) {
                    if (const UIElement* trackElement = session.screen->FindById(selectedTrack->targetElementId)) {
                        ImGui::TextWrapped("Track Target: %s", trackElement->GetName().c_str());
                    }
                    int selectedTrackPropertyIndex = 0;
                    for (std::size_t propertyIndex = 0; propertyIndex < propertyValues.size(); ++propertyIndex) {
                        if (selectedTrack->property == propertyValues[propertyIndex]) {
                            selectedTrackPropertyIndex = static_cast<int>(propertyIndex);
                            break;
                        }
                    }
                    if (ImGui::Combo("Selected Track Property", &selectedTrackPropertyIndex, propertyLabels.data(), static_cast<int>(propertyLabels.size()))) {
                        StopAnimationPreview(session, true);
                        selectedTrack->property = propertyValues[static_cast<std::size_t>(selectedTrackPropertyIndex)];
                        if (UIElement* targetElement = session.screen->FindById(selectedTrack->targetElementId)) {
                            const UIValue capturedValue = CaptureAnimatedPropertyValue(*targetElement, selectedTrack->property);
                            for (UIKeyframe& keyframe : selectedTrack->keyframes) {
                                keyframe.value = capturedValue;
                            }
                        }
                        ScrubAnimationPreview(session, session.animationPreviewTime);
                    }
                }
            }

            ImGui::NextColumn();

            if (!selectedClip) {
                ImGui::TextDisabled("No animation clip selected.");
                ImGui::Columns(1);
                ImGui::End();
                return;
            }

            const bool isPlaying = session.isAnimationPreviewPlaying;
            ImGui::Text("Timeline");
            if (ImGui::Button(isPlaying ? "Restart" : "Play Preview")) {
                StartAnimationPreview(session);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop Preview")) {
                StopAnimationPreview(session, true);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderFloat("Zoom", &session.animationTimelineZoom, 0.5f, 4.0f, "%.2fx");
            ImGui::SameLine();
            ImGui::TextDisabled("%d key(s) selected", static_cast<int>(session.selectedAnimationKeys.size()));

            float previewTime = std::clamp(session.animationPreviewTime, 0.0f, std::max(0.0f, selectedClip->duration));
            const float sliderMax = std::max(0.01f, selectedClip->duration);
            if (ImGui::SliderFloat("Current Time", &previewTime, 0.0f, sliderMax, "%.2f s")) {
                StopAnimationPreview(session, true);
                ScrubAnimationPreview(session, previewTime);
            }

            DrawAnimationTimeline(session, *selectedClip);

            UIAnimationTrack* selectedTrack = GetSelectedAnimationTrack(session);
            if (session.wantsKeyboardCapture &&
                !ImGui::GetIO().WantTextInput &&
                ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
                !session.selectedAnimationKeys.empty()) {
                DeleteSelectedAnimationKeys(session, *selectedClip);
                selectedTrack = GetSelectedAnimationTrack(session);
            }

            if (selectedTrack) {
                ImGui::SeparatorText("Keyframe Tools");
                if (ImGui::Button("Add Key At Current Time")) {
                    AddAnimationKeyframeAtTime(session, *selectedClip, *selectedTrack, session.animationPreviewTime, true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Selected Keyframes")) {
                    DeleteSelectedAnimationKeys(session, *selectedClip);
                    selectedTrack = GetSelectedAnimationTrack(session);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Selected Track")) {
                    StopAnimationPreview(session, true);
                    selectedClip->tracks.erase(selectedClip->tracks.begin() + session.selectedAnimationTrackIndex);
                    SyncAnimationSelection(session);
                    selectedTrack = GetSelectedAnimationTrack(session);
                }
            }

            UIKeyframe* selectedKeyframe = GetSelectedAnimationKeyframe(session);
            if (selectedTrack && selectedKeyframe) {
                ImGui::SeparatorText("Selected Keyframe");
                float keyframeTime = selectedKeyframe->time;
                if (ImGui::DragFloat("Time", &keyframeTime, 0.01f, 0.0f, std::max(0.0f, selectedClip->duration), "%.2f s")) {
                    selectedKeyframe->time = std::clamp(keyframeTime, 0.0f, std::max(0.0f, selectedClip->duration));
                    SortTrackKeyframes(*selectedTrack);
                    SyncAnimationSelection(session);
                    ScrubAnimationPreview(session, session.animationPreviewTime);
                }

                int easingIndex = 0;
                const auto& easingValues = GetAnimationEasingValues();
                const auto& easingLabels = GetAnimationEasingLabels();
                for (std::size_t easingValueIndex = 0; easingValueIndex < easingValues.size(); ++easingValueIndex) {
                    if (selectedKeyframe->easing == easingValues[easingValueIndex]) {
                        easingIndex = static_cast<int>(easingValueIndex);
                        break;
                    }
                }
                if (ImGui::Combo("Easing", &easingIndex, easingLabels.data(), static_cast<int>(easingLabels.size()))) {
                    selectedKeyframe->easing = easingValues[static_cast<std::size_t>(easingIndex)];
                    ScrubAnimationPreview(session, session.animationPreviewTime);
                }

                if (DrawAnimationValueEditor("Value", selectedTrack->property, selectedKeyframe->value)) {
                    ScrubAnimationPreview(session, session.animationPreviewTime);
                }

                if (ImGui::Button("Delete Selected Keyframe")) {
                    DeleteSelectedAnimationKeys(session, *selectedClip);
                    selectedTrack = GetSelectedAnimationTrack(session);
                }
            }

            ImGui::Columns(1);
            ImGui::End();
        }

        void DrawTextInspector(UIText& text) {
            ImGui::SeparatorText("Text");
            ImGui::InputTextMultiline("Content", &text.text, ImVec2(-1.0f, 100.0f));
            static const std::array<const char*, 3> alignmentOptions = { "Left", "Center", "Right" };
            int currentAlignment = 0;
            for (std::size_t index = 0; index < alignmentOptions.size(); ++index) {
                if (text.alignment == alignmentOptions[index]) {
                    currentAlignment = static_cast<int>(index);
                    break;
                }
            }
            if (ImGui::Combo("Alignment", &currentAlignment, alignmentOptions.data(), static_cast<int>(alignmentOptions.size()))) {
                text.alignment = alignmentOptions[static_cast<std::size_t>(currentAlignment)];
            }
            ImGui::Checkbox("Wrap Text", &text.wrapText);
        }

        void DrawImageInspector(UIImage& image) {
            ImGui::SeparatorText("Image");
            DrawAssetPathCombo("Image Path", image.imagePath, GetTextureAssetPaths());
            image.style.texturePath = image.imagePath;
            ImGui::Checkbox("Preserve Aspect Ratio", &image.preserveAspectRatio);
        }

        void DrawButtonInspector(UIScreen& screen, UIButton& button) {
            ImGui::SeparatorText("Button");
            ImGui::InputText("Label", &button.label);
            if (!button.style.presetName.empty()) {
                ImGui::Checkbox("Use Preset Transition Style", &button.usePresetTransitionStyle);
            }

            const auto theme = LoadScreenTheme(screen);
            const UIStylePreset* preset = (theme != nullptr && !button.style.presetName.empty())
                ? FindStylePreset(*theme, button.style.presetName)
                : nullptr;
            const bool usingPresetButtonStyle =
                button.usePresetTransitionStyle &&
                preset != nullptr &&
                preset->buttonStyle.enabled;
            const ResolvedUIButtonStyle resolvedButtonStyle = ResolveButtonStyle(button, button, theme.get());

            static const std::array<const char*, 4> transitionModeLabels = {
                "None",
                "ColorTint",
                "Scale",
                "Animation"
            };
            int transitionModeIndex = static_cast<int>(usingPresetButtonStyle ? resolvedButtonStyle.transitionMode : button.transitionMode);
            ImGui::BeginDisabled(usingPresetButtonStyle);
            if (ImGui::Combo("Transition Mode", &transitionModeIndex, transitionModeLabels.data(), static_cast<int>(transitionModeLabels.size()))) {
                button.usePresetTransitionStyle = false;
                button.transitionMode = static_cast<UIButtonTransitionMode>(transitionModeIndex);
            }

            if (DrawColorEditor("Normal Color", button.normalColor)) {
                button.usePresetTransitionStyle = false;
                button.style.backgroundColor = button.normalColor;
            }
            if (DrawColorEditor("Hover Color", button.hoverColor)) {
                button.usePresetTransitionStyle = false;
            }
            if (DrawColorEditor("Pressed Color", button.pressedColor)) {
                button.usePresetTransitionStyle = false;
            }
            if (DrawColorEditor("Disabled Color", button.disabledColor)) {
                button.usePresetTransitionStyle = false;
            }
            if (DrawFloatInputAndSlider("Normal Scale", button.normalScale, 0.01f, 0.5f, 2.0f, "%.2f")) {
                button.usePresetTransitionStyle = false;
            }
            if (DrawFloatInputAndSlider("Hover Scale", button.hoverScale, 0.01f, 0.5f, 2.0f, "%.2f")) {
                button.usePresetTransitionStyle = false;
            }
            if (DrawFloatInputAndSlider("Pressed Scale", button.pressedScale, 0.01f, 0.5f, 2.0f, "%.2f")) {
                button.usePresetTransitionStyle = false;
            }
            if (DrawFloatInputAndSlider("Transition Duration", button.transitionDuration, 0.01f, 0.01f, 1.0f, "%.2f s")) {
                button.usePresetTransitionStyle = false;
            }
            ImGui::EndDisabled();
            ImGui::InputText("onClick", &button.events.onClick);
        }

        void DrawSliderInspector(UISlider& slider) {
            ImGui::SeparatorText("Slider");
            DrawFloatInputAndSlider("Min Value", slider.minValue, 0.1f, -1000.0f, 1000.0f, "%.2f");
            DrawFloatInputAndSlider("Max Value", slider.maxValue, 0.1f, -1000.0f, 1000.0f, "%.2f");
            DrawFloatInputAndSlider("Value", slider.value, 0.1f, slider.minValue, std::max(slider.minValue, slider.maxValue), "%.2f");
            ImGui::Checkbox("Whole Numbers", &slider.wholeNumbers);
            DrawColorEditor("Fill Color", slider.fillColor);
            DrawColorEditor("Handle Color", slider.handleColor);
            ImGui::InputText("onValueChanged", &slider.events.onValueChanged);
            if (slider.maxValue < slider.minValue) {
                std::swap(slider.minValue, slider.maxValue);
            }
            slider.value = std::clamp(slider.value, slider.minValue, slider.maxValue);
            if (slider.wholeNumbers) {
                slider.value = std::round(slider.value);
            }
        }

        void DrawToggleInspector(UIToggle& toggle) {
            ImGui::SeparatorText("Toggle");
            ImGui::InputText("Label", &toggle.label);
            ImGui::Checkbox("Is On", &toggle.isOn);
            DrawColorEditor("On Color", toggle.onColor);
            DrawColorEditor("Off Color", toggle.offColor);
            DrawColorEditor("Knob Color", toggle.knobColor);
            ImGui::InputText("onValueChanged", &toggle.events.onValueChanged);
        }

        void DrawProgressBarInspector(UIProgressBar& progressBar) {
            ImGui::SeparatorText("Progress Bar");
            DrawFloatInputAndSlider("Min Value", progressBar.minValue, 0.1f, -1000.0f, 1000.0f, "%.2f");
            DrawFloatInputAndSlider("Max Value", progressBar.maxValue, 0.1f, -1000.0f, 1000.0f, "%.2f");
            DrawFloatInputAndSlider("Value", progressBar.value, 0.1f, progressBar.minValue, std::max(progressBar.minValue, progressBar.maxValue), "%.2f");
            ImGui::Checkbox("Show Percentage", &progressBar.showPercentage);
            DrawColorEditor("Fill Color", progressBar.fillColor);
            if (progressBar.maxValue < progressBar.minValue) {
                std::swap(progressBar.minValue, progressBar.maxValue);
            }
            progressBar.value = std::clamp(progressBar.value, progressBar.minValue, progressBar.maxValue);
        }

        void DrawRadialProgressBarInspector(UIRadialProgressBar& radialProgressBar) {
            ImGui::SeparatorText("Radial Progress Bar");
            DrawFloatInputAndSlider("Min Value", radialProgressBar.minValue, 0.1f, -1000.0f, 1000.0f, "%.2f");
            DrawFloatInputAndSlider("Max Value", radialProgressBar.maxValue, 0.1f, -1000.0f, 1000.0f, "%.2f");
            DrawFloatInputAndSlider("Value", radialProgressBar.value, 0.1f, radialProgressBar.minValue, std::max(radialProgressBar.minValue, radialProgressBar.maxValue), "%.2f");
            ImGui::Checkbox("Show Percentage", &radialProgressBar.showPercentage);
            DrawColorEditor("Background Fill Color", radialProgressBar.backgroundFillColor);
            DrawColorEditor("Fill Color", radialProgressBar.fillColor);
            DrawFloatInputAndSlider("Start Angle", radialProgressBar.startAngleDegrees, 1.0f, -360.0f, 360.0f, "%.1f deg");
            DrawFloatInputAndSlider("Sweep Angle", radialProgressBar.sweepAngleDegrees, 1.0f, 0.0f, 360.0f, "%.1f deg");
            DrawFloatInputAndSlider("Outer Radius Ratio", radialProgressBar.outerRadiusRatio, 0.01f, 0.05f, 1.0f, "%.2f");
            DrawFloatInputAndSlider("Inner Radius Ratio", radialProgressBar.innerRadiusRatio, 0.01f, 0.05f, 0.98f, "%.2f");
            ImGui::Checkbox("Clockwise", &radialProgressBar.clockwise);
            DrawAssetPathCombo("Background Image", radialProgressBar.backgroundImagePath, GetTextureAssetPaths(), "<None>");
            ImGui::Checkbox("Tint Background Image", &radialProgressBar.tintBackgroundImage);
            DrawAssetPathCombo("Fill Image", radialProgressBar.fillImagePath, GetTextureAssetPaths(), "<None>");
            ImGui::Checkbox("Tint Fill Image", &radialProgressBar.tintFillImage);
            if (radialProgressBar.maxValue < radialProgressBar.minValue) {
                std::swap(radialProgressBar.minValue, radialProgressBar.maxValue);
            }
            radialProgressBar.value = std::clamp(radialProgressBar.value, radialProgressBar.minValue, radialProgressBar.maxValue);
            radialProgressBar.outerRadiusRatio = std::clamp(radialProgressBar.outerRadiusRatio, 0.05f, 1.0f);
            radialProgressBar.innerRadiusRatio = std::clamp(radialProgressBar.innerRadiusRatio, 0.05f, 0.98f);
            radialProgressBar.sweepAngleDegrees = std::clamp(radialProgressBar.sweepAngleDegrees, 0.0f, 360.0f);
        }

        void DrawInputFieldInspector(UIInputField& inputField) {
            ImGui::SeparatorText("Input Field");
            ImGui::InputText("Text", &inputField.text);
            ImGui::InputText("Placeholder", &inputField.placeholder);
            ImGui::Checkbox("Read Only", &inputField.readOnly);
            ImGui::Checkbox("Password", &inputField.password);
            ImGui::InputText("onValueChanged", &inputField.events.onValueChanged);
        }

        std::unique_ptr<UIElement> CreateElementForEditor(UIScreen& screen, UIElementType type, const UIElement* parent) {
            const std::string name = MakeReadableName(screen, type);

            switch (type) {
            case UIElementType::Panel:
                return std::make_unique<UIPanel>(name);
            case UIElementType::Image:
                return std::make_unique<UIImage>(name);
            case UIElementType::Text:
                return std::make_unique<UIText>(name, "New Text");
            case UIElementType::Button:
                return std::make_unique<UIButton>(name, "Button");
            case UIElementType::Slider:
                return std::make_unique<UISlider>(name);
            case UIElementType::Toggle:
                return std::make_unique<UIToggle>(name, "Toggle");
            case UIElementType::ProgressBar:
                return std::make_unique<UIProgressBar>(name);
            case UIElementType::RadialProgressBar:
                return std::make_unique<UIRadialProgressBar>(name);
            case UIElementType::InputField:
                return std::make_unique<UIInputField>(name);
            default:
                return std::make_unique<UIElement>(type, name);
            }
        }

        bool AddElementToSelection(UiEditorSession& session, UIElementType type) {
            EnsureScreen(session);
            if (!session.screen) {
                return false;
            }

            UIElement* parent = GetSelectedElement(session);
            if (!parent) {
                parent = session.screen->GetRootCanvas();
            }
            if (!parent) {
                return false;
            }

            auto newElement = CreateElementForEditor(*session.screen, type, parent);
            if (!newElement) {
                return false;
            }

            UIElement& elementRef = *newElement;
            InitializeNewElement(elementRef, type, parent);
            parent->AddChild(std::move(newElement));
            session.selectedElementId = elementRef.GetId();
            return true;
        }

        bool DeleteElementById(UiEditorSession& session, UIElementId elementId) {
            if (!session.screen || elementId == 0) {
                return false;
            }

            UIElement* canvas = session.screen->GetRootCanvas();
            if (!canvas || canvas->GetId() == elementId) {
                return false;
            }

            UIElement* element = session.screen->FindById(elementId);
            UIElement* parent = element ? element->GetParent() : nullptr;
            if (!parent) {
                return false;
            }

            const UIElementId fallbackSelectionId = parent->GetId();
            if (!canvas->RemoveChild(elementId)) {
                return false;
            }

            if (session.selectedElementId == elementId) {
                session.selectedElementId = fallbackSelectionId;
            }
            if (session.renameElementId == elementId) {
                session.renameElementId = 0;
                session.renameBuffer[0] = '\0';
            }
            if (session.draggedElementId == elementId) {
                session.draggedElementId = 0;
                session.isDraggingElement = false;
            }
            if (session.resizedElementId == elementId) {
                session.resizedElementId = 0;
                session.isResizingElement = false;
                session.activeResizeHandle = ResizeHandle::None;
            }

            return true;
        }

        void StartRename(UiEditorSession& session, const UIElement& element) {
            session.renameElementId = element.GetId();
            std::memset(session.renameBuffer, 0, sizeof(session.renameBuffer));
            std::snprintf(session.renameBuffer, sizeof(session.renameBuffer), "%s", element.GetName().c_str());
            ImGui::OpenPopup("Rename UI Element");
        }

        void DrawRenamePopup(UiEditorSession& session) {
            if (ImGui::BeginPopupModal("Rename UI Element", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                TrackWindowInputCapture(session);
                ImGui::TextUnformatted("Rename selected UI element");
                ImGui::Separator();
                ImGui::SetNextItemWidth(280.0f);
                ImGui::InputText("##RenameUiElement", session.renameBuffer, sizeof(session.renameBuffer));

                if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
                    if (session.screen && session.renameElementId != 0) {
                        if (UIElement* element = session.screen->FindById(session.renameElementId)) {
                            if (std::strlen(session.renameBuffer) > 0) {
                                element->SetName(session.renameBuffer);
                                EngineUi::ShowToast("[ UI Element Renamed ]");
                            }
                        }
                    }
                    session.renameElementId = 0;
                    session.renameBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                    session.renameElementId = 0;
                    session.renameBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        void DrawHierarchyNode(UIElement& element, UiEditorSession& session) {
            ImGui::PushID(static_cast<int>(element.GetId()));

            bool isVisible = element.visible;
            if (ImGui::Checkbox("##Visible", &isVisible)) {
                element.visible = isVisible;
            }

            ImGui::SameLine();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (element.GetChildren().empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (session.selectedElementId == element.GetId()) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (element.GetChildren().empty()) {
                flags |= ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }

            const std::string label = std::format("{} ({})###ui-hierarchy-{}", element.GetName(), ToString(element.GetType()), element.GetId());
            const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked()) {
                session.selectedElementId = element.GetId();
            }

            if (ImGui::BeginPopupContextItem("UIHierarchyItemContext")) {
                session.selectedElementId = element.GetId();

                if (ImGui::MenuItem("Rename")) {
                    StartRename(session, element);
                }
                ImGui::Separator();
                const bool canReorder = element.GetParent() != nullptr;
                if (ImGui::MenuItem("Bring To Front", nullptr, false, canReorder)) {
                    ReorderElementLayer(element, LayerMoveDirection::ToFront);
                }
                if (ImGui::MenuItem("Send To Back", nullptr, false, canReorder)) {
                    ReorderElementLayer(element, LayerMoveDirection::ToBack);
                }
                if (ImGui::MenuItem("Move Forward", nullptr, false, canReorder)) {
                    ReorderElementLayer(element, LayerMoveDirection::Forward);
                }
                if (ImGui::MenuItem("Move Backward", nullptr, false, canReorder)) {
                    ReorderElementLayer(element, LayerMoveDirection::Backward);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy", nullptr, false, false)) {
                    EngineUi::ShowToast("[ Copy Placeholder ]");
                }

                const bool canDelete = !IsRootCanvas(session, element);
                if (ImGui::MenuItem("Delete", nullptr, false, canDelete)) {
                    session.pendingDeleteElementId = element.GetId();
                }
                if (!canDelete) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Canvas cannot be deleted");
                }

                ImGui::EndPopup();
            }

            if (opened && !element.GetChildren().empty()) {
                for (UIElement* child : GetChildrenSortedForDrawMutable(element)) {
                    DrawHierarchyNode(*child, session);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        void BuildDockLayout(ImGuiID dockspaceId) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

            ImGuiID centerId = dockspaceId;
            ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.22f, nullptr, &centerId);
            ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.24f, nullptr, &centerId);
            ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.25f, nullptr, &centerId);
            ImGuiID animationId = ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.45f, nullptr, &rightId);

            ImGui::DockBuilderDockWindow("UI Components###GameUIEditorComponents", leftId);
            ImGui::DockBuilderDockWindow("UI Canvas###GameUIEditorCanvas", centerId);
            ImGui::DockBuilderDockWindow("UI Hierarchy###GameUIEditorHierarchy", bottomId);
            ImGui::DockBuilderDockWindow("UI Inspector###GameUIEditorInspector", rightId);
            ImGui::DockBuilderDockWindow("UI Animation###GameUIEditorAnimation", animationId);
            ImGui::DockBuilderFinish(dockspaceId);
        }

        void DrawToolbar(UiEditorSession& session) {
            const auto& labels = GetResolutionLabels();

            if (ImGui::Button("New")) {
                StopAnimationPreview(session, true);
                session.screen = std::make_unique<UIScreen>("Untitled UI", ResolutionFromIndex(session.selectedResolution));
                session.currentPath.clear();
                session.selectedElementId = session.screen->GetRootCanvas() ? session.screen->GetRootCanvas()->GetId() : 0;
                session.draggedElementId = 0;
                session.resizedElementId = 0;
                session.isDraggingElement = false;
                session.isResizingElement = false;
                session.activeResizeHandle = ResizeHandle::None;
                session.selectedAnimationClipIndex = -1;
                session.selectedAnimationTrackIndex = -1;
                session.selectedAnimationKeyframeIndex = -1;
                session.animationPreviewTime = 0.0f;
                SetFileDialogPath(session, std::filesystem::path("Assets") / "ui");
                SyncAnimationSelection(session);
                ResetHistory(session);
                EngineUi::ShowToast("[ Game UI New ]");
            }

            ImGui::SameLine();
            if (ImGui::Button("Open")) {
                OpenFileDialog(session, UiFileDialogMode::Open, session.currentPath.empty()
                    ? std::filesystem::path("Assets") / "ui"
                    : session.currentPath);
            }

            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                if (!session.currentPath.empty()) {
                    if (SaveCurrentScreen(session, session.currentPath)) {
                        EngineUi::ShowToast("[ Game UI Saved ]");
                    }
                    else {
                        EngineUi::ShowToast("[ Game UI Save Failed ]");
                    }
                }
                else {
                    OpenFileDialog(session, UiFileDialogMode::SaveAs, std::filesystem::path("Assets") / "ui" / "Untitled.ui.json");
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Save As")) {
                OpenFileDialog(session, UiFileDialogMode::SaveAs, session.currentPath.empty()
                    ? std::filesystem::path("Assets") / "ui" / "Untitled.ui.json"
                    : session.currentPath);
            }

            ImGui::SameLine();
            if (ImGui::Button("Compile")) {
                if (CompileCurrentScreen(session)) {
                    EngineUi::ShowToast("[ UI Compile Placeholder ]");
                }
                else {
                    EngineUi::ShowToast("[ UI Compile Failed ]");
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Preview")) {
                if (session.previewMode) {
                    StopAnimationPreview(session, true);
                }
                session.previewMode = !session.previewMode;
                session.isDraggingElement = false;
                session.isResizingElement = false;
                session.draggedElementId = 0;
                session.resizedElementId = 0;
                session.activeResizeHandle = ResizeHandle::None;
                ResetPreviewInteraction(session);
                EngineUi::ShowToast(session.previewMode ? "[ UI Preview Enabled ]" : "[ UI Preview Disabled ]");
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh Assets")) {
                RefreshAssetPathCaches();
                EngineUi::ShowToast("[ UI Assets Refreshed ]");
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::BeginCombo("Resolution", labels[session.selectedResolution])) {
                for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
                    const bool isSelected = session.selectedResolution == i;
                    if (ImGui::Selectable(labels[i], isSelected)) {
                        session.selectedResolution = i;
                        if (session.screen) {
                            const glm::vec2 resolution = ResolutionFromIndex(i);
                            session.screen->SetReferenceResolution(resolution);
                            if (UIElement* canvas = session.screen->GetRootCanvas()) {
                                canvas->transform.size = resolution;
                            }
                        }
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderFloat("Zoom", &session.zoomPercent, 25.0f, 300.0f, "%.0f%%");

            ImGui::SameLine();
            ImGui::Checkbox("Grid", &session.showGrid);

            ImGui::SameLine();
            ImGui::Checkbox("Snap", &session.enableSnapping);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::DragFloat("Grid Size", &session.gridSize, 1.0f, 1.0f, 200.0f, "%.0f px");
        }

        void DrawFileDialog(UiEditorSession& session) {
            if (session.fileDialogMode == UiFileDialogMode::None) {
                return;
            }

            const char* popupTitle = session.fileDialogMode == UiFileDialogMode::Open ? "Open UI File" : "Save UI File";
            if (!ImGui::BeginPopupModal(popupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }

            TrackWindowInputCapture(session);

            ImGui::TextUnformatted(session.fileDialogMode == UiFileDialogMode::Open
                ? "Open a UI file from Assets/ui"
                : "Choose a destination inside Assets/ui");
            ImGui::Separator();
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("Path", session.filePathBuffer, sizeof(session.filePathBuffer));

            const auto uiFiles = GetAvailableUiFiles();
            if (!uiFiles.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Available Files");
                ImGui::BeginChild("UiFileList", ImVec2(420.0f, 180.0f), true);
                for (const auto& file : uiFiles) {
                    const std::string label = file.generic_string();
                    if (ImGui::Selectable(label.c_str())) {
                        SetFileDialogPath(session, file);
                    }
                }
                ImGui::EndChild();
            }

            const bool confirm = ImGui::Button(session.fileDialogMode == UiFileDialogMode::Open ? "Open" : "Save", ImVec2(100.0f, 0.0f));
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(100.0f, 0.0f));

            if (confirm) {
                const std::filesystem::path requestedPath = session.filePathBuffer;
                const bool success = session.fileDialogMode == UiFileDialogMode::Open
                    ? LoadScreenFromPath(session, requestedPath)
                    : SaveCurrentScreen(session, requestedPath);

                if (success) {
                    EngineUi::ShowToast(session.fileDialogMode == UiFileDialogMode::Open
                        ? "[ Game UI Opened ]"
                        : "[ Game UI Saved ]");
                    session.fileDialogMode = UiFileDialogMode::None;
                    ImGui::CloseCurrentPopup();
                }
                else {
                    EngineUi::ShowToast(session.fileDialogMode == UiFileDialogMode::Open
                        ? "[ Game UI Open Failed ]"
                        : "[ Game UI Save Failed ]");
                }
            }

            if (cancel) {
                session.fileDialogMode = UiFileDialogMode::None;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void DrawComponentsPanel(UiEditorSession& session) {
            EnsureScreen(session);

            if (!ImGui::Begin("UI Components###GameUIEditorComponents")) {
                ImGui::End();
                return;
            }

            TrackWindowInputCapture(session);

            ImGui::TextUnformatted("Components");
            ImGui::Separator();
            struct ComponentButton {
                const char* label;
                UIElementType type;
            };

            static const std::array<ComponentButton, 10> buttons = {{
                { "Panel", UIElementType::Panel },
                { "Image", UIElementType::Image },
                { "Text", UIElementType::Text },
                { "Button", UIElementType::Button },
                { "Slider", UIElementType::Slider },
                { "Toggle", UIElementType::Toggle },
                { "Progress Bar", UIElementType::ProgressBar },
                { "Radial Progress Bar", UIElementType::RadialProgressBar },
                { "Scroll View", UIElementType::ScrollView },
                { "Input Field", UIElementType::InputField }
            }};

            for (const auto& button : buttons) {
                if (ImGui::Button(button.label, ImVec2(-1.0f, 0.0f))) {
                    if (AddElementToSelection(session, button.type)) {
                        EngineUi::ShowToast(std::format("[ Added {} ]", button.label));
                    }
                    else {
                        EngineUi::ShowToast("[ Failed To Add UI Element ]");
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            UIElement* selected = GetSelectedElement(session);
            ImGui::TextUnformatted("Selected Parent");
            ImGui::TextWrapped("%s", selected ? selected->GetName().c_str() : "Canvas");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted("Current Asset");
            ImGui::TextWrapped("%s", CurrentPathLabel(session).c_str());
            ImGui::End();
        }

        void DrawCanvasPanel(UiEditorSession& session) {
            EnsureScreen(session);

            if (!ImGui::Begin("UI Canvas###GameUIEditorCanvas")) {
                ImGui::End();
                return;
            }

            TrackWindowInputCapture(session);

            const glm::vec2 referenceResolution = session.screen ? session.screen->GetReferenceResolution() : glm::vec2(1920.0f, 1080.0f);
            const ImVec2 avail = ImGui::GetContentRegionAvail();

            ImGui::Text("Screen: %s", session.screen ? session.screen->GetName().c_str() : "None");
            ImGui::Text("Reference Resolution: %.0f x %.0f", referenceResolution.x, referenceResolution.y);
            ImGui::Text("Zoom: %.0f%%", session.zoomPercent);
            ImGui::Separator();

            const ImVec2 previewRegion(
                std::max(240.0f, avail.x - 24.0f),
                std::max(180.0f, avail.y - 16.0f)
            );
            ImGui::InvisibleButton("GameUiCanvasPlaceholder", previewRegion);
            const ImVec2 regionMin = ImGui::GetItemRectMin();
            const ImVec2 regionMax = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(regionMin, regionMax, IM_COL32(28, 31, 39, 255), 10.0f);
            drawList->AddRect(regionMin, regionMax, IM_COL32(92, 100, 118, 255), 10.0f, 0, 1.5f);

            const float fitScale = std::min(
                previewRegion.x / std::max(referenceResolution.x, 1.0f),
                previewRegion.y / std::max(referenceResolution.y, 1.0f)
            );
            const float previewScale = std::max(0.05f, fitScale * (session.zoomPercent / 100.0f));
            const ImVec2 canvasSize(referenceResolution.x * previewScale, referenceResolution.y * previewScale);
            const ImVec2 canvasMin(
                regionMin.x + (previewRegion.x - canvasSize.x) * 0.5f,
                regionMin.y + (previewRegion.y - canvasSize.y) * 0.5f
            );
            const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
            const UIRect rootRect{
                glm::vec2(0.0f, 0.0f),
                referenceResolution,
                0.0f
            };
            UIElement* selectedElement = GetSelectedElement(session);
            UIRect selectedParentRect;
            UIRect selectedElementRect;
            const bool hasSelectedElementRect =
                selectedElement &&
                selectedElement->GetType() != UIElementType::Canvas &&
                session.screen &&
                session.screen->GetRootCanvas() &&
                TryGetElementRects(*session.screen->GetRootCanvas(), rootRect, selectedElement->GetId(), selectedParentRect, selectedElementRect);

            if (session.previewMode) {
                if (ImGui::IsItemHovered()) {
                    const ImVec2 mousePos = ImGui::GetMousePos();
                    if (mousePos.x >= canvasMin.x && mousePos.x <= canvasMax.x &&
                        mousePos.y >= canvasMin.y && mousePos.y <= canvasMax.y) {
                        const glm::vec2 uiPoint(
                            (mousePos.x - canvasMin.x) / previewScale,
                            (mousePos.y - canvasMin.y) / previewScale
                        );
                        const UIElementId hoveredId = HitTestCanvas(session, uiPoint, rootRect);
                        if (hoveredId != 0 && session.screen) {
                            if (const UIElement* hoveredElement = session.screen->FindById(hoveredId)) {
                                session.previewHoveredElementId = IsPreviewInteractiveType(hoveredElement->GetType()) ? hoveredId : 0;
                            }
                            else {
                                session.previewHoveredElementId = 0;
                            }
                        }
                        else {
                            session.previewHoveredElementId = 0;
                        }

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            session.previewPressedElementId = session.previewHoveredElementId;
                        }
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                            if (session.previewPressedElementId != 0 &&
                                session.previewPressedElementId == session.previewHoveredElementId &&
                                session.screen) {
                                if (const UIElement* clickedElement = session.screen->FindById(session.previewPressedElementId)) {
                                    if (const auto* button = dynamic_cast<const UIButton*>(clickedElement)) {
                                        const std::string eventName = button->events.onClick.empty()
                                            ? std::string("<empty>")
                                            : button->events.onClick;
                                        EngineUi::LogPrint("UI 事件触发：{}\n", eventName);
                                    }
                                }
                            }
                            session.previewPressedElementId = 0;
                        }
                    }
                    else {
                        ResetPreviewInteraction(session);
                    }
                }
                else {
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        ResetPreviewInteraction(session);
                    }
                }
            }
            else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const ImVec2 mousePos = ImGui::GetMousePos();
                if (mousePos.x >= canvasMin.x && mousePos.x <= canvasMax.x &&
                    mousePos.y >= canvasMin.y && mousePos.y <= canvasMax.y) {
                    const glm::vec2 uiPoint(
                        (mousePos.x - canvasMin.x) / previewScale,
                        (mousePos.y - canvasMin.y) / previewScale
                    );
                    const ResizeHandle hitHandle = hasSelectedElementRect
                        ? HitTestResizeHandles(
                            *selectedElement,
                            ToCanvasPoint(selectedElementRect.position, canvasMin, previewScale),
                            ImVec2(
                                ToCanvasPoint(selectedElementRect.position, canvasMin, previewScale).x + selectedElementRect.size.x * previewScale,
                                ToCanvasPoint(selectedElementRect.position, canvasMin, previewScale).y + selectedElementRect.size.y * previewScale),
                            mousePos)
                        : ResizeHandle::None;

                    session.isDraggingElement = false;
                    session.draggedElementId = 0;
                    session.isResizingElement = false;
                    session.resizedElementId = 0;
                    session.activeResizeHandle = ResizeHandle::None;

                    if (hitHandle != ResizeHandle::None && hasSelectedElementRect) {
                        session.isResizingElement = true;
                        session.resizedElementId = selectedElement->GetId();
                        session.activeResizeHandle = hitHandle;
                        session.resizeDragStartPoint = uiPoint;
                        session.resizeInitialRect = selectedElementRect;
                        session.resizeInitialParentRect = selectedParentRect;
                        session.resizeInitialTransform = selectedElement->transform;
                    }
                    else {
                        const UIElementId hitElementId = HitTestCanvas(session, uiPoint, rootRect);
                        session.selectedElementId = hitElementId;
                    }

                    if (!session.isResizingElement && session.selectedElementId != 0 && session.screen && session.screen->GetRootCanvas()) {
                        const UIElementId hitElementId = session.selectedElementId;
                        if (UIElement* hitElement = session.screen->FindById(hitElementId)) {
                            if (hitElement->GetType() != UIElementType::Canvas && hitElement->visible) {
                                UIRect parentRect;
                                UIRect elementRect;
                                if (TryGetElementRects(*session.screen->GetRootCanvas(), rootRect, hitElementId, parentRect, elementRect)) {
                                    session.isDraggingElement = true;
                                    session.draggedElementId = hitElementId;
                                    session.dragPointerOffset = uiPoint - elementRect.position;
                                }
                            }
                        }
                    }
                }
                else {
                    session.selectedElementId = 0;
                    session.isDraggingElement = false;
                    session.draggedElementId = 0;
                    session.isResizingElement = false;
                    session.resizedElementId = 0;
                    session.activeResizeHandle = ResizeHandle::None;
                }
            }

            if (!session.previewMode && session.isResizingElement) {
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    session.isResizingElement = false;
                    session.resizedElementId = 0;
                    session.activeResizeHandle = ResizeHandle::None;
                }
                else if (!session.screen || !session.screen->GetRootCanvas()) {
                    session.isResizingElement = false;
                    session.resizedElementId = 0;
                    session.activeResizeHandle = ResizeHandle::None;
                }
                else {
                    UIElement* resizedElement = session.screen->FindById(session.resizedElementId);
                    if (!resizedElement || resizedElement->GetType() == UIElementType::Canvas) {
                        session.isResizingElement = false;
                        session.resizedElementId = 0;
                        session.activeResizeHandle = ResizeHandle::None;
                    }
                    else {
                        const ImVec2 mousePos = ImGui::GetMousePos();
                        const glm::vec2 uiPoint(
                            (mousePos.x - canvasMin.x) / previewScale,
                            (mousePos.y - canvasMin.y) / previewScale
                        );
                        glm::vec2 minPoint = session.resizeInitialRect.position;
                        glm::vec2 maxPoint = session.resizeInitialRect.position + session.resizeInitialRect.size;

                        switch (session.activeResizeHandle) {
                        case ResizeHandle::TopLeft:
                            minPoint.x = uiPoint.x;
                            minPoint.y = uiPoint.y;
                            break;
                        case ResizeHandle::Top:
                            minPoint.y = uiPoint.y;
                            break;
                        case ResizeHandle::TopRight:
                            maxPoint.x = uiPoint.x;
                            minPoint.y = uiPoint.y;
                            break;
                        case ResizeHandle::Right:
                            maxPoint.x = uiPoint.x;
                            break;
                        case ResizeHandle::BottomRight:
                            maxPoint.x = uiPoint.x;
                            maxPoint.y = uiPoint.y;
                            break;
                        case ResizeHandle::Bottom:
                            maxPoint.y = uiPoint.y;
                            break;
                        case ResizeHandle::BottomLeft:
                            minPoint.x = uiPoint.x;
                            maxPoint.y = uiPoint.y;
                            break;
                        case ResizeHandle::Left:
                            minPoint.x = uiPoint.x;
                            break;
                        case ResizeHandle::None:
                            break;
                        }

                        if (session.enableSnapping) {
                            minPoint.x = SnapToGrid(minPoint.x, session.gridSize);
                            minPoint.y = SnapToGrid(minPoint.y, session.gridSize);
                            maxPoint.x = SnapToGrid(maxPoint.x, session.gridSize);
                            maxPoint.y = SnapToGrid(maxPoint.y, session.gridSize);
                        }

                        const float minDimension = 4.0f;
                        if (maxPoint.x - minPoint.x < minDimension) {
                            if (session.activeResizeHandle == ResizeHandle::TopLeft ||
                                session.activeResizeHandle == ResizeHandle::Left ||
                                session.activeResizeHandle == ResizeHandle::BottomLeft) {
                                minPoint.x = maxPoint.x - minDimension;
                            }
                            else {
                                maxPoint.x = minPoint.x + minDimension;
                            }
                        }
                        if (maxPoint.y - minPoint.y < minDimension) {
                            if (session.activeResizeHandle == ResizeHandle::TopLeft ||
                                session.activeResizeHandle == ResizeHandle::Top ||
                                session.activeResizeHandle == ResizeHandle::TopRight) {
                                minPoint.y = maxPoint.y - minDimension;
                            }
                            else {
                                maxPoint.y = minPoint.y + minDimension;
                            }
                        }

                        const glm::vec2 finalSize = maxPoint - minPoint;
                        resizedElement->transform.size = ComputeSerializedSizeFromFinalRect(
                            session.resizeInitialTransform,
                            session.resizeInitialParentRect,
                            finalSize);
                        resizedElement->transform.position = ComputeSerializedPositionFromFinalRect(
                            session.resizeInitialTransform,
                            session.resizeInitialParentRect,
                            minPoint,
                            finalSize);
                    }
                }
            }
            else if (!session.previewMode && session.isDraggingElement) {
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    session.isDraggingElement = false;
                    session.draggedElementId = 0;
                }
                else if (!session.screen || !session.screen->GetRootCanvas()) {
                    session.isDraggingElement = false;
                    session.draggedElementId = 0;
                }
                else {
                    UIElement* draggedElement = session.screen->FindById(session.draggedElementId);
                    if (!draggedElement || draggedElement->GetType() == UIElementType::Canvas) {
                        session.isDraggingElement = false;
                        session.draggedElementId = 0;
                    }
                    else {
                        UIRect parentRect;
                        UIRect elementRect;
                        if (!TryGetElementRects(*session.screen->GetRootCanvas(), rootRect, draggedElement->GetId(), parentRect, elementRect)) {
                            session.isDraggingElement = false;
                            session.draggedElementId = 0;
                        }
                        else {
                            const ImVec2 mousePos = ImGui::GetMousePos();
                            const glm::vec2 uiPoint(
                                (mousePos.x - canvasMin.x) / previewScale,
                                (mousePos.y - canvasMin.y) / previewScale
                            );
                            const glm::vec2 desiredRectPosition = uiPoint - session.dragPointerOffset;
                            glm::vec2 newPosition =
                                desiredRectPosition -
                                (parentRect.position + parentRect.size * draggedElement->transform.anchorMin) +
                                elementRect.size * draggedElement->transform.pivot;

                            if (session.enableSnapping) {
                                newPosition.x = SnapToGrid(newPosition.x, session.gridSize);
                                newPosition.y = SnapToGrid(newPosition.y, session.gridSize);
                            }

                            draggedElement->transform.position = newPosition;
                        }
                    }
                }
            }

            drawList->PushClipRect(regionMin, regionMax, true);
            drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(18, 20, 26, 255), 6.0f);
            if (session.showGrid) {
                DrawCanvasGrid(drawList, canvasMin, canvasMax, previewScale);
            }
            drawList->AddRect(canvasMin, canvasMax, IM_COL32(160, 170, 190, 255), 6.0f, 0, 1.5f);

            if (session.screen && session.screen->GetRootCanvas()) {
                DrawPreviewElement(*session.screen->GetRootCanvas(), rootRect, session, drawList, canvasMin, previewScale);
                UIElement* currentSelectedElement = GetSelectedElement(session);
                if (currentSelectedElement &&
                    !session.previewMode &&
                    currentSelectedElement->GetType() != UIElementType::Canvas &&
                    TryGetElementRects(*session.screen->GetRootCanvas(), rootRect, currentSelectedElement->GetId(), selectedParentRect, selectedElementRect)) {
                    const ImVec2 selectedMin = ToCanvasPoint(selectedElementRect.position, canvasMin, previewScale);
                    const ImVec2 selectedMax(
                        selectedMin.x + selectedElementRect.size.x * previewScale,
                        selectedMin.y + selectedElementRect.size.y * previewScale
                    );
                    DrawResizeHandles(drawList, *currentSelectedElement, selectedMin, selectedMax);
                }
            }
            else {
                const ImVec2 center((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f);
                drawList->AddText(ImVec2(center.x - 56.0f, center.y - 8.0f), IM_COL32(255, 204, 96, 255), "No UI Screen");
            }

            drawList->PopClipRect();

            ImGui::End();
        }

        void DrawInspectorPanel(UiEditorSession& session) {
            EnsureScreen(session);

            if (!ImGui::Begin("UI Inspector###GameUIEditorInspector")) {
                ImGui::End();
                return;
            }

            TrackWindowInputCapture(session);

            UIElement* selected = GetSelectedElement(session);
            if (selected) {
                ImGui::Text("Selected: %s", selected->GetName().c_str());
                ImGui::Text("Type: %s", ToString(selected->GetType()).data());
                ImGui::Text("Children: %d", static_cast<int>(selected->GetChildren().size()));
                DrawBasicInspector(*selected);
                DrawLayerInspector(*selected);
                DrawTransformInspector(*selected);
                DrawStyleInspector(*session.screen, *selected);
                DrawBindingsInspector(*session.screen, *selected);

                if (auto* text = dynamic_cast<UIText*>(selected)) {
                    DrawTextInspector(*text);
                }
                else if (auto* image = dynamic_cast<UIImage*>(selected)) {
                    DrawImageInspector(*image);
                }
                else if (auto* button = dynamic_cast<UIButton*>(selected)) {
                    DrawButtonInspector(*session.screen, *button);
                }
                else if (auto* slider = dynamic_cast<UISlider*>(selected)) {
                    DrawSliderInspector(*slider);
                }
                else if (auto* toggle = dynamic_cast<UIToggle*>(selected)) {
                    DrawToggleInspector(*toggle);
                }
                else if (auto* radialProgressBar = dynamic_cast<UIRadialProgressBar*>(selected)) {
                    DrawRadialProgressBarInspector(*radialProgressBar);
                }
                else if (auto* progressBar = dynamic_cast<UIProgressBar*>(selected)) {
                    DrawProgressBarInspector(*progressBar);
                }
                else if (auto* inputField = dynamic_cast<UIInputField*>(selected)) {
                    DrawInputFieldInspector(*inputField);
                }
            }
            else {
                ImGui::TextDisabled("No UI element selected.");
            }

            if (session.screen && session.screen->GetRootCanvas()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextUnformatted("Screen Theme");
                {
                    std::string themePath = session.screen->GetThemePath();
                    if (DrawAssetPathCombo("Theme File", themePath, GetThemeAssetPaths(), "<No Theme>")) {
                        session.screen->SetThemePath(themePath);
                    }
                }
                ImGui::Text("Root Name: %s", session.screen->GetRootCanvas()->GetName().c_str());
                ImGui::Text("Total Elements: %d", CountDescendants(*session.screen->GetRootCanvas()));
            }

            ImGui::End();
        }

        void DrawHierarchyPanel(UiEditorSession& session) {
            EnsureScreen(session);

            if (!ImGui::Begin("UI Hierarchy###GameUIEditorHierarchy")) {
                ImGui::End();
                return;
            }

            TrackWindowInputCapture(session);

            ImGui::TextUnformatted("Hierarchy");
            ImGui::Separator();
            if (session.screen && session.screen->GetRootCanvas()) {
                DrawHierarchyNode(*session.screen->GetRootCanvas(), session);
            }
            else {
                ImGui::TextDisabled("No UI screen loaded.");
            }

            DrawRenamePopup(session);

            if (session.wantsKeyboardCapture &&
                session.selectedElementId != 0 &&
                !ImGui::GetIO().WantTextInput &&
                ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                UIElement* selectedElement = session.screen ? session.screen->FindById(session.selectedElementId) : nullptr;
                if (selectedElement && !IsRootCanvas(session, *selectedElement)) {
                    session.pendingDeleteElementId = session.selectedElementId;
                }
            }

            if (session.pendingDeleteElementId != 0) {
                const UIElementId elementId = session.pendingDeleteElementId;
                session.pendingDeleteElementId = 0;

                if (DeleteElementById(session, elementId)) {
                    EngineUi::ShowToast("[ UI Element Deleted ]");
                }
                else {
                    EngineUi::ShowToast("[ Delete Blocked ]");
                }
            }

            ImGui::End();
        }

    } // namespace

    void UIEditorWindow::Draw(UserState& state) {
        if (!state.showGameUiEditor) {
            // 窗口关闭时显式释放输入占用，避免其他编辑器窗口被错误拦截。
            UiEditorSession& hiddenSession = GetSession();
            hiddenSession.wantsMouseCapture = false;
            hiddenSession.wantsKeyboardCapture = false;
            return;
        }

        UiEditorSession& session = GetSession();
        EnsureScreen(session);
        // 每帧重新统计输入捕获状态，由各个子窗口在绘制时逐步置位。
        session.wantsMouseCapture = false;
        session.wantsKeyboardCapture = false;

        //ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
		ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;//窗户模式固定，禁止用户调整位置大小，保持界面布局稳定。
        ImGui::SetNextWindowSize(ImVec2(1420.0f, 900.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(120.0f, 80.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Game UI Editor###GameUIEditorRoot", &state.showGameUiEditor, rootFlags)) {
            TrackWindowInputCapture(session);
            ImGui::End();
            return;
        }

        TrackWindowInputCapture(session);
        //DrawRootWindowMoveHandles(session);

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            // Ctrl+S 直接走当前路径保存；没有路径时退化为 Save As。
            if (!session.currentPath.empty()) {
                if (SaveCurrentScreen(session, session.currentPath)) {
                    EngineUi::ShowToast("[ Game UI Saved ]");
                }
                else {
                    EngineUi::ShowToast("[ Game UI Save Failed ]");
                }
            }
            else {
                OpenFileDialog(session, UiFileDialogMode::SaveAs, std::filesystem::path("Assets") / "ui" / "Untitled.ui.json");
            }
        }

        // Undo / Redo 快捷键。
        // 路由到非文本输入：当 ImGui 任一项处于活动状态（用户正在编辑文本框 / 拖拽控件）时
        // 让位给那条交互，避免在输入框里按 Ctrl+Z 触发我们而不是文本撤销。
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl &&
            !ImGui::IsAnyItemActive()) {
            const bool wantsUndo = ImGui::IsKeyPressed(ImGuiKey_Z, false) && !ImGui::GetIO().KeyShift;
            const bool wantsRedo =
                ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                (ImGui::IsKeyPressed(ImGuiKey_Z, false) && ImGui::GetIO().KeyShift);
            if (wantsUndo) {
                if (CanUndo(session)) {
                    PerformUndo(session);
                    EngineUi::ShowToast("[ UI Editor: Undo ]");
                }
                else {
                    EngineUi::ShowToast("[ UI Editor: Nothing to Undo ]");
                }
            }
            else if (wantsRedo) {
                if (CanRedo(session)) {
                    PerformRedo(session);
                    EngineUi::ShowToast("[ UI Editor: Redo ]");
                }
                else {
                    EngineUi::ShowToast("[ UI Editor: Nothing to Redo ]");
                }
            }
        }

        if (session.previewMode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            StopAnimationPreview(session, true);
            session.previewMode = false;
            ResetPreviewInteraction(session);
            EngineUi::ShowToast("[ UI Preview Disabled ]");
        }

        UpdateAnimationPreview(session, ImGui::GetIO().DeltaTime);

        DrawToolbar(session);
        ImGui::Separator();
        DrawFileDialog(session);

        ImGuiID dockspaceId = ImGui::GetID("GameUIEditorDockspace");
        // 第一次打开时自动生成一套编辑器布局，后续沿用 ImGui 的 docking 状态。
        if (!session.layoutInitialized || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
            BuildDockLayout(dockspaceId);
            session.layoutInitialized = true;
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();

        DrawComponentsPanel(session);
        DrawCanvasPanel(session);
        DrawInspectorPanel(session);
        DrawHierarchyPanel(session);
        DrawAnimationPanel(session);

        // 帧末提交一次：用户松手 / 失焦后才会真正入栈，
        // 把一次拖拽 / 一段输入折叠成一个 undo step。
        CommitHistoryIfChanged(session);
    }

    void UIEditorWindow::SetTexturePreviewResolver(std::function<void*(const std::string&)> resolver) {
        // 由 RenderSystem 注入纹理解析逻辑。
        GetTexturePreviewResolver() = std::move(resolver);
    }

    void UIEditorWindow::SetRuntimeUiFileChangedCallback(std::function<void(const std::filesystem::path&)> callback) {
        // 由 RenderSystem 注入运行时 UI 热重载逻辑。
        GetRuntimeUiFileChangedCallback() = std::move(callback);
    }

    bool UIEditorWindow::WantsMouseCapture() {
        return GetSession().wantsMouseCapture;
    }

    bool UIEditorWindow::WantsKeyboardCapture() {
        return GetSession().wantsKeyboardCapture;
    }

} // namespace engine

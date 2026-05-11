#pragma once

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "UIAnimation.hpp"
#include "UIRenderer.hpp"
#include "UISerializer.hpp"

namespace engine {

    // 运行时 UI 管理器。
    // 负责多屏幕加载、可见性控制、输入分发、命中测试、事件触发，
    // 并把当前屏幕栈交给具体渲染后端绘制。
    class UIManager {
    public:
        // 事件回调类型，接收事件名称字符串
        using EventCallback = std::function<void(const std::string&)>;

        // 全局UI设置：控制缩放、透明度、动画速度等显示参数
        struct UISettings {
            float globalScale = 1.0f;       // 全局缩放比例
            float hudOpacity = 1.0f;        // HUD整体透明度
            float speedTextScale = 1.0f;    // 速度文本缩放比例
            bool showDebugHud = false;      // 是否显示调试HUD
            float animationSpeed = 1.0f;    // 动画播放速度倍率
        };

        // 单条事件日志记录，调试面板用于显示最近触发的 UI 事件。
        struct EventLogEntry {
            std::string eventName;          // 事件名（如 "StartGame"）
            bool hadHandler = false;        // 触发时是否找到了已注册的处理器
            std::uint64_t sequence = 0;     // 自管理器创建以来的单调递增序号
        };

        // 已加载的屏幕实例，包含屏幕对象、文件路径及其动画状态
        struct LoadedScreen {
            std::unique_ptr<UIScreen> screen;       // 屏幕对象（拥有所有权）
            std::filesystem::path path;             // 屏幕文件路径
            UIAnimator animator;                    // 该屏幕的动画控制器
            std::string activeEnterAnimation;       // 当前播放的进入动画名称
            std::string activeExitAnimation;        // 当前播放的退出动画名称
            bool pendingHideAfterExit = false;      // 退出动画结束后是否自动隐藏
        };

        // --- 屏幕加载与卸载 ---
        bool LoadScreen(const std::filesystem::path& path);                          // 从文件加载屏幕（名称取自文件）
        bool LoadScreen(const std::string& name, const std::filesystem::path& path); // 以指定名称加载屏幕
        bool ReloadScreen(const std::filesystem::path& path);                        // 重新加载已存在的屏幕（保留可见性和渲染顺序）
        bool ReplaceWithScreen(const std::filesystem::path& path);                   // 清除所有屏幕后加载新屏幕
        bool UnloadScreen(std::string_view name);                                    // 按名称卸载屏幕，名称为空则卸载栈顶
        void ClearScreens();                                                         // 清除所有已加载屏幕

        // --- 屏幕栈操作与可见性控制 ---
        void ShowScreen(std::string_view name);     // 显示指定屏幕并播放进入动画
        void HideScreen(std::string_view name);     // 隐藏指定屏幕（可能先播放退出动画）
        void ToggleScreen(std::string_view name);   // 切换指定屏幕的显示/隐藏状态
        void SwitchToScreen(std::string_view name);  // 隐藏其他屏幕，仅显示指定屏幕
        void PushScreen(std::string_view name);     // 将指定屏幕推到栈顶并显示
        void PopScreen();                           // 隐藏栈顶的可见屏幕

        // --- 每帧更新与渲染 ---
        void Update(float deltaTime);               // 每帧更新：数据绑定、动画、按钮状态、滑块拖拽
        void Render(const UIRenderContext& context);  // 按渲染顺序绘制所有可见屏幕

        // --- 输入处理 ---
        void HandleMouseMove(const glm::vec2& position); // 处理鼠标移动，更新悬停元素
        void HandleMouseDown();                          // 处理鼠标按下，记录按下元素并触发onPressed事件
        void HandleMouseUp();                            // 处理鼠标释放，触发onClick/onReleased事件及切换Toggle
        void HandleTextInput(const std::string& text);   // 处理文本输入，追加到当前聚焦的输入框
        void HandleBackspace();                          // 处理退格键，删除输入框末尾字符
        void ClearInputState();                          // 清除所有输入状态（悬停/按下/聚焦）
        void RestartVisibleScreenAnimations();            // 重新播放所有可见屏幕的动画

        // --- 事件与访问器 ---
        void RegisterEventHandler(const std::string& eventName, EventCallback callback); // 注册事件处理回调
        bool HasEventHandler(const std::string& eventName) const { return mEventHandlers.contains(eventName); } // 判断事件名是否已绑定处理器
        std::vector<std::string> GetRegisteredEventNames() const; // 调试用：列出所有已注册事件名
        const std::deque<EventLogEntry>& GetRecentEvents() const { return mRecentEvents; } // 最近触发的事件日志（最多保留若干条）
        void ClearRecentEvents() { mRecentEvents.clear(); } // 清除事件日志
        void SetRenderer(UIRendererPtr renderer) { mRenderer = std::move(renderer); }     // 设置渲染后端
        const UIRendererPtr& GetRenderer() const { return mRenderer; }
        UIDataContext& GetDataContext() { return mDataContext; }        // 获取数据绑定上下文
        const UIDataContext& GetDataContext() const { return mDataContext; }
        UISettings& GetSettings() { return mSettings; }                // 获取全局UI设置
        const UISettings& GetSettings() const { return mSettings; }
        const std::vector<LoadedScreen>& GetLoadedScreens() const { return mLoadedScreens; }

        // --- 屏幕查询 ---
        UIScreen* GetPrimaryScreen();                     // 获取第一个加载的屏幕（栈底）
        const UIScreen* GetPrimaryScreen() const;
        UIScreen* GetActiveScreen() { return mActiveScreen; }  // 获取当前活跃屏幕（栈顶可见屏幕）
        const UIScreen* GetActiveScreen() const { return mActiveScreen; }
        UIScreen* GetScreen(std::string_view name);       // 按名称查找屏幕
        const UIScreen* GetScreen(std::string_view name) const;
        const std::filesystem::path& GetActiveScreenPath() const { return mActiveScreen ? mActiveScreenPath : mEmptyPath; }
        UIAnimator* GetAnimator(std::string_view screenName);       // 获取指定屏幕的动画控制器
        const UIAnimator* GetAnimator(std::string_view screenName) const;

        // --- 屏幕状态查询 ---
        bool IsScreenLoaded(std::string_view name) const;                          // 检查屏幕是否已加载
        bool HasVisibleScreen() const;                                             // 是否有任何可见屏幕
        std::size_t GetLoadedScreenCount() const { return mLoadedScreens.size(); } // 已加载屏幕总数
        std::size_t GetVisibleScreenCount() const;                                 // 当前可见屏幕数量
        std::vector<std::string> GetVisibleScreenNames() const;                    // 获取所有可见屏幕名称列表

        // --- 元素查找与调试 ---
        UIElement* FindById(UIElementId id);               // 在所有可见屏幕中按ID查找元素（从栈顶向下）
        const UIElement* FindById(UIElementId id) const;
        const UIElement* DebugHitTestElement(const glm::vec2& point, std::string* outScreenName = nullptr, UIElementId* outElementId = nullptr) const; // 调试用：返回指定坐标处命中的元素

        UIElementId GetHoveredElementId() const { return mHoveredElementId; }  // 当前鼠标悬停的元素ID
        UIElementId GetPressedElementId() const { return mPressedElementId; } // 当前鼠标按下的元素ID
        void TriggerEvent(const std::string& eventName);                      // 触发已注册的事件回调

    private:
        // 命中测试结果：记录被命中的屏幕、元素及其ID
        struct HitResult {
            UIScreen* screen = nullptr;
            UIElement* element = nullptr;
            UIElementId elementId = 0;
        };

        static std::filesystem::path NormalizePath(const std::filesystem::path& path);  // 规范化屏幕文件路径（补全目录和扩展名）
        HitResult HitTest(const glm::vec2& point) const;                               // 从栈顶向下进行命中测试
        bool IsInteractive(const UIElement& element) const;                             // 判断元素是否可交互（按钮/切换/滑块/输入框）
        void RefreshActiveScreenCache();    // 刷新活跃屏幕缓存（栈顶可见屏幕）
        void ClearPressedStates();          // 清除所有按钮的按下状态
        void SyncScreenRenderOrder();       // 根据栈位置同步每个屏幕的渲染顺序
        void ClearInputStateForScreen(const UIScreen* screen);          // 清除指定屏幕相关的输入状态
        void SyncAnimator(LoadedScreen& loadedScreen);                  // 同步动画控制器到屏幕对象
        void TryAutoPlayScreenAnimation(LoadedScreen& loadedScreen);    // 尝试自动播放屏幕的入场/默认动画
        bool BeginScreenEnterTransition(LoadedScreen& loadedScreen);    // 开始屏幕进入过渡动画
        bool BeginScreenExitTransition(LoadedScreen& loadedScreen);     // 开始屏幕退出过渡动画
        bool IsScreenTransitionBlockingInput(const LoadedScreen& loadedScreen) const; // 检查屏幕过渡是否阻塞输入
        void UpdateConditionalAnimations(LoadedScreen& loadedScreen);   // 更新条件动画（如低能量警告脉冲）
        void ApplyBindings(UIScreen& screen);                           // 对屏幕应用所有数据绑定
        void ApplyBinding(UIScreen& screen, UIPropertyBinding& binding); // 应用单个属性绑定
        LoadedScreen* FindLoadedScreen(std::string_view name);          // 按名称查找已加载屏幕
        const LoadedScreen* FindLoadedScreen(std::string_view name) const;
        LoadedScreen* FindLoadedScreenByPath(const std::filesystem::path& path);       // 按路径查找已加载屏幕
        const LoadedScreen* FindLoadedScreenByPath(const std::filesystem::path& path) const;

    private:
        // 已加载屏幕按栈顺序存放，越靠后越在上层。
        std::vector<LoadedScreen> mLoadedScreens;
        UIScreen* mActiveScreen = nullptr;
        std::filesystem::path mActiveScreenPath;
        std::filesystem::path mEmptyPath;
        UIRendererPtr mRenderer;
        UIDataContext mDataContext;
        UISettings mSettings;
        std::unordered_map<std::string, EventCallback> mEventHandlers;
        std::deque<EventLogEntry> mRecentEvents;     // 最近事件日志（FIFO，限长 32 条）
        std::uint64_t mEventSequence = 0;            // 事件自增序号
        static constexpr std::size_t kMaxRecentEvents = 32; // 事件日志容量上限
        glm::vec2 mMousePosition = glm::vec2(0.0f);
        UIScreen* mHoveredScreen = nullptr;
        UIElementId mHoveredElementId = 0;
        UIScreen* mPressedScreen = nullptr;
        UIElementId mPressedElementId = 0;
        UIScreen* mFocusedScreen = nullptr;
        UIElementId mFocusedElementId = 0;
        float mLastDeltaTime = 0.0f;
    };

} // namespace engine

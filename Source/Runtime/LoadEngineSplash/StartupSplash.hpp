#pragma once

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

struct GLFWwindow;

namespace engine {

    class StartupSplash final {
    public:
        StartupSplash();
        StartupSplash(const StartupSplash&) = delete;
        StartupSplash& operator=(const StartupSplash&) = delete;
        ~StartupSplash();

        void Show();
        void Close();

        void SetMediaPath(const std::filesystem::path& path);
        void SetImagePath(std::wstring path);
        void SetVideoPath(std::wstring path);
        void SetTitleText(std::string_view text);
        void SetVersionText(std::string_view text);
        void SetProgress(float progress, std::string_view status);

    private:
        enum class MediaKind {
            None,
            Image,
            Video
        };

        struct Impl;

        static MediaKind DetectMediaKind(const std::filesystem::path& path);
        static std::string PathToString(const std::filesystem::path& path);

        void ThreadMain();
        void RequestWakeup();

        std::unique_ptr<Impl> mImpl;
        std::thread mThread;
        std::mutex mMutex;
        std::condition_variable mReadyCv;
        std::filesystem::path mMediaPath = "Assets/Splash/Splash.png";
        //std::filesystem::path mMediaPath = "Assets/Splash/video.mp4";

        std::string mTitleText = "project name:xx";
        std::string mVersionText = "v0.0.1";
        std::string mStatus = "Preparing startup...";
        float mProgress = 0.0f;
        bool mReady = false;
        bool mClosed = true;
        MediaKind mMediaKind = MediaKind::Image;
    };

} // namespace engine

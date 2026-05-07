#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace Gdiplus {
class Image;
}
#endif

namespace engine {

class StartupSplash final {
public:
    StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;
    ~StartupSplash();

    void Show();
    void Close();

    void SetImagePath(std::wstring path);
    void SetTitleText(std::wstring text);
    void SetVersionText(std::wstring text);
    void SetProgress(float progress, std::string_view status);

private:
#ifdef _WIN32
    static constexpr wchar_t kWindowClassName[] = L"EngineStartupSplashWindow";
    static constexpr wchar_t kDefaultImagePath[] = L"Assets/Splash/Splash.png";

    static std::wstring Utf8ToWide(std::string_view text);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    std::wstring GetImagePathForLoad();
    SIZE ComputeWindowSize();
    void LoadSplashImage();
    void ThreadMain();
    void Paint(HWND hwnd);
    void RequestRepaint();

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mReadyCv;
    HWND mWindow = nullptr;
    std::wstring mImagePath = kDefaultImagePath;
    std::wstring mTitleText = L"Steer engine";
    std::wstring mVersionText = L"v0.0.1";
    std::wstring mStatus = L"Preparing startup...";
    float mProgress = 0.0f;
    bool mReady = false;
    bool mClosed = true;
    ULONG_PTR mGdiplusToken = 0;
    std::unique_ptr<Gdiplus::Image> mSplashImage;
#endif
};

} // namespace engine

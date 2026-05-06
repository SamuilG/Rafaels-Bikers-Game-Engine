#include "StartupSplash.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <gdiplus.h>
#pragma comment(lib, "Gdiplus.lib")
#endif

namespace engine {

StartupSplash::StartupSplash() = default;

StartupSplash::~StartupSplash() {
    Close();
}

void StartupSplash::Show() {
#ifdef _WIN32
    if (mThread.joinable()) {
        return;
    }

    {
        std::scoped_lock lock(mMutex);
        mClosed = false;
        mReady = false;
        mProgress = 0.0f;
        mStatus = L"Preparing startup...";
    }

    // The splash owns its own UI thread so it can keep repainting while the main
    // thread is blocked inside renderer and asset initialization.
    mThread = std::thread([this]() { ThreadMain(); });

    std::unique_lock lock(mMutex);
    mReadyCv.wait(lock, [this]() { return mReady; });
#endif
}

void StartupSplash::Close() {
#ifdef _WIN32
    std::thread threadToJoin;
    HWND hwnd = nullptr;
    {
        std::scoped_lock lock(mMutex);
        if (mClosed) {
            if (mThread.joinable()) {
                threadToJoin = std::move(mThread);
            }
        } else {
            mClosed = true;
            hwnd = mWindow;
            if (mThread.joinable()) {
                threadToJoin = std::move(mThread);
            }
        }
    }

    if (hwnd) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    if (threadToJoin.joinable()) {
        threadToJoin.join();
    }
#endif
}

void StartupSplash::SetImagePath(std::wstring path) {
#ifdef _WIN32
    {
        std::scoped_lock lock(mMutex);
        mImagePath = std::move(path);
    }
    RequestRepaint();
#else
    (void)path;
#endif
}

void StartupSplash::SetTitleText(std::wstring text) {
#ifdef _WIN32
    {
        std::scoped_lock lock(mMutex);
        mTitleText = std::move(text);
    }
    RequestRepaint();
#else
    (void)text;
#endif
}

void StartupSplash::SetVersionText(std::wstring text) {
#ifdef _WIN32
    {
        std::scoped_lock lock(mMutex);
        mVersionText = std::move(text);
    }
    RequestRepaint();
#else
    (void)text;
#endif
}

void StartupSplash::SetProgress(float progress, std::string_view status) {
#ifdef _WIN32
    {
        std::scoped_lock lock(mMutex);
        mProgress = std::clamp(progress, 0.0f, 1.0f);
        mStatus = Utf8ToWide(status);
    }
    RequestRepaint();
#else
    (void)progress;
    (void)status;
#endif
}

#ifdef _WIN32

std::wstring StartupSplash::Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);

    if (wideLength <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        wideLength);
    return wide;
}

LRESULT CALLBACK StartupSplash::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    StartupSplash* splash = nullptr;

    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        splash = static_cast<StartupSplash*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(splash));
    } else {
        splash = reinterpret_cast<StartupSplash*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!splash) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_APP + 1:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        splash->Paint(hwnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

std::wstring StartupSplash::GetImagePathForLoad() {
    std::scoped_lock lock(mMutex);
    return mImagePath.empty() ? std::wstring(kDefaultImagePath) : mImagePath;
}

SIZE StartupSplash::ComputeWindowSize() {
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    constexpr int kMinWidth = 520;
    constexpr int kMinHeight = 292;
    const int maxWidth = std::max(kMinWidth, static_cast<int>(screenWidth * 0.40f));
    const int maxHeight = std::max(kMinHeight, static_cast<int>(screenHeight * 0.40f));

    UINT imageWidth = 16;
    UINT imageHeight = 9;
    if (mSplashImage && mSplashImage->GetLastStatus() == Gdiplus::Ok &&
        mSplashImage->GetWidth() > 0 && mSplashImage->GetHeight() > 0) {
        imageWidth = mSplashImage->GetWidth();
        imageHeight = mSplashImage->GetHeight();
    }

    // The splash keeps the art aspect ratio and then clamps to a reasonable
    // percentage of the user's monitor so it feels deliberate instead of huge.
    const double scaleX = static_cast<double>(maxWidth) / static_cast<double>(imageWidth);
    const double scaleY = static_cast<double>(maxHeight) / static_cast<double>(imageHeight);
    const double scale = std::min(scaleX, scaleY);

    SIZE size{};
    size.cx = std::max(kMinWidth, static_cast<int>(imageWidth * scale));
    size.cy = std::max(kMinHeight, static_cast<int>(imageHeight * scale));
    return size;
}

void StartupSplash::LoadSplashImage() {
    mSplashImage.reset();

    const std::wstring imagePath = GetImagePathForLoad();
    if (!std::filesystem::exists(imagePath)) {
        return;
    }

    auto image = std::make_unique<Gdiplus::Image>(imagePath.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok) {
        return;
    }

    mSplashImage = std::move(image);
}

void StartupSplash::ThreadMain() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&mGdiplusToken, &gdiplusStartupInput, nullptr);
    LoadSplashImage();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &StartupSplash::WndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kWindowClassName;

    RegisterClassExW(&windowClass);

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const SIZE windowSize = ComputeWindowSize();
    const int x = (screenWidth - windowSize.cx) / 2;
    const int y = (screenHeight - windowSize.cy) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClassName,
        L"Rafael's Bikers",
        WS_POPUP | WS_VISIBLE,
        x,
        y,
        windowSize.cx,
        windowSize.cy,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);

    {
        std::scoped_lock lock(mMutex);
        mWindow = hwnd;
        mReady = true;
    }
    mReadyCv.notify_all();

    if (!hwnd) {
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    {
        std::scoped_lock lock(mMutex);
        mWindow = nullptr;
    }

    mSplashImage.reset();
    if (mGdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(mGdiplusToken);
        mGdiplusToken = 0;
    }
}

void StartupSplash::Paint(HWND hwnd) {
    PAINTSTRUCT paintStruct{};
    HDC screenDc = BeginPaint(hwnd, &paintStruct);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(
        screenDc,
        clientRect.right - clientRect.left,
        clientRect.bottom - clientRect.top);
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

    float progress = 0.0f;
    std::wstring titleText;
    std::wstring versionText;
    std::wstring statusText;
    {
        std::scoped_lock lock(mMutex);
        progress = mProgress;
        titleText = mTitleText;
        versionText = mVersionText;
        statusText = mStatus;
    }

    RECT backgroundRect = clientRect;
    HBRUSH fallbackBrush = CreateSolidBrush(RGB(18, 20, 26));
    FillRect(memoryDc, &backgroundRect, fallbackBrush);
    DeleteObject(fallbackBrush);

    Gdiplus::Graphics graphics(memoryDc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    if (mSplashImage && mSplashImage->GetLastStatus() == Gdiplus::Ok) {
        graphics.DrawImage(
            mSplashImage.get(),
            Gdiplus::Rect(0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top));
    }

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    const int overlayHeight = 132;

    // The bottom gradient keeps the progress text readable regardless of how
    // bright or detailed the splash image is.
    Gdiplus::LinearGradientBrush overlayBrush(
        Gdiplus::Point(0, height - overlayHeight),
        Gdiplus::Point(0, height),
        Gdiplus::Color(10, 0, 0, 0),
        Gdiplus::Color(175, 0, 0, 0));
    graphics.FillRectangle(&overlayBrush, 0, height - overlayHeight, width, overlayHeight);

    constexpr int kBarMargin = 28;
    constexpr int kBarHeight = 12;
    constexpr int kBarBottom = 24;

    RECT barOuter{
        kBarMargin,
        clientRect.bottom - kBarBottom - kBarHeight,
        clientRect.right - kBarMargin,
        clientRect.bottom - kBarBottom
    };
    HBRUSH barBackgroundBrush = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(memoryDc, &barOuter, barBackgroundBrush);
    DeleteObject(barBackgroundBrush);

    const int barWidth = barOuter.right - barOuter.left;
    RECT barInner = barOuter;
    barInner.right = barInner.left + static_cast<int>(barWidth * progress);
    HBRUSH progressBrush = CreateSolidBrush(RGB(214, 120, 48));
    FillRect(memoryDc, &barInner, progressBrush);
    DeleteObject(progressBrush);

    HBRUSH borderBrush = CreateSolidBrush(RGB(245, 245, 245));
    FrameRect(memoryDc, &barOuter, borderBrush);
    DeleteObject(borderBrush);

    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(242, 255, 255, 255));
    Gdiplus::SolidBrush secondaryBrush(Gdiplus::Color(228, 232, 232, 232));
    Gdiplus::StringFormat leftTopFormat;
    leftTopFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    leftTopFormat.SetLineAlignment(Gdiplus::StringAlignmentNear);

    Gdiplus::StringFormat rightBottomFormat;
    rightBottomFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    rightBottomFormat.SetLineAlignment(Gdiplus::StringAlignmentNear);

    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font titleFont(&fontFamily, 24.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font metaFont(&fontFamily, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font statusFont(&fontFamily, 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    // Title is anchored to the top-left so it behaves like a proper game banner.
    if (!titleText.empty()) {
        graphics.DrawString(
            titleText.c_str(),
            -1,
            &titleFont,
            Gdiplus::RectF(28.0f, 22.0f, static_cast<Gdiplus::REAL>(width - 56), 34.0f),
            &leftTopFormat,
            &titleBrush);
    }

    // Version sits in the lower-right corner so it feels like build metadata.
    if (!versionText.empty()) {
        graphics.DrawString(
            versionText.c_str(),
            -1,
            &metaFont,
            Gdiplus::RectF(
                28.0f,
                static_cast<Gdiplus::REAL>(barOuter.top - 28),
                static_cast<Gdiplus::REAL>(width - 56),
                22.0f),
            &rightBottomFormat,
            &secondaryBrush);
    }

    // The current loading stage stays near the progress bar to preserve context.
    if (!statusText.empty()) {
        graphics.DrawString(
            statusText.c_str(),
            -1,
            &statusFont,
            Gdiplus::RectF(
                28.0f,
                static_cast<Gdiplus::REAL>(barOuter.top - 28),
                static_cast<Gdiplus::REAL>(width - 170),
                22.0f),
            &leftTopFormat,
            &secondaryBrush);
    }

    BitBlt(
        screenDc,
        0,
        0,
        clientRect.right - clientRect.left,
        clientRect.bottom - clientRect.top,
        memoryDc,
        0,
        0,
        SRCCOPY);

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);

    EndPaint(hwnd, &paintStruct);
}

void StartupSplash::RequestRepaint() {
    if (mWindow) {
        PostMessageW(mWindow, WM_APP + 1, 0, 0);
    }
}

#endif

} // namespace engine

#include "StartupSplash.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if !defined(GLFW_INCLUDE_NONE)
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stb_image.h>

namespace engine {

    namespace {

        constexpr char kDefaultMediaPath[] = "Assets/Splash/Splash.png";
        constexpr int kVideoFps = 30;

        //  ffmpeg / ffprobe 
        constexpr char kBundledFfmpegDir[] = "ThirdParty/ffmpeg/bin";

        struct Color {
            float r;
            float g;
            float b;
            float a;
        };

        // 转成小写
        std::string ToLowerAscii(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
                });
            return value;
        }

		// 转成大写//capitalize
        std::string ToUpperAscii(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
                });
            return value;
        }

        // 允许传入 URL 或附带 ? / # 的路径
        std::string StripQueryAndFragment(const std::string& value) {
            const std::size_t stop = value.find_first_of("?#");
            return stop == std::string::npos ? value : value.substr(0, stop);
        }

        // 给命令行参数补引号，保证路径中带空格时 ffmpeg / ffprobe 仍能正确解析
        std::string QuoteForShell(const std::string& value) {
            std::string quoted = "\"";
            for (char ch : value) {
                if (ch == '"') {
                    quoted += "\\\"";
                }
                else {
                    quoted += ch;
                }
            }
            quoted += "\"";
            return quoted;
        }

        // debug log
        void AppendSplashLog(const std::string& message) {
            /*std::ofstream logFile("startup_splash.log", std::ios::app);
            if (!logFile) {
                return;
            }

            logFile << message << '\n';*/
        }

		// 获取当前路径。
        std::filesystem::path GetCurrentDirectoryPath() {
            std::error_code ec;
            std::filesystem::path path = std::filesystem::current_path(ec);
            if (ec) {
                return {};
            }
            return path;
        }


        // 找 Assets / ThirdParty
        std::vector<std::filesystem::path> BuildSearchRoots() {
            std::vector<std::filesystem::path> roots;
            std::filesystem::path current = GetCurrentDirectoryPath();
            for (int i = 0; !current.empty() && i < 5; ++i) {
                roots.push_back(current);
                const std::filesystem::path parent = current.parent_path();
                if (parent == current) {
                    break;
                }
                current = parent;
            }
            return roots;
        }

        // 将相对路径解析成真实存在的文件路径。
        std::filesystem::path ResolveExistingPath(const std::filesystem::path& inputPath) {
            if (inputPath.empty()) {
                return inputPath;
            }

            std::error_code ec;
            if (inputPath.is_absolute()) {
                return std::filesystem::exists(inputPath, ec) && !ec ? inputPath : inputPath;
            }

            for (const auto& root : BuildSearchRoots()) {
                const std::filesystem::path candidate = root / inputPath;
                if (std::filesystem::exists(candidate, ec) && !ec) {
                    return candidate;
                }
                ec.clear();
            }

            return inputPath;
        }

        // 查找 ffmpeg / ffprobe 的真实位置。
        // 优先使用项目内打包版本；如果项目里没有，再回退到系统 PATH。
        std::filesystem::path ResolveMediaToolPath(std::string_view toolName) {
#if defined(_WIN32)
            const std::filesystem::path executableName = std::string(toolName) + ".exe";
#else
            const std::filesystem::path executableName = std::string(toolName);
#endif

            std::error_code ec;
            for (const auto& root : BuildSearchRoots()) {
                const std::filesystem::path candidate = root / kBundledFfmpegDir / executableName;
                if (std::filesystem::exists(candidate, ec) && !ec) {
                    AppendSplashLog("Resolved tool " + std::string(toolName) + " to bundled path: " + candidate.string());
                    return candidate;
                }
                ec.clear();
            }

            AppendSplashLog("Falling back to PATH for tool: " + std::string(toolName));
            return std::filesystem::path(std::string(toolName));
        }

        // 对视频解码进程的跨平台包装
        struct VideoProcessPipe {
#if defined(_WIN32)
            HANDLE readHandle = nullptr;
            PROCESS_INFORMATION processInfo{};
#else
            FILE* pipe = nullptr;
#endif
        };

#if defined(_WIN32)
        // Windows 分支使用 CreateProcess 

        std::wstring ToWideAscii(std::string_view value) {
            return std::wstring(value.begin(), value.end());
        }

        // Windows 的 CreateProcess 需要手动拼命令行
        std::wstring QuoteForCommandLine(const std::wstring& value) {
            return L"\"" + value + L"\"";
        }

        // 构建真正传给 CreateProcess 的命令行字符串。
        std::wstring BuildProcessCommandLine(const std::filesystem::path& executablePath, const std::string& arguments) {
            const std::wstring executable = executablePath.wstring();
            const std::wstring args = ToWideAscii(arguments);
            if (executablePath.is_absolute() || executablePath.has_parent_path()) {
                return QuoteForCommandLine(executable) + (args.empty() ? L"" : L" " + args);
            }
            return executable + (args.empty() ? L"" : L" " + args);
        }

        // 启动一个媒体处理子进程，并把标准输出重定向
        // ffprobe 用它读文本输出，ffmpeg 用它持续输出原始 RGBA 视频帧
        bool LaunchProcessWithPipe(
            const std::filesystem::path& executablePath,
            const std::string& arguments,
            bool captureStderr,
            VideoProcessPipe& pipe) {
            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE;

            HANDLE readPipe = nullptr;
            HANDLE writePipe = nullptr;
            if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
                return false;
            }

            if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                return false;
            }

            HANDLE nullHandle = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            startupInfo.dwFlags = STARTF_USESTDHANDLES;
            startupInfo.hStdInput = nullptr;
            startupInfo.hStdOutput = writePipe;
            startupInfo.hStdError = captureStderr ? writePipe : nullHandle;

            PROCESS_INFORMATION processInfo{};
            std::wstring commandLine = BuildProcessCommandLine(executablePath, arguments);
            std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
            mutableCommandLine.push_back(L'\0');

            const bool hasExplicitExecutable = executablePath.is_absolute() || executablePath.has_parent_path();
            const std::wstring executable = executablePath.wstring();
            const wchar_t* applicationName = hasExplicitExecutable ? executable.c_str() : nullptr;

            const BOOL created = CreateProcessW(
                applicationName,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startupInfo,
                &processInfo);

            CloseHandle(writePipe);
            if (nullHandle != INVALID_HANDLE_VALUE) {
                CloseHandle(nullHandle);
            }

            if (!created) {
                CloseHandle(readPipe);
                return false;
            }

            pipe.readHandle = readPipe;
            pipe.processInfo = processInfo;
            return true;
        }


        //  ffprobe 读取视频宽高使用(抓第一帧)
        std::string RunProcessAndCaptureFirstLine(const std::filesystem::path& executablePath, const std::string& arguments) {
            VideoProcessPipe pipe;
            if (!LaunchProcessWithPipe(executablePath, arguments, true, pipe)) {
                return {};
            }

            std::string output;
            char buffer[256]{};
            DWORD bytesRead = 0;
            while (ReadFile(pipe.readHandle, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                output += buffer;
                const std::size_t newline = output.find_first_of("\r\n");
                if (newline != std::string::npos) {
                    output.resize(newline);
                    break;
                }
            }

            if (pipe.readHandle) {
                CloseHandle(pipe.readHandle);
                pipe.readHandle = nullptr;
            }

            if (pipe.processInfo.hProcess) {
                WaitForSingleObject(pipe.processInfo.hProcess, 5000);
                CloseHandle(pipe.processInfo.hProcess);
                pipe.processInfo.hProcess = nullptr;
            }

            if (pipe.processInfo.hThread) {
                CloseHandle(pipe.processInfo.hThread);
                pipe.processInfo.hThread = nullptr;
            }

            return output;
        }

        // splash 主循环会不断拉取原始像素帧
        VideoProcessPipe OpenProcessPipeForVideo(const std::filesystem::path& executablePath, const std::string& arguments) {
            VideoProcessPipe pipe;
            LaunchProcessWithPipe(executablePath, arguments, false, pipe);
            return pipe;
        }

        // 尽可能把一整帧视频数据读满
        // 读不满通常意味着视频播放结束，或者 ffmpeg 进程提前退出
        std::size_t ReadFromVideoPipe(VideoProcessPipe& pipe, void* buffer, std::size_t bytesToRead) {
            std::size_t totalRead = 0;
            auto* byteBuffer = static_cast<std::uint8_t*>(buffer);

            while (totalRead < bytesToRead) {
                DWORD chunkRead = 0;
                const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(bytesToRead - totalRead, static_cast<std::size_t>(1u << 20)));
                if (!ReadFile(pipe.readHandle, byteBuffer + totalRead, chunkSize, &chunkRead, nullptr) || chunkRead == 0) {
                    break;
                }
                totalRead += static_cast<std::size_t>(chunkRead);
            }

            return totalRead;
        }

        // 关闭视频管道并回收子进程资源。
        // 如果进程还活着，会主动结束它，避免 splash 退出后留下后台进程
        void CloseVideoPipe(VideoProcessPipe& pipe) {
            if (pipe.readHandle) {
                CloseHandle(pipe.readHandle);
                pipe.readHandle = nullptr;
            }

            if (pipe.processInfo.hProcess) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(pipe.processInfo.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                    TerminateProcess(pipe.processInfo.hProcess, 0);
                    WaitForSingleObject(pipe.processInfo.hProcess, 1000);
                }
                CloseHandle(pipe.processInfo.hProcess);
                pipe.processInfo.hProcess = nullptr;
            }

            if (pipe.processInfo.hThread) {
                CloseHandle(pipe.processInfo.hThread);
                pipe.processInfo.hThread = nullptr;
            }
        }

        // 判断视频管道当前是否可用
        bool IsVideoPipeOpen(const VideoProcessPipe& pipe) {
            return pipe.readHandle != nullptr;
        }
#else
        // Linux 分支当前仍然使用 popen
        // 这条路径还没有在真实 Linux 环境中实跑验证，但从代码结构上是独立可编译的。
        std::string RunProcessAndCaptureFirstLine(const std::filesystem::path& executablePath, const std::string& arguments) {
            const std::string command =
                QuoteForShell(executablePath.string()) + (arguments.empty() ? std::string() : " " + arguments) + " 2>&1";

            std::string output;
            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe) {
                return output;
            }

            char buffer[256]{};
            if (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe)) {
                output = buffer;
            }
            pclose(pipe);
            return output;
        }

        VideoProcessPipe OpenProcessPipeForVideo(const std::filesystem::path& executablePath, const std::string& arguments) {
            VideoProcessPipe pipe;
            const std::string command =
                QuoteForShell(executablePath.string()) + (arguments.empty() ? std::string() : " " + arguments);
            pipe.pipe = popen(command.c_str(), "r");
            return pipe;
        }

        std::size_t ReadFromVideoPipe(VideoProcessPipe& pipe, void* buffer, std::size_t bytesToRead) {
            return pipe.pipe ? std::fread(buffer, 1, bytesToRead, pipe.pipe) : 0;
        }

        void CloseVideoPipe(VideoProcessPipe& pipe) {
            if (pipe.pipe) {
                pclose(pipe.pipe);
                pipe.pipe = nullptr;
            }
        }

        bool IsVideoPipeOpen(const VideoProcessPipe& pipe) {
            return pipe.pipe != nullptr;
        }
#endif

        // 一个极简的 5x7 像素字模，用来在不引入字体库的情况下绘制标题和状态文字
        std::array<std::uint8_t, 7> GlyphRows(char ch) {
            switch (ch) {
            case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
            case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
            case 'D': return { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
            case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
            case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
            case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
            case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'I': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
            case 'J': return { 0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C };
            case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
            case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
            case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
            case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
            case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
            case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
            case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
            case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
            case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
            case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
            case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
            case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
            case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
            case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
            case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
            case '1': return { 0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F };
            case '2': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
            case '3': return { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E };
            case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
            case '5': return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
            case '6': return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
            case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
            case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
            case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
            case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 };
            case ':': return { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 };
            case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
            case '_': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F };
            case '/': return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
            case '\'': return { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 };
            case '(': return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
            case ')': return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
            case ' ': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            default: return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x00, 0x08 };
            }
        }

        // 按固定字宽估算文本宽度，方便做右对齐布局。
        float MeasureTextWidth(const std::string& text, float scale) {
            return static_cast<float>(text.size()) * (6.0f * scale);
        }

        // 画纯色矩形。
        // 底部遮罩、进度条、像素字体的小方块都复用这一个函数。
        void DrawFilledRect(float x, float y, float width, float height, const Color& color) {
            glColor4f(color.r, color.g, color.b, color.a);
            glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + width, y);
            glVertex2f(x + width, y + height);
            glVertex2f(x, y + height);
            glEnd();
        }

        // 使用上面的 5x7 字模逐像素绘制文本。
        // 这套方案很朴素，但跨平台、零额外依赖，足够支撑启动页上的少量文字。
        void DrawText(const std::string& text, float x, float y, float scale, const Color& color, bool rightAligned = false) {
            const std::string upper = ToUpperAscii(text);
            float cursorX = rightAligned ? x - MeasureTextWidth(upper, scale) : x;

            glDisable(GL_TEXTURE_2D);
            for (char ch : upper) {
                const auto rows = GlyphRows(ch);
                for (int row = 0; row < 7; ++row) {
                    for (int col = 0; col < 5; ++col) {
                        if ((rows[row] >> (4 - col)) & 0x1) {
                            DrawFilledRect(
                                cursorX + static_cast<float>(col) * scale,
                                y + static_cast<float>(row) * scale,
                                scale,
                                scale,
                                color);
                        }
                    }
                }
                cursorX += 6.0f * scale;
            }
        }

        // 读取静态图片并强制转换成 RGBA。
        // 图片模式下只需要在启动时加载一次即可。
        bool TryLoadImageFile(const std::filesystem::path& path, int& width, int& height, std::vector<unsigned char>& pixels) {
            int channels = 0;
            unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
            if (!data) {
                return false;
            }

            pixels.assign(data, data + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
            stbi_image_free(data);
            return true;
        }

        // 通过 ffprobe 查询视频原始分辨率。
        // 分辨率会用于计算 splash 窗口的宽高比，避免视频被严重拉伸。
        std::pair<int, int> ProbeVideoDimensions(const std::filesystem::path& path) {
            const std::filesystem::path resolvedPath = ResolveExistingPath(path);
            const std::string quotedPath = QuoteForShell(resolvedPath.string());
            const std::string commandArguments =
                " -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 " + quotedPath;
            const std::filesystem::path toolPath = ResolveMediaToolPath("ffprobe");
            const std::string output = RunProcessAndCaptureFirstLine(toolPath, commandArguments);
            AppendSplashLog("ffprobe path: " + resolvedPath.string());
            AppendSplashLog("ffprobe output: " + output);
            const std::size_t separator = output.find('x');
            if (separator == std::string::npos) {
                AppendSplashLog("ffprobe did not return dimensions, using fallback size 1280x720");
                return { 1280, 720 };
            }

            try {
                const int width = std::stoi(output.substr(0, separator));
                const int height = std::stoi(output.substr(separator + 1));
                if (width > 0 && height > 0) {
                    return { width, height };
                }
            }
            catch (...) {
            }

            return { 1280, 720 };
        }

        // 打开 ffmpeg 视频输出管道，并要求它持续输出 RGBA 原始帧。
        // 这里会把视频缩放到 splash 当前窗口大小，以减少运行时额外处理。
        VideoProcessPipe OpenVideoPipe(const std::filesystem::path& path, int width, int height) {
            const std::filesystem::path resolvedPath = ResolveExistingPath(path);
            const std::string quotedPath = QuoteForShell(resolvedPath.string());
            const std::string commandArguments =
                "-loglevel error -i " + quotedPath +
                " -vf fps=" + std::to_string(kVideoFps) + ",scale=" + std::to_string(width) + ":" + std::to_string(height) +
                ":flags=lanczos -f rawvideo -pix_fmt rgba -an -sn -";
            const std::filesystem::path toolPath = ResolveMediaToolPath("ffmpeg");

            AppendSplashLog("Opening video pipe for: " + resolvedPath.string());
            VideoProcessPipe pipe = OpenProcessPipeForVideo(toolPath, commandArguments);
            AppendSplashLog(IsVideoPipeOpen(pipe) ? "Video pipe opened successfully" : "Video pipe failed to open");
            return pipe;
        }

    } // namespace

    struct StartupSplash::Impl {
        // OpenGL 纹理与像素缓存。
        // 图片模式和视频模式最终都会汇总到这一张纹理上显示。
        GLFWwindow* window = nullptr;
        unsigned int texture = 0;
        int textureWidth = 0;
        int textureHeight = 0;
        std::vector<unsigned char> texturePixels;
        bool textureDirty = false;
        bool textureReady = false;
        bool showTexture = false;

        // 视频播放状态。
        VideoProcessPipe videoPipe{};
        bool videoActive = false;
        bool videoFinished = false;
        bool firstVideoFrameLogged = false;
        std::chrono::steady_clock::time_point nextVideoFrameAt{};

        int windowWidth = 0;
        int windowHeight = 0;
    };

    StartupSplash::StartupSplash()
        : mImpl(std::make_unique<Impl>()) {
    }

    StartupSplash::~StartupSplash() {
        // 析构时保证 splash 线程和相关资源都被完整回收。
        Close();
    }

    void StartupSplash::Show() {
        // 避免重复创建 splash 线程。
        if (mThread.joinable()) {
            return;
        }

        {
            std::scoped_lock lock(mMutex);
            mClosed = false;
            mReady = false;
            mProgress = 0.0f;
            mStatus = "Preparing startup...";
            mMediaKind = DetectMediaKind(mMediaPath);
        }

        AppendSplashLog("---- Splash Show ----");

        mThread = std::thread([this]() { ThreadMain(); });

        // 等待渲染线程把窗口创建到可用状态，避免主线程继续初始化时 splash 还没准备好。
        std::unique_lock lock(mMutex);
        mReadyCv.wait(lock, [this]() { return mReady; });
    }

    void StartupSplash::Close() {
        // 先把线程对象转移出来，再在锁外 join，避免持锁等待造成死锁。
        std::thread threadToJoin;
        {
            std::scoped_lock lock(mMutex);
            mClosed = true;
            if (mThread.joinable()) {
                threadToJoin = std::move(mThread);
            }
        }

        RequestWakeup();

        if (threadToJoin.joinable()) {
            threadToJoin.join();
        }
    }

    // 统一设置媒体路径，并根据扩展名自动判断当前应该走图片模式还是视频模式。
    void StartupSplash::SetMediaPath(const std::filesystem::path& path) {
        std::scoped_lock lock(mMutex);
        mMediaPath = path;
        mMediaKind = DetectMediaKind(path);
    }

    void StartupSplash::SetImagePath(std::wstring path) {
        SetMediaPath(std::filesystem::path(std::move(path)));
    }

    void StartupSplash::SetVideoPath(std::wstring path) {
        SetMediaPath(std::filesystem::path(std::move(path)));
    }

    void StartupSplash::SetTitleText(std::string_view text) {
        {
            std::scoped_lock lock(mMutex);
            mTitleText = std::string(text);
        }
        RequestWakeup();
    }

    void StartupSplash::SetVersionText(std::string_view text) {
        {
            std::scoped_lock lock(mMutex);
            mVersionText = std::string(text);
        }
        RequestWakeup();
    }

    void StartupSplash::SetProgress(float progress, std::string_view status) {
        {
            std::scoped_lock lock(mMutex);
            mProgress = std::clamp(progress, 0.0f, 1.0f);
            mStatus = std::string(status);
        }
        RequestWakeup();
    }

    // 根据路径后缀自动识别图片或视频。
    // 这样调用方只需要传一个媒体路径，不需要额外指定播放模式。
    StartupSplash::MediaKind StartupSplash::DetectMediaKind(const std::filesystem::path& path) {
        const std::string cleaned = StripQueryAndFragment(PathToString(path));
        const std::string extension = ToLowerAscii(std::filesystem::path(cleaned).extension().string());

        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
            extension == ".tga" || extension == ".hdr" || extension == ".pic" || extension == ".psd") {
            return MediaKind::Image;
        }

        if (extension == ".mp4" || extension == ".mov" || extension == ".mkv" || extension == ".avi" ||
            extension == ".webm" || extension == ".m4v" || extension == ".mpeg" || extension == ".mpg" ||
            extension == ".gif") {
            return MediaKind::Video;
        }

        return MediaKind::Image;
    }

    std::string StartupSplash::PathToString(const std::filesystem::path& path) {
        return path.string();
    }

    void StartupSplash::ThreadMain() {
        // 用局部 lambda 向 Show() 通知“窗口已经准备完成”。
        auto signalReady = [this]() {
            {
                std::scoped_lock lock(mMutex);
                mReady = true;
            }
            mReadyCv.notify_all();
            };

        if (GLFW_TRUE != glfwInit()) {
            AppendSplashLog("glfwInit failed in splash thread");
            signalReady();
            return;
        }

        std::filesystem::path mediaPath;
        MediaKind mediaKind = MediaKind::Image;
        {
            std::scoped_lock lock(mMutex);
            mediaPath = mMediaPath.empty() ? std::filesystem::path(kDefaultMediaPath) : mMediaPath;
            mediaKind = mMediaKind;
        }

        {
            std::ostringstream stream;
            stream << "Current directory: " << GetCurrentDirectoryPath().string();
            AppendSplashLog(stream.str());
            AppendSplashLog("Requested media path: " + mediaPath.string());
            AppendSplashLog("Resolved media path: " + ResolveExistingPath(mediaPath).string());
            AppendSplashLog("Detected media kind: " + std::to_string(static_cast<int>(mediaKind)));
        }

        int mediaWidth = 1280;
        int mediaHeight = 720;
        std::vector<unsigned char> initialPixels;
        bool hasInitialImage = false;

        // 先拿到素材的基础尺寸信息。
        // 图片模式会直接把整张图读进内存；视频模式只在这里探测分辨率。
        if (mediaKind == MediaKind::Image) {
            hasInitialImage = TryLoadImageFile(mediaPath, mediaWidth, mediaHeight, initialPixels);
            AppendSplashLog(hasInitialImage ? "Loaded splash image successfully" : "Failed to load splash image");
        }
        else if (mediaKind == MediaKind::Video) {
            std::tie(mediaWidth, mediaHeight) = ProbeVideoDimensions(mediaPath);
            AppendSplashLog("Video dimensions: " + std::to_string(mediaWidth) + "x" + std::to_string(mediaHeight));
        }

        // 根据素材原始宽高比
        GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
        const int screenWidth = videoMode ? videoMode->width : 1920;
        const int screenHeight = videoMode ? videoMode->height : 1080;
        const int minWidth = 520;
        const int minHeight = 292;
        const int maxWidth = std::max(minWidth, static_cast<int>(static_cast<float>(screenWidth) * 0.30f));
        const int maxHeight = std::max(minHeight, static_cast<int>(static_cast<float>(screenHeight) * 0.30f));
        const double scaleX = static_cast<double>(maxWidth) / static_cast<double>(std::max(mediaWidth, 1));
        const double scaleY = static_cast<double>(maxHeight) / static_cast<double>(std::max(mediaHeight, 1));
        const double scale = std::min(scaleX, scaleY);
        mImpl->windowWidth = std::max(minWidth, static_cast<int>(static_cast<double>(mediaWidth) * scale));
        mImpl->windowHeight = std::max(minHeight, static_cast<int>(static_cast<double>(mediaHeight) * scale));

        int monitorX = 0;
        int monitorY = 0;
        if (primaryMonitor) {
            glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
        }

        const int windowX = monitorX + (screenWidth - mImpl->windowWidth) / 2;
        const int windowY = monitorY + (screenHeight - mImpl->windowHeight) / 2;

        // splash 使用轻量级 OpenGL 窗口：
        // 无边框、不可缩放、置顶，用来在主引擎加载期间尽快展示媒体和进度条。
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

        mImpl->window = glfwCreateWindow(
            mImpl->windowWidth,
            mImpl->windowHeight,
            "Rafael's Bikers Splash",
            nullptr,
            nullptr);

        signalReady();

        if (!mImpl->window) {
            AppendSplashLog("Failed to create splash window");
            return;
        }

        glfwSetWindowPos(mImpl->window, windowX, windowY);

        glfwMakeContextCurrent(mImpl->window);
        glfwSwapInterval(1);

        // 建立一个简单的 2D 正交投影，后续可以直接用像素坐标绘制。
        glViewport(0, 0, mImpl->windowWidth, mImpl->windowHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(mImpl->windowWidth), static_cast<double>(mImpl->windowHeight), 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // 创建显示媒体内容的纹理对象。
        glGenTextures(1, &mImpl->texture);
        glBindTexture(GL_TEXTURE_2D, mImpl->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

        // 图片模式：初始化时就能拿到整张图，直接标记为待上传。
        // 视频模式：这里只打开 ffmpeg 管道，实际帧数据会在主循环里逐帧读取。
        if (hasInitialImage) {
            mImpl->textureWidth = mediaWidth;
            mImpl->textureHeight = mediaHeight;
            mImpl->texturePixels = std::move(initialPixels);
            mImpl->textureDirty = true;
            mImpl->showTexture = true;
        }
        else if (mediaKind == MediaKind::Video) {
            mImpl->textureWidth = mImpl->windowWidth;
            mImpl->textureHeight = mImpl->windowHeight;
            mImpl->texturePixels.resize(static_cast<std::size_t>(mImpl->textureWidth) * static_cast<std::size_t>(mImpl->textureHeight) * 4u);
            mImpl->videoPipe = OpenVideoPipe(mediaPath, mImpl->textureWidth, mImpl->textureHeight);
            mImpl->videoActive = IsVideoPipeOpen(mImpl->videoPipe);
            mImpl->videoFinished = !mImpl->videoActive;
            mImpl->nextVideoFrameAt = std::chrono::steady_clock::now();
            AppendSplashLog(mImpl->videoActive ? "Splash video playback initialized" : "Splash video playback failed to initialize");
        }

        const auto frameDuration = std::chrono::milliseconds(1000 / kVideoFps);

        // splash 主循环：
        // 1. 处理关闭请求
        // 2. 视频模式下拉取新帧
        // 3. 更新纹理
        // 4. 绘制媒体、底部遮罩、进度条和文字
        while (!glfwWindowShouldClose(mImpl->window)) {
            {
                std::scoped_lock lock(mMutex);
                if (mClosed) {
                    glfwSetWindowShouldClose(mImpl->window, GLFW_TRUE);
                }
            }

            glfwPollEvents();

            // 视频模式按固定帧率从 ffmpeg 管道中拉取一帧 RGBA 数据。
            if (mImpl->videoActive && std::chrono::steady_clock::now() >= mImpl->nextVideoFrameAt) {
                const std::size_t frameBytes =
                    static_cast<std::size_t>(mImpl->textureWidth) * static_cast<std::size_t>(mImpl->textureHeight) * 4u;
                std::size_t readBytes = ReadFromVideoPipe(mImpl->videoPipe, mImpl->texturePixels.data(), frameBytes);
                if (readBytes == frameBytes) {
                    if (!mImpl->firstVideoFrameLogged) {
                        AppendSplashLog("Received first video frame");
                        mImpl->firstVideoFrameLogged = true;
                    }
                    mImpl->textureDirty = true;
                    mImpl->showTexture = true;
                    mImpl->nextVideoFrameAt = std::chrono::steady_clock::now() + frameDuration;
                }
                else {
                    AppendSplashLog("Video playback finished, keeping the last decoded frame visible");
                    CloseVideoPipe(mImpl->videoPipe);
                    mImpl->videoActive = false;
                    mImpl->videoFinished = true;
                }
            }

            // 如果像素缓冲有变化，就重新上传到 OpenGL 纹理。
            if (mImpl->textureDirty) {
                glBindTexture(GL_TEXTURE_2D, mImpl->texture);
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_RGBA,
                    mImpl->textureWidth,
                    mImpl->textureHeight,
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    mImpl->texturePixels.data());
                mImpl->textureDirty = false;
                mImpl->textureReady = true;
            }

            glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // 先画媒体贴图，再叠加 UI。
            if (mImpl->showTexture && mImpl->textureReady) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, mImpl->texture);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(static_cast<float>(mImpl->windowWidth), 0.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(static_cast<float>(mImpl->windowWidth), static_cast<float>(mImpl->windowHeight));
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, static_cast<float>(mImpl->windowHeight));
                glEnd();
                glDisable(GL_TEXTURE_2D);
            }

            // 底部半透明黑条，用来衬托进度条和文字，提高视频/图片背景上的可读性。
            DrawFilledRect(
                0.0f,
                static_cast<float>(mImpl->windowHeight - 70),
                static_cast<float>(mImpl->windowWidth),
                140.0f,
                { 0.0f, 0.0f, 0.0f, 0.55f });

            float progress = 0.0f;
            std::string titleText;
            std::string versionText;
            std::string statusText;
            {
                std::scoped_lock lock(mMutex);
                progress = mProgress;
                titleText = mTitleText;
                versionText = mVersionText;
                statusText = mStatus;
            }

            const float barMargin = 28.0f;
            const float barHeight = 12.0f;
            const float barBottom = 24.0f;
            const float barY = static_cast<float>(mImpl->windowHeight) - barBottom - barHeight;

            // 先画进度条底色，再按加载进度绘制前景填充。
            DrawFilledRect(barMargin, barY, static_cast<float>(mImpl->windowWidth) - barMargin * 2.0f, barHeight, { 0.12f, 0.12f, 0.12f, 1.0f });
            DrawFilledRect(barMargin, barY, (static_cast<float>(mImpl->windowWidth) - barMargin * 2.0f) * progress, barHeight, { 0.84f, 0.47f, 0.18f, 1.0f });

            // 给进度条补一个细边框，让深色背景下也更清晰。
            glColor4f(0.96f, 0.96f, 0.96f, 1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(barMargin, barY);
            glVertex2f(static_cast<float>(mImpl->windowWidth) - barMargin, barY);
            glVertex2f(static_cast<float>(mImpl->windowWidth) - barMargin, barY + barHeight);
            glVertex2f(barMargin, barY + barHeight);
            glEnd();

            DrawText(titleText, 28.0f, 24.0f, 3.2f, { 0.98f, 0.98f, 0.98f, 1.0f });
            DrawText(versionText, static_cast<float>(mImpl->windowWidth) - 28.0f, barY - 28.0f, 2.0f, { 0.90f, 0.90f, 0.90f, 1.0f }, true);
            DrawText(statusText, 28.0f, barY - 28.0f, 2.0f, { 0.90f, 0.90f, 0.90f, 1.0f });

            glfwSwapBuffers(mImpl->window);
            // 稍微睡一会儿，避免 splash 线程空转占满一个 CPU 核心。
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }

        // 统一清理视频进程、OpenGL 纹理和窗口对象。
        CloseVideoPipe(mImpl->videoPipe);

        if (mImpl->texture != 0) {
            glDeleteTextures(1, &mImpl->texture);
            mImpl->texture = 0;
        }

        glfwDestroyWindow(mImpl->window);
        mImpl->window = nullptr;
    }

    void StartupSplash::RequestWakeup() {
        // 当主线程更新进度或文字时，发送一个空事件把 glfwPollEvents 从等待态唤醒。
        if (mThread.joinable()) {
            glfwPostEmptyEvent();
        }
    }

} // namespace engine


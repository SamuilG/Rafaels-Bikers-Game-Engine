#pragma once

namespace engine {

    struct WindowContext {
        int  Width = 1280;
        int  Height = 720;
        bool ShouldQuit = false;
        bool Resized = false;
    };
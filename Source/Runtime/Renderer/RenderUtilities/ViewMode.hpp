#pragma once

namespace engine::view_mode {
    // Keep existing IDs stable for keyboard shortcuts and saved session state.
    enum : int {
        Default, Mipmaps, Depth, Derivatives, Overdraw, Overshading,
        SSAO, SSR, Normals, Wireframe, Albedo, Shadow, Count
    };

    constexpr bool IsGeometry(int mode) {
        return (mode >= Mipmaps && mode <= Overshading)
            || mode == Wireframe || mode == Albedo || mode == Shadow;
    }
    constexpr bool IsBuffer(int mode) {
        return mode >= SSAO && mode <= Normals;
    }
}

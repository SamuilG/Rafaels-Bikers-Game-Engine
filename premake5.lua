workspace "EngineWorkspace"
    -- Keep the solution file (.sln / Makefile) in the root directory
    location "." 
    language "C++"
    cppdialect "C++23"
    platforms { "x64" }
    configurations { "Debug", "Release" }

    flags { "NoPCH", "MultiProcessorCompile" }

    -- [Garbage] Route all intermediate build files (.obj, .pdb) to the Intermediate folder
    objdir "Intermediate/Obj/%{prj.name}/%{cfg.buildcfg}-%{cfg.platform}"

    startproject "Engine"

    -- ==========================================
    -- Global Compiler and System Settings
    -- ==========================================
    filter "toolset:gcc or toolset:clang"
        linkoptions { "-pthread" }
        buildoptions { "-march=native", "-Wall", "-pthread" }

    filter "toolset:gcc"
        links { "stdc++exp" }

    filter "toolset:msc-*"
        defines { "_CRT_SECURE_NO_WARNINGS=1", "_SCL_SECURE_NO_WARNINGS=1" }
        buildoptions { "/utf-8" } 
    
    filter "system:linux"
        links { "dl", "pthread", "m" }

    filter "configurations:Debug"
        symbols "On"
        defines { "_DEBUG=1", "DEBUG=1" }

    filter "configurations:Release"
        optimize "On"
        defines { "NDEBUG=1" }
    filter "*"

    -- ==========================================
    -- Project 1: GLFW Static Library
    -- ==========================================
    project "GLFW"
        -- [Garbage] Hide the GLFW project file in the Intermediate folder
        location "Intermediate/Projects" 
        kind "StaticLib"
        language "C"
        
        -- Route the generated static library to the Intermediate folder
        targetdir "Intermediate/Bin/%{cfg.buildcfg}-%{cfg.platform}/%{prj.name}"

        includedirs { "ThirdParty/glfw/include" }
        
        files {
            "ThirdParty/glfw/src/context.c",
            "ThirdParty/glfw/src/init.c",
            "ThirdParty/glfw/src/input.c",
            "ThirdParty/glfw/src/monitor.c",
            "ThirdParty/glfw/src/vulkan.c",
            "ThirdParty/glfw/src/window.c",
            "ThirdParty/glfw/src/osmesa_context.c",
            "ThirdParty/glfw/src/egl_context.c",
            "ThirdParty/glfw/src/platform.c",
            "ThirdParty/glfw/src/null_*.c"
        }

        filter "system:windows"
            defines { "_GLFW_WIN32" }
            files {
                "ThirdParty/glfw/src/win32_init.c",   "ThirdParty/glfw/src/win32_joystick.c",
                "ThirdParty/glfw/src/win32_monitor.c","ThirdParty/glfw/src/win32_time.c",
                "ThirdParty/glfw/src/win32_thread.c", "ThirdParty/glfw/src/win32_window.c",
                "ThirdParty/glfw/src/wgl_context.c",  "ThirdParty/glfw/src/win32_module.c"
            }
            
        filter "system:linux"
            defines { "_GLFW_X11" }
            files {
                "ThirdParty/glfw/src/x11_init.c",     "ThirdParty/glfw/src/x11_monitor.c",
                "ThirdParty/glfw/src/x11_window.c",   "ThirdParty/glfw/src/xkb_unicode.c",
                "ThirdParty/glfw/src/posix_time.c",   "ThirdParty/glfw/src/posix_thread.c",
                "ThirdParty/glfw/src/posix_module.c", "ThirdParty/glfw/src/posix_poll.c",
                "ThirdParty/glfw/src/glx_context.c",  "ThirdParty/glfw/src/linux_joystick.c"
            }
        filter "*"

    -- ==========================================
    -- Project 2: JoltPhysics Static Library
    -- ==========================================
    project "JoltPhysics"
        location "Intermediate/Projects" 
        kind "StaticLib"
        language "C++"
        
        targetdir "Intermediate/Bin/%{cfg.buildcfg}-%{cfg.platform}/%{prj.name}"

        defines {
            "JPH_CROSS_PLATFORM_DETERMINISTIC"
        }

        includedirs { "ThirdParty/JoltPhysics" }

        files {
            "ThirdParty/JoltPhysics/Jolt/**.cpp",
            "ThirdParty/JoltPhysics/Jolt/**.h",
            "ThirdParty/JoltPhysics/Jolt/**.inl",
            "ThirdParty/JoltPhysics/Jolt/**.natvis"
        }
  -- ==========================================
    -- Project 3: Dear ImGui Static Library
    -- ==========================================
    project "ImGui"
    location "Intermediate/Projects"
    kind "StaticLib"
    language "C++"
    cppdialect "C++23"

    targetdir "Intermediate/Bin/%{cfg.buildcfg}-%{cfg.platform}/%{prj.name}"

    defines {
        "IMGUI_IMPL_VULKAN_USE_VOLK"
    }

    includedirs {
        "ThirdParty/imgui",
        "ThirdParty/imgui/backends",
        "ThirdParty/glfw/include",
        "ThirdParty/volk/include",
        "ThirdParty/vulkan/include"
    }

    files {
        "ThirdParty/imgui/imgui.cpp",
        "ThirdParty/imgui/imgui_draw.cpp",
        "ThirdParty/imgui/imgui_tables.cpp",
        "ThirdParty/imgui/imgui_widgets.cpp",
		"ThirdParty/imgui/imgui_demo.cpp",
        "ThirdParty/imgui/imgui.h",
        "ThirdParty/imgui/imconfig.h",
        "ThirdParty/imgui/imgui_internal.h",
        "ThirdParty/imgui/imstb_rectpack.h",
        "ThirdParty/imgui/imstb_textedit.h",
        "ThirdParty/imgui/imstb_truetype.h",

        "ThirdParty/imgui/backends/imgui_impl_glfw.cpp",
        "ThirdParty/imgui/backends/imgui_impl_glfw.h",
        "ThirdParty/imgui/backends/imgui_impl_vulkan.cpp",
        "ThirdParty/imgui/backends/imgui_impl_vulkan.h"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "*"
    -- ==========================================
    -- Project 4: Core Engine Application
    -- ==========================================
    project "Engine"
        -- [Garbage] Hide the Engine project file in the Intermediate folder
        location "Intermediate/Projects" 
        kind "ConsoleApp"
        
        -- [Useful] Output the final Engine executable to the root Bin folder
        targetdir "Bin"            
        -- Ensure Visual Studio debugger runs from the root to locate Assets properly
        debugdir "%{wks.location}" 
        
        defines {
            "GLM_ENABLE_EXPERIMENTAL",
            "GLM_FORCE_RADIANS",
            "NOMINMAX",
            "FLECS_STATIC",
            "JPH_CROSS_PLATFORM_DETERMINISTIC",
            "ZSTD_DISABLE_ASM=1"
        }

        includedirs {
            ".",
            "Source",                      
            "Source/Runtime/Rhi",          
            "ThirdParty/glfw/include",
            "ThirdParty/glm/include",
            "ThirdParty/vulkan/include",
            "ThirdParty/stb/include",
            "ThirdParty/volk/include",
            "ThirdParty/rapidobj/include",
            "ThirdParty/VulkanMemoryAllocator/include",
            "ThirdParty/zstd/include",
            "ThirdParty/tgen/include",
            "ThirdParty/reflect/include",
            "ThirdParty/flecs-4.1.4/include",
            "ThirdParty/JoltPhysics",
            "ThirdParty/imgui",
			"ThirdParty/imgui/backends",
			"ThirdParty/imgui/ImGuizmo",
        }
        
        files {
            "main.cpp",
            "Source/**.hpp", "Source/**.cpp", "Source/**.h", "Source/**.c",
            
            "ThirdParty/volk/src/volk.c",
            "ThirdParty/VulkanMemoryAllocator/src/*.cpp",
            "ThirdParty/tgen/src/*.cpp",
            "ThirdParty/zstd/src/common/*.c",
            "ThirdParty/zstd/src/decompress/*.c",
            "ThirdParty/flecs-4.1.4/distr/flecs.c",
            "ThirdParty/flecs-4.1.4/distr/flecs.h",
			"ThirdParty/imgui/ImGuizmo/ImGuizmo.cpp",
			"ThirdParty/imgui/ImGuizmo/ImGuizmo.h",
        }
        
        filter "files:ThirdParty/flecs-4.1.4/distr/flecs.c"
            flags { "NoPCH" }
            compileas "C"
        filter "*"

        links { "GLFW", "JoltPhysics" ,"ImGui" }
        dependson { "Shaders" } 

        filter "system:windows"
            defines { "VK_USE_PLATFORM_WIN32_KHR" }
            
        filter "system:linux"
            defines { "VK_USE_PLATFORM_XLIB_KHR" }
            links { "vulkan", "X11", "pthread" }
        filter "*"

    -- ==========================================
    -- Project 5: Automatic Shader Compilation
    -- ==========================================
    project "Shaders"
        -- [Garbage] Hide the Shaders project file in the Intermediate folder
        location "Intermediate/Projects" 
        kind "Utility" 
        
        -- Target shader files
        files { 
            "Assets/Shaders/*.vert", 
            "Assets/Shaders/*.frag",
            "Assets/Shaders/*.comp",
            "Assets/Shaders/*.geom"
        }

        -- Custom build commands for compiling shaders
        filter { "system:windows", "files:Assets/Shaders/*.vert or files:Assets/Shaders/*.frag or files:Assets/Shaders/*.comp or files:Assets/Shaders/*.geom" }
            buildmessage "Compiling shader %{file.name}..."
            buildcommands {
                "if not exist \"%{wks.location}\\Assets\\Shaders\\spirv\" mkdir \"%{wks.location}\\Assets\\Shaders\\spirv\"",
                "\"%{wks.location}/Assets/Shaders/glslc.exe\" \"%{file.abspath}\" -o \"%{wks.location}/Assets/Shaders/spirv/%{file.name}.spv\""
            }
            buildoutputs { "%{wks.location}/Assets/Shaders/spirv/%{file.name}.spv" }

        filter { "system:linux", "files:Assets/Shaders/*.vert or files:Assets/Shaders/*.frag or files:Assets/Shaders/*.comp or files:Assets/Shaders/*.geom" }
            buildmessage "Compiling shader %{file.name}..."
            buildcommands {
                "mkdir -p \"%{wks.location}/Assets/Shaders/spirv\"",
                "glslc \"%{file.abspath}\" -o \"%{wks.location}/Assets/Shaders/spirv/%{file.name}.spv\""
            }
            buildoutputs { "%{wks.location}/Assets/Shaders/spirv/%{file.name}.spv" }
        filter "*"

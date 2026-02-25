workspace "COMP5892M-vulkan"
	language "C++"
	cppdialect "C++23"

	platforms { "x64" }
	configurations { "debug", "release" }

	flags "NoPCH"
	flags "MultiProcessorCompile"

	startproject "a12"

	debugdir "%{wks.location}"
	objdir "_build_/%{cfg.buildcfg}-%{cfg.platform}-%{cfg.toolset}"
	targetsuffix "-%{cfg.buildcfg}-%{cfg.platform}-%{cfg.toolset}"
	
	-- Default toolset options
	filter "toolset:gcc or toolset:clang"
		linkoptions { "-pthread" }
		buildoptions { "-march=native", "-Wall", "-pthread" }

	filter "toolset:gcc"
		links { "stdc++exp" }

	filter "toolset:msc-*"
		defines { "_CRT_SECURE_NO_WARNINGS=1" }
		defines { "_SCL_SECURE_NO_WARNINGS=1" }
		buildoptions { "/utf-8" }
	
	filter "*"

	-- default options for GLSLC
	glslcOptions = "-O --target-env=vulkan1.2"

	-- default libraries
	filter "system:linux"
		links "dl"
	
	filter "system:windows"

	filter "*"

	-- default outputs
	filter "kind:StaticLib"
		targetdir "lib/"

	filter "kind:ConsoleApp"
		targetdir "bin/"
		targetextension ".exe"
	
	filter "*"

	--configurations
	filter "debug"
		symbols "On"
		defines { "_DEBUG=1" }

	filter "release"
		optimize "On"
		defines { "NDEBUG=1" }

	filter "*"

	defines { "SOLUTION_CODE=1" }
	glslcOptions = glslcOptions .. " -DSOLUTION_CODE"

-- Third party dependencies
include "third_party" 

-- GLSLC helpers
dofile( "util/glslc.lua" )

-- Projects






project "a12"
    local sources = { 
        "a12/**.cpp",
        "a12/**.hpp",
        "a12/**.hxx",
        -- 1. 将 flecs.c 显式加入编译源文件列表
        "flecs-4.1.4/distr/flecs.c", 
        "flecs-4.1.4/distr/flecs.h"
    }

    kind "ConsoleApp"
    location "a12"
	
    files( sources )
    
    -- 2. 添加 flecs 的包含目录，以便 #include "flecs.h" 能找到文件
    includedirs { "flecs-4.1.4/include" }

    -- 3. 全局定义 FLECS_STATIC，解决 __imp_ 链接错误
    defines { "FLECS_STATIC" }

    -- 4. 针对 C 语言文件的特殊处理（防止因为项目是 C++ 而导致的预编译头或语言冲突）
    filter "files:flecs-4.1.4/distr/flecs.c"
        flags { "NoPCH" } -- 确保不使用预编译头
        compileas "C"     -- 强制作为 C 语言编译
    filter "*"

    filter "system:linux"
        links { "pthread" } -- 显式链接线程库，Flecs 在 Linux 下需要它
    
    filter "files:flecs-4.1.4/distr/flecs.c"
        flags { "NoPCH" }
        -- Linux 默认编译器通常能根据扩展名识别 C 语言，但显式指定更安全
        compileas "C" 
    filter "*"

    dependson "a12-shaders"

    links "labut2"
    links "x-volk"
    links "x-stb"
    links "x-glfw"
    links "x-vma"

    dependson "x-glm"

project "a12-shaders"
	local shaders = { 
		"a12/shaders/*.vert",
		"a12/shaders/*.frag",
		"a12/shaders/*.comp",
		"a12/shaders/*.geom",
		"a12/shaders/*.tesc",
		"a12/shaders/*.tese"
	}

	kind "Utility"
	location "a12/shaders"

	files( shaders )

	handle_glsl_files( glslcOptions, "assets/a12/shaders", {} )

project "a12-bake"
	local sources = { 
		"a12-bake/**.cpp",
		"a12-bake/**.hpp",
		"a12-bake/**.hxx"
	}

	kind "ConsoleApp"
	location "a12-bake"

	files( sources )

	links "labut2" -- for lut::Error
	links "x-tgen"
	links "x-zstd"

	dependson "x-glm" 
	dependson "x-rapidobj"

project "labut2"
	local sources = {
		"labut2/**.cpp",
		"labut2/**.hpp",
		"labut2/**.hxx"
	}

	kind "StaticLib"
	location "labut2"

	files( sources )

project()

--EOF

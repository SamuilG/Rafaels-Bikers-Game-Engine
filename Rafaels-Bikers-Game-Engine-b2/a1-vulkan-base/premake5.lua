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
		"a12/**.hxx"
	}

	kind "ConsoleApp"
	location "a12"

	files( sources )

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

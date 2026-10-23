$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/anti-aliasing'
$shaderDirectory = Join-Path $outputDirectory 'shaders'
New-Item -ItemType Directory -Force -Path $shaderDirectory | Out-Null
$compiler = Join-Path $repoRoot 'glslc.exe'
foreach ($shader in @('fullscreen.vert', 'taa.frag', 'taa_motion.vert', 'taa_motion_skinned.vert', 'taa_motion.frag', '../../Tests/Rendering/Shaders/aa_triangle.vert', '../../Tests/Rendering/Shaders/aa_triangle.frag')) {
    & $compiler --target-env=vulkan1.3 ('-I' + (Join-Path $repoRoot 'Assets/Shaders')) (Join-Path $repoRoot "Assets/Shaders/$shader") -o (Join-Path $shaderDirectory ((Split-Path $shader -Leaf) + ".spv"))
    if ($LASTEXITCODE -ne 0) { throw "Production shader compilation failed: $shader" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
$sources = @('Tests/Rendering/AntiAliasingGpuTests.cpp', 'Source/Runtime/Renderer/RenderUtilities/AntiAliasing.cpp',
    'Source/Runtime/Rhi/allocator.cpp', 'Source/Runtime/Rhi/vkimage.cpp', 'Source/Runtime/Rhi/vkbuffer.cpp',
    'Source/Runtime/Rhi/commands.cpp', 'Source/Runtime/Rhi/synch.cpp', 'Source/Runtime/Rhi/error.cpp',
    'Source/Runtime/Rhi/to_string.cpp', 'Source/Runtime/Rhi/load.cpp', 'ThirdParty/volk/src/volk.c',
    'ThirdParty/VulkanMemoryAllocator/src/vk_mem_alloc.cpp', 'ThirdParty/stb/src/stb_image.c')
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/MDd', '/Od', '/Z7', '/utf-8', '/MP2', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS')
foreach ($include in @('Source', 'ThirdParty/volk/include', 'ThirdParty/volk/include/volk', 'ThirdParty/vulkan/include',
    'ThirdParty/glm/include', 'ThirdParty/stb/include', 'ThirdParty/VulkanMemoryAllocator/include', 'ThirdParty/reflect/include')) {
    $arguments += '/I"' + (Join-Path $repoRoot $include) + '"'
}
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'AntiAliasingGpuTests.exe') + '"'
$buildScript = Join-Path $outputDirectory 'build-tests.cmd'
$buildCommand = @"
@echo off
setlocal
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$repoRoot"
cl $($arguments -join ' ')
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($buildScript, $buildCommand, [Text.UTF8Encoding]::new($false))
$buildLog = Join-Path $outputDirectory 'build.log'
& $env:ComSpec /d /c "`"$buildScript`"" 2>&1 | Out-File -LiteralPath $buildLog -Encoding utf8
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $buildLog -Tail 40; throw "Anti-aliasing GPU test build failed. See $buildLog" }
& (Join-Path $outputDirectory 'AntiAliasingGpuTests.exe') $shaderDirectory | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw 'Anti-aliasing GPU regression failed.' }

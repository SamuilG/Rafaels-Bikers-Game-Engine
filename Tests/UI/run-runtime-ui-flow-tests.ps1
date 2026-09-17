param([switch]$GameOnly)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$variant = if ($GameOnly) { 'game' } else { 'editor' }
$outputDirectory = Join-Path $repoRoot "Intermediate/Diagnostics/runtime-ui-flow/$variant"
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "C++ environment script was not found: $vcvars" }

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$sources = @(
    'Tests/UI/RuntimeUiFlowTests.cpp',
    'Tests/UI/HeadlessUiAdapters.cpp',
    'Source/Runtime/UI/VisualUIEditor/GameUIEventRouter.cpp',
    'Source/Runtime/UI/VisualUIEditor/UIManager.cpp',
    'Source/Runtime/UI/VisualUIEditor/UIScreen.cpp',
    'Source/Runtime/UI/VisualUIEditor/UIElement.cpp',
    'Source/Runtime/UI/VisualUIEditor/UITransform.cpp',
    'Source/Runtime/UI/VisualUIEditor/UIAnimation.cpp',
    'Source/Runtime/UI/VisualUIEditor/UITheme.cpp',
    'Source/Runtime/UI/VisualUIEditor/UISerializer.cpp',
    # EngineUi.hpp includes Flecs' C++ facade, whose builtin entity constants
    # reference the C library even though no ECS world is created by this suite.
    'ThirdParty/flecs-4.1.4/distr/flecs.c'
)
$includes = @(
    '.', 'Source', 'Source/Runtime/Rhi',
    'ThirdParty/glm/include', 'ThirdParty/glfw/include',
    'ThirdParty/vulkan/include', 'ThirdParty/volk/include',
    'ThirdParty/VulkanMemoryAllocator/include', 'ThirdParty/flecs-4.1.4/include',
    'ThirdParty/imgui', 'ThirdParty/JoltPhysics'
)
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/MDd', '/Od', '/Zi', '/utf-8', '/MP2',
    '/D_DEBUG=1', '/DDEBUG=1', '/DNOMINMAX', '/DGLM_ENABLE_EXPERIMENTAL', '/Dflecs_STATIC=')
if ($GameOnly) { $arguments += '/DGAME_ONLY=1' }
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fd"' + (Join-Path $outputDirectory 'compile.pdb') + '"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'RuntimeUiFlowTests.exe') + '"'
$arguments += '/link'
$arguments += '/PDB:"' + (Join-Path $outputDirectory 'RuntimeUiFlowTests.pdb') + '"'
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
Set-Content -LiteralPath $buildScript -Value $buildCommand -Encoding Ascii
$buildLog = Join-Path $outputDirectory 'build.log'
& $env:ComSpec /d /c "`"$buildScript`"" 2>&1 | Out-File -LiteralPath $buildLog -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $buildLog -Tail 45
    throw "UI regression build failed with exit code $LASTEXITCODE. See $buildLog"
}
Write-Host "Built $variant UI flow tests. Compiler diagnostics: $buildLog"

Push-Location -LiteralPath $repoRoot
try {
    $testExecutable = Join-Path $outputDirectory 'RuntimeUiFlowTests.exe'
    # Let cmd merge native stderr so Windows PowerShell does not turn a failed
    # assertion into a terminating NativeCommandError before the log is saved.
    & $env:ComSpec /d /c "`"$testExecutable`" 2>&1" | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
    if ($LASTEXITCODE -ne 0) { throw "UI regression tests failed with exit code $LASTEXITCODE." }
}
finally { Pop-Location }

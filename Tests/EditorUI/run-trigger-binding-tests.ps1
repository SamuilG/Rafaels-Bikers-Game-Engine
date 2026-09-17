$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/trigger-bindings'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$sources = @('Tests/EditorUI/TriggerBindingTests.cpp', 'Source/Runtime/Trigger/trigger.cpp',
    'ThirdParty/flecs-4.1.4/distr/flecs.c')
$includes = @('Source', 'Source/Runtime/Rhi', 'ThirdParty/glm/include', 'ThirdParty/glfw/include',
    'ThirdParty/vulkan/include', 'ThirdParty/volk/include', 'ThirdParty/VulkanMemoryAllocator/include',
    'ThirdParty/flecs-4.1.4/include', 'ThirdParty/imgui', 'ThirdParty/JoltPhysics')
# Compile the real trigger implementation and flecs initialization dependencies.
# The test fails if any debug drawing or GPU cleanup boundary is ever entered.
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/MD', '/O2', '/GL', '/Gy', '/Gw', '/utf-8',
    '/DNOMINMAX', '/DGLM_ENABLE_EXPERIMENTAL', '/Dflecs_STATIC=')
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'TriggerBindingTests.exe') + '"'
$arguments += '/link', '/LTCG', '/OPT:REF', '/INCREMENTAL:NO'
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
    Get-Content -LiteralPath $buildLog -Tail 35
    throw "Trigger binding test build failed. See $buildLog"
}
Write-Host "Built real TriggerSystem tests. Compiler diagnostics: $buildLog"
$testExecutable = Join-Path $outputDirectory 'TriggerBindingTests.exe'
& $env:ComSpec /d /c "`"$testExecutable`" 2>&1" | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "Trigger binding tests failed with exit code $LASTEXITCODE." }

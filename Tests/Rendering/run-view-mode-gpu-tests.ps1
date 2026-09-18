$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/view-modes'
$shaderDirectory = Join-Path $outputDirectory 'shaders'
New-Item -ItemType Directory -Force -Path $shaderDirectory | Out-Null
$compiler = Join-Path $repoRoot 'glslc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { $compiler = Join-Path $repoRoot 'ThirdParty/shaderc/win-x86_64/glslc.exe' }
foreach ($shader in @('debug.vert', 'skinned.vert', 'debug_mip.frag', 'debug_depth.frag', 'debug_deriv.frag', 'overdraw.frag')) {
    & $compiler --target-env=vulkan1.3 ('-I' + (Join-Path $repoRoot 'Assets/Shaders')) (Join-Path $repoRoot "Assets/Shaders/$shader") -o (Join-Path $shaderDirectory "$shader.spv")
    if ($LASTEXITCODE -ne 0) { throw "Production shader compilation failed: $shader" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
$sources = @('Tests/Rendering/ViewModeGpuTests.cpp', 'ThirdParty/volk/src/volk.c')
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/MDd', '/Od', '/Zi', '/utf-8', '/MP2', '/D_DEBUG=1')
foreach ($include in @('ThirdParty/volk/include', 'ThirdParty/volk/include/volk', 'ThirdParty/vulkan/include', 'ThirdParty/glm/include', 'ThirdParty/stb/include')) {
    $arguments += '/I"' + (Join-Path $repoRoot $include) + '"'
}
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fd"' + (Join-Path $outputDirectory 'compile.pdb') + '"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'ViewModeGpuTests.exe') + '"'
$arguments += '/link'
$arguments += '/PDB:"' + (Join-Path $outputDirectory 'ViewModeGpuTests.pdb') + '"'
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
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $buildLog -Tail 40; throw "GPU test build failed. See $buildLog" }
Write-Host "Compiled production debug shaders and standalone Vulkan GPU tests. Compiler diagnostics: $buildLog"
$testExecutable = Join-Path $outputDirectory 'ViewModeGpuTests.exe'
& $env:ComSpec /d /c "`"$testExecutable`" `"$shaderDirectory`" `"$outputDirectory`" 2>&1" | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "View mode GPU tests failed with exit code $LASTEXITCODE." }

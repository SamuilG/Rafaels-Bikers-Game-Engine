$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/gpu-frame-timer-device'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$arguments = @('/nologo', '/std:c++20', '/EHsc', '/MDd', '/Od', '/utf-8', '/W4')
foreach ($include in @('Source', 'ThirdParty/vulkan/include', 'ThirdParty/volk/include', 'ThirdParty/volk/include/volk')) { $arguments += '/I"' + (Join-Path $repoRoot $include) + '"' }
$arguments += '"' + (Join-Path $repoRoot 'Tests/Rendering/GpuFrameTimerDeviceTests.cpp') + '"'
$arguments += '"' + (Join-Path $repoRoot 'ThirdParty/volk/src/volk.c') + '"'
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'GpuFrameTimerDeviceTests.exe') + '"'
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
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $buildLog -Tail 35
    throw "GPU frame timer device test build failed. See $buildLog"
}
& (Join-Path $outputDirectory 'GpuFrameTimerDeviceTests.exe') | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw 'GPU frame timer device regression failed.' }

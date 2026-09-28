$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/scene-frame'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$arguments = @('/nologo', '/std:c++20', '/EHsc', '/MDd', '/Od', '/Zi', '/utf-8', '/MP2', '/Dflecs_STATIC=')
foreach ($include in @('Source', 'ThirdParty/flecs-4.1.4/include')) { $arguments += '/I"' + (Join-Path $repoRoot $include) + '"' }
foreach ($source in @('Tests/State/SceneFrameTests.cpp', 'ThirdParty/flecs-4.1.4/distr/flecs.c')) { $arguments += '"' + (Join-Path $repoRoot $source) + '"' }
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fd"' + (Join-Path $outputDirectory 'compile.pdb') + '"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'SceneFrameTests.exe') + '"'
$arguments += '/link', ('/PDB:"' + (Join-Path $outputDirectory 'SceneFrameTests.pdb') + '"')
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
    Get-Content -LiteralPath $buildLog -Tail 45
    throw "Scene frame test build failed. See $buildLog"
}
Write-Host "Built real Flecs scene-frame tests. Compiler diagnostics: $buildLog"
$testExecutable = Join-Path $outputDirectory 'SceneFrameTests.exe'
& $env:ComSpec /d /c "`"$testExecutable`" 2>&1" | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "Scene frame tests failed with exit code $LASTEXITCODE." }

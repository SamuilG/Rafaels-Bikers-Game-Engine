$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/frame-graph'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio x64 C++ build tools were not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
$executable = Join-Path $outputDirectory 'FrameGraphTests.exe'
$buildScript = Join-Path $outputDirectory 'build-tests.cmd'
$command = @"
@echo off
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$repoRoot"
cl /nologo /std:c++20 /EHsc /MD /Od /Z7 /utf-8 /W4 /I"$repoRoot/Source" "$repoRoot/Tests/Rendering/FrameGraphTests.cpp" /Fo"$outputDirectory/FrameGraphTests.obj" /Fe"$executable" /link /DEBUG:NONE
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($buildScript, $command, [Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c "`"$buildScript`"" *> (Join-Path $outputDirectory 'build.log')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $outputDirectory 'build.log') -Tail 35; throw 'Frame graph CPU test build failed.' }
& $executable | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw 'Frame graph CPU tests failed.' }

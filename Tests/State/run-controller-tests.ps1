$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/state-controllers'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$arguments = @('/nologo', '/std:c++20', '/EHsc', '/MDd', '/Od', '/Zi', '/utf-8', '/W4', '/DGLM_ENABLE_EXPERIMENTAL')
foreach ($include in @('Source', 'ThirdParty/glm/include')) { $arguments += '/I"' + (Join-Path $repoRoot $include) + '"' }
$arguments += '"' + (Join-Path $repoRoot 'Tests/State/ControllerTests.cpp') + '"'
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fd"' + (Join-Path $outputDirectory 'compile.pdb') + '"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'ControllerTests.exe') + '"'
$arguments += '/link', ('/PDB:"' + (Join-Path $outputDirectory 'ControllerTests.pdb') + '"')
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
    throw "State controller test build failed. See $buildLog"
}
Write-Host "Built real CPU state/controller tests. Compiler diagnostics: $buildLog"
$testExecutable = Join-Path $outputDirectory 'ControllerTests.exe'
& $env:ComSpec /d /c "`"$testExecutable`" 2>&1" | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "State controller tests failed with exit code $LASTEXITCODE." }

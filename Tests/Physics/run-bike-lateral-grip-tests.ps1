$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/portal-jitter-fix'
$joltLibrary = Join-Path $repoRoot 'Intermediate/Bin/Debug-x64/JoltPhysics/JoltPhysics.lib'
if (-not (Test-Path -LiteralPath $joltLibrary)) {
    throw "Build the project's x64 Debug JoltPhysics target first: $joltLibrary"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = $null
if (Test-Path -LiteralPath $vswhere) {
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $visualStudio) {
    $visualStudio = Join-Path $env:ProgramFiles 'Microsoft Visual Studio/18/Community'
}
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) {
    throw 'Visual Studio with the x64 C++ build tools was not found.'
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$buildCommand = @'
@echo off
setlocal
call "{0}"
if errorlevel 1 exit /b %errorlevel%
cd /d "{1}"
cl /nologo /std:c++latest /EHsc /MDd /Zi /D_DEBUG=1 /DDEBUG=1 /DJPH_CROSS_PLATFORM_DETERMINISTIC /I"ThirdParty\JoltPhysics" /I"Source\Runtime" "Tests\Physics\BikeLateralGripTests.cpp" /Fo"Intermediate\Diagnostics\portal-jitter-fix\BikeLateralGripTests.obj" /Fd"Intermediate\Diagnostics\portal-jitter-fix\compile.pdb" /Fe"Intermediate\Diagnostics\portal-jitter-fix\BikeLateralGripTests.exe" /link "Intermediate\Bin\Debug-x64\JoltPhysics\JoltPhysics.lib" /PDB:"Intermediate\Diagnostics\portal-jitter-fix\BikeLateralGripTests.pdb"
exit /b %errorlevel%
'@ -f $vcvars, $repoRoot
$buildScript = Join-Path $outputDirectory 'build-tests.cmd'
Set-Content -LiteralPath $buildScript -Value $buildCommand -Encoding Ascii
& $env:ComSpec /d /c "`"$buildScript`"" 2>&1 | Tee-Object -FilePath (Join-Path $outputDirectory 'build.log')
if ($LASTEXITCODE -ne 0) { throw "Regression build failed with exit code $LASTEXITCODE." }

$testExecutable = Join-Path $outputDirectory 'BikeLateralGripTests.exe'
& $testExecutable 2>&1 | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "Regression tests failed with exit code $LASTEXITCODE." }

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/editor-layout'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$sources = @(
    'Tests/EditorUI/EditorLayoutTests.cpp',
    'Source/Runtime/UI/SwitchLanguage.cpp',
    'ThirdParty/imgui/imgui.cpp',
    'ThirdParty/imgui/imgui_draw.cpp',
    'ThirdParty/imgui/imgui_tables.cpp',
    'ThirdParty/imgui/imgui_widgets.cpp'
)
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/MDd', '/Od', '/Zi', '/utf-8', '/MP2', '/D_DEBUG=1')
$arguments += '/I"' + (Join-Path $repoRoot 'Source') + '"'
$arguments += '/I"' + (Join-Path $repoRoot 'ThirdParty/imgui') + '"'
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$arguments += '/Fo"' + $outputDirectory + '\\"'
$arguments += '/Fd"' + (Join-Path $outputDirectory 'compile.pdb') + '"'
$arguments += '/Fe"' + (Join-Path $outputDirectory 'EditorLayoutTests.exe') + '"'
$arguments += '/link'
$arguments += '/PDB:"' + (Join-Path $outputDirectory 'EditorLayoutTests.pdb') + '"'
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
    Get-Content -LiteralPath $buildLog -Tail 40
    throw "Editor layout test build failed. See $buildLog"
}
Write-Host "Built standalone ImGui layout tests. Compiler diagnostics: $buildLog"
$testExecutable = Join-Path $outputDirectory 'EditorLayoutTests.exe'
& $env:ComSpec /d /c "`"$testExecutable`" 2>&1" | Tee-Object -FilePath (Join-Path $outputDirectory 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "Editor layout tests failed with exit code $LASTEXITCODE." }

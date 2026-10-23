$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $repoRoot 'Intermediate/Diagnostics/anti-aliasing-integration'
$projectFile = Join-Path $repoRoot 'Intermediate/Projects/Editor.vcxproj'
if (-not (Test-Path -LiteralPath $projectFile)) { throw 'Generate the Visual Studio projects first.' }
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio with x64 C++ build tools was not found.' }
$vcvars = Join-Path $visualStudio 'VC/Auxiliary/Build/vcvars64.bat'

# Match the actual executable's compile contract and link the built production
# libraries. The test creates real Vulkan/GLFW/Jolt systems with a hidden window.
[xml] $project = Get-Content -LiteralPath $projectFile -Raw
$definitions = @($project.Project.ItemDefinitionGroup | Where-Object { $_.Condition -match 'Release\|x64' })[0]
if (-not $definitions) { throw 'Editor.vcxproj has no Release|x64 compile definitions.' }
$arguments = @('/nologo', '/std:c++latest', '/EHsc', '/MD', '/Od', '/utf-8', '/bigobj', '/Z7')
foreach ($definition in $definitions.ClCompile.PreprocessorDefinitions.Split(';')) {
    if ($definition -and -not $definition.StartsWith('%(')) { $arguments += '/D' + $definition }
}
foreach ($include in $definitions.ClCompile.AdditionalIncludeDirectories.Split(';')) {
    if ($include -and -not $include.StartsWith('%(')) {
        $absolute = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $projectFile) $include))
        $arguments += '/I"' + $absolute + '"'
    }
}
$arguments += '"' + (Join-Path $repoRoot 'Tests/Rendering/AntiAliasingIntegrationTests.cpp') + '"'
$arguments += '/Fo"' + (Join-Path $outputDirectory 'AntiAliasingIntegrationTests.obj') + '"'
$executable = Join-Path $outputDirectory 'AntiAliasingIntegrationTests.exe'
$arguments += '/Fe"' + $executable + '"'
foreach ($library in @('BikersProject', 'Engine', 'GLFW', 'JoltPhysics', 'ImGui')) {
    $path = Join-Path $repoRoot "Intermediate/Bin/Release-x64/$library/$library.lib"
    if (-not (Test-Path -LiteralPath $path)) { throw "Build Release x64 first; missing $path" }
    $arguments += '"' + $path + '"'
}
$arguments += '/link', '/INCREMENTAL:NO', '/MACHINE:X64', 'opengl32.lib', 'comdlg32.lib',
    'user32.lib', 'gdi32.lib', 'shell32.lib', 'ole32.lib', 'winmm.lib', 'advapi32.lib'
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
    Get-Content -LiteralPath $buildLog -Tail 50
    throw "Anti-aliasing integration test build failed. See $buildLog"
}

$stdout = Join-Path $outputDirectory 'results.txt'
$stderr = Join-Path $outputDirectory 'errors.txt'
$process = Start-Process -FilePath $executable -ArgumentList ('"' + $outputDirectory + '"') `
    -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
# Retain the native process handle before it exits. Windows PowerShell 5 can
# otherwise return a null ExitCode for a short-lived Start-Process -PassThru.
$null = $process.Handle
if (-not $process.WaitForExit(180000)) {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    throw 'The hidden integration process exceeded 180 seconds; see its diagnostics.'
}
$process.WaitForExit()
Get-Content -LiteralPath $stdout -Tail 30
if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Tail 30 }
if ($process.ExitCode -ne 0) { throw "Anti-aliasing integration failed with exit code $($process.ExitCode)." }

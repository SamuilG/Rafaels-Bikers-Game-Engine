param([string] $Destination)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectRoot = Join-Path $repoRoot 'Projects/Bikers'
$manifestPath = Join-Path $projectRoot 'Bikers.steerproj'
if (-not $Destination) { $Destination = Join-Path $repoRoot 'Intermediate/Packages/Bikers' }
$outputRoot = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
if (Test-Path -LiteralPath $outputRoot) {
    throw "Package destination already exists: $outputRoot. Choose a new directory; existing files will not be deleted or overwritten."
}

$sourceDirectories = @('Assets', 'Source/Projects/Bikers')
$sourceFiles = @(
    'gamecontrollerdb.txt',
    'Source/Runtime/Scene/level1.cpp', 'Source/Runtime/Scene/level1.hpp',
    'Source/Runtime/Scene/TestScene.cpp', 'Source/Runtime/Scene/TestScene.hpp',
    'Source/Runtime/Physics/bikeController.cpp', 'Source/Runtime/Physics/bikeController.hpp',
    'Source/Runtime/UI/GameUi.cpp', 'Source/Runtime/UI/GameUi.hpp'
)
foreach ($relative in $sourceDirectories) {
    $source = [IO.Path]::GetFullPath((Join-Path $repoRoot $relative)).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Required project directory is missing: $source. Finish creating the Bikers module before packaging."
    }
    if ($outputRoot.Equals($source, [StringComparison]::OrdinalIgnoreCase) -or
        $outputRoot.StartsWith($source + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package destination must be outside copied source directories: $outputRoot"
    }
}
foreach ($relative in ($sourceFiles + @('Projects/Bikers/Bikers.steerproj', 'Projects/Bikers/README.md'))) {
    $source = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required project file is missing: $source" }
}
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'Assets/Shaders') -PathType Container)) {
    throw 'The project Assets directory must contain Shaders.'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $manifest.contentModule -cne 'bikers') {
    throw 'Bikers.steerproj must describe schemaVersion 1 and the bikers content module.'
}
$snapshotRelative = [string]$manifest.sceneSnapshot
if (-not $snapshotRelative -or [IO.Path]::IsPathRooted($snapshotRelative)) {
    throw 'The project sceneSnapshot must be a nonempty relative path.'
}
$snapshotSource = [IO.Path]::GetFullPath((Join-Path $projectRoot $snapshotRelative))
$normalizedProjectRoot = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $snapshotSource.StartsWith($normalizedProjectRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The project sceneSnapshot must stay inside Projects/Bikers.'
}
if ((Test-Path -LiteralPath $snapshotSource) -and -not (Test-Path -LiteralPath $snapshotSource -PathType Leaf)) {
    throw "The scene snapshot is not a file: $snapshotSource"
}

# All inputs are validated before any package files are created. A copy failure
# deliberately leaves its new destination for inspection rather than deleting it.
New-Item -ItemType Directory -Path $outputRoot | Out-Null
foreach ($relative in $sourceDirectories) {
    $target = Join-Path $outputRoot $relative
    $parent = Split-Path -Parent $target
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot $relative) -Destination $target -Recurse
}
foreach ($relative in $sourceFiles) {
    $target = Join-Path $outputRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot $relative) -Destination $target
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $outputRoot 'README.md')
if (Test-Path -LiteralPath $snapshotSource -PathType Leaf) {
    $snapshotTarget = Join-Path $outputRoot $snapshotRelative
    if ((Test-Path -LiteralPath $snapshotTarget) -or
        [IO.Path]::GetFullPath($snapshotTarget).Equals((Join-Path $outputRoot 'Bikers.steerproj'), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Scene snapshot destination conflicts with package content: $snapshotTarget"
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $snapshotTarget) | Out-Null
    Copy-Item -LiteralPath $snapshotSource -Destination $snapshotTarget
}
$manifest.assetRoot = 'Assets'
$json = $manifest | ConvertTo-Json -Depth 16
[IO.File]::WriteAllText((Join-Path $outputRoot 'Bikers.steerproj'), $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Host "Packaged Bikers resources and source: $outputRoot"
Write-Host "Open $(Join-Path $outputRoot 'Bikers.steerproj') with the current Editor."
Write-Host 'The bikers content module is compiled into Editor/Demo; this package is not a hot-loadable DLL.'

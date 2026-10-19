$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$projectDirectory = Join-Path $repoRoot 'Intermediate/Projects'
$script:assertionCount = 0

function Assert-Target([bool] $condition, [string] $message) {
    if (-not $condition) { throw "Build target boundary failed: $message" }
    $script:assertionCount++
}

function Read-Project([string] $name) {
    $path = Join-Path $projectDirectory "$name.vcxproj"
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Generated project missing: $path. Run .\premake5.exe vs2026 first."
    }
    $xml = [xml](Get-Content -LiteralPath $path -Raw)
    $namespace = [Xml.XmlNamespaceManager]::new($xml.NameTable)
    $namespace.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
    return @{ Name = $name; Xml = $xml; Namespace = $namespace }
}

function Get-Nodes($project, [string] $xpath) {
    return @($project.Xml.SelectNodes($xpath, $project.Namespace))
}

function Test-Configuration([Xml.XmlElement] $node, [string] $configuration) {
    $condition = $node.GetAttribute('Condition')
    return (-not $condition) -or ($condition -match "==\s*'$([regex]::Escape($configuration))\|x64'")
}

function Get-ConfigurationValue($project, [string] $configuration, [string] $group, [string] $property) {
    $values = @()
    foreach ($node in (Get-Nodes $project "/msb:Project/msb:$group")) {
        if (Test-Configuration $node $configuration) {
            foreach ($value in @($node.SelectNodes("msb:$property", $project.Namespace))) {
                $values += $value.InnerText
            }
        }
    }
    Assert-Target ($values.Count -eq 1) "$($project.Name)/$configuration must define one $property; found $($values.Count)."
    return $values[0]
}

function Get-CompileValue($project, [string] $configuration, [string] $property) {
    $values = @()
    foreach ($node in (Get-Nodes $project '/msb:Project/msb:ItemDefinitionGroup')) {
        if (Test-Configuration $node $configuration) {
            foreach ($value in @($node.SelectNodes("msb:ClCompile/msb:$property", $project.Namespace))) {
                $values += $value.InnerText
            }
        }
    }
    Assert-Target ($values.Count -eq 1) "$($project.Name)/$configuration must define one compiler $property."
    return @($values[0].Split(';') | Where-Object { $_ -and $_ -notmatch '^%\(' } | Sort-Object -Unique)
}

function Resolve-ProjectPath([string] $value, [string] $name, [string] $configuration) {
    $resolved = $value.Replace('$(ProjectDir)', ($projectDirectory + '\')).Replace('$(SolutionDir)', ($repoRoot + '\'))
    $resolved = $resolved.Replace('$(ProjectName)', $name).Replace('$(Configuration)', $configuration).Replace('$(Platform)', 'x64')
    Assert-Target ($resolved -notmatch '\$\(') "Cannot resolve generated path $value for $name/$configuration."
    if (-not [IO.Path]::IsPathRooted($resolved)) { $resolved = Join-Path $projectDirectory $resolved }
    return [IO.Path]::GetFullPath($resolved).TrimEnd('\', '/').ToLowerInvariant()
}

$projects = @{}
$sources = @{}
$targetNames = @('Engine', 'BikersProject', 'Editor', 'Demo')
$bikersFiles = @(
    'source/runtime/scene/level1.cpp',
    'source/runtime/scene/testscene.cpp',
    'source/runtime/physics/bikecontroller.cpp',
    'source/runtime/ui/gameui.cpp',
    'source/projects/bikers/bikersproject.cpp'
)
$demoFiles = @(
    'source/runtime/loadenginesplash/startupsplash.cpp'
)
$blankFile = 'source/runtime/scene/blank.cpp'
$outputPaths = @{}
$intermediatePaths = @{}
$compileContracts = @{}

foreach ($name in $targetNames) {
    $project = Read-Project $name
    $projects[$name] = $project
    $configurations = @(Get-Nodes $project '/msb:Project/msb:ItemGroup/msb:ProjectConfiguration' | ForEach-Object { $_.GetAttribute('Include') } | Sort-Object)
    Assert-Target (($configurations -join ';') -eq 'Debug|x64;Release|x64') "$name must expose only Debug|x64 and Release|x64."

    $sourceFiles = @()
    foreach ($compile in (Get-Nodes $project '/msb:Project/msb:ItemGroup/msb:ClCompile[@Include]')) {
        $absolute = Resolve-ProjectPath $compile.GetAttribute('Include') $name 'Debug'
        Assert-Target (Test-Path -LiteralPath $absolute -PathType Leaf) "$name references missing source $absolute."
        $relative = $absolute.Substring($repoRoot.Length).TrimStart('\', '/').Replace('\', '/')
        $sourceFiles += $relative
        Assert-Target (-not $compile.GetAttribute('Condition')) "$name source $relative has a conditional inclusion; validate both configurations explicitly before allowing this."
        Assert-Target (@($compile.SelectNodes('msb:ExcludedFromBuild', $project.Namespace) | Where-Object { $_.InnerText -eq 'true' }).Count -eq 0) "$name source $relative must compile in both configurations."
    }
    Assert-Target ($sourceFiles.Count -gt 0) "$name must compile actual sources."
    Assert-Target (@($sourceFiles | Sort-Object -Unique).Count -eq $sourceFiles.Count) "$name contains duplicate compile items."
    $sources[$name] = $sourceFiles

    foreach ($configuration in @('Debug', 'Release')) {
        $expectedKind = if ($name -in @('Engine', 'BikersProject')) { 'StaticLibrary' } else { 'Application' }
        $kind = Get-ConfigurationValue $project $configuration 'PropertyGroup' 'ConfigurationType'
        Assert-Target ($kind -eq $expectedKind) "$name/$configuration is $kind, expected $expectedKind."
        $targetName = Get-ConfigurationValue $project $configuration 'PropertyGroup' 'TargetName'
        $extension = Get-ConfigurationValue $project $configuration 'PropertyGroup' 'TargetExt'
        $expectedExtension = if ($name -in @('Engine', 'BikersProject')) { '.lib' } else { '.exe' }
        Assert-Target ($targetName -eq $name -and $extension -eq $expectedExtension) "$name/$configuration must produce $name$expectedExtension."
        $outDir = Get-ConfigurationValue $project $configuration 'PropertyGroup' 'OutDir'
        $intDir = Get-ConfigurationValue $project $configuration 'PropertyGroup' 'IntDir'
        $outputPath = Join-Path (Resolve-ProjectPath $outDir $name $configuration) ($targetName + $extension)
        $intermediatePath = Resolve-ProjectPath $intDir $name $configuration
        Assert-Target (-not $outputPaths.ContainsKey($outputPath)) "$name/$configuration output collides with another target/configuration: $outputPath."
        Assert-Target (-not $intermediatePaths.ContainsKey($intermediatePath)) "$name/$configuration intermediate directory collides with another target/configuration: $intermediatePath."
        $outputPaths[$outputPath] = "$name/$configuration"
        $intermediatePaths[$intermediatePath] = "$name/$configuration"

        $definitions = @(Get-CompileValue $project $configuration 'PreprocessorDefinitions')
        $includes = @(Get-CompileValue $project $configuration 'AdditionalIncludeDirectories' | ForEach-Object { Resolve-ProjectPath $_ $name $configuration } | Sort-Object -Unique)
        Assert-Target (@($definitions | Where-Object { $_ -match '^GAME_ONLY(?:=|$)' }).Count -eq 0) "$name/$configuration still uses GAME_ONLY."
        Assert-Target (@($definitions | Where-Object { $_ -cmatch '^FLECS_STATIC(?:=|$)' }).Count -eq 0) "$name/$configuration defines FLECS_STATIC; Flecs requires the case-sensitive spelling flecs_STATIC."
        foreach ($required in @('flecs_STATIC', 'JPH_CROSS_PLATFORM_DETERMINISTIC', 'GLM_ENABLE_EXPERIMENTAL', 'GLM_FORCE_RADIANS')) {
            Assert-Target ($definitions -ccontains $required) "$name/$configuration is missing shared definition $required (case-sensitive)."
        }
        $compileContracts["$name/$configuration"] = @{
            AbiDefinitions = @($definitions | Where-Object { $_ -cmatch '^(flecs_|FLECS_|JPH_|GLM_)' }); Includes = $includes
        }
    }
}
Write-Host '[PASS] Target kinds, source existence, configurations, and distinct final/intermediate outputs.'

foreach ($file in ($bikersFiles + $demoFiles)) {
    Assert-Target ($sources.Engine -notcontains $file) "Engine must not compile application source $file."
}
Assert-Target (@($sources.Engine | Where-Object { $_ -match '/loadenginesplash/' -or $_ -match '(?:^|/)main\.(?:c|cc|cpp|cxx)$' -or $_ -match '^source/(applications|projects)/' }).Count -eq 0) 'Engine must not compile splash implementations, project content, or application entry points.'
foreach ($file in $bikersFiles) {
    Assert-Target ($sources.BikersProject -contains $file) "BikersProject must compile $file."
    foreach ($name in @('Editor', 'Demo')) {
        Assert-Target ($sources[$name] -notcontains $file) "$name must link BikersProject without recompiling $file."
    }
}
Assert-Target (@($sources.BikersProject | Where-Object { $_ -match '(?:^|/)main\.(?:c|cc|cpp|cxx)$' -or $_ -match '^source/applications/' -or $_ -match '/loadenginesplash/' }).Count -eq 0) 'BikersProject must not own application entry points or the Demo splash.'
foreach ($file in $demoFiles) {
    Assert-Target ($sources.Demo -contains $file) "Demo must compile $file."
    Assert-Target ($sources.Editor -notcontains $file) "Editor must not compile Demo source $file."
}
Assert-Target ($sources.Engine -contains $blankFile) 'Engine must compile the shared generic scene once.'
Assert-Target ($sources.Editor -notcontains $blankFile) 'Editor must link the shared generic scene.'
Assert-Target ($sources.Demo -notcontains $blankFile) 'Demo must link the shared generic scene.'
Assert-Target ($sources.BikersProject -notcontains $blankFile) 'BikersProject must not compile the Editor blank scene.'
foreach ($name in @('Editor', 'Demo')) {
    $entry = "source/applications/$($name.ToLowerInvariant())/main.cpp"
    Assert-Target ($sources[$name] -contains $entry) "$name must compile its own entry point $entry."
    $mains = @($sources[$name] | Where-Object { $_ -match '(?:^|/)main\.(?:c|cc|cpp|cxx)$' })
    Assert-Target ($mains.Count -eq 1) "$name must compile exactly one entry point."
    Assert-Target (@($sources[$name] | Where-Object { $sources.Engine -contains $_ }).Count -eq 0) "$name must consume Engine through its project reference, without recompiling Engine sources."
}
for ($first = 0; $first -lt $targetNames.Count; $first++) {
    for ($second = $first + 1; $second -lt $targetNames.Count; $second++) {
        $left = $targetNames[$first]
        $right = $targetNames[$second]
        Assert-Target (@($sources[$left] | Where-Object { $sources[$right] -contains $_ }).Count -eq 0) "$left and $right must not compile the same source."
    }
}
Write-Host '[PASS] Engine, BikersProject, Editor, and Demo source ownership and entry points.'

foreach ($name in $targetNames) {
    $references = @(Get-Nodes $projects[$name] '/msb:Project/msb:ItemGroup/msb:ProjectReference[@Include]' | ForEach-Object {
        [IO.Path]::GetFileNameWithoutExtension($_.GetAttribute('Include'))
    })
    if ($name -eq 'Engine') {
        Assert-Target ($references -notcontains 'BikersProject' -and $references -notcontains 'Editor' -and $references -notcontains 'Demo') 'Engine must not depend on project content or either application.'
    } elseif ($name -eq 'BikersProject') {
        Assert-Target ($references -notcontains 'Editor' -and $references -notcontains 'Demo') 'BikersProject must not depend on either application.'
    } else {
        Assert-Target ($references -contains 'Engine') "$name must reference Engine."
        Assert-Target ($references -contains 'BikersProject') "$name must reference BikersProject."
        $other = if ($name -eq 'Editor') { 'Demo' } else { 'Editor' }
        Assert-Target ($references -notcontains $other) "$name must not depend on $other."
    }
}
Write-Host '[PASS] Applications share BikersProject and Engine without reverse content/application dependencies.'

foreach ($configuration in @('Debug', 'Release')) {
    $engineContract = $compileContracts["Engine/$configuration"]
    foreach ($name in @('BikersProject', 'Editor', 'Demo')) {
        $contract = $compileContracts["$name/$configuration"]
        Assert-Target (($engineContract.AbiDefinitions -join ';') -ceq ($contract.AbiDefinitions -join ';')) "$name/$configuration differs from Engine's FLECS/JPH/GLM definitions."
        foreach ($include in $engineContract.Includes) {
            Assert-Target ($contract.Includes -contains $include) "$name/$configuration is missing shared Engine include directory $include."
        }
    }
}
Write-Host '[PASS] Shared include paths and FLECS/JPH/GLM compile contract; no GAME_ONLY target variant.'

$solutionPath = Join-Path $repoRoot 'EngineWorkspace.slnx'
Assert-Target (Test-Path -LiteralPath $solutionPath) 'EngineWorkspace.slnx is missing; generate with vs2026 for this verification.'
$solution = [xml](Get-Content -LiteralPath $solutionPath -Raw)
$buildTypes = @($solution.SelectNodes('/Solution/Configurations/BuildType') | ForEach-Object { $_.GetAttribute('Name') } | Sort-Object)
Assert-Target (($buildTypes -join ';') -eq 'Debug;Release') 'The solution must expose only Debug and Release build configurations.'
$startupProjects = @($solution.SelectNodes('/Solution/Project[@DefaultStartup="true"]'))
Assert-Target ($startupProjects.Count -eq 1) 'The solution must have exactly one default startup project.'
Assert-Target ([IO.Path]::GetFileName($startupProjects[0].GetAttribute('Path')) -eq 'Demo.vcxproj') 'Demo must be the default startup project.'
foreach ($name in $targetNames) {
    $matches = @($solution.SelectNodes('/Solution/Project') | Where-Object { [IO.Path]::GetFileName($_.GetAttribute('Path')) -eq "$name.vcxproj" })
    Assert-Target ($matches.Count -eq 1) "The solution must include $name exactly once."
}
Write-Host '[PASS] Solution membership, Debug/Release configurations, and Demo startup target.'
Write-Host "Build target verification passed ($script:assertionCount assertions against generated project XML)."

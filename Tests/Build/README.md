# Generated build target verification

Run from the repository root after changing source ownership, build settings, or application entry points:

```powershell
.\premake5.exe vs2026
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Build/verify-targets.ps1
```

The script reads the actual generated `Intermediate/Projects/{Engine,BikersProject,Editor,Demo}.vcxproj` files and `EngineWorkspace.slnx`. It uses only PowerShell and .NET XML APIs; it does not require a compiler, graphics device, or installed test libraries. Missing projects and failed assertions throw an error and cause a nonzero process exit code.

It verifies:

- Engine and BikersProject are static libraries; Editor and Demo are executable targets in both Debug and Release.
- BikersProject owns the game scenes, bike controller, game HUD implementation, and project options factory. Demo owns its splash and entry point. Editor owns its blank scene and entry point. Engine compiles none of these content or application sources.
- All source files exist and no source is compiled by more than one target. Both applications reference BikersProject and Engine. Engine has no reverse content or application dependency; BikersProject has no application dependency.
- Shared include directories and Flecs/Jolt/GLM definitions agree across targets. The case-sensitive `flecs_STATIC` definition is required; `FLECS_STATIC` is rejected because Flecs does not recognize it and would apply DLL import/export declarations. No target uses the old `GAME_ONLY` build variant.
- Each target/configuration has a unique final artifact path and intermediate directory. Editor and Demo may share an output directory because their executable names differ.
- The solution contains the four targets, exposes only Debug/Release, and launches Demo by default.

This is a dependency-boundary check of generated build inputs. It does not replace building the solution or running both applications. Generate with `vs2026` for this check because it validates the `.slnx` startup setting; the `vs2022` generator writes the older `.sln` format.

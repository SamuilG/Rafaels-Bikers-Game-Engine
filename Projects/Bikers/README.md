# Rafael's Bikers project

Open `Bikers.steerproj` in the Steer Editor to load the Bikers content project.
The repository descriptor locates resources through `../../Assets`, relative to
the descriptor itself. The process working directory does not determine project
paths. `Scenes/Main.snapshot.json` is the project-local scene snapshot; its
absence is allowed for a project that has not been saved yet.

Use the editor's top **Play / Pause / Stop** controls to run inside the **Game
Viewport** tab. **Scene Viewport** has an independent editor camera; the two
viewports share one rendering surface and remain peer tabs. Stop returns to
the original editable scene, preserving unsaved edits. The optional **Project >
Run Saved Project in Separate Window** command launches the saved project in
the standalone Demo application.

Create a portable content package from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Package-BikersProject.ps1
# Or choose a new destination directory:
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Package-BikersProject.ps1 -Destination 'D:\Exports\Bikers'
```

The default output is `Intermediate/Packages/Bikers`. Packaging refuses an
existing destination and does not delete or overwrite it. The package contains
`Bikers.steerproj`, this README, the complete `Assets` directory, `gamecontrollerdb.txt`, an existing
scene snapshot if present, and the Bikers module/scene/controller/HUD source
files under their original `Source` paths. It does not include engine binaries
or a copy of the complete engine source tree. The packaged descriptor uses
`assetRoot: "Assets"`, so the complete package directory can be moved and opened
from another location with the current Editor.

The `bikers` C++ content module is compiled into the current Editor and Demo
applications. This package carries resources and source code; opening it does
not compile C++, load a DLL, or hot-reload changed source files. A new C++ content
module must be built and registered in the application's project factory before
the Editor can open a descriptor naming it. Rebuild the hosts to use changes to
the packaged Bikers source.

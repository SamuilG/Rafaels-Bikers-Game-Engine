# Project descriptor regressions

From the repository root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Project/run-project-descriptor-tests.ps1
```

The runner requires Visual Studio C++ build tools. It compiles the production
header-only `ProjectDescriptor` loader and exercises actual temporary descriptor
files and resource directories without opening a window or requiring a GPU.
Build and result logs are written to `Intermediate/Diagnostics/project-descriptor`.

The first eight groups cover descriptor-relative paths under an unrelated working
directory; packaged assets and default/absent snapshots; schema, malformed JSON,
and missing descriptors; required string fields; asset directory validation;
snapshot containment and path normalization; invalid snapshot field types; and
content-module names being preserved for host-owned factory validation. The
loader validates project format and paths, while the host decides whether a
particular C++ content module is built and registered.

The ninth group calls the production `FrameScheduler` with a committed Playing
state. Project editing must suppress gameplay, events, physics, physics feedback,
animation, and game effects while scene updates, presentation, and audio remain
active. Leaving editing mode must restore the complete simulation order.

The tenth group gives the loader an otherwise valid descriptor with an existing
snapshot. Invalid JSON, invalid entity vector dimensions/types, and particle
counts outside `uint32_t` must fail before opening the scene, with the snapshot
file path in the error. Legacy snapshots may omit entity/particle arrays and
optional particle properties.

The suite creates unique directories under
`Intermediate/Diagnostics/project-descriptor/fixtures` and only removes those
owned fixture directories when each test finishes. It does not
modify repository assets or saved project snapshots.

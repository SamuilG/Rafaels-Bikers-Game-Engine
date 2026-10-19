# Project descriptor regressions

## Generic scene projects

`Tests/Project/run-generic-project-tests.ps1` compiles the production scene validator and transactional project storage service. It covers stable IDs, parent cycles/missing parents, finite transforms, rigid camera poses, resource traversal, Unicode Save As, changed/new asset synchronization, saving an opened package, project relocation, protected unrelated destinations, and rollback after a read-only scene blocks the commit. Fixtures are created under `Intermediate/Diagnostics/generic-project` and cleaned up without touching repository assets.

After building Release Editor/Engine, `Tests/Project/run-generic-play-integration-tests.ps1` runs a hidden real Vulkan Application. It imports repeated multi-mesh GLBs and static/dynamic instances, deletes a mesh, authors a hierarchy/light/camera/particle group, and checks reconstruction before physics, simulation, pause/resume, restart, Stop restoring original entity/body handles, another Play, and scene reopening. This suite needs a working Vulkan device and the repository Assets.

The integration suite also rejects a malformed document after subsystem initialization, then reopens a valid document with a Unicode filename to verify cleanup. Its native Bikers smoke check links `BikersProject`, loads the shipped C++ scene factory, advances the vehicle simulation, pauses, and verifies Stop retains the original compound body. Test windows remain hidden, native game audio is muted, and existing project snapshots/layouts are not modified.

On Windows, restricted process sandboxes may deny `std::filesystem::weakly_canonical` metadata access even where file writes are allowed, or block MSVC PDB service communication. Run these local test scripts in the normal developer terminal when those environment restrictions occur.

## Native descriptor and snapshot compatibility

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

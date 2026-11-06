# Stable procedural runtime

Use one local runtime at `build/stable`, opened through the existing
`RME Procedural.lnk` shortcut. Source checkouts and Visual Studio output are
development directories; the shortcut identifies the packaged application.

Build and test the Release editor before packaging. From the source checkout:

```powershell
./tools/package_world_runtime.ps1 `
  -BinaryDirectory ./build/full-editor/Release `
  -RuntimeDataDirectory /path/to/validated/runtime/data `
  -Destination /path/to/new/runtime `
  -Revision (git rev-parse HEAD) `
  -PackageVersion 2026.11.06.1
```

The destination must be new. The explicit data tree supplies client definitions,
profiles and recipes; DAT/SPR assets stay external. Linked data directories are
rejected. The package includes adjacent DLLs and an isolated `editor.cfg`.
`build-info.json` records the package version, source revision and executable
hash. `runtime-manifest.json` records SHA-256 and size for every shipped binary.
Maps, recipes and preferences remain editable and are outside this manifest.
The package version identifies this distribution, not the upstream RME version.

Before replacing a runtime, preserve its maps, private profiles, recipes,
preferences and reports, plus a rollback of its executable and DLLs. Verify
copied file hashes. Update internal absolute paths and the existing shortcut
only after validation. Archive unique inputs from older runtimes before removing
them; keep an inventory with original paths and hashes. Never clean a dirty
source checkout, external client assets or user-created maps as build debris.

Verify the manifest, CLI startup with only the package DLLs, and a real-client
generation with `--verify`. For existing-map analysis, confirm the exported OTBM
is unchanged when using `--review-existing`. Run the Editor/ActionQueue tests,
including preview invalidation after client item definitions change.

Automated codec and editor tests do not establish native UI appearance or
movement on a game server. Record these validations separately in the local
release notes. Keep the previous runtime recoverable until manual testing is
complete.

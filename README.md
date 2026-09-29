qtbase (msorvig fork)
=====================

This is a patched fork of qtbase. See `README.git` for the upstream repository
description.

Branches named `<qt-branch>-<patch-set>` carry a single patch set on top of the
corresponding upstream branch. Combined patch sets are merged to the plain Qt
branch names (`6.11`, `6.12`, `dev`).

wasm-dynlnk
-----------

WebAssembly dynamic linking: load Qt plugins asynchronously with
`emscripten_dlopen()` instead of requiring every plugin to be preloaded at
startup.

Branches: `dev-wasm-dynlnk`

- wasm: allow asyncify for side modules
- wasm: skip plugin verification for plugin load()
- QLibrary/QPluginLoader: add async loading API
- wasm: don't pass link-only flags when compiling
- QCoreApplication: fix qGlobalPostedEventsCount() export on wasm
- tests: fix building with wasm shared-library config

Companion patches live in qtdeclarative on the branch of the same name.

wasm-webgpu
-----------

A WebGPU backend for the Qt RHI, on top of Dawn for native builds and
emdawnwebgpu for WebAssembly builds.

Branches: `dev-wasm-webgpu`

- Add a WebGPU RHI backend
- Add WebGPU shader variants to prebuilt shaders
- tests/manual/rhi: rebuild shaders with WGSL variants

Companion patches live in qtdeclarative, qtshadertools and qtquick3d on the
branch of the same name.

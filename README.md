qtbase (msorvig fork)
=====================

This is a patched fork of qtbase. See `README.git` for the upstream repository
description.

Branches named `<qt-branch>-<patch-set>` carry a single patch set on top of the
corresponding upstream branch. Combined patch sets are merged to the plain Qt
branch name (`dev`).

wasm-dynlnk
-----------

WebAssembly dynamic linking: load Qt plugins asynchronously with
`emscripten_dlopen()` instead of requiring every plugin to be preloaded at
startup.

Branches: `dev-wasm-dynlnk`

- QLibrary/QPluginLoader: add async loading API
- wasm: don't pass link-only flags when compiling
- QCoreApplication: fix qGlobalPostedEventsCount() export on wasm
- tests: fix building with wasm shared-library config

Companion patches live in qtdeclarative on the branch of the same name.

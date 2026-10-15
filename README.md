qtbase (msorvig fork)
=====================

This is a patched fork of qtbase. See `README.git` for the upstream repository
description.

Branches named `<qt-branch>-<patch-set>` carry a single patch set on top of the
corresponding upstream branch. Combined patch sets are merged to the plain Qt
branch name (`dev`).

wasi
----

WASI (wasm32-wasip2) as a platform target, built with the WASI SDK, and a
webhost feature for code that uses web APIs, enabled on WASI via nuscripten.

Branches: `dev-wasi`

- Add WASI (wasm32-wasip2) platform target
- Add webhost feature for code that uses web APIs
- Enable webhost on WASI via nuscripten

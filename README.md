qtbase (msorvig fork)
=====================

This is a patched fork of qtbase. See `README.git` for the upstream repository
description.

Branches named `<qt-branch>-<patch-set>` carry a single patch set on top of the
corresponding upstream branch. Combined patch sets are merged to the plain Qt
branch name (`dev`).

wasm-webgpu
-----------

A WebGPU backend for the Qt RHI, on top of Dawn for native builds and
emdawnwebgpu for WebAssembly builds.

Branches: `dev-wasm-webgpu`

- wasm: take a 2d canvas context only for raster windows
- wasm: resize the canvas when the device pixel ratio changes
- RHI: add a WebGPU backend
- Widgets: support the WebGPU RHI backend
- RHI examples and manual tests: add WebGPU and WGSL shader variants
- tst_qrhi: test the WebGPU backend
- tests/manual/rhi: optional QtAgent screenshot integration

Companion patches live in qtdeclarative, qtshadertools and qtquick3d on the
branch of the same name.

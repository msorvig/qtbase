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
- WebGPU: implement generateMips()
- WebGPU: support sampling depth textures with a plain sampler
- WebGPU: support HDR canvases on wasm
- WebGPU: keep the canvas texture valid across finish() on wasm
- WebGPU: support buffer-to-buffer copies
- WebGPU: implement clearStorageBuffer()
- WebGPU: support indirect draws and indirect compute dispatch
- WebGPU: support half precision vertex attributes
- WebGPU: support texture view formats
- WebGPU: support texture array ranges
- WebGPU: support 1D textures
- WebGPU: Support depth texture readback
- Widgets: support WebGPU for RHI-based composition
- QRhiWidget: add Api::WebGPU
- RHI examples and manual tests: add WGSL shader variants
- tst_qrhi: test the WebGPU backend
- tests/manual/rhi: optional QtAgent screenshot integration

Companion patches live in qtdeclarative, qtshadertools and qtquick3d on the
branch of the same name.

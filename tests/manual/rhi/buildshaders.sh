#!/bin/bash
# Copyright (C) 2024 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#
# Recompile all RHI manual test shaders with WGSL support added.
# Tessellation and geometry shader stages are skipped (not supported in WGSL).
#
# Usage: ./buildshaders.sh [path/to/qsb]
#   Default qsb location: uses qsb from PATH, or pass explicit path as first argument.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QSB="${1:-qsb}"

if ! "$QSB" --version &>/dev/null; then
    echo "Error: qsb not found. Pass path as first argument: $0 /path/to/qsb"
    exit 1
fi

echo "Using qsb: $("$QSB" --version)"
echo ""

FAILURES=()
run() {
    echo "  $*"
    if ! "$@" 2>&1; then
        FAILURES+=("$*")
    fi
}

# shared/
echo "=== shared ==="
cd "$SCRIPT_DIR/shared"
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c color.vert -o color.vert.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c color.frag -o color.frag.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c texture.vert -o texture.vert.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c texture.frag -o texture.frag.qsb
run "$QSB" --glsl "310 es,150"     --msl 12 --wgsl 100 -c texture_ms4.frag -o texture_ms4.frag.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c texture_arr.vert -o texture_arr.vert.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c texture_arr.frag -o texture_arr.frag.qsb

# shared/imgui/
echo "=== shared/imgui ==="
cd "$SCRIPT_DIR/shared/imgui"
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c imgui.vert -o imgui.vert.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 -c imgui.frag -o imgui.frag.qsb

# computebuffer/
echo "=== computebuffer ==="
cd "$SCRIPT_DIR/computebuffer"
run "$QSB" --glsl "310 es,430" --msl 12 --wgsl 100 buffer.comp -o buffer.comp.qsb
run "$QSB" --glsl "310 es,430" --msl 12 --wgsl 100 main.vert -o main.vert.qsb
run "$QSB" --glsl "310 es,430" --msl 12 --wgsl 100 main.frag -o main.frag.qsb

# computeimage/
echo "=== computeimage ==="
cd "$SCRIPT_DIR/computeimage"
run "$QSB" --glsl "310 es,430" --msl 12 --wgsl 100 image.comp -o image.comp.qsb

# cubemap/
echo "=== cubemap ==="
cd "$SCRIPT_DIR/cubemap"
run "$QSB" --glsl "100 es,120" --msl 12 --wgsl 100 -c cubemap.vert -o cubemap.vert.qsb
run "$QSB" --glsl "100 es,120" --msl 12 --wgsl 100 -c cubemap.frag -o cubemap.frag.qsb

# cubemap_render/
echo "=== cubemap_render ==="
cd "$SCRIPT_DIR/cubemap_render"
run "$QSB" --glsl "300 es,120" --msl 12 --wgsl 100 cubemap_oneface.vert -o cubemap_oneface.vert.qsb
run "$QSB" --glsl "300 es,120" --msl 12 --wgsl 100 cubemap_oneface.frag -o cubemap_oneface.frag.qsb
run "$QSB" --glsl "300 es,120" --msl 12 --wgsl 100 cubemap_mrt.vert    -o cubemap_mrt.vert.qsb
run "$QSB" --glsl "300 es,120" --msl 12 --wgsl 100 cubemap_mrt.frag    -o cubemap_mrt.frag.qsb
run "$QSB" --glsl "300 es,120" --msl 12 --wgsl 100 cubemap_sample.vert -o cubemap_sample.vert.qsb
run "$QSB" --glsl "300 es,120" --msl 12 --wgsl 100 cubemap_sample.frag -o cubemap_sample.frag.qsb

# displacement/ - skip tesc/tese (tessellation not supported in WGSL)
echo "=== displacement (vert/frag only; tesc/tese skipped - no WGSL tessellation) ==="
cd "$SCRIPT_DIR/displacement"
run "$QSB" --glsl 320es,410 --msl 12 --msltess --wgsl 100 material.vert -o material.vert.qsb
run "$QSB" --glsl 320es,410 --msl 12 --wgsl 100 material.frag -o material.frag.qsb

# float16texture_with_compute/
echo "=== float16texture_with_compute ==="
cd "$SCRIPT_DIR/float16texture_with_compute"
run "$QSB" --glsl "430,310 es" --msl 12 --wgsl 100 load.comp      -o load.comp.qsb
run "$QSB" --glsl "430,310 es" --msl 12 --wgsl 100 prefilter.comp -o prefilter.comp.qsb

# geometryshader/ - skip geom stage (not supported in WGSL)
echo "=== geometryshader (vert/frag only; geom skipped - no WGSL geometry shaders) ==="
cd "$SCRIPT_DIR/geometryshader"
run "$QSB" --glsl 320es,410 --wgsl 100 test.vert -o test.vert.qsb
run "$QSB" --glsl 320es,410 --wgsl 100 test.frag -o test.frag.qsb

# hdr/ - uses --qt6 shorthand; keep as-is and add wgsl
echo "=== hdr ==="
cd "$SCRIPT_DIR/hdr"
run "$QSB" --qt6 --wgsl 100 hdrtexture.vert -o hdrtexture.vert.qsb
run "$QSB" --qt6 --wgsl 100 hdrtexture.frag -o hdrtexture.frag.qsb

# instancing/
echo "=== instancing ==="
cd "$SCRIPT_DIR/instancing"
run "$QSB" --glsl "330,300 es" --msl 12 --wgsl 100 inst.vert -o inst.vert.qsb
run "$QSB" --glsl "330,300 es" --msl 12 --wgsl 100 inst.frag -o inst.frag.qsb

# mrt/
echo "=== mrt ==="
cd "$SCRIPT_DIR/mrt"
run "$QSB" --glsl "100 es,120" --msl 12 --wgsl 100 -c mrt.vert -o mrt.vert.qsb
run "$QSB" --glsl "100 es,120" --msl 12 --wgsl 100 -c mrt.frag -o mrt.frag.qsb

# multiview/
echo "=== multiview ==="
cd "$SCRIPT_DIR/multiview"
run "$QSB" --view-count 2 --glsl "300 es,330" --msl 12 -c --wgsl 100 multiview.vert -o multiview.vert.qsb
run "$QSB"               --glsl "300 es,330" --msl 12 -c --wgsl 100 multiview.frag -o multiview.frag.qsb
run "$QSB"               --glsl "300 es,330" --msl 12 -c --wgsl 100 texture.vert   -o texture.vert.qsb
run "$QSB"               --glsl "300 es,330" --msl 12 -c --wgsl 100 texture.frag   -o texture.frag.qsb

# noninstanced/
echo "=== noninstanced ==="
cd "$SCRIPT_DIR/noninstanced"
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 material.vert -o material.vert.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 material.frag -o material.frag.qsb

# polygonmode/
echo "=== polygonmode ==="
cd "$SCRIPT_DIR/polygonmode"
run "$QSB" --glsl 320es,410,120 --msl 12 --wgsl 100 test.vert -o test.vert.qsb
run "$QSB" --glsl 320es,410,120 --msl 12 --wgsl 100 test.frag -o test.frag.qsb

# shadowmap/
echo "=== shadowmap ==="
cd "$SCRIPT_DIR/shadowmap"
run "$QSB" --glsl "120,300 es" --msl 12 --wgsl 100 -c shadowmap.vert -o shadowmap.vert.qsb
run "$QSB" --glsl "120,300 es" --msl 12 --wgsl 100 -c shadowmap.frag -o shadowmap.frag.qsb
run "$QSB" --glsl "120,300 es" --msl 12 --wgsl 100 -c main.vert      -o main.vert.qsb
run "$QSB" --glsl "120,300 es" --msl 12 --wgsl 100 -c main.frag      -o main.frag.qsb

# stenciloutline/
echo "=== stenciloutline ==="
cd "$SCRIPT_DIR/stenciloutline"
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 material.vert -o material.vert.qsb
run "$QSB" --glsl "100 es,120,150" --msl 12 --wgsl 100 material.frag -o material.frag.qsb

# tessellation/ - skip tesc/tese (tessellation not supported in WGSL)
echo "=== tessellation (vert/frag only; tesc/tese skipped - no WGSL tessellation) ==="
cd "$SCRIPT_DIR/tessellation"
run "$QSB" --glsl 320es,410 --msl 12 --msltess --wgsl 100 test.vert -o test.vert.qsb
run "$QSB" --glsl 320es,410 --msl 12 --wgsl 100 test.frag -o test.frag.qsb

# tex1d/
echo "=== tex1d ==="
cd "$SCRIPT_DIR/tex1d"
run "$QSB" --glsl "120,150,300 es" --msl 12 --wgsl 100 -c texture1d.vert -o texture1d.vert.qsb
run "$QSB" --glsl "120,150,300 es" --msl 12 --wgsl 100 -c texture1d.frag -o texture1d.frag.qsb

# tex3d/
echo "=== tex3d ==="
cd "$SCRIPT_DIR/tex3d"
run "$QSB" --glsl "300 es,150" --msl 12 --wgsl 100 -c texture3d.vert -o texture3d.vert.qsb
run "$QSB" --glsl "300 es,150" --msl 12 --wgsl 100 -c texture3d.frag -o texture3d.frag.qsb

echo ""
if [ ${#FAILURES[@]} -eq 0 ]; then
    echo "Done. All shaders compiled successfully."
else
    echo "Done with ${#FAILURES[@]} failure(s):"
    for f in "${FAILURES[@]}"; do
        echo "  FAILED: $f"
    done
fi

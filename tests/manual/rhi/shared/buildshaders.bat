:: Copyright (C) 2024 The Qt Company Ltd.
:: SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
qsb --wgsl 100 --glsl "100 es,120,150" --hlsl 50 --msl 12 -c color.vert -o color.vert.qsb
qsb --wgsl 100 --glsl "100 es,120,150" --hlsl 50 --msl 12 -c color.frag -o color.frag.qsb
qsb --wgsl 100 --glsl "100 es,120,150" --hlsl 50 --msl 12 -c texture.vert -o texture.vert.qsb
qsb --wgsl 100 --glsl "100 es,120,150" --hlsl 50 --msl 12 -c texture.frag -o texture.frag.qsb
qsb --wgsl 100 --glsl "310 es,150" --hlsl 50 --msl 12 -c texture_ms4.frag -o texture_ms4.frag.qsb
qsb --wgsl 100 --glsl "100 es,120,150" --hlsl 50 --msl 12 -c texture_arr.vert -o texture_arr.vert.qsb
qsb --wgsl 100 --glsl "100 es,120,150" --hlsl 50 --msl 12 -c texture_arr.frag -o texture_arr.frag.qsb

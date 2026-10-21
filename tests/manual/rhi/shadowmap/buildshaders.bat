:: Copyright (C) 2024 The Qt Company Ltd.
:: SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
qsb --wgsl 100 --glsl "120,300 es" --hlsl 50 --msl 12 -c shadowmap.vert -o shadowmap.vert.qsb
qsb --wgsl 100 --glsl "120,300 es" --hlsl 50 --msl 12 -c shadowmap.frag -o shadowmap.frag.qsb
qsb --wgsl 100 --glsl "120,300 es" --hlsl 50 --msl 12 -c main.vert -o main.vert.qsb
qsb --wgsl 100 --glsl "120,300 es" --hlsl 50 --msl 12 -c main.frag -o main.frag.qsb

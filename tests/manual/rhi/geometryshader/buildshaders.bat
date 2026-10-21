:: Copyright (C) 2024 The Qt Company Ltd.
:: SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
qsb --wgsl 100 --glsl 320es,410 --hlsl 50 test.vert -o test.vert.qsb
qsb --glsl 320es,410 --hlsl 50 test.geom -o test.geom.qsb
qsb --wgsl 100 --glsl 320es,410 --hlsl 50 test.frag -o test.frag.qsb

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

// Redirect to the top-level emscripten.h shim
#ifndef QT_EMSCRIPTEN_EMSCRIPTEN_SHIM_H
#define QT_EMSCRIPTEN_EMSCRIPTEN_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/emscripten.h>
#else
#  include <emscripten.h>
#endif

#endif

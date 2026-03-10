// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QT_EMSCRIPTEN_VAL_SHIM_H
#define QT_EMSCRIPTEN_VAL_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/val.h>
#else
#  include <val.h>
namespace emscripten {
    using namespace notscripten;
}
#endif

#endif // QT_EMSCRIPTEN_VAL_SHIM_H

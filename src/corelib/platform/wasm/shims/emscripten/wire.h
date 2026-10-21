// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

// Emscripten wire.h compatibility shim for NuScripten/WASI builds.
#ifndef QT_EMSCRIPTEN_WIRE_SHIM_H
#define QT_EMSCRIPTEN_WIRE_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/wire.h>
#else

#include <string>
#include <emscripten/val.h>

namespace emscripten {
namespace internal {

template<typename T>
struct BindingType {
    using WireType = T;
    static WireType toWireType(const T& v) { return v; }
    static T fromWireType(WireType v) { return v; }
};

template<>
struct BindingType<std::string> {
    using WireType = emscripten::val;
    static WireType toWireType(const std::string& s) { return emscripten::val(s); }
    static std::string fromWireType(const WireType& v) { return v.as<std::string>(); }
};

} // namespace internal
} // namespace emscripten

#endif // __EMSCRIPTEN__
#endif // QT_EMSCRIPTEN_WIRE_SHIM_H

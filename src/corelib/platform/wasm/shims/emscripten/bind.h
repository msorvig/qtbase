// Emscripten bind.h compatibility shim for NuScripten/WASI builds.
#ifndef QT_EMSCRIPTEN_BIND_SHIM_H
#define QT_EMSCRIPTEN_BIND_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/bind.h>
#else

#include <emscripten/val.h>

namespace emscripten {

// Stub for emscripten::function() - no-op for now.
template<typename ReturnType, typename... Args>
void function(const char*, ReturnType (*)(Args...)) {}

template<typename ReturnType, typename... Args>
void function(const char*, ReturnType (*)(Args...), int /*async*/) {}

template<typename Func>
void function(const char*, Func&&) {}

} // namespace emscripten

// EMSCRIPTEN_BINDINGS runs the block as a static initializer.
#define EMSCRIPTEN_BINDINGS(name) \
    static struct _emscripten_bindings_##name { \
        _emscripten_bindings_##name(); \
    } _emscripten_bindings_instance_##name; \
    _emscripten_bindings_##name::_emscripten_bindings_##name()

#endif // __EMSCRIPTEN__
#endif // QT_EMSCRIPTEN_BIND_SHIM_H

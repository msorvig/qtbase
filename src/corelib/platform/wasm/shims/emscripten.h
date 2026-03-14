// Emscripten main header compatibility shim for NuScripten/WASI builds.
#ifndef QT_EMSCRIPTEN_SHIM_H
#define QT_EMSCRIPTEN_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten.h>
#else

#include <emscripten/html5.h>

#define EMSCRIPTEN_KEEPALIVE __attribute__((used))
#define EM_ASM(code, ...) ((void)0)
#define EM_ASM_INT(code, ...) (0)
#define EM_ASM_DOUBLE(code, ...) (0.0)

// EM_JS: declares a C function implemented in JS. For WASI, stub it out.
#define EM_JS(ret, name, params, body) \
    ret name params { return ret(); }

// EM_ASYNC_JS: async version of EM_JS (for JSPI). Stub for WASI.
#define EM_ASYNC_JS(ret, name, params, body) \
    ret name params { return ret(); }

inline void emscripten_sleep(unsigned int) {} // TODO: JSPI

// Async call: schedule callback to run after ms milliseconds
inline void emscripten_async_call(void (*func)(void*), void* arg, int ms) {
    // TODO: implement via NuScripten setTimeout
    (void)func; (void)arg; (void)ms;
}

#endif // __EMSCRIPTEN__
#endif // QT_EMSCRIPTEN_SHIM_H

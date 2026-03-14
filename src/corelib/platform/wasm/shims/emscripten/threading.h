// Emscripten threading.h stub for WASI (single-threaded).
#ifndef QT_EMSCRIPTEN_THREADING_SHIM_H
#define QT_EMSCRIPTEN_THREADING_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/threading.h>
#else

#include <cstdint>

typedef uintptr_t pthread_t;

inline pthread_t emscripten_main_runtime_thread_id() { return 0; }
inline int emscripten_is_main_runtime_thread() { return 1; }

#endif // __EMSCRIPTEN__
#endif

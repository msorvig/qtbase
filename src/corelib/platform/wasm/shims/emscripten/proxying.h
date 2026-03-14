// Emscripten proxying.h stub for WASI (single-threaded).
#ifndef QT_EMSCRIPTEN_PROXYING_SHIM_H
#define QT_EMSCRIPTEN_PROXYING_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/proxying.h>
#else

#include <functional>
#include <emscripten/threading.h>

namespace emscripten {

// Stub ProxyingQueue for single-threaded WASI builds.
// Only used behind QT_CONFIG(thread) guards, but the header is included unconditionally
// by some files so the type needs to exist.
class ProxyingQueue {
public:
    void proxySync(pthread_t, std::function<void()> fn) { fn(); }
    void proxyAsync(pthread_t, std::function<void()> fn) { fn(); }
};

} // namespace emscripten

#endif // __EMSCRIPTEN__
#endif

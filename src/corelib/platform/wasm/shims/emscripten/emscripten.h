// Redirect to the top-level emscripten.h shim
#ifndef QT_EMSCRIPTEN_EMSCRIPTEN_SHIM_H
#define QT_EMSCRIPTEN_EMSCRIPTEN_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/emscripten.h>
#else
#  include <emscripten.h>
#endif

#endif

// Emscripten html5.h compatibility shim for NuScripten/WASI builds.
#ifndef QT_EMSCRIPTEN_HTML5_SHIM_H
#define QT_EMSCRIPTEN_HTML5_SHIM_H

#ifdef __EMSCRIPTEN__
#  include_next <emscripten/html5.h>
#else

#include <cstdint>

#define EMSCRIPTEN_RESULT_SUCCESS 0
#define EMSCRIPTEN_RESULT int
#define EM_TRUE 1
#define EM_FALSE 0
#define EM_BOOL int
#define EM_UTF8 char

#define EMSCRIPTEN_EVENT_TARGET_WINDOW ((const char*)2)
#define EMSCRIPTEN_EVENT_TARGET_DOCUMENT ((const char*)1)

// DOM key locations
#define DOM_KEY_LOCATION_STANDARD 0
#define DOM_KEY_LOCATION_LEFT 1
#define DOM_KEY_LOCATION_RIGHT 2
#define DOM_KEY_LOCATION_NUMPAD 3

// Resize events
typedef struct EmscriptenUiEvent {
    int windowInnerWidth, windowInnerHeight;
    int windowOuterWidth, windowOuterHeight;
    int scrollTop, scrollLeft;
    int documentBodyClientWidth, documentBodyClientHeight;
    int isFullscreen;
} EmscriptenUiEvent;

typedef EM_BOOL (*em_ui_callback_func)(int, const EmscriptenUiEvent*, void*);

inline EMSCRIPTEN_RESULT emscripten_set_resize_callback(
    const char*, void*, EM_BOOL, em_ui_callback_func) {
    return EMSCRIPTEN_RESULT_SUCCESS; // TODO: implement via val
}

// Element size
inline EMSCRIPTEN_RESULT emscripten_get_element_css_size(
    const char*, double* w, double* h) {
    *w = 800; *h = 600; // TODO: implement via val
    return EMSCRIPTEN_RESULT_SUCCESS;
}

// WebGL context
typedef int EMSCRIPTEN_WEBGL_CONTEXT_HANDLE;

typedef struct EmscriptenWebGLContextAttributes {
    EM_BOOL alpha, depth, stencil, antialias;
    EM_BOOL premultipliedAlpha, preserveDrawingBuffer;
    EM_BOOL failIfMajorPerformanceCaveat;
    int majorVersion, minorVersion;
    EM_BOOL enableExtensionsByDefault;
    EM_BOOL explicitSwapControl, renderViaOffscreenBackBuffer;
    EM_BOOL proxyContextToMainThread;
} EmscriptenWebGLContextAttributes;

inline void emscripten_webgl_init_context_attributes(EmscriptenWebGLContextAttributes* a) {
    a->alpha = EM_TRUE; a->depth = EM_TRUE; a->stencil = EM_FALSE;
    a->antialias = EM_TRUE; a->premultipliedAlpha = EM_TRUE;
    a->preserveDrawingBuffer = EM_FALSE; a->failIfMajorPerformanceCaveat = EM_FALSE;
    a->majorVersion = 2; a->minorVersion = 0;
    a->enableExtensionsByDefault = EM_TRUE;
    a->explicitSwapControl = EM_FALSE; a->renderViaOffscreenBackBuffer = EM_FALSE;
    a->proxyContextToMainThread = EM_FALSE;
}

inline EMSCRIPTEN_WEBGL_CONTEXT_HANDLE emscripten_webgl_create_context(const char*, const EmscriptenWebGLContextAttributes*) { return 0; }
inline EMSCRIPTEN_RESULT emscripten_webgl_destroy_context(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE) { return 0; }
inline EMSCRIPTEN_RESULT emscripten_webgl_make_context_current(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE) { return 0; }
inline EMSCRIPTEN_RESULT emscripten_webgl_get_context_attributes(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE, EmscriptenWebGLContextAttributes*) { return 0; }
inline EM_BOOL emscripten_is_webgl_context_lost(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE) { return EM_FALSE; }

// Animation frame
inline long emscripten_request_animation_frame(EM_BOOL(*)(double, void*), void*) { return 0; }
inline void emscripten_cancel_animation_frame(long) {}

// Performance
inline double emscripten_performance_now(void) { return 0.0; } // TODO: implement via val

// Focus events
typedef struct EmscriptenFocusEvent { char nodeName[128]; char id[128]; } EmscriptenFocusEvent;
typedef EM_BOOL (*em_focus_callback_func)(int, const EmscriptenFocusEvent*, void*);
inline EMSCRIPTEN_RESULT emscripten_set_focusin_callback(const char*, void*, EM_BOOL, em_focus_callback_func) { return 0; }
inline EMSCRIPTEN_RESULT emscripten_set_focusout_callback(const char*, void*, EM_BOOL, em_focus_callback_func) { return 0; }

#endif // __EMSCRIPTEN__
#endif // QT_EMSCRIPTEN_HTML5_SHIM_H

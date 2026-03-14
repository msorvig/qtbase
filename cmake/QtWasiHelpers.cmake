# Copyright (C) 2025 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

function(qt_internal_setup_wasi_target_properties wasiTarget)

    # exceptions
    if(QT_FEATURE_wasm_exceptions)
        target_compile_options("${wasiTarget}" INTERFACE
            -fwasm-exceptions -mllvm --wasm-use-legacy-eh=false)
        target_link_options("${wasiTarget}" INTERFACE
            -fwasm-exceptions -mllvm --wasm-use-legacy-eh=false)
    elseif(QT_FEATURE_exceptions)
        target_compile_options("${wasiTarget}" INTERFACE -fexceptions)
        target_link_options("${wasiTarget}" INTERFACE -fexceptions)
    endif()

    # simd
    if(QT_FEATURE_wasm_simd128)
        target_compile_options("${wasiTarget}" INTERFACE -msimd128)
    endif()

    # WASI libc emulations for APIs that don't have native implementations
    target_compile_definitions("${wasiTarget}" INTERFACE
        _WASI_EMULATED_MMAN _WASI_EMULATED_SIGNAL _WASI_EMULATED_GETPID)

    # Define Q_OS_WEB when targeting the web via NuScripten.
    # This enables JS/DOM interop APIs (EcmaString, DOMRect, val) and the wasm QPA plugin.
    # TODO: make this conditional on a configure option for non-web WASI targets.
    target_compile_definitions("${wasiTarget}" INTERFACE Q_OS_WEB)
    target_link_options("${wasiTarget}" INTERFACE
        -lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-getpid)

endfunction()

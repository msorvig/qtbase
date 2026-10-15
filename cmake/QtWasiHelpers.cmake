# Copyright (C) 2025 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

function(qt_internal_setup_wasi_target_properties wasiTarget)

    # exceptions
    if(QT_FEATURE_wasm_exceptions)
        target_compile_options("${wasiTarget}" INTERFACE
            -fwasm-exceptions "SHELL:-mllvm --wasm-use-legacy-eh=false")
        target_link_options("${wasiTarget}" INTERFACE
            -fwasm-exceptions "SHELL:-mllvm --wasm-use-legacy-eh=false")
        # libc++abi needs libunwind for wasm exceptions, and the compiler driver
        # does not link it. Link order matters: libunwind must come after libc++abi.
        target_link_libraries("${wasiTarget}" INTERFACE c++abi unwind)
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

    target_link_options("${wasiTarget}" INTERFACE
        -lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-getpid)

endfunction()

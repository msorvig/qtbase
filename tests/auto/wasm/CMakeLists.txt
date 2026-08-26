# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

add_subdirectory(fetchapi)
add_subdirectory(localfileapi)
add_subdirectory(qwasmwindowstack)
add_subdirectory(qwasmwindowtreenode)
add_subdirectory(qwasmpromise)
# calls QWasmAccessibilityEnable() in the statically linked platform plugin
if(NOT QT_FEATURE_shared)
    add_subdirectory(selenium)
endif()

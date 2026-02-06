// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qrhiwebgpu_p.h"

#import <Foundation/Foundation.h>
#import <QuartzCore/CATransaction.h>

QT_BEGIN_NAMESPACE

// Dawn presents with -[CAMetalDrawable present] inside wgpuSurfacePresent().
// While QNSView handles an expose it sets presentsWithTransaction on the
// QMetalLayer, which ties that present to the calling thread's Core Animation
// transaction. Qt's render threads have no CFRunLoop to commit their implicit
// transaction, so the frame (and the layer state Dawn set when configuring the
// surface) would stay pending until something else commits, such as a resize.
// Flush it here instead. Qt's Metal backend avoids this by handing the present
// to the main thread (QMetalLayer::mainThreadPresentation), which Dawn's API
// does not allow.
void qrhiwebgpu_presentSurface(WGPUSurface surface)
{
    wgpuSurfacePresent(surface);
    if (!NSThread.isMainThread)
        [CATransaction flush];
}

QT_END_NAMESPACE

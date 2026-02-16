// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:execute-external-code

#include "qlibrary_p.h"

#include <qcoreapplication.h>
#include <qfile.h>
#include <qqueue.h>
#include <private/qfilesystementry_p.h>

#include <emscripten.h>
#include <dlfcn.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

namespace {

struct WasmDlopenContext {
    QLibraryPrivate *library;
    QLibraryPrivate::LoadAsyncSuccessCallback onSuccess;
    QLibraryPrivate::LoadAsyncFailCallback onFail;
    QString qualifiedFileName;
    int dlFlags;
};

// Serialized dlopen queue.
//
// emscripten_dlopen does not support concurrent loads of shared libraries
// that have common transitive dependencies. For example, if plugin A and
// plugin B both depend on libFoo.so, calling emscripten_dlopen on both
// concurrently triggers an assertion:
//
//   "Attempt to load 'libFoo.so' twice before the first load completed"
//
// This happens because emscripten's internal dynamic linker starts fetching
// libFoo.so for plugin A, and before that completes, starts fetching it
// again for plugin B. The workaround is to serialize all emscripten_dlopen
// calls so that only one is in flight at a time.
static QQueue<WasmDlopenContext *> s_queue;
static bool s_loading = false;

static void startNextDlopen();

static void dlopenDone(WasmDlopenContext *ctx, bool success, void *handle)
{
    if (success) {
        ctx->library->pHnd.storeRelaxed(handle);
        QMutexLocker locker(&ctx->library->mutex);
        ctx->library->qualifiedFileName = ctx->qualifiedFileName;
        ctx->library->errorString.clear();
    } else {
        QString errorMsg = QString::fromLocal8Bit(dlerror());
        if (errorMsg.isEmpty())
            errorMsg = QLibrary::tr("Unknown error loading library");
        QMutexLocker locker(&ctx->library->mutex);
        ctx->library->errorString = QLibrary::tr("Cannot load library %1: %2")
                .arg(ctx->library->fileName, errorMsg);
    }

    if (success) {
        if (ctx->onSuccess) ctx->onSuccess();
    } else {
        if (ctx->onFail) ctx->onFail(ctx->library->errorString);
    }

    ctx->library->release();
    delete ctx;

    s_loading = false;
    startNextDlopen();
}

extern "C" void qt_wasm_dlopen_success(void *user_data, void *handle)
{
    dlopenDone(static_cast<WasmDlopenContext *>(user_data), true, handle);
}

extern "C" void qt_wasm_dlopen_error(void *user_data)
{
    dlopenDone(static_cast<WasmDlopenContext *>(user_data), false, nullptr);
}

static void startNextDlopen()
{
    if (s_loading || s_queue.isEmpty())
        return;

    s_loading = true;
    WasmDlopenContext *ctx = s_queue.dequeue();

    emscripten_dlopen(QFile::encodeName(ctx->qualifiedFileName).constData(),
                      ctx->dlFlags,
                      ctx,
                      qt_wasm_dlopen_success,
                      qt_wasm_dlopen_error);
}

} // anonymous namespace

void QLibraryPrivate::load_sys_async(LoadAsyncSuccessCallback onSuccess, LoadAsyncFailCallback onFail)
{
#if defined(QT_STATIC)
    if (onFail) {
        QMetaObject::invokeMethod(qApp, [onFail]() {
            onFail(QLibrary::tr("Dynamic loading is not supported with static linking"));
        }, Qt::QueuedConnection);
    }
    return;
#endif

    QMutexLocker locker(&mutex);
    QString attempt;
    QFileSystemEntry fsEntry(fileName);

    QString path = fsEntry.path();
    QString name = fsEntry.fileName();
    if (path == "."_L1 && !fileName.startsWith(path))
        path.clear();
    else
        path += u'/';

    QStringList suffixes;
    QStringList prefixes;
    if (pluginState != IsAPlugin) {
        prefixes << prefix_sys().toString();
        suffixes = suffixes_sys(fullVersion);
    }

    int dlFlags = 0;
    auto lh = loadHints();
    if (lh & QLibrary::ResolveAllSymbolsHint)
        dlFlags |= RTLD_NOW;
    else
        dlFlags |= RTLD_LAZY;
    if (lh & QLibrary::ExportExternalSymbolsHint)
        dlFlags |= RTLD_GLOBAL;
    else
        dlFlags |= RTLD_LOCAL;

    if (fsEntry.isAbsolute()) {
        suffixes.prepend(QString());
        prefixes.prepend(QString());
    } else {
        suffixes.append(QString());
        prefixes.append(QString());
    }

    for (int prefix = 0; prefix < prefixes.size(); prefix++) {
        for (int suffix = 0; suffix < suffixes.size(); suffix++) {
            if (path.isEmpty() && prefixes.at(prefix).contains(u'/'))
                continue;
            if (!suffixes.at(suffix).isEmpty() && name.endsWith(suffixes.at(suffix)))
                continue;
            attempt = path + prefixes.at(prefix) + name + suffixes.at(suffix);
            goto found;
        }
    }

found:
    if (attempt.isEmpty())
        attempt = fileName;

    libraryRefCount.ref();
    locker.unlock();

    auto *ctx = new WasmDlopenContext{
        this,
        std::move(onSuccess),
        std::move(onFail),
        attempt,
        dlFlags
    };

    s_queue.enqueue(ctx);
    startNextDlopen();
}

QT_END_NAMESPACE

// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:data-parser

#include "qlocale_p.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#else
#include <emscripten/val.h>

#include <string>
#include <vector>
#endif

QT_BEGIN_NAMESPACE

#ifndef QT_NO_SYSTEMLOCALE

namespace {

QStringList navigatorLanguages()
{
#ifdef Q_OS_WASM
    // Read navigator.languages using EM_ASM_PTR instead of emscripten::val. The
    // latter does not support being used from static constructors (emscripten #23170)
    char *joined = reinterpret_cast<char *>(EM_ASM_PTR({
        return stringToNewUTF8(navigator.languages.join("\n"));
    }));
    const QStringList qtLanguages =
            QString::fromUtf8(joined).split(u'\n', Qt::SkipEmptyParts);
    free(joined);
    return qtLanguages;
#else
    using emscripten::val;
    const val navigator = val::global("navigator");
    const auto languages = emscripten::vecFromJSArray<std::string>(navigator["languages"]);
    QStringList qtLanguages;
    for (const std::string &language : languages)
        qtLanguages.append(QString::fromStdString(language));
    return qtLanguages;
#endif
}

}

QVariant QSystemLocale::query(QueryType query, QVariant &&in) const
{
    Q_UNUSED(in);

    switch (query) {
    case QSystemLocale::UILanguages:
        return QVariant(navigatorLanguages());
    default:
    break;
    }

    return QVariant();
}

QLocale QSystemLocale::fallbackLocale() const
{
    const QStringList languages = navigatorLanguages();
    if (languages.isEmpty())
        return QLocale(u"en-US");
    return QLocale(languages[0]);
}

#endif // QT_NO_SYSTEMLOCALE

QT_END_NAMESPACE

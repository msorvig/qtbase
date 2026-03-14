// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#ifndef QNETWORKREPLYWEBIMPL_H
#define QNETWORKREPLYWEBIMPL_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of the Network Access API.  This header file may change from
// version to version without notice, or even be removed.
//
// We mean it.
//

#include "qnetworkreply.h"
#include "qnetworkreply_p.h"
#include "qnetworkaccessmanager.h"

#include <QtCore/qfile.h>

#include <private/qtnetworkglobal_p.h>

#include <emscripten/val.h>

#include <memory>

QT_BEGIN_NAMESPACE

class QIODevice;

class QNetworkReplyWebImplPrivate;
class QNetworkReplyWebImpl: public QNetworkReply
{
    Q_OBJECT
public:
    QNetworkReplyWebImpl(QObject *parent = nullptr);
    ~QNetworkReplyWebImpl();
    virtual void abort() override;

    // reimplemented from QNetworkReply
    virtual void close() override;
    virtual qint64 bytesAvailable() const override;
    virtual bool isSequential () const override;
    qint64 size() const override;

    virtual qint64 readData(char *data, qint64 maxlen) override;

    void setup(QNetworkAccessManager::Operation op, const QNetworkRequest &request,
               QIODevice *outgoingData);

    Q_DECLARE_PRIVATE(QNetworkReplyWebImpl)

    Q_PRIVATE_SLOT(d_func(), void emitReplyError(QNetworkReply::NetworkError errorCode, const QString &errorString))
    Q_PRIVATE_SLOT(d_func(), void emitDataReadProgress(qint64 done, qint64 total))
    Q_PRIVATE_SLOT(d_func(), void dataReceived(const QByteArray &buffer))

private:
    QByteArray methodName() const;
};

class QNetworkReplyWebImplPrivate: public QNetworkReplyPrivate
{
public:
    QNetworkReplyWebImplPrivate();
    ~QNetworkReplyWebImplPrivate();

    QNetworkAccessManagerPrivate *managerPrivate;
    void doSendRequest();

    void emitReplyError(QNetworkReply::NetworkError errorCode, const QString &);
    void emitDataReadProgress(qint64 done, qint64 total);
    void dataReceived(const QByteArray &buffer);
    void headersReceived(emscripten::val headers);

    void setStatusCode(int status, const QByteArray &statusText);

    void setup(QNetworkAccessManager::Operation op, const QNetworkRequest &request,
               QIODevice *outgoingData);

    State state;
    void _q_bufferOutgoingData();
    void _q_bufferOutgoingDataFinished();

    qint64 bytesDownloaded;

    qint64 downloadBufferReadPosition;
    qint64 downloadBufferCurrentSize;
    qint64 totalDownloadSize;
    qint64 percentFinished;
    QByteArray downloadBuffer;

    QIODevice *outgoingData;
    std::shared_ptr<QRingBuffer> outgoingDataBuffer;

    emscripten::val m_abortController = emscripten::val::undefined();

    static QNetworkReply::NetworkError statusCodeFromHttp(int httpStatusCode, const QUrl &url);

    void setReplyFinished();
    void setCanceled();

    Q_DECLARE_PUBLIC(QNetworkReplyWebImpl)
};

QT_END_NAMESPACE

#endif // QNETWORKREPLYWEBIMPL_H

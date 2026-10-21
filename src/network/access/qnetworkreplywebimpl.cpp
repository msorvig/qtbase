// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:data-parser

#include "qnetworkreplywebimpl_p.h"
#include "qnetworkrequest.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qthread.h>
#include <QtCore/private/qoffsetstringarray_p.h>
#include <QtCore/private/qtools_p.h>
#include <QtCore/private/qstdweb_p.h>

#include <private/qnetworkaccessmanager_p.h>
#include <private/qnetworkfile_p.h>

#include <emscripten/val.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

namespace {

static constexpr auto BannedHeaders = qOffsetStringArray(
    "accept-charset",
    "accept-encoding",
    "access-control-request-headers",
    "access-control-request-method",
    "connection",
    "content-length",
    "cookie",
    "cookie2",
    "date",
    "dnt",
    "expect",
    "host",
    "keep-alive",
    "origin",
    "referer",
    "te",
    "trailer",
    "transfer-encoding",
    "upgrade",
    "via"
);

bool isUnsafeHeader(QLatin1StringView header) noexcept
{
    return header.startsWith("proxy-"_L1, Qt::CaseInsensitive)
        || header.startsWith("sec-"_L1, Qt::CaseInsensitive)
        || BannedHeaders.contains(header, Qt::CaseInsensitive);
}
} // namespace

QNetworkReplyWebImplPrivate::QNetworkReplyWebImplPrivate()
    : QNetworkReplyPrivate()
    , managerPrivate(0)
    , bytesDownloaded(0)
    , downloadBufferReadPosition(0)
    , downloadBufferCurrentSize(0)
    , totalDownloadSize(0)
    , percentFinished(0)
    , outgoingData(nullptr)
{
}

QNetworkReplyWebImplPrivate::~QNetworkReplyWebImplPrivate()
{
}

QNetworkReplyWebImpl::QNetworkReplyWebImpl(QObject *parent)
    : QNetworkReply(*new QNetworkReplyWebImplPrivate(), parent)
{
    Q_D(QNetworkReplyWebImpl);
    d->state = QNetworkReplyPrivate::Idle;
}

QNetworkReplyWebImpl::~QNetworkReplyWebImpl()
{
    if (isRunning())
        abort();
    close();
}

QByteArray QNetworkReplyWebImpl::methodName() const
{
    const Q_D(QNetworkReplyWebImpl);
    switch (operation()) {
    case QNetworkAccessManager::HeadOperation:
        return "HEAD";
    case QNetworkAccessManager::GetOperation:
        return "GET";
    case QNetworkAccessManager::PutOperation:
        return "PUT";
    case QNetworkAccessManager::PostOperation:
        return "POST";
    case QNetworkAccessManager::DeleteOperation:
        return "DELETE";
    case QNetworkAccessManager::CustomOperation:
        return d->request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
    default:
        break;
    }
    return QByteArray();
}

void QNetworkReplyWebImpl::close()
{
    Q_D(QNetworkReplyWebImpl);

    if (d->state != QNetworkReplyPrivate::Aborted &&
        d->state != QNetworkReplyPrivate::Finished &&
        d->state != QNetworkReplyPrivate::Idle) {
            d->state = QNetworkReplyPrivate::Finished;
            d->setCanceled();
    }
    QNetworkReply::close();
}

void QNetworkReplyWebImpl::abort()
{
    Q_D(QNetworkReplyWebImpl);

    if (d->state == QNetworkReplyPrivate::Finished || d->state == QNetworkReplyPrivate::Aborted)
        return;

    d->state = QNetworkReplyPrivate::Aborted;
    d->setCanceled();
}

void QNetworkReplyWebImplPrivate::setCanceled()
{
    Q_Q(QNetworkReplyWebImpl);

    if (!m_abortController.isUndefined())
        m_abortController.call<void>("abort");

    emitReplyError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
    q->setFinished(true);
    emit q->finished();
}

qint64 QNetworkReplyWebImpl::bytesAvailable() const
{
    Q_D(const QNetworkReplyWebImpl);

    return QNetworkReply::bytesAvailable() + d->downloadBufferCurrentSize - d->downloadBufferReadPosition;
}

bool QNetworkReplyWebImpl::isSequential() const
{
    return true;
}

qint64 QNetworkReplyWebImpl::size() const
{
    return QNetworkReply::size();
}

/*!
    \internal
*/
qint64 QNetworkReplyWebImpl::readData(char *data, qint64 maxlen)
{
    Q_D(QNetworkReplyWebImpl);

    qint64 howMuch = qMin(maxlen, (d->downloadBuffer.size() - d->downloadBufferReadPosition));
    memcpy(data, d->downloadBuffer.constData() + d->downloadBufferReadPosition, howMuch);
    d->downloadBufferReadPosition += howMuch;

    return howMuch;
}

void QNetworkReplyWebImplPrivate::setup(QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *data)
{
    Q_Q(QNetworkReplyWebImpl);

    outgoingData = data;
    request = req;
    url = request.url();
    operation = op;

    q->QIODevice::open(QIODevice::ReadOnly);
    if (outgoingData && outgoingData->isSequential()) {
        bool bufferingDisallowed =
            request.attribute(QNetworkRequest::DoNotBufferUploadDataAttribute, false).toBool();

        if (bufferingDisallowed) {
            if (!request.header(QNetworkRequest::ContentLengthHeader).isValid()) {
                state = Buffering;
                _q_bufferOutgoingData();
                return;
            }
        } else {
            state = Buffering;
            _q_bufferOutgoingData();
            return;
        }
    }
    doSendRequest();
}

void QNetworkReplyWebImplPrivate::doSendRequest()
{
    Q_Q(QNetworkReplyWebImpl);
    totalDownloadSize = 0;

    // Create AbortController for cancellation support
    m_abortController = emscripten::val::global("AbortController").new_();

    // Build the fetch init object
    emscripten::val init = emscripten::val::object();
    init.set("method", std::string(q->methodName().constData()));
    init.set("signal", m_abortController["signal"]);

    // Credentials
    if (request.attribute(QNetworkRequest::UseCredentialsAttribute, false).toBool())
        init.set("credentials", std::string("include"));
    else
        init.set("credentials", std::string("same-origin"));

    // Cache control
    QNetworkRequest::CacheLoadControl cacheControl =
        (QNetworkRequest::CacheLoadControl)request.attribute(
            QNetworkRequest::CacheLoadControlAttribute,
            QNetworkRequest::PreferNetwork).toInt();

    switch (cacheControl) {
    case QNetworkRequest::AlwaysCache:
        init.set("cache", std::string("force-cache"));
        break;
    case QNetworkRequest::PreferCache:
        init.set("cache", std::string("force-cache"));
        break;
    case QNetworkRequest::AlwaysNetwork:
        init.set("cache", std::string("no-store"));
        break;
    case QNetworkRequest::PreferNetwork:
    default:
        init.set("cache", std::string("default"));
        break;
    }

    // Request headers
    emscripten::val headers = emscripten::val::global("Headers").new_();
    QStringList trimmedHeaders;
    const QList<QByteArray> headersData = request.rawHeaderList();
    for (const auto &headerName : headersData) {
        if (isUnsafeHeader(QLatin1StringView(headerName.constData()))) {
            trimmedHeaders.push_back(QString::fromLatin1(headerName));
        } else {
            headers.call<void>("append",
                std::string(headerName.constData()),
                std::string(request.rawHeader(headerName).constData()));
        }
    }
    if (!trimmedHeaders.isEmpty()) {
        qWarning() << "Qt has trimmed the following forbidden headers from the request:"
                   << trimmedHeaders.join(QLatin1StringView(", "));
    }
    init.set("headers", headers);

    // Request body
    if (outgoingData) {
        QByteArray requestData;
        if (outgoingDataBuffer) {
            requestData.reserve(outgoingDataBuffer->size());
            while (!outgoingDataBuffer->isEmpty())
                requestData.append(outgoingDataBuffer->read());
        } else {
            requestData = outgoingData->readAll();
        }
        if (!requestData.isEmpty()) {
            qstdweb::Uint8Array jsData = qstdweb::Uint8Array::copyFrom(requestData);
            init.set("body", jsData.val());
        }
    }

    // Call fetch()
    std::string urlString = request.url().toString().toStdString();
    emscripten::val fetchPromise = emscripten::val::global("fetch")(urlString, init);

    // The QNetworkReplyWebImpl may be deleted during the async operation,
    // so we use a QPointer guard to prevent accessing dangling pointers in callbacks.
    QPointer<QNetworkReplyWebImpl> guard(q);

    qstdweb::Promise promise(fetchPromise);
    promise
        .addThenFunction([guard, this](emscripten::val response) {
            if (!guard)
                return;

            int status = response["status"].as<int>();
            std::string statusText = response["statusText"].as<std::string>();

            setStatusCode(status, QByteArray::fromStdString(statusText));
            headersReceived(response["headers"]);

            bool ok = response["ok"].as<bool>();

            // Get the body as an ArrayBuffer
            emscripten::val arrayBufferPromise = response.call<emscripten::val>("arrayBuffer");
            qstdweb::Promise bodyPromise(arrayBufferPromise);
            bodyPromise
                .addThenFunction([guard, this, ok, status](emscripten::val arrayBuffer) {
                    if (!guard)
                        return;

                    // Convert ArrayBuffer to QByteArray
                    qstdweb::ArrayBuffer ab(arrayBuffer);
                    qstdweb::Uint8Array uint8Array(ab);
                    QByteArray buffer = uint8Array.copyToQByteArray();
                    dataReceived(buffer);

                    if (!ok) {
                        emitReplyError(statusCodeFromHttp(status, request.url()),
                                       QString::fromUtf8(buffer));
                    }

                    setReplyFinished();
                })
                .addCatchFunction([guard, this](emscripten::val error) {
                    if (!guard)
                        return;

                    std::string errorMessage = error["message"].as<std::string>();
                    emitReplyError(QNetworkReply::UnknownNetworkError,
                                   QString::fromStdString(errorMessage));
                    setReplyFinished();
                });
        })
        .addCatchFunction([guard, this](emscripten::val error) {
            if (!guard)
                return;

            std::string name = error["name"].as<std::string>();
            std::string errorMessage = error["message"].as<std::string>();

            if (name == "AbortError") {
                // Already handled by setCanceled()
                return;
            }

            emitReplyError(QNetworkReply::UnknownNetworkError,
                           QString::fromStdString(errorMessage));
            setReplyFinished();
        });

    state = Working;
}

void QNetworkReplyWebImplPrivate::emitReplyError(QNetworkReply::NetworkError errorCode, const QString &errorString)
{
    Q_Q(QNetworkReplyWebImpl);

    q->setError(errorCode, errorString);
    emit q->errorOccurred(errorCode);
}

void QNetworkReplyWebImplPrivate::emitDataReadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    Q_Q(QNetworkReplyWebImpl);

    totalDownloadSize = bytesTotal;

    percentFinished = bytesTotal ? (bytesReceived / bytesTotal) * 100 : 100;

    emit q->downloadProgress(bytesReceived, bytesTotal);
}

void QNetworkReplyWebImplPrivate::dataReceived(const QByteArray &buffer)
{
    Q_Q(QNetworkReplyWebImpl);

    const qsizetype bufferSize = buffer.size();
    if (bufferSize > 0)
        q->setReadBufferSize(bufferSize);

    bytesDownloaded = bufferSize;

    if (percentFinished != 100)
        downloadBufferCurrentSize += bufferSize;
    else
        downloadBufferCurrentSize = bufferSize;

    totalDownloadSize = downloadBufferCurrentSize;

    downloadBuffer.append(buffer);

    emit q->readyRead();
}

//taken from qnetworkrequest.cpp
static int parseHeaderName(const QByteArray &headerName)
{
    if (headerName.isEmpty())
        return -1;

    auto is = [&](const char *what) {
        return qstrnicmp(headerName.data(), headerName.size(), what) == 0;
    };

    switch (QtMiscUtils::toAsciiLower(headerName.front())) {
    case 'c':
        if (is("content-type"))
            return QNetworkRequest::ContentTypeHeader;
        else if (is("content-length"))
            return QNetworkRequest::ContentLengthHeader;
        else if (is("cookie"))
            return QNetworkRequest::CookieHeader;
        break;

    case 'l':
        if (is("location"))
            return QNetworkRequest::LocationHeader;
        else if (is("last-modified"))
            return QNetworkRequest::LastModifiedHeader;
        break;

    case 's':
        if (is("set-cookie"))
            return QNetworkRequest::SetCookieHeader;
        else if (is("server"))
            return QNetworkRequest::ServerHeader;
        break;

    case 'u':
        if (is("user-agent"))
            return QNetworkRequest::UserAgentHeader;
        break;
    }

    return -1; // nothing found
}

void QNetworkReplyWebImplPrivate::headersReceived(emscripten::val headers)
{
    Q_Q(QNetworkReplyWebImpl);

    // Use Headers.forEach() to iterate all response headers.
    // forEach calls the callback with (value, name, headers).
    // We use emscripten::val::module_property to get a C++ function callable from JS.

    // Collect headers by converting entries to an array and iterating
    emscripten::val entries = emscripten::val::global("Array").call<emscripten::val>(
        "from", headers.call<emscripten::val>("entries"));

    int length = entries["length"].as<int>();
    for (int i = 0; i < length; ++i) {
        emscripten::val entry = entries[i];
        std::string name = entry[0].as<std::string>();
        std::string value = entry[1].as<std::string>();

        QByteArray headerName = QByteArray::fromStdString(name);
        QByteArray headerValue = QByteArray::fromStdString(value);

        if (headerName.isEmpty() || headerValue.isEmpty())
            continue;

        int headerIndex = parseHeaderName(headerName);

        if (headerIndex == -1)
            q->setRawHeader(headerName, headerValue);
        else
            q->setHeader(static_cast<QNetworkRequest::KnownHeaders>(headerIndex),
                         (QVariant)headerValue);
    }
    emit q->metaDataChanged();
}

void QNetworkReplyWebImplPrivate::_q_bufferOutgoingDataFinished()
{
    Q_Q(QNetworkReplyWebImpl);

    if (state != Buffering)
        return;

    QObject::disconnect(outgoingData, SIGNAL(readyRead()), q, SLOT(_q_bufferOutgoingData()));
    QObject::disconnect(outgoingData, SIGNAL(readChannelFinished()), q, SLOT(_q_bufferOutgoingDataFinished()));

    doSendRequest();
}

void QNetworkReplyWebImplPrivate::_q_bufferOutgoingData()
{
    Q_Q(QNetworkReplyWebImpl);

    if (!outgoingDataBuffer) {
        outgoingDataBuffer = std::make_shared<QRingBuffer>();

        QObject::connect(outgoingData, SIGNAL(readyRead()), q, SLOT(_q_bufferOutgoingData()));
        QObject::connect(outgoingData, SIGNAL(readChannelFinished()), q, SLOT(_q_bufferOutgoingDataFinished()));
    }

    qint64 bytesBuffered = 0;
    qint64 bytesToBuffer = 0;

    forever {
        bytesToBuffer = outgoingData->bytesAvailable();
        if (bytesToBuffer <= 0)
            bytesToBuffer = 2*1024;

        char *dst = outgoingDataBuffer->reserve(bytesToBuffer);
        bytesBuffered = outgoingData->read(dst, bytesToBuffer);

        if (bytesBuffered == -1) {
            outgoingDataBuffer->chop(bytesToBuffer);
            _q_bufferOutgoingDataFinished();
            break;
        } else if (bytesBuffered == 0) {
            outgoingDataBuffer->chop(bytesToBuffer);
            break;
        } else {
            outgoingDataBuffer->chop(bytesToBuffer - bytesBuffered);
        }
    }
}

void QNetworkReplyWebImplPrivate::setReplyFinished()
{
    Q_Q(QNetworkReplyWebImpl);
    state = QNetworkReplyPrivate::Finished;
    q->setFinished(true);
    emit q->readChannelFinished();
    emit q->finished();
}

void QNetworkReplyWebImplPrivate::setStatusCode(int status, const QByteArray &statusText)
{
    Q_Q(QNetworkReplyWebImpl);
    q->setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
    q->setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, statusText);
}

//taken from qhttpthreaddelegate.cpp
QNetworkReply::NetworkError QNetworkReplyWebImplPrivate::statusCodeFromHttp(int httpStatusCode, const QUrl &url)
{
    QNetworkReply::NetworkError code;
    switch (httpStatusCode) {
    case 400:               // Bad Request
        code = QNetworkReply::ProtocolInvalidOperationError;
        break;

    case 401:               // Authorization required
        code = QNetworkReply::AuthenticationRequiredError;
        break;

    case 403:               // Access denied
        code = QNetworkReply::ContentAccessDenied;
        break;

    case 404:               // Not Found
        code = QNetworkReply::ContentNotFoundError;
        break;

    case 405:               // Method Not Allowed
        code = QNetworkReply::ContentOperationNotPermittedError;
        break;

    case 407:
        code = QNetworkReply::ProxyAuthenticationRequiredError;
        break;

    case 409:               // Resource Conflict
        code = QNetworkReply::ContentConflictError;
        break;

    case 410:               // Content no longer available
        code = QNetworkReply::ContentGoneError;
        break;

    case 418:               // I'm a teapot
        code = QNetworkReply::ProtocolInvalidOperationError;
        break;

    case 500:               // Internal Server Error
        code = QNetworkReply::InternalServerError;
        break;

    case 501:               // Server does not support this functionality
        code = QNetworkReply::OperationNotImplementedError;
        break;

    case 503:               // Service unavailable
        code = QNetworkReply::ServiceUnavailableError;
        break;

    default:
        if (httpStatusCode > 500) {
            code = QNetworkReply::UnknownServerError;
        } else if (httpStatusCode >= 400) {
            code = QNetworkReply::UnknownContentError;
        } else {
            qWarning("QNetworkAccess: got HTTP status code %d which is not expected from url: \"%s\"",
                     httpStatusCode, qPrintable(url.toString()));
            code = QNetworkReply::ProtocolFailure;
        }
    };

    return code;
}

QT_END_NAMESPACE

#include "moc_qnetworkreplywebimpl_p.cpp"

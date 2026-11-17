#include "cast/CastMediaServer.h"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QUuid>
#include <memory>

namespace honeycomb {

CastMediaServer::CastMediaServer(QObject* parent) : QObject(parent) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &CastMediaServer::onNewConnection);
}

CastMediaServer::~CastMediaServer() {
    stopServer();
}

bool CastMediaServer::ensureStarted() {
    if (m_server->isListening()) return true;
    return m_server->listen(QHostAddress::AnyIPv4, 0);
}

void CastMediaServer::stopServer() {
    if (m_server && m_server->isListening()) {
        m_server->close();
    }
}

QString CastMediaServer::localAddressForTarget(const QString& targetHost, quint16 targetPort) {
    QUdpSocket udp;
    udp.connectToHost(QHostAddress(targetHost), targetPort > 0 ? targetPort : 8009);
    const QHostAddress local = udp.localAddress();
    if (!local.isNull() && local.protocol() == QAbstractSocket::IPv4Protocol && !local.isLoopback()) {
        return local.toString();
    }

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                return entry.ip().toString();
            }
        }
    }
    return "127.0.0.1";
}

QString CastMediaServer::mimeTypeForPath(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "flac") return "audio/flac";
    if (ext == "m4a" || ext == "mp4" || ext == "aac") return "audio/mp4";
    if (ext == "ogg" || ext == "oga") return "audio/ogg";
    if (ext == "opus") return "audio/opus";
    if (ext == "wav") return "audio/wav";
    if (ext == "webm") return "audio/webm";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    return "application/octet-stream";
}

QString CastMediaServer::setMedia(const QString& trackPath,
                                  const QString& artPath,
                                  const QString& targetHost,
                                  quint16 targetPort,
                                  QString* outContentType,
                                  QString* outArtUrl) {
    if (!ensureStarted()) return {};

    m_token = QUuid::createUuid().toString(QUuid::Id128);
    m_trackPath = trackPath;
    m_artPath = artPath;
    m_mimeType = mimeTypeForPath(trackPath);

    if (outContentType) *outContentType = m_mimeType;

    const QString localIp = localAddressForTarget(targetHost, targetPort);
    const quint16 port = m_server->serverPort();
    const QString ext = QFileInfo(trackPath).suffix().toLower();
    const QString trackUrl = QString("http://%1:%2/track/%3%4")
                                 .arg(localIp)
                                 .arg(port)
                                 .arg(m_token, ext.isEmpty() ? QString() : "." + ext);

    if (outArtUrl) {
        if (!artPath.isEmpty() && QFileInfo::exists(artPath)) {
            const QString artExt = QFileInfo(artPath).suffix().toLower();
            *outArtUrl = QString("http://%1:%2/art/%3.%4")
                             .arg(localIp)
                             .arg(port)
                             .arg(m_token, artExt.isEmpty() ? QString("jpg") : artExt);
        } else {
            outArtUrl->clear();
        }
    }

    return trackUrl;
}

void CastMediaServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer]() {
            buffer->append(sock->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                const QByteArray req = buffer->left(headerEnd + 4);
                buffer->clear();
                handleHttpRequest(sock, req);
            }
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void CastMediaServer::handleHttpRequest(QTcpSocket* sock, const QByteArray& rawRequest) {
    const QList<QByteArray> lines = rawRequest.split('\n');
    if (lines.isEmpty()) {
        sendErrorResponse(sock, 400, "Bad Request");
        return;
    }

    const QList<QByteArray> parts = lines[0].trimmed().split(' ');
    if (parts.size() < 2) {
        sendErrorResponse(sock, 400, "Bad Request");
        return;
    }

    const QByteArray method = parts[0].toUpper();
    if (method != "GET" && method != "HEAD") {
        sendErrorResponse(sock, 405, "Method Not Allowed");
        return;
    }

    QByteArray rangeHeader;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "range") {
            rangeHeader = line.mid(colon + 1).trimmed();
        }
    }

    const QString path = QString::fromUtf8(parts[1]);
    const QString expectedTrackPrefix = "/track/" + m_token;
    const QString expectedArtPrefix = "/art/" + m_token;

    if (!m_token.isEmpty() && path.startsWith(expectedTrackPrefix) && !m_trackPath.isEmpty()) {
        sendFileResponse(sock, m_trackPath, m_mimeType, rangeHeader, method == "HEAD");
        return;
    }
    if (!m_token.isEmpty() && path.startsWith(expectedArtPrefix) && !m_artPath.isEmpty()) {
        sendFileResponse(sock, m_artPath, mimeTypeForPath(m_artPath), rangeHeader, method == "HEAD");
        return;
    }

    sendErrorResponse(sock, 404, "Not Found");
}

void CastMediaServer::sendFileResponse(QTcpSocket* sock,
                                       const QString& filePath,
                                       const QString& contentType,
                                       const QByteArray& rangeHeader,
                                       bool headOnly) {
    auto file = std::make_shared<QFile>(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        sendErrorResponse(sock, 404, "Not Found");
        return;
    }

    const qint64 totalSize = file->size();
    qint64 start = 0;
    qint64 end = totalSize > 0 ? totalSize - 1 : 0;
    bool partial = false;

    if (rangeHeader.startsWith("bytes=")) {
        const QByteArray spec = rangeHeader.mid(6).split(',')[0].trimmed();
        const int dash = spec.indexOf('-');
        if (dash >= 0) {
            const QByteArray sStr = spec.left(dash).trimmed();
            const QByteArray eStr = spec.mid(dash + 1).trimmed();
            if (!sStr.isEmpty()) {
                start = sStr.toLongLong();
                end = eStr.isEmpty() ? (totalSize - 1) : qMin(eStr.toLongLong(), totalSize - 1);
                partial = true;
            } else if (!eStr.isEmpty()) {
                const qint64 suffix = eStr.toLongLong();
                start = qMax<qint64>(0, totalSize - suffix);
                end = totalSize - 1;
                partial = true;
            }
        }
    }

    if (totalSize > 0 && (start < 0 || start > end || start >= totalSize)) {
        sendErrorResponse(sock, 416, "Range Not Satisfiable");
        return;
    }

    const qint64 contentLen = (totalSize == 0) ? 0 : (end - start + 1);
    QByteArray headers;
    if (partial) {
        headers += "HTTP/1.1 206 Partial Content\r\n";
        headers += QByteArray("Content-Range: bytes ") + QByteArray::number(start) + "-"
                   + QByteArray::number(end) + "/" + QByteArray::number(totalSize) + "\r\n";
    } else {
        headers += "HTTP/1.1 200 OK\r\n";
    }
    headers += "Content-Type: " + contentType.toUtf8() + "\r\n";
    headers += "Content-Length: " + QByteArray::number(contentLen) + "\r\n";
    headers += "Accept-Ranges: bytes\r\n";
    headers += "Access-Control-Allow-Origin: *\r\n";
    headers += "Connection: close\r\n\r\n";

    sock->write(headers);
    if (headOnly || contentLen == 0) {
        sock->disconnectFromHost();
        return;
    }

    file->seek(start);
    auto remaining = std::make_shared<qint64>(contentLen);

    auto pump = [sock, file, remaining]() {
        while (*remaining > 0 && sock->bytesToWrite() < 256 * 1024) {
            const qint64 toRead = qMin<qint64>(65536, *remaining);
            const QByteArray chunk = file->read(toRead);
            if (chunk.isEmpty()) {
                *remaining = 0;
                break;
            }
            sock->write(chunk);
            *remaining -= chunk.size();
        }
        if (*remaining <= 0) {
            sock->disconnectFromHost();
        }
    };

    connect(sock, &QTcpSocket::bytesWritten, sock, [pump](qint64) { pump(); });
    pump();
}

void CastMediaServer::sendErrorResponse(QTcpSocket* sock, int code, const QByteArray& reason) {
    QByteArray resp = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    resp += "Content-Length: 0\r\nConnection: close\r\n\r\n";
    sock->write(resp);
    sock->disconnectFromHost();
}

} // namespace honeycomb

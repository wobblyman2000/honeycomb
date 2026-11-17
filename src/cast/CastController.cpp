#include "cast/CastController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

namespace honeycomb {

namespace {

constexpr const char* kDefaultMediaReceiverAppId = "CC1AD845";
constexpr const char* kSenderId = "sender-0";
constexpr const char* kReceiverId = "receiver-0";
constexpr const char* kNsConnection = "urn:x-cast:com.google.cast.tp.connection";
constexpr const char* kNsHeartbeat = "urn:x-cast:com.google.cast.tp.heartbeat";
constexpr const char* kNsReceiver = "urn:x-cast:com.google.cast.receiver";
constexpr const char* kNsMedia = "urn:x-cast:com.google.cast.media";

void appendVarint(QByteArray& out, quint64 v) {
    while (v >= 0x80) {
        out.append(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.append(static_cast<char>(v));
}

void appendStringField(QByteArray& out, int fieldNum, const QByteArray& val) {
    appendVarint(out, static_cast<quint64>((fieldNum << 3) | 2));
    appendVarint(out, static_cast<quint64>(val.size()));
    out.append(val);
}

QByteArray encodeCastMessage(const QString& sourceId,
                             const QString& destinationId,
                             const QString& ns,
                             const QByteArray& payloadUtf8) {
    QByteArray pb;
    // 1: protocol_version = CASTV2_1_0 (0)
    appendVarint(pb, (1 << 3) | 0);
    appendVarint(pb, 0);
    // 2: source_id
    appendStringField(pb, 2, sourceId.toUtf8());
    // 3: destination_id
    appendStringField(pb, 3, destinationId.toUtf8());
    // 4: namespace
    appendStringField(pb, 4, ns.toUtf8());
    // 5: payload_type = STRING (0)
    appendVarint(pb, (5 << 3) | 0);
    appendVarint(pb, 0);
    // 6: payload_utf8
    appendStringField(pb, 6, payloadUtf8);

    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(pb.size()), frame.data());
    frame.append(pb);
    return frame;
}

bool readVarint(const QByteArray& data, int& pos, quint64& out) {
    out = 0;
    int shift = 0;
    while (pos < data.size()) {
        const quint8 b = static_cast<quint8>(data[pos++]);
        out |= static_cast<quint64>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

struct DecodedCastMsg {
    QString sourceId;
    QString destinationId;
    QString ns;
    QByteArray payloadUtf8;
};

bool decodeCastMessage(const QByteArray& pb, DecodedCastMsg& msg) {
    int pos = 0;
    while (pos < pb.size()) {
        quint64 tag = 0;
        if (!readVarint(pb, pos, tag)) return false;
        const int field = static_cast<int>(tag >> 3);
        const int wire = static_cast<int>(tag & 0x07);

        if (wire == 0) {
            quint64 val = 0;
            if (!readVarint(pb, pos, val)) return false;
        } else if (wire == 2) {
            quint64 len = 0;
            if (!readVarint(pb, pos, len)) return false;
            if (pos + static_cast<int>(len) > pb.size()) return false;
            const QByteArray bytes = pb.mid(pos, static_cast<int>(len));
            pos += static_cast<int>(len);
            if (field == 2) msg.sourceId = QString::fromUtf8(bytes);
            else if (field == 3) msg.destinationId = QString::fromUtf8(bytes);
            else if (field == 4) msg.ns = QString::fromUtf8(bytes);
            else if (field == 6) msg.payloadUtf8 = bytes;
        } else if (wire == 1) {
            if (pos + 8 > pb.size()) return false;
            pos += 8;
        } else if (wire == 5) {
            if (pos + 4 > pb.size()) return false;
            pos += 4;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

CastController::CastController(QObject* parent) : QObject(parent) {
    m_server = new CastMediaServer(this);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(5000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            sendCastMessage(kReceiverId, kNsHeartbeat, QJsonObject{{"type", "PING"}});
        }
    });

    m_statusPollTimer = new QTimer(this);
    m_statusPollTimer->setInterval(2000);
    connect(m_statusPollTimer, &QTimer::timeout, this, [this]() {
        if (!m_transportId.isEmpty() && m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            sendCastMessage(m_transportId, kNsMedia, QJsonObject{
                {"type", "GET_STATUS"},
                {"requestId", m_nextRequestId++}
            });
        }
    });

    m_posTickTimer = new QTimer(this);
    m_posTickTimer->setInterval(250);
    connect(m_posTickTimer, &QTimer::timeout, this, [this]() {
        if (m_state == AudioEngine::State::Playing) {
            m_currentPosSec += 0.25;
            if (m_durationSec > 0.0 && m_currentPosSec > m_durationSec) {
                m_currentPosSec = m_durationSec;
            }
            emit positionChanged(m_currentPosSec);
        }
    });
}

CastController::~CastController() {
    disconnectFromDevice();
}

void CastController::connectToDevice(const CastDevice& device) {
    disconnectFromDevice();
    if (!device.isValid()) return;

    m_device = device;
    m_connecting = true;
    m_launchSent = false;
    m_sessionId.clear();
    m_transportId.clear();
    m_mediaSessionId = 0;
    m_rxBuffer.clear();

    m_socket = new QSslSocket(this);
    m_socket->setPeerVerifyMode(QSslSocket::VerifyNone);

    connect(m_socket, &QSslSocket::encrypted, this, [this]() {
        m_connecting = false;
        sendCastMessage(kReceiverId, kNsConnection, QJsonObject{{"type", "CONNECT"}});
        sendCastMessage(kReceiverId, kNsReceiver, QJsonObject{
            {"type", "GET_STATUS"},
            {"requestId", m_nextRequestId++}
        });
        m_heartbeatTimer->start();
    });

    connect(m_socket, &QSslSocket::readyRead, this, &CastController::onSocketReadyRead);

    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError>&) {
        if (m_socket) m_socket->ignoreSslErrors();
    });

    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        const QString err = m_socket ? m_socket->errorString() : "Cast connection error";
        emit errorOccurred(err);
        disconnectFromDevice();
    });

    connect(m_socket, &QSslSocket::disconnected, this, [this]() {
        if (!m_transportId.isEmpty() || m_connecting) {
            disconnectFromDevice();
        }
    });

    m_socket->connectToHostEncrypted(device.address, device.port);
}

void CastController::disconnectFromDevice() {
    const bool wasConnected = !m_transportId.isEmpty() || m_connecting;

    m_heartbeatTimer->stop();
    m_statusPollTimer->stop();
    m_posTickTimer->stop();

    if (m_socket) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            if (!m_sessionId.isEmpty()) {
                sendCastMessage(kReceiverId, kNsReceiver, QJsonObject{
                    {"type", "STOP"},
                    {"sessionId", m_sessionId},
                    {"requestId", m_nextRequestId++}
                });
            }
            if (!m_transportId.isEmpty()) {
                sendCastMessage(m_transportId, kNsConnection, QJsonObject{{"type", "CLOSE"}});
            }
            sendCastMessage(kReceiverId, kNsConnection, QJsonObject{{"type", "CLOSE"}});
            m_socket->flush();
        }
        m_socket->disconnect();
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_server->stopServer();
    m_connecting = false;
    m_launchSent = false;
    m_hasPendingLoad = false;
    m_loadingNewTrack = false;
    m_sessionId.clear();
    m_transportId.clear();
    m_mediaSessionId = 0;
    m_rxBuffer.clear();
    setState(AudioEngine::State::Stopped);

    if (wasConnected) {
        emit connectedChanged(false, QString());
    }
}

void CastController::playTrack(const Track& track, const QString& artPath, double startSeconds) {
    m_pendingTrack = track;
    m_pendingArtPath = artPath;
    m_pendingStartSec = qMax(0.0, startSeconds);
    m_hasPendingLoad = true;
    m_trackFinishedEmitted = false;

    if (!m_transportId.isEmpty()) {
        sendLoadPending();
    }
}

void CastController::togglePause() {
    if (m_state == AudioEngine::State::Playing) {
        setPaused(true);
    } else if (m_state == AudioEngine::State::Paused) {
        setPaused(false);
    }
}

void CastController::setPaused(bool paused) {
    if (m_transportId.isEmpty() || m_mediaSessionId == 0) return;
    sendCastMessage(m_transportId, kNsMedia, QJsonObject{
        {"type", paused ? "PAUSE" : "PLAY"},
        {"mediaSessionId", m_mediaSessionId},
        {"requestId", m_nextRequestId++}
    });
}

void CastController::stop() {
    if (m_transportId.isEmpty() || m_mediaSessionId == 0) {
        setState(AudioEngine::State::Stopped);
        return;
    }
    m_loadingNewTrack = true;
    sendCastMessage(m_transportId, kNsMedia, QJsonObject{
        {"type", "STOP"},
        {"mediaSessionId", m_mediaSessionId},
        {"requestId", m_nextRequestId++}
    });
    setState(AudioEngine::State::Stopped);
}

void CastController::seekSeconds(double pos) {
    if (m_transportId.isEmpty() || m_mediaSessionId == 0) return;
    m_currentPosSec = qMax(0.0, pos);
    sendCastMessage(m_transportId, kNsMedia, QJsonObject{
        {"type", "SEEK"},
        {"mediaSessionId", m_mediaSessionId},
        {"currentTime", m_currentPosSec},
        {"requestId", m_nextRequestId++}
    });
    emit positionChanged(m_currentPosSec);
}

void CastController::setVolume(int vol0to100) {
    m_volume = qBound(0, vol0to100, 100);
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    sendCastMessage(kReceiverId, kNsReceiver, QJsonObject{
        {"type", "SET_VOLUME"},
        {"volume", QJsonObject{{"level", m_volume / 100.0}}},
        {"requestId", m_nextRequestId++}
    });
}

void CastController::sendCastMessage(const QString& destinationId,
                                     const QString& ns,
                                     const QJsonObject& payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_socket->write(encodeCastMessage(kSenderId, destinationId, ns, json));
}

void CastController::onSocketReadyRead() {
    if (!m_socket) return;
    m_rxBuffer.append(m_socket->readAll());

    while (m_rxBuffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(m_rxBuffer.constData());
        if (len > 1024 * 1024) {
            emit errorOccurred("Invalid Cast packet length");
            disconnectFromDevice();
            return;
        }
        if (m_rxBuffer.size() < static_cast<int>(4 + len)) {
            break;
        }
        const QByteArray packet = m_rxBuffer.mid(4, static_cast<int>(len));
        m_rxBuffer.remove(0, static_cast<int>(4 + len));
        handleCastPacket(packet);
    }
}

void CastController::handleCastPacket(const QByteArray& packet) {
    DecodedCastMsg msg;
    if (!decodeCastMessage(packet, msg) || msg.payloadUtf8.isEmpty()) return;

    const QJsonDocument doc = QJsonDocument::fromJson(msg.payloadUtf8);
    if (!doc.isObject()) return;
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();

    if (msg.ns == kNsHeartbeat) {
        if (type == "PING") {
            sendCastMessage(msg.sourceId, kNsHeartbeat, QJsonObject{{"type", "PONG"}});
        }
        return;
    }

    if (msg.ns == kNsReceiver) {
        if (type == "RECEIVER_STATUS") {
            handleReceiverStatus(obj.value("status").toObject());
        }
        return;
    }

    if (msg.ns == kNsMedia) {
        if (type == "MEDIA_STATUS") {
            handleMediaStatus(obj);
        }
        return;
    }
}

void CastController::handleReceiverStatus(const QJsonObject& status) {
    const QJsonArray apps = status.value("applications").toArray();
    QString foundSessionId;
    QString foundTransportId;

    for (const QJsonValue& val : apps) {
        const QJsonObject app = val.toObject();
        if (app.value("appId").toString() == kDefaultMediaReceiverAppId) {
            foundSessionId = app.value("sessionId").toString();
            foundTransportId = app.value("transportId").toString();
            break;
        }
    }

    if (!foundTransportId.isEmpty()) {
        const bool newlyConnected = (m_transportId != foundTransportId);
        m_sessionId = foundSessionId;
        m_transportId = foundTransportId;

        if (newlyConnected) {
            sendCastMessage(m_transportId, kNsConnection, QJsonObject{{"type", "CONNECT"}});
            m_statusPollTimer->start();
            setVolume(m_volume);
            emit connectedChanged(true, m_device.name);
        }

        if (m_hasPendingLoad) {
            sendLoadPending();
        }
    } else if (!m_launchSent) {
        m_launchSent = true;
        sendCastMessage(kReceiverId, kNsReceiver, QJsonObject{
            {"type", "LAUNCH"},
            {"appId", kDefaultMediaReceiverAppId},
            {"requestId", m_nextRequestId++}
        });
    }
}

void CastController::sendLoadPending() {
    if (m_transportId.isEmpty() || !m_hasPendingLoad) return;

    m_hasPendingLoad = false;
    m_loadingNewTrack = true;
    m_trackFinishedEmitted = false;

    QString contentType;
    QString artUrl;
    const QString mediaUrl = m_server->setMedia(m_pendingTrack.path,
                                                m_pendingArtPath,
                                                m_device.address,
                                                m_device.port,
                                                &contentType,
                                                &artUrl);
    if (mediaUrl.isEmpty()) {
        emit errorOccurred("Failed to start local HTTP media server for Cast");
        return;
    }

    QJsonObject metadata{
        {"metadataType", 3}, // MUSIC_TRACK
        {"title", m_pendingTrack.title},
        {"artist", m_pendingTrack.artist},
        {"albumName", m_pendingTrack.album}
    };
    if (!artUrl.isEmpty()) {
        metadata["images"] = QJsonArray{QJsonObject{{"url", artUrl}}};
    }

    QJsonObject media{
        {"contentId", mediaUrl},
        {"streamType", "BUFFERED"},
        {"contentType", contentType},
        {"metadata", metadata}
    };

    if (m_pendingTrack.durationMs > 0) {
        m_durationSec = m_pendingTrack.durationMs / 1000.0;
        emit durationChanged(m_durationSec);
    }
    m_currentPosSec = m_pendingStartSec;

    sendCastMessage(m_transportId, kNsMedia, QJsonObject{
        {"type", "LOAD"},
        {"requestId", m_nextRequestId++},
        {"autoplay", true},
        {"currentTime", m_pendingStartSec},
        {"media", media}
    });
}

void CastController::handleMediaStatus(const QJsonObject& payload) {
    const QJsonArray list = payload.value("status").toArray();
    if (list.isEmpty()) return;

    const QJsonObject st = list.first().toObject();
    if (st.contains("mediaSessionId")) {
        m_mediaSessionId = static_cast<qint64>(st.value("mediaSessionId").toDouble());
    }

    const QJsonObject media = st.value("media").toObject();
    if (media.contains("duration")) {
        const double dur = media.value("duration").toDouble();
        if (dur > 0.0) {
            m_durationSec = dur;
            emit durationChanged(m_durationSec);
        }
    }

    if (st.contains("currentTime")) {
        m_currentPosSec = st.value("currentTime").toDouble();
        emit positionChanged(m_currentPosSec);
    }

    const QString playerState = st.value("playerState").toString();
    const QString idleReason = st.value("idleReason").toString();

    if (playerState == "PLAYING") {
        m_loadingNewTrack = false;
        setState(AudioEngine::State::Playing);
        if (!m_posTickTimer->isActive()) m_posTickTimer->start();
    } else if (playerState == "BUFFERING") {
        m_loadingNewTrack = false;
        setState(AudioEngine::State::Playing);
        m_posTickTimer->stop();
    } else if (playerState == "PAUSED") {
        m_loadingNewTrack = false;
        setState(AudioEngine::State::Paused);
        m_posTickTimer->stop();
    } else if (playerState == "IDLE") {
        m_posTickTimer->stop();
        if (idleReason == "FINISHED" && !m_loadingNewTrack && !m_trackFinishedEmitted) {
            m_trackFinishedEmitted = true;
            setState(AudioEngine::State::Stopped);
            emit trackFinished();
        } else if (idleReason == "CANCELLED" || idleReason == "ERROR") {
            m_loadingNewTrack = false;
            setState(AudioEngine::State::Stopped);
        }
    }
}

void CastController::setState(AudioEngine::State st) {
    if (m_state == st) return;
    m_state = st;
    emit stateChanged(m_state);
}

} // namespace honeycomb

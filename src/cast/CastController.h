#pragma once

#include "audio/AudioEngine.h"
#include "cast/CastDevice.h"
#include "cast/CastMediaServer.h"
#include "core/Track.h"

#include <QJsonObject>
#include <QObject>
#include <QSslSocket>
#include <QTimer>

namespace honeycomb {

class CastController : public QObject {
    Q_OBJECT
public:
    explicit CastController(QObject* parent = nullptr);
    ~CastController() override;

    void connectToDevice(const CastDevice& device);
    void disconnectFromDevice();

    bool isConnected() const { return !m_transportId.isEmpty(); }
    bool isConnecting() const { return m_connecting; }
    CastDevice currentDevice() const { return m_device; }
    AudioEngine::State state() const { return m_state; }

    void playTrack(const Track& track, const QString& artPath, double startSeconds = 0.0);
    void togglePause();
    void setPaused(bool paused);
    void stop();
    void seekSeconds(double pos);
    void setVolume(int vol0to100);

signals:
    void connectedChanged(bool connected, const QString& deviceName);
    void stateChanged(AudioEngine::State state);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void trackFinished();
    void errorOccurred(const QString& message);

private:
    void sendCastMessage(const QString& destinationId,
                         const QString& ns,
                         const QJsonObject& payload);
    void onSocketReadyRead();
    void handleCastPacket(const QByteArray& packet);
    void handleReceiverStatus(const QJsonObject& status);
    void handleMediaStatus(const QJsonObject& payload);
    void sendLoadPending();
    void setState(AudioEngine::State st);

    CastDevice m_device;
    CastMediaServer* m_server = nullptr;
    QSslSocket* m_socket = nullptr;
    QByteArray m_rxBuffer;

    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_statusPollTimer = nullptr;
    QTimer* m_posTickTimer = nullptr;

    bool m_connecting = false;
    bool m_launchSent = false;
    bool m_hasPendingLoad = false;
    bool m_loadingNewTrack = false;
    bool m_trackFinishedEmitted = false;

    int m_nextRequestId = 1;
    QString m_sessionId;
    QString m_transportId;
    qint64 m_mediaSessionId = 0;

    Track m_pendingTrack;
    QString m_pendingArtPath;
    double m_pendingStartSec = 0.0;

    AudioEngine::State m_state = AudioEngine::State::Stopped;
    double m_currentPosSec = 0.0;
    double m_durationSec = 0.0;
    int m_volume = 80;
};

} // namespace honeycomb

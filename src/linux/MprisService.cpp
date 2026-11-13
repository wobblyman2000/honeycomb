#include "MprisService.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QUrl>

namespace honeycomb {

MprisRootAdaptor::MprisRootAdaptor(MprisService* parent)
    : QDBusAbstractAdaptor(parent), m_service(parent) {}

void MprisRootAdaptor::Raise() { emit m_service->raiseRequested(); }
void MprisRootAdaptor::Quit() { emit m_service->quitRequested(); }

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisService* parent)
    : QDBusAbstractAdaptor(parent), m_service(parent) {}

QString MprisPlayerAdaptor::playbackStatus() const {
    switch (m_service->state()) {
    case AudioEngine::State::Playing: return "Playing";
    case AudioEngine::State::Paused: return "Paused";
    case AudioEngine::State::Stopped: return "Stopped";
    }
    return "Stopped";
}

QVariantMap MprisPlayerAdaptor::metadata() const { return m_service->metadata(); }
double MprisPlayerAdaptor::volume() const { return m_service->volume(); }
void MprisPlayerAdaptor::setVolume(double v) { emit m_service->volumeRequested(qBound(0.0, v, 1.0)); }
qlonglong MprisPlayerAdaptor::position() const { return m_service->positionUs(); }

void MprisPlayerAdaptor::Next() { emit m_service->nextRequested(); }
void MprisPlayerAdaptor::Previous() { emit m_service->previousRequested(); }
void MprisPlayerAdaptor::Pause() { emit m_service->pauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { emit m_service->playPauseRequested(); }
void MprisPlayerAdaptor::Stop() { emit m_service->stopRequested(); }
void MprisPlayerAdaptor::Play() { emit m_service->playRequested(); }
void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
    emit m_service->seekRelativeRequested(static_cast<double>(offsetUs) / 1000000.0);
}
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath&, qlonglong positionUs) {
    emit m_service->seekAbsoluteRequested(static_cast<double>(positionUs) / 1000000.0);
}

MprisService::MprisService(const QString& artCacheDir, QObject* parent)
    : QObject(parent), m_artCacheDir(artCacheDir) {
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);

    auto bus = QDBusConnection::sessionBus();
    bus.registerObject("/org/mpris/MediaPlayer2", this);
    bus.registerService("org.mpris.MediaPlayer2.honeycomb");
}

void MprisService::setPlaybackState(AudioEngine::State state) {
    if (m_state == state) return;
    m_state = state;
    QString statusStr = "Stopped";
    if (state == AudioEngine::State::Playing) statusStr = "Playing";
    else if (state == AudioEngine::State::Paused) statusStr = "Paused";
    emitPropertiesChanged("org.mpris.MediaPlayer2.Player", {{"PlaybackStatus", statusStr}});
}

void MprisService::setCurrentTrack(const Track& track) {
    QVariantMap meta;
    meta["mpris:trackid"] = QVariant::fromValue(
        QDBusObjectPath(QString("/org/honeycomb/track/%1").arg(qMax<int64_t>(1, track.id))));
    meta["mpris:length"] = static_cast<qlonglong>(track.durationMs * 1000LL);
    meta["xesam:title"] = track.title;
    meta["xesam:artist"] = QStringList{track.artist};
    meta["xesam:album"] = track.album;
    if (!track.albumArtist.isEmpty()) {
        meta["xesam:albumArtist"] = QStringList{track.albumArtist};
    }
    if (!track.artHash.isEmpty()) {
        const QString artPath = m_artCacheDir + "/" + track.artHash + ".img";
        if (QFileInfo::exists(artPath)) {
            meta["mpris:artUrl"] = QUrl::fromLocalFile(artPath).toString();
        }
    }
    m_metadata = meta;
    emitPropertiesChanged("org.mpris.MediaPlayer2.Player", {{"Metadata", m_metadata}});
}

void MprisService::setPositionSeconds(double posSec) {
    m_positionSec = posSec;
}

void MprisService::setVolume(double vol0to1) {
    m_volume = qBound(0.0, vol0to1, 1.0);
}

void MprisService::emitPropertiesChanged(const QString& interfaceName, const QVariantMap& changed) {
    QDBusMessage msg = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged");
    msg << interfaceName << changed << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

} // namespace honeycomb

#pragma once

#include "audio/AudioEngine.h"
#include "core/Track.h"

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

namespace honeycomb {

class MprisService;

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(MprisService* parent);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return "Honeycomb"; }
    QString desktopEntry() const { return "honeycomb"; }
    QStringList supportedUriSchemes() const { return {"file"}; }
    QStringList supportedMimeTypes() const {
        return {"audio/flac", "audio/mpeg", "audio/ogg", "audio/opus", "audio/mp4", "audio/wav"};
    }

public slots:
    void Raise();
    void Quit();

private:
    MprisService* m_service;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious CONSTANT)
    Q_PROPERTY(bool CanPlay READ canPlay CONSTANT)
    Q_PROPERTY(bool CanPause READ canPause CONSTANT)
    Q_PROPERTY(bool CanSeek READ canSeek CONSTANT)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(MprisService* parent);

    QString playbackStatus() const;
    double rate() const { return 1.0; }
    void setRate(double) {}
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const { return true; }
    bool canGoPrevious() const { return true; }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const { return true; }
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath&, qlonglong positionUs);

private:
    MprisService* m_service;
};

class MprisService : public QObject {
    Q_OBJECT
public:
    explicit MprisService(const QString& artCacheDir, QObject* parent = nullptr);

    void setPlaybackState(AudioEngine::State state);
    void setCurrentTrack(const Track& track);
    void setPositionSeconds(double posSec);
    void setVolume(double vol0to1);

    AudioEngine::State state() const { return m_state; }
    QVariantMap metadata() const { return m_metadata; }
    double volume() const { return m_volume; }
    qlonglong positionUs() const { return static_cast<qlonglong>(m_positionSec * 1000000.0); }

signals:
    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRelativeRequested(double deltaSec);
    void seekAbsoluteRequested(double posSec);
    void volumeRequested(double vol0to1);
    void raiseRequested();
    void quitRequested();

private:
    void emitPropertiesChanged(const QString& interfaceName, const QVariantMap& changed);

    QString m_artCacheDir;
    AudioEngine::State m_state = AudioEngine::State::Stopped;
    QVariantMap m_metadata;
    double m_positionSec = 0.0;
    double m_volume = 0.8;
};

} // namespace honeycomb

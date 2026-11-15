#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <mpv/client.h>

namespace honeycomb {

class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };
    enum class ReplayGainMode { Off = 0, Track = 1, Album = 2 };

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    void playFile(const QString& path);
    void togglePause();
    void setPaused(bool paused);
    void stop();
    void seekSeconds(double pos);
    void setVolume(int vol0to100);
    int volume() const { return m_userVolume; }

    void setReplayGain(ReplayGainMode mode);
    ReplayGainMode replayGain() const { return m_replayGain; }

    void setCrossfadeSeconds(int seconds);
    int crossfadeSeconds() const { return m_crossfadeSec; }

    State state() const { return m_state; }

signals:
    void stateChanged(AudioEngine::State state);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void trackFinished();
    void errorOccurred(const QString& message);

private:
    static void onMpvWakeup(void* ctx);
    void processMpvEvents();
    void applyMpvVolume(double vol);

    mpv_handle* m_mpv = nullptr;
    State m_state = State::Stopped;
    ReplayGainMode m_replayGain = ReplayGainMode::Album;
    int m_crossfadeSec = 0;
    int m_userVolume = 85;
    double m_durationSec = 0.0;
    bool m_crossfadeTriggered = false;
    QTimer* m_fadeInTimer = nullptr;
    double m_fadeInElapsed = 0.0;
};

} // namespace honeycomb

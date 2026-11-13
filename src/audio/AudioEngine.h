#pragma once

#include <QObject>
#include <QString>
#include <mpv/client.h>

namespace honeycomb {

class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    void playFile(const QString& path);
    void togglePause();
    void setPaused(bool paused);
    void stop();
    void seekSeconds(double pos);
    void setVolume(int vol0to100);

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

    mpv_handle* m_mpv = nullptr;
    State m_state = State::Stopped;
};

} // namespace honeycomb

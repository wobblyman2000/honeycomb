#include "AudioEngine.h"

#include <QMetaObject>
#include <clocale>

namespace honeycomb {

AudioEngine::AudioEngine(QObject* parent) : QObject(parent) {
    // libmpv requires C numeric locale
    std::setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) return;

    mpv_set_option_string(m_mpv, "vid", "no");
    mpv_set_option_string(m_mpv, "audio-display", "no");
    mpv_set_option_string(m_mpv, "gapless-audio", "yes");
    mpv_set_option_string(m_mpv, "replaygain", "album");

    if (mpv_initialize(m_mpv) < 0) {
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);

    mpv_set_wakeup_callback(m_mpv, &AudioEngine::onMpvWakeup, this);

    m_fadeInTimer = new QTimer(this);
    m_fadeInTimer->setInterval(50);
    connect(m_fadeInTimer, &QTimer::timeout, this, [this]() {
        if (m_crossfadeSec <= 0) {
            m_fadeInTimer->stop();
            applyMpvVolume(m_userVolume);
            return;
        }
        m_fadeInElapsed += 0.05;
        const double fadeDur = qMax(0.5, m_crossfadeSec * 0.5);
        const double ratio = qBound(0.0, m_fadeInElapsed / fadeDur, 1.0);
        applyMpvVolume(m_userVolume * ratio);
        if (ratio >= 1.0) {
            m_fadeInTimer->stop();
        }
    });
}

AudioEngine::~AudioEngine() {
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void AudioEngine::onMpvWakeup(void* ctx) {
    auto* self = static_cast<AudioEngine*>(ctx);
    QMetaObject::invokeMethod(self, &AudioEngine::processMpvEvents, Qt::QueuedConnection);
}

void AudioEngine::applyMpvVolume(double vol) {
    if (!m_mpv) return;
    double v = qBound(0.0, vol, 100.0);
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &v);
}

void AudioEngine::playFile(const QString& path) {
    if (!m_mpv) return;
    m_durationSec = 0.0;
    m_crossfadeTriggered = false;
    m_fadeInTimer->stop();

    if (m_crossfadeSec > 0) {
        m_fadeInElapsed = 0.0;
        applyMpvVolume(0.0);
        m_fadeInTimer->start();
    } else {
        applyMpvVolume(m_userVolume);
    }

    const QByteArray p = path.toUtf8();
    const char* cmd[] = {"loadfile", p.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
    int flag = 0;
    mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
    m_state = State::Playing;
    emit stateChanged(m_state);
}

void AudioEngine::togglePause() {
    setPaused(m_state == State::Playing);
}

void AudioEngine::setPaused(bool paused) {
    if (!m_mpv) return;
    int flag = paused ? 1 : 0;
    mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
}

void AudioEngine::stop() {
    if (!m_mpv) return;
    m_fadeInTimer->stop();
    m_crossfadeTriggered = false;
    applyMpvVolume(m_userVolume);
    const char* cmd[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
    m_state = State::Stopped;
    emit stateChanged(m_state);
}

void AudioEngine::seekSeconds(double pos) {
    if (!m_mpv) return;
    m_crossfadeTriggered = false;
    const QByteArray s = QByteArray::number(pos, 'f', 2);
    const char* cmd[] = {"seek", s.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void AudioEngine::setVolume(int vol0to100) {
    m_userVolume = qBound(0, vol0to100, 100);
    if (!m_fadeInTimer->isActive()) {
        applyMpvVolume(m_userVolume);
    }
}

void AudioEngine::setReplayGain(ReplayGainMode mode) {
    m_replayGain = mode;
    if (!m_mpv) return;
    const char* val = "album";
    switch (mode) {
    case ReplayGainMode::Off: val = "no"; break;
    case ReplayGainMode::Track: val = "track"; break;
    case ReplayGainMode::Album: val = "album"; break;
    }
    mpv_set_property_string(m_mpv, "replaygain", val);
}

void AudioEngine::setCrossfadeSeconds(int seconds) {
    m_crossfadeSec = qBound(0, seconds, 12);
    if (m_crossfadeSec == 0) {
        m_fadeInTimer->stop();
        applyMpvVolume(m_userVolume);
    }
}

void AudioEngine::processMpvEvents() {
    if (!m_mpv) return;
    while (true) {
        mpv_event* ev = mpv_wait_event(m_mpv, 0);
        if (!ev || ev->event_id == MPV_EVENT_NONE) break;

        switch (ev->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto* prop = static_cast<mpv_event_property*>(ev->data);
            if (!prop || !prop->data) break;
            if (qstrcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                const double pos = *static_cast<double*>(prop->data);
                emit positionChanged(pos);

                // Crossfade fade-out near end of track
                if (m_crossfadeSec > 0 && m_state == State::Playing &&
                    m_durationSec > (m_crossfadeSec * 2.0 + 2.0) && !m_crossfadeTriggered) {
                    const double fadeOut = qMax(0.5, m_crossfadeSec * 0.5);
                    const double remaining = m_durationSec - pos;
                    if (remaining <= fadeOut && remaining > 0.15) {
                        m_fadeInTimer->stop();
                        applyMpvVolume(m_userVolume * qBound(0.0, remaining / fadeOut, 1.0));
                    } else if (remaining <= 0.15) {
                        m_crossfadeTriggered = true;
                        emit trackFinished();
                    }
                }
            } else if (qstrcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                m_durationSec = *static_cast<double*>(prop->data);
                emit durationChanged(m_durationSec);
            } else if (qstrcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
                const bool paused = (*static_cast<int*>(prop->data)) != 0;
                if (m_state != State::Stopped) {
                    m_state = paused ? State::Paused : State::Playing;
                    emit stateChanged(m_state);
                }
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            auto* ef = static_cast<mpv_event_end_file*>(ev->data);
            if (ef && ef->reason == MPV_END_FILE_REASON_EOF) {
                if (!m_crossfadeTriggered) {
                    emit trackFinished();
                }
            } else if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                emit errorOccurred(QString("Playback error (mpv code %1)").arg(ef->error));
            }
            break;
        }
        default:
            break;
        }
    }
}

} // namespace honeycomb

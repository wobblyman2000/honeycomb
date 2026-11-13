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

void AudioEngine::playFile(const QString& path) {
    if (!m_mpv) return;
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
    const char* cmd[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
    m_state = State::Stopped;
    emit stateChanged(m_state);
}

void AudioEngine::seekSeconds(double pos) {
    if (!m_mpv) return;
    const QByteArray s = QByteArray::number(pos, 'f', 2);
    const char* cmd[] = {"seek", s.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}

void AudioEngine::setVolume(int vol0to100) {
    if (!m_mpv) return;
    double v = qBound(0, vol0to100, 100);
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &v);
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
                emit positionChanged(*static_cast<double*>(prop->data));
            } else if (qstrcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                emit durationChanged(*static_cast<double*>(prop->data));
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
                emit trackFinished();
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

#pragma once

#include "audio/AudioEngine.h"
#include "core/Track.h"
#include "ui/ArtCache.h"

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace honeycomb {

class MiniPlayerWindow : public QWidget {
    Q_OBJECT
public:
    explicit MiniPlayerWindow(ArtCache* artCache, QWidget* parent = nullptr);

    void setTrack(const Track& track);
    void setPlaybackState(AudioEngine::State state);
    void setPositionSeconds(double posSec, double durSec);

signals:
    void playPauseRequested();
    void nextRequested();
    void prevRequested();
    void expandRequested();

private:
    ArtCache* m_artCache = nullptr;
    QLabel* m_artLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_artistLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_playPauseBtn = nullptr;
    QPushButton* m_pinBtn = nullptr;
};

} // namespace honeycomb

#pragma once

#include "audio/AudioEngine.h"
#include "core/Track.h"
#include "ui/ArtCache.h"
#include "ui/LyricsReader.h"

#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

namespace honeycomb {

class TheaterWindow : public QWidget {
    Q_OBJECT
public:
    explicit TheaterWindow(ArtCache* artCache, QWidget* parent = nullptr);

    void setTrack(const Track& track, const LyricsData& lyrics);
    void setPlaybackState(AudioEngine::State state);
    void setPositionSeconds(double posSec, double durSec);

signals:
    void playPauseRequested();
    void nextRequested();
    void prevRequested();
    void seekRequested(double posSec);
    void exitRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateActiveLyric(int64_t posMs);

    ArtCache* m_artCache = nullptr;
    QPixmap m_blurredBg;

    QLabel* m_artLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_artistLabel = nullptr;
    QLabel* m_albumLabel = nullptr;
    QLabel* m_formatLabel = nullptr;
    QListWidget* m_lyricsList = nullptr;

    QPushButton* m_playPauseBtn = nullptr;
    QSlider* m_seekSlider = nullptr;
    QLabel* m_timeLabel = nullptr;

    LyricsData m_lyrics;
    int m_activeLyricRow = -1;
    double m_durationSec = 0.0;
    bool m_userSeeking = false;
};

} // namespace honeycomb

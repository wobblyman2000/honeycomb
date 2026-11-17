#include "MiniPlayerWindow.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace honeycomb {

MiniPlayerWindow::MiniPlayerWindow(ArtCache* artCache, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint), m_artCache(artCache) {
    setWindowTitle("Honeycomb Mini");
    setFixedSize(410, 92);
    setStyleSheet("QWidget { background: #14151a; color: #e8eaf0; }");

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_artLabel = new QLabel(this);
    m_artLabel->setFixedSize(68, 68);
    m_artLabel->setScaledContents(true);
    m_artLabel->setPixmap(m_artCache->getArtwork("", 68));
    root->addWidget(m_artLabel);

    auto* mid = new QVBoxLayout();
    mid->setSpacing(3);
    m_titleLabel = new QLabel("Not Playing", this);
    m_titleLabel->setStyleSheet("font-weight: 700; font-size: 13px;");
    m_artistLabel = new QLabel("", this);
    m_artistLabel->setStyleSheet("color: #9aa0b0; font-size: 11px;");

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4);
    m_progress->setStyleSheet(
        "QProgressBar { background: #262833; border: none; border-radius: 2px; } "
        "QProgressBar::chunk { background: #f5a623; border-radius: 2px; }");

    mid->addStretch();
    mid->addWidget(m_titleLabel);
    mid->addWidget(m_artistLabel);
    mid->addSpacing(4);
    mid->addWidget(m_progress);
    mid->addStretch();
    root->addLayout(mid, 1);

    auto* prevBtn = new QPushButton("⏮", this);
    prevBtn->setFixedSize(28, 28);
    m_playPauseBtn = new QPushButton("▶", this);
    m_playPauseBtn->setFlat(true);
    m_playPauseBtn->setFixedSize(32, 32);
    m_playPauseBtn->setStyleSheet("QPushButton { background: transparent; border: none; color: #f5a623; font-size: 18px; font-weight: bold; } QPushButton:hover { color: #ffd27d; }");
    auto* nextBtn = new QPushButton("⏭", this);
    nextBtn->setFixedSize(28, 28);

    m_pinBtn = new QPushButton("📌", this);
    m_pinBtn->setFixedSize(26, 26);
    m_pinBtn->setCheckable(true);
    m_pinBtn->setChecked(true);
    m_pinBtn->setToolTip("Always on Top");

    auto* expandBtn = new QPushButton("⤢", this);
    expandBtn->setFixedSize(26, 26);
    expandBtn->setToolTip("Return to Full Player");

    root->addWidget(prevBtn);
    root->addWidget(m_playPauseBtn);
    root->addWidget(nextBtn);
    root->addWidget(m_pinBtn);
    root->addWidget(expandBtn);

    connect(prevBtn, &QPushButton::clicked, this, &MiniPlayerWindow::prevRequested);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &MiniPlayerWindow::playPauseRequested);
    connect(nextBtn, &QPushButton::clicked, this, &MiniPlayerWindow::nextRequested);
    connect(expandBtn, &QPushButton::clicked, this, &MiniPlayerWindow::expandRequested);
    connect(m_pinBtn, &QPushButton::toggled, this, [this](bool onTop) {
        setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
        show();
    });
}

void MiniPlayerWindow::setTrack(const Track& track) {
    m_artLabel->setPixmap(m_artCache->getFullArtwork(track.artHash, 68));
    m_titleLabel->setText(track.title);
    m_artistLabel->setText(track.artist);
}

void MiniPlayerWindow::setPlaybackState(AudioEngine::State state) {
    m_playPauseBtn->setText(state == AudioEngine::State::Playing ? "⏸" : "▶");
}

void MiniPlayerWindow::setPositionSeconds(double posSec, double durSec) {
    if (durSec > 0.0) {
        m_progress->setValue(static_cast<int>((posSec / durSec) * 1000.0));
    } else {
        m_progress->setValue(0);
    }
}

} // namespace honeycomb

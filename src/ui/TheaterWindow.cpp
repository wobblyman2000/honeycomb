#include "TheaterWindow.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace honeycomb {

namespace {

QString formatSec(double secs) {
    if (secs < 0) secs = 0;
    const int s = static_cast<int>(secs);
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QPixmap makeBlurredBackdrop(const QPixmap& src) {
    if (src.isNull()) return {};
    // Fast, smooth downscale + upscale creates a rich atmospheric blur
    const QImage small = src.toImage().scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(small.scaled(640, 640, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

QPixmap roundCover(const QPixmap& src, int size) {
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, 16, 16);
    p.setClipPath(path);
    p.drawPixmap(0, 0, size, size, src);
    p.end();
    return out;
}

} // namespace

TheaterWindow::TheaterWindow(ArtCache* artCache, QWidget* parent)
    : QWidget(parent, Qt::Window), m_artCache(artCache) {
    setWindowTitle("Honeycomb — Theater Mode");
    resize(1280, 760);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(42, 28, 42, 28);
    root->setSpacing(20);

    // Top bar
    auto* topBar = new QHBoxLayout();
    auto* badge = new QLabel("THEATER MODE", this);
    badge->setStyleSheet("font-size: 11px; font-weight: 800; letter-spacing: 2px; color: #f5a623;");
    auto* exitBtn = new QPushButton("Exit Theater (Esc)", this);
    exitBtn->setStyleSheet("background: rgba(24,25,32,0.75); border: 1px solid #3c3f52; padding: 6px 14px;");
    topBar->addWidget(badge);
    topBar->addStretch();
    topBar->addWidget(exitBtn);
    root->addLayout(topBar);

    // Center content: Left (Art + Track Info) | Right (Lyrics)
    auto* center = new QHBoxLayout();
    center->setSpacing(48);

    auto* leftCol = new QVBoxLayout();
    leftCol->addStretch();
    m_artLabel = new QLabel(this);
    m_artLabel->setFixedSize(400, 400);
    m_artLabel->setAlignment(Qt::AlignCenter);
    m_artLabel->setPixmap(roundCover(m_artCache->getFullArtwork("", 400), 400));

    m_titleLabel = new QLabel("Not Playing", this);
    m_titleLabel->setStyleSheet("font-size: 26px; font-weight: 800; color: #ffffff;");
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setMaximumWidth(420);

    m_artistLabel = new QLabel("", this);
    m_artistLabel->setStyleSheet("font-size: 18px; color: #d6dae6;");
    m_artistLabel->setWordWrap(true);

    m_albumLabel = new QLabel("", this);
    m_albumLabel->setStyleSheet("font-size: 14px; color: #9aa0b0;");

    m_formatLabel = new QLabel("", this);
    m_formatLabel->setStyleSheet("font-size: 12px; font-weight: 700; color: #f5a623;");

    leftCol->addWidget(m_artLabel, 0, Qt::AlignLeft);
    leftCol->addSpacing(18);
    leftCol->addWidget(m_titleLabel);
    leftCol->addWidget(m_artistLabel);
    leftCol->addWidget(m_albumLabel);
    leftCol->addWidget(m_formatLabel);
    leftCol->addStretch();
    center->addLayout(leftCol, 0);

    m_lyricsList = new QListWidget(this);
    m_lyricsList->setStyleSheet(
        "QListWidget { background: rgba(16, 17, 22, 0.45); border: 1px solid rgba(255,255,255,0.08); "
        "border-radius: 12px; padding: 18px; font-size: 17px; } "
        "QListWidget::item { padding: 6px 8px; }");
    m_lyricsList->setWordWrap(true);
    m_lyricsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_lyricsList->setFocusPolicy(Qt::NoFocus);
    center->addWidget(m_lyricsList, 1);

    root->addLayout(center, 1);

    // Bottom transport bar
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(12);
    auto* prevBtn = new QPushButton("⏮", this);
    m_playPauseBtn = new QPushButton("▶", this);
    m_playPauseBtn->setFlat(true);
    m_playPauseBtn->setFixedSize(44, 40);
    m_playPauseBtn->setStyleSheet("QPushButton { background: transparent; border: none; color: #f5a623; font-size: 24px; font-weight: bold; } QPushButton:hover { color: #ffd27d; }");
    auto* nextBtn = new QPushButton("⏭", this);

    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setRange(0, 1000);
    m_timeLabel = new QLabel("0:00 / 0:00", this);
    m_timeLabel->setMinimumWidth(95);

    bottom->addWidget(prevBtn);
    bottom->addWidget(m_playPauseBtn);
    bottom->addWidget(nextBtn);
    bottom->addWidget(m_seekSlider, 1);
    bottom->addWidget(m_timeLabel);
    root->addLayout(bottom);

    connect(exitBtn, &QPushButton::clicked, this, &TheaterWindow::exitRequested);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &TheaterWindow::playPauseRequested);
    connect(prevBtn, &QPushButton::clicked, this, &TheaterWindow::prevRequested);
    connect(nextBtn, &QPushButton::clicked, this, &TheaterWindow::nextRequested);
    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_userSeeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_userSeeking = false;
        if (m_durationSec > 0.0) {
            emit seekRequested((m_seekSlider->value() / 1000.0) * m_durationSec);
        }
    });
}

void TheaterWindow::setTrack(const Track& track, const LyricsData& lyrics) {
    const QPixmap full = m_artCache->getFullArtwork(track.artHash, 400);
    m_artLabel->setPixmap(roundCover(full, 400));
    m_blurredBg = track.artHash.isEmpty() ? QPixmap() : makeBlurredBackdrop(full);

    m_titleLabel->setText(track.title);
    m_artistLabel->setText(track.artist);
    m_albumLabel->setText(track.year > 0 ? QString("%1 (%2)").arg(track.album).arg(track.year) : track.album);
    m_formatLabel->setText(QString("%1  •  %2 kbps").arg(track.format).arg(track.bitrate));

    m_lyrics = lyrics;
    m_activeLyricRow = -1;
    m_lyricsList->clear();

    if (m_lyrics.lines.isEmpty()) {
        auto* item = new QListWidgetItem("No lyrics found for this track.\n\nTip: Place a .lrc file next to the audio file or embed a LYRICS tag.");
        item->setForeground(QColor("#8c92a4"));
        item->setTextAlignment(Qt::AlignCenter);
        m_lyricsList->addItem(item);
    } else {
        for (const auto& l : m_lyrics.lines) {
            auto* item = new QListWidgetItem(l.text.isEmpty() ? " " : l.text);
            item->setForeground(m_lyrics.isSynced ? QColor("#8c92a4") : QColor("#e8eaf0"));
            item->setTextAlignment(Qt::AlignCenter);
            m_lyricsList->addItem(item);
        }
    }
    update();
}

void TheaterWindow::setPlaybackState(AudioEngine::State state) {
    m_playPauseBtn->setText(state == AudioEngine::State::Playing ? "⏸" : "▶");
}

void TheaterWindow::setPositionSeconds(double posSec, double durSec) {
    m_durationSec = durSec;
    if (!m_userSeeking && durSec > 0.0) {
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(static_cast<int>((posSec / durSec) * 1000.0));
        m_seekSlider->blockSignals(false);
    }
    m_timeLabel->setText(QString("%1 / %2").arg(formatSec(posSec), formatSec(durSec)));
    updateActiveLyric(static_cast<int64_t>(posSec * 1000.0));
}

void TheaterWindow::updateActiveLyric(int64_t posMs) {
    if (!m_lyrics.isSynced || m_lyrics.lines.isEmpty()) return;

    int active = -1;
    for (int i = 0; i < m_lyrics.lines.size(); ++i) {
        if (m_lyrics.lines[i].timeMs <= posMs) active = i;
        else break;
    }
    if (active == m_activeLyricRow) return;

    if (m_activeLyricRow >= 0 && m_activeLyricRow < m_lyricsList->count()) {
        auto* prev = m_lyricsList->item(m_activeLyricRow);
        prev->setForeground(QColor("#8c92a4"));
        QFont f = prev->font();
        f.setBold(false);
        prev->setFont(f);
    }
    m_activeLyricRow = active;
    if (m_activeLyricRow >= 0 && m_activeLyricRow < m_lyricsList->count()) {
        auto* cur = m_lyricsList->item(m_activeLyricRow);
        cur->setForeground(QColor("#f5a623"));
        QFont f = cur->font();
        f.setBold(true);
        cur->setFont(f);
        m_lyricsList->scrollToItem(cur, QAbstractItemView::PositionAtCenter);
    }
}

void TheaterWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#101116"));
    if (!m_blurredBg.isNull()) {
        p.drawPixmap(rect(), m_blurredBg);
        p.fillRect(rect(), QColor(12, 13, 18, 188));
    }
}

void TheaterWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        emit exitRequested();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        emit playPauseRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace honeycomb

#include "ui/WaveformWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <algorithm>

namespace honeycomb {

namespace {
constexpr int kBarCount = 120;
}

WaveformWidget::WaveformWidget(QWidget* parent) : QWidget(parent) {
    setFixedHeight(44);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click or drag to seek");
}

WaveformWidget::~WaveformWidget() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(300);
    }
}

void WaveformWidget::setTrack(const QString& filePath, double durationSec) {
    m_trackPath = filePath;
    m_durationSec = qMax(0.0, durationSec);
    m_positionSec = 0.0;

    if (filePath.isEmpty()) {
        m_peaks.clear();
        update();
        return;
    }

    auto it = m_cache.constFind(filePath);
    if (it != m_cache.constEnd()) {
        m_peaks = it.value();
        update();
        return;
    }

    m_peaks.clear();
    update();
    startExtraction(filePath);
}

void WaveformWidget::setPositionSeconds(double seconds) {
    if (m_dragging) return;
    m_positionSec = qMax(0.0, seconds);
    update();
}

void WaveformWidget::setDurationSeconds(double durationSec) {
    m_durationSec = qMax(0.0, durationSec);
    update();
}

void WaveformWidget::startExtraction(const QString& filePath) {
    if (m_proc) {
        m_proc->disconnect();
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(200);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    m_pcmBuffer.clear();
    m_extractingPath = filePath;

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_proc) m_pcmBuffer.append(m_proc->readAllStandardOutput());
    });
    connect(m_proc, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_proc) {
            m_pcmBuffer.append(m_proc->readAllStandardOutput());
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        if (status != QProcess::NormalExit || exitCode != 0 || m_pcmBuffer.size() < 2) {
            m_pcmBuffer.clear();
            return;
        }

        const int totalSamples = m_pcmBuffer.size() / 2;
        const auto* samples = reinterpret_cast<const qint16*>(m_pcmBuffer.constData());
        QVector<float> peaks(kBarCount, 0.08f);
        const int chunkSize = qMax(1, totalSamples / kBarCount);

        float globalMax = 1.0f;
        for (int b = 0; b < kBarCount; ++b) {
            const int start = b * chunkSize;
            const int end = (b == kBarCount - 1) ? totalSamples : qMin(totalSamples, start + chunkSize);
            int maxAbs = 0;
            for (int i = start; i < end; ++i) {
                const int v = qAbs(static_cast<int>(samples[i]));
                if (v > maxAbs) maxAbs = v;
            }
            peaks[b] = static_cast<float>(maxAbs);
            if (peaks[b] > globalMax) globalMax = peaks[b];
        }

        for (int b = 0; b < kBarCount; ++b) {
            peaks[b] = qBound(0.08f, peaks[b] / globalMax, 1.0f);
        }

        m_cache.insert(m_extractingPath, peaks);
        if (m_trackPath == m_extractingPath) {
            m_peaks = peaks;
            update();
        }
        m_pcmBuffer.clear();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_pcmBuffer.clear();
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
    });

    m_proc->start("ffmpeg", {
        "-v", "error",
        "-i", filePath,
        "-vn",
        "-ac", "1",
        "-ar", "1000",
        "-f", "s16le",
        "-"
    });
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();
    p.fillRect(r, QColor("#161820"));
    p.setPen(QColor("#282c3a"));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    const double frac = (m_durationSec > 0.0) ? qBound(0.0, m_positionSec / m_durationSec, 1.0) : 0.0;
    const int playX = static_cast<int>(frac * r.width());

    if (m_peaks.isEmpty()) {
        const int cy = r.center().y();
        p.setPen(QColor("#383d4d"));
        p.drawLine(4, cy, r.width() - 5, cy);
        if (frac > 0.0) {
            p.setPen(QColor("#f5a623"));
            p.drawLine(4, cy, qMax(4, playX), cy);
        }
        return;
    }

    const int marginX = 4;
    const int availW = qMax(1, r.width() - 2 * marginX);
    const float step = static_cast<float>(availW) / m_peaks.size();
    const int barW = qMax(1, static_cast<int>(step * 0.68f));
    const int midY = r.center().y();
    const int maxHalfH = qMax(2, (r.height() - 10) / 2);

    for (int i = 0; i < m_peaks.size(); ++i) {
        const int x = marginX + static_cast<int>(i * step);
        const int halfH = qMax(2, static_cast<int>(m_peaks[i] * maxHalfH));
        const bool played = (x + barW / 2) <= playX;
        p.fillRect(x, midY - halfH, barW, halfH * 2,
                   played ? QColor("#f5a623") : QColor("#4d5366"));
    }

    if (m_durationSec > 0.0) {
        p.setPen(QColor("#ffd27d"));
        p.drawLine(playX, 2, playX, r.height() - 3);
    }
}

void WaveformWidget::emitSeekAtX(int x) {
    if (m_durationSec <= 0.0 || width() <= 0) return;
    const double frac = qBound(0.0, static_cast<double>(x) / width(), 1.0);
    m_positionSec = frac * m_durationSec;
    update();
    emit seekRequested(m_positionSec);
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_durationSec > 0.0) {
        m_dragging = true;
        emitSeekAtX(event->pos().x());
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        emitSeekAtX(event->pos().x());
    }
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        emitSeekAtX(event->pos().x());
    }
}

} // namespace honeycomb

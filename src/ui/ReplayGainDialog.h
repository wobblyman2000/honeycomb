#pragma once

#include "core/Track.h"

#include <QDialog>
#include <QProcess>
#include <QVector>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace honeycomb {

class ReplayGainDialog : public QDialog {
    Q_OBJECT
public:
    explicit ReplayGainDialog(const QVector<Track>& tracks, QWidget* parent = nullptr);
    ~ReplayGainDialog() override;

private:
    struct RgResult {
        bool ok = false;
        double lufs = -18.0;
        double trackGainDb = 0.0;
        double trackPeakLinear = 1.0;
    };

    void analyzeNext();
    void writeTags();

    QVector<Track> m_tracks;
    QVector<RgResult> m_results;
    int m_currentIndex = 0;
    bool m_canceled = false;

    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_writeBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QProcess* m_proc = nullptr;
    QByteArray m_stderrBuf;
};

} // namespace honeycomb

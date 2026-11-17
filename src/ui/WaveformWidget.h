#pragma once

#include <QHash>
#include <QProcess>
#include <QString>
#include <QVector>
#include <QWidget>

namespace honeycomb {

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);
    ~WaveformWidget() override;

    void setTrack(const QString& filePath, double durationSec);
    void setPositionSeconds(double seconds);
    void setDurationSeconds(double durationSec);

signals:
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void startExtraction(const QString& filePath);
    void emitSeekAtX(int x);

    QString m_trackPath;
    double m_durationSec = 0.0;
    double m_positionSec = 0.0;
    bool m_dragging = false;

    QVector<float> m_peaks;
    QHash<QString, QVector<float>> m_cache;
    QProcess* m_proc = nullptr;
    QByteArray m_pcmBuffer;
    QString m_extractingPath;
};

} // namespace honeycomb

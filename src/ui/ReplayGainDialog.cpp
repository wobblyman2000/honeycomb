#include "ui/ReplayGainDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

namespace honeycomb {

ReplayGainDialog::ReplayGainDialog(const QVector<Track>& tracks, QWidget* parent)
    : QDialog(parent), m_tracks(tracks) {
    setWindowTitle("Analyze Volume (ReplayGain Scanner)");
    resize(740, 460);

    m_results.resize(m_tracks.size());

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    m_statusLabel = new QLabel(QString("Analyzing %1 track(s) with EBU R128...").arg(m_tracks.size()), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, qMax(1, m_tracks.size()));
    m_progress->setValue(0);

    m_table = new QTableWidget(m_tracks.size(), 4, this);
    m_table->setHorizontalHeaderLabels({"Track", "Loudness (LUFS)", "Track Gain", "Peak"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    for (int i = 0; i < m_tracks.size(); ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(QString("%1 — %2").arg(m_tracks[i].artist, m_tracks[i].title)));
        m_table->setItem(i, 1, new QTableWidgetItem("Pending"));
        m_table->setItem(i, 2, new QTableWidgetItem("—"));
        m_table->setItem(i, 3, new QTableWidgetItem("—"));
    }

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_writeBtn = new QPushButton("Write ReplayGain Tags", this);
    m_writeBtn->setEnabled(false);
    m_closeBtn = new QPushButton("Cancel", this);
    btnRow->addWidget(m_writeBtn);
    btnRow->addWidget(m_closeBtn);

    root->addWidget(m_statusLabel);
    root->addWidget(m_progress);
    root->addWidget(m_table, 1);
    root->addLayout(btnRow);

    connect(m_writeBtn, &QPushButton::clicked, this, &ReplayGainDialog::writeTags);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_canceled = true;
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
        }
        reject();
    });

    analyzeNext();
}

ReplayGainDialog::~ReplayGainDialog() {
    m_canceled = true;
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(300);
    }
}

void ReplayGainDialog::analyzeNext() {
    if (m_canceled) return;
    if (m_currentIndex >= m_tracks.size()) {
        int okCount = 0;
        double sumLufs = 0.0;
        for (const auto& r : m_results) {
            if (r.ok) {
                ++okCount;
                sumLufs += r.lufs;
            }
        }
        const double albumGain = okCount > 0 ? (-18.0 - (sumLufs / okCount)) : 0.0;
        m_statusLabel->setText(QString("Done! Analyzed %1/%2 tracks. Calculated Album Gain: %3 dB.")
                                   .arg(okCount)
                                   .arg(m_tracks.size())
                                   .arg(albumGain, 0, 'f', 2));
        m_writeBtn->setEnabled(okCount > 0);
        m_closeBtn->setText("Close");
        return;
    }

    const int idx = m_currentIndex;
    const Track& t = m_tracks[idx];
    m_statusLabel->setText(QString("Analyzing (%1/%2): %3").arg(idx + 1).arg(m_tracks.size()).arg(t.title));
    m_table->setItem(idx, 1, new QTableWidgetItem("Scanning..."));

    m_stderrBuf.clear();
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        if (m_proc) m_stderrBuf.append(m_proc->readAllStandardError());
    });
    connect(m_proc, &QProcess::finished, this, [this, idx](int, QProcess::ExitStatus) {
        if (m_proc) {
            m_stderrBuf.append(m_proc->readAllStandardError());
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        if (m_canceled) return;

        const QString text = QString::fromUtf8(m_stderrBuf);
        static const QRegularExpression reI("I:\\s*(-?\\d+(?:\\.\\d+)?)\\s*LUFS");
        static const QRegularExpression rePeak("Peak:\\s*(-?\\d+(?:\\.\\d+)?)\\s*dBFS");

        QRegularExpressionMatch lastI;
        auto itI = reI.globalMatch(text);
        while (itI.hasNext()) lastI = itI.next();

        QRegularExpressionMatch lastP;
        auto itP = rePeak.globalMatch(text);
        while (itP.hasNext()) lastP = itP.next();

        if (lastI.hasMatch()) {
            RgResult res;
            res.ok = true;
            res.lufs = lastI.captured(1).toDouble();
            res.trackGainDb = -18.0 - res.lufs;
            const double peakDb = lastP.hasMatch() ? lastP.captured(1).toDouble() : 0.0;
            res.trackPeakLinear = qPow(10.0, peakDb / 20.0);
            m_results[idx] = res;

            m_table->setItem(idx, 1, new QTableWidgetItem(QString("%1 LUFS").arg(res.lufs, 0, 'f', 1)));
            m_table->setItem(idx, 2, new QTableWidgetItem(QString("%1%2 dB")
                                                              .arg(res.trackGainDb >= 0 ? "+" : "")
                                                              .arg(res.trackGainDb, 0, 'f', 2)));
            m_table->setItem(idx, 3, new QTableWidgetItem(QString::number(res.trackPeakLinear, 'f', 4)));
        } else {
            m_table->setItem(idx, 1, new QTableWidgetItem("Failed"));
        }

        m_progress->setValue(idx + 1);
        ++m_currentIndex;
        analyzeNext();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this, idx](QProcess::ProcessError) {
        m_table->setItem(idx, 1, new QTableWidgetItem("ffmpeg error"));
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        m_progress->setValue(idx + 1);
        ++m_currentIndex;
        analyzeNext();
    });

    m_proc->start("ffmpeg", {
        "-nostats",
        "-i", t.path,
        "-filter_complex", "ebur128=peak=true",
        "-f", "null",
        "-"
    });
}

void ReplayGainDialog::writeTags() {
    int okCount = 0;
    double sumLufs = 0.0;
    double albumPeak = 0.0;
    for (const auto& r : m_results) {
        if (r.ok) {
            ++okCount;
            sumLufs += r.lufs;
            if (r.trackPeakLinear > albumPeak) albumPeak = r.trackPeakLinear;
        }
    }
    if (okCount == 0) return;

    const double albumGain = -18.0 - (sumLufs / okCount);
    const QString albGainStr = QString("%1%2 dB").arg(albumGain >= 0 ? "+" : "").arg(albumGain, 0, 'f', 2);
    const QString albPeakStr = QString::number(albumPeak, 'f', 6);

    int written = 0;
    int failed = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (!m_results[i].ok) continue;
        const QByteArray pathUtf8 = m_tracks[i].path.toUtf8();
        TagLib::FileRef ref(pathUtf8.constData());
        if (ref.isNull() || !ref.file()) {
            ++failed;
            continue;
        }
        const QString trkGainStr = QString("%1%2 dB")
                                       .arg(m_results[i].trackGainDb >= 0 ? "+" : "")
                                       .arg(m_results[i].trackGainDb, 0, 'f', 2);
        const QString trkPeakStr = QString::number(m_results[i].trackPeakLinear, 'f', 6);

        TagLib::PropertyMap props = ref.file()->properties();
        props.replace("REPLAYGAIN_TRACK_GAIN", TagLib::StringList(TagLib::String(trkGainStr.toStdWString())));
        props.replace("REPLAYGAIN_TRACK_PEAK", TagLib::StringList(TagLib::String(trkPeakStr.toStdWString())));
        if (m_tracks.size() > 1) {
            props.replace("REPLAYGAIN_ALBUM_GAIN", TagLib::StringList(TagLib::String(albGainStr.toStdWString())));
            props.replace("REPLAYGAIN_ALBUM_PEAK", TagLib::StringList(TagLib::String(albPeakStr.toStdWString())));
        }
        ref.file()->setProperties(props);
        if (ref.save()) ++written;
        else ++failed;
    }

    QMessageBox::information(this, "ReplayGain Written",
                             QString("Wrote ReplayGain tags to %1 file(s)%2.")
                                 .arg(written)
                                 .arg(failed > 0 ? QString(" (%1 failed)").arg(failed) : QString()));
    accept();
}

} // namespace honeycomb

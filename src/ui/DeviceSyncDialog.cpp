#include "ui/DeviceSyncDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QVBoxLayout>

namespace honeycomb {

DeviceSyncDialog::DeviceSyncDialog(Database* db,
                                   const QVector<Collection>& collections,
                                   const QVector<Database::Playlist>& playlists,
                                   QWidget* parent)
    : QDialog(parent), m_db(db), m_collections(collections), m_playlists(playlists) {
    setWindowTitle("Sync to USB / Portable Device");
    resize(640, 480);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto* form = new QFormLayout();

    m_sourceCombo = new QComboBox(this);
    for (const auto& c : m_collections) {
        m_sourceCombo->addItem("Collection: " + c.name, QString("col:") + c.id);
    }
    for (const auto& pl : m_playlists) {
        m_sourceCombo->addItem(QString("Playlist: %1 (%2)").arg(pl.name).arg(pl.trackCount), QString("pl:") + pl.id);
    }

    auto* destRow = new QHBoxLayout();
    m_destEdit = new QLineEdit(this);
    m_destEdit->setPlaceholderText("/run/media/... or mounted USB / SD / DAP folder");
    auto* browseBtn = new QPushButton("Browse...", this);
    destRow->addWidget(m_destEdit, 1);
    destRow->addWidget(browseBtn);

    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem("Copy Original Files (Lossless / No Transcode)", "copy");
    m_formatCombo->addItem("Transcode to MP3 (320 kbps)", "mp3_320");
    m_formatCombo->addItem("Transcode to MP3 (192 kbps)", "mp3_192");
    m_formatCombo->addItem("Transcode to Opus (160 kbps)", "opus_160");
    m_formatCombo->addItem("Transcode to AAC / M4A (256 kbps)", "aac_256");

    m_skipExistingChk = new QCheckBox("Skip tracks already present on device", this);
    m_skipExistingChk->setChecked(true);
    m_exportM3uChk = new QCheckBox("Write .m3u8 playlist file to device root", this);
    m_exportM3uChk->setChecked(true);

    form->addRow("Source:", m_sourceCombo);
    form->addRow("Device Folder:", destRow);
    form->addRow("Audio Format:", m_formatCombo);
    form->addRow("", m_skipExistingChk);
    form->addRow("", m_exportM3uChk);
    root->addLayout(form);

    m_statusLabel = new QLabel("Ready to sync.", this);
    m_progress = new QProgressBar(this);
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_startBtn = new QPushButton("Start Sync", this);
    m_closeBtn = new QPushButton("Close", this);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_closeBtn);

    root->addWidget(m_statusLabel);
    root->addWidget(m_progress);
    root->addWidget(m_log, 1);
    root->addLayout(btnRow);

    connect(browseBtn, &QPushButton::clicked, this, &DeviceSyncDialog::onBrowseDest);
    connect(m_startBtn, &QPushButton::clicked, this, &DeviceSyncDialog::onStartSync);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_canceled = true;
        if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill();
        reject();
    });
}

DeviceSyncDialog::~DeviceSyncDialog() {
    m_canceled = true;
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(300);
    }
}

QString DeviceSyncDialog::sanitizeName(const QString& s) {
    QString out = s.trimmed();
    static const QRegularExpression bad("[\\\\/:*?\"<>|]");
    out.replace(bad, "_");
    return out.isEmpty() ? QString("Unknown") : out;
}

void DeviceSyncDialog::onBrowseDest() {
    const QString startDir = QDir("/run/media").exists() ? QString("/run/media") : QDir::homePath();
    const QString dir = QFileDialog::getExistingDirectory(this, "Select USB / Portable Device Folder", startDir);
    if (!dir.isEmpty()) m_destEdit->setText(dir);
}

void DeviceSyncDialog::onStartSync() {
    const QString destRoot = m_destEdit->text().trimmed();
    if (destRoot.isEmpty() || !QDir(destRoot).exists()) {
        QMessageBox::warning(this, "Device Sync", "Please select a valid mounted destination folder.");
        return;
    }

    const QString key = m_sourceCombo->currentData().toString();
    m_tracks.clear();
    if (key.startsWith("pl:")) {
        m_tracks = m_db->queryPlaylistTracks(key.mid(3));
    } else if (key.startsWith("col:")) {
        const QString cid = key.mid(4);
        for (const auto& c : m_collections) {
            if (c.id == cid) {
                m_tracks = m_db->queryTracks(c.rules, QString(), SortOrder::AlbumArtistYear,
                                             QString(), QString(), c.limitCount, c.limitOrder);
                break;
            }
        }
    }

    if (m_tracks.isEmpty()) {
        QMessageBox::information(this, "Device Sync", "No tracks found in the selected source.");
        return;
    }

    m_index = 0;
    m_copied = 0;
    m_skipped = 0;
    m_failed = 0;
    m_canceled = false;
    m_syncedRelPaths.clear();
    m_log->clear();
    m_progress->setRange(0, m_tracks.size());
    m_progress->setValue(0);
    m_startBtn->setEnabled(false);
    m_closeBtn->setText("Cancel");

    syncNextTrack();
}

void DeviceSyncDialog::syncNextTrack() {
    if (m_canceled) {
        m_statusLabel->setText("Sync canceled.");
        m_startBtn->setEnabled(true);
        m_closeBtn->setText("Close");
        return;
    }
    if (m_index >= m_tracks.size()) {
        finishSync();
        return;
    }

    const Track& t = m_tracks[m_index];
    const QString destRoot = m_destEdit->text().trimmed();
    const QString fmt = m_formatCombo->currentData().toString();

    const QString artistDir = sanitizeName(!t.albumArtist.isEmpty() ? t.albumArtist : t.artist);
    const QString albumDir = sanitizeName(t.album);
    const QString srcExt = QFileInfo(t.path).suffix().toLower();
    QString outExt = srcExt;
    if (fmt.startsWith("mp3_")) outExt = "mp3";
    else if (fmt.startsWith("opus_")) outExt = "opus";
    else if (fmt.startsWith("aac_")) outExt = "m4a";

    const QString numPrefix = t.trackNumber > 0 ? QString("%1 - ").arg(t.trackNumber, 2, 10, QChar('0')) : "";
    const QString fileName = sanitizeName(numPrefix + t.title) + "." + outExt;
    const QString relPath = artistDir + "/" + albumDir + "/" + fileName;
    const QString fullDest = destRoot + "/" + relPath;

    QDir().mkpath(QFileInfo(fullDest).absolutePath());
    m_statusLabel->setText(QString("Syncing (%1/%2): %3").arg(m_index + 1).arg(m_tracks.size()).arg(t.title));

    if (m_skipExistingChk->isChecked() && QFileInfo::exists(fullDest)) {
        ++m_skipped;
        m_syncedRelPaths.append(relPath);
        m_log->appendPlainText("Skipped (exists): " + relPath);
        m_progress->setValue(++m_index);
        QMetaObject::invokeMethod(this, &DeviceSyncDialog::syncNextTrack, Qt::QueuedConnection);
        return;
    }

    if (fmt == "copy") {
        if (QFile::exists(fullDest)) QFile::remove(fullDest);
        if (QFile::copy(t.path, fullDest)) {
            ++m_copied;
            m_syncedRelPaths.append(relPath);
            m_log->appendPlainText("Copied: " + relPath);
        } else {
            ++m_failed;
            m_log->appendPlainText("FAILED to copy: " + t.path);
        }
        m_progress->setValue(++m_index);
        QMetaObject::invokeMethod(this, &DeviceSyncDialog::syncNextTrack, Qt::QueuedConnection);
        return;
    }

    QStringList args = {"-y", "-v", "error", "-i", t.path, "-vn", "-map_metadata", "0"};
    if (fmt == "mp3_320") args << "-c:a" << "libmp3lame" << "-b:a" << "320k";
    else if (fmt == "mp3_192") args << "-c:a" << "libmp3lame" << "-b:a" << "192k";
    else if (fmt == "opus_160") args << "-c:a" << "libopus" << "-b:a" << "160k";
    else if (fmt == "aac_256") args << "-c:a" << "aac" << "-b:a" << "256k";
    args << fullDest;

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, [this, relPath](int exitCode, QProcess::ExitStatus status) {
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        if (status == QProcess::NormalExit && exitCode == 0) {
            ++m_copied;
            m_syncedRelPaths.append(relPath);
            m_log->appendPlainText("Transcoded: " + relPath);
        } else {
            ++m_failed;
            m_log->appendPlainText("FAILED to transcode: " + relPath);
        }
        m_progress->setValue(++m_index);
        syncNextTrack();
    });
    m_proc->start("ffmpeg", args);
}

void DeviceSyncDialog::finishSync() {
    if (m_exportM3uChk->isChecked() && !m_syncedRelPaths.isEmpty()) {
        const QString plName = sanitizeName(m_sourceCombo->currentText()) + ".m3u8";
        const QString plPath = m_destEdit->text().trimmed() + "/" + plName;
        QFile f(plPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "#EXTM3U\n";
            for (const QString& rel : m_syncedRelPaths) {
                out << rel << "\n";
            }
            m_log->appendPlainText("Wrote playlist: " + plName);
        }
    }

    m_statusLabel->setText(QString("Sync complete — %1 synced, %2 skipped, %3 failed.")
                               .arg(m_copied)
                               .arg(m_skipped)
                               .arg(m_failed));
    m_startBtn->setEnabled(true);
    m_closeBtn->setText("Close");
}

} // namespace honeycomb

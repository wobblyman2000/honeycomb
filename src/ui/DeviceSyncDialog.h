#pragma once

#include "core/Collection.h"
#include "core/Track.h"
#include "db/Database.h"

#include <QDialog>
#include <QProcess>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace honeycomb {

class DeviceSyncDialog : public QDialog {
    Q_OBJECT
public:
    DeviceSyncDialog(Database* db,
                     const QVector<Collection>& collections,
                     const QVector<Database::Playlist>& playlists,
                     QWidget* parent = nullptr);
    ~DeviceSyncDialog() override;

private slots:
    void onBrowseDest();
    void onStartSync();

private:
    void syncNextTrack();
    void finishSync();
    static QString sanitizeName(const QString& s);

    Database* m_db = nullptr;
    QVector<Collection> m_collections;
    QVector<Database::Playlist> m_playlists;

    QComboBox* m_sourceCombo = nullptr;
    QLineEdit* m_destEdit = nullptr;
    QComboBox* m_formatCombo = nullptr;
    QCheckBox* m_skipExistingChk = nullptr;
    QCheckBox* m_exportM3uChk = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QVector<Track> m_tracks;
    QStringList m_syncedRelPaths;
    int m_index = 0;
    int m_copied = 0;
    int m_skipped = 0;
    int m_failed = 0;
    bool m_canceled = false;
    QProcess* m_proc = nullptr;
};

} // namespace honeycomb

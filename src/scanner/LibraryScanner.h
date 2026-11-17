#pragma once

#include "db/Database.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <atomic>

namespace honeycomb {

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(Database* db, const QString& artCacheDir, QObject* parent = nullptr);
    ~LibraryScanner() override;

    void startScan(const QStringList& folders, bool toInbox = false);
    void cancel();
    bool isScanning() const { return m_scanning.load(); }

    void setAutoWatchEnabled(bool enabled);
    bool isAutoWatchEnabled() const { return m_autoWatchEnabled; }
    void watchFolders(const QStringList& folders);

signals:
    void scanStarted();
    void scanProgress(int scannedCount, const QString& currentFile);
    void scanFinished(int totalTracksAddedOrUpdated);

private:
    void refreshWatcher();

    Database* m_db = nullptr;
    QString m_artCacheDir;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancel{false};

    bool m_autoWatchEnabled = true;
    QStringList m_watchedFolders;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_watchDebounceTimer = nullptr;
};

} // namespace honeycomb

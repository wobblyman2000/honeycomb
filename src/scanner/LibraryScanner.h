#pragma once

#include "db/Database.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

namespace honeycomb {

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(Database* db, const QString& artCacheDir, QObject* parent = nullptr);
    ~LibraryScanner() override;

    void startScan(const QStringList& folders);
    void cancel();
    bool isScanning() const { return m_scanning.load(); }

signals:
    void scanStarted();
    void scanProgress(int scannedCount, const QString& currentFile);
    void scanFinished(int totalTracksAddedOrUpdated);

private:
    Database* m_db = nullptr;
    QString m_artCacheDir;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancel{false};
};

} // namespace honeycomb

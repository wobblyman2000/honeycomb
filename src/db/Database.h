#pragma once

#include "core/Collection.h"
#include "core/Track.h"

#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>
#include <sqlite3.h>

namespace honeycomb {

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const QString& dbPath);
    void close();

    // Library Folders
    QStringList libraryFolders() const;
    bool addLibraryFolder(const QString& path);
    bool removeLibraryFolder(const QString& path);

    // Tracks
    bool upsertTracks(const QVector<Track>& tracks);
    int trackCount() const;
    void incrementPlayCount(const QString& path);
    void setTrackRating(int64_t trackId, int rating);

    // Queries
    QVector<Album> queryAlbums(const RuleGroup& rules,
                               const QString& searchText,
                               SortOrder sortOrder) const;

    QVector<Track> queryTracks(const RuleGroup& rules,
                               const QString& searchText,
                               SortOrder sortOrder,
                               const QString& albumFilter = QString(),
                               const QString& albumArtistFilter = QString(),
                               int limitCount = 0,
                               LimitOrder limitOrder = LimitOrder::Default) const;

    // Collections & Smart Playlists
    QVector<Collection> loadCollections() const;
    bool saveCollection(const Collection& col);
    bool deleteCollection(const QString& id);

private:
    bool initSchema();
    void seedDefaultCollections();
    QString buildSearchSql(const QString& searchText, QVariantList& bindValues) const;

    mutable QMutex m_mutex;
    sqlite3* m_db = nullptr;
};

} // namespace honeycomb

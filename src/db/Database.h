#pragma once

#include "core/Collection.h"
#include "core/Track.h"

#include <QMutex>
#include <QSet>
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
    bool updateTracksTags(const QVector<Track>& tracks);
    int pruneMissingTracks(const QSet<QString>& seenPaths,
                           const QStringList& configuredFolders,
                           const QStringList& scannedRoots);
    int trackCount() const;
    void incrementPlayCount(const QString& path);
    void setTrackRating(int64_t trackId, int rating);
    bool setAlbumType(const QString& album, const QString& albumArtist, bool isCompilation, const QString& albumType);
    QStringList distinctAlbumTypes() const;

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

    // Static Playlists
    struct Playlist {
        QString id;
        QString name;
        int trackCount = 0;
    };
    QVector<Playlist> loadPlaylists() const;
    bool createPlaylist(const QString& id, const QString& name);
    bool renamePlaylist(const QString& id, const QString& newName);
    bool deletePlaylist(const QString& id);
    bool addTracksToPlaylist(const QString& playlistId, const QStringList& trackPaths);
    bool removeTrackFromPlaylist(const QString& playlistId, int position);
    bool movePlaylistTrack(const QString& playlistId, int fromPos, int toPos);
    QVector<Track> queryPlaylistTracks(const QString& playlistId, const QString& searchText = QString()) const;

private:
    bool initSchema();
    void seedDefaultCollections();
    QString buildSearchSql(const QString& searchText, QVariantList& bindValues) const;

    mutable QMutex m_mutex;
    sqlite3* m_db = nullptr;
};

} // namespace honeycomb

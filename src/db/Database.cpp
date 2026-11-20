#include "Database.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace honeycomb {

namespace {

void bindVariantList(sqlite3_stmt* stmt, const QVariantList& values) {
    for (int i = 0; i < values.size(); ++i) {
        const int idx = i + 1;
        const QVariant& v = values[i];
        if (v.typeId() == QMetaType::LongLong || v.typeId() == QMetaType::Int) {
            sqlite3_bind_int64(stmt, idx, v.toLongLong());
        } else {
            const QByteArray utf8 = v.toString().toUtf8();
            sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
        }
    }
}

QString colText(sqlite3_stmt* stmt, int col) {
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? QString::fromUtf8(reinterpret_cast<const char*>(txt)) : QString();
}

QString albumTypeRankExpr(const QString& typeCol, const QString& compCol) {
    return QString(
        "CASE LOWER(COALESCE(NULLIF(%1, ''), CASE WHEN %2 = 1 THEN 'compilation' ELSE 'studio' END)) "
        "WHEN 'studio' THEN 1 "
        "WHEN 'album' THEN 1 "
        "WHEN 'live' THEN 2 "
        "WHEN 'live album' THEN 2 "
        "WHEN 'ep' THEN 3 "
        "WHEN 'single' THEN 4 "
        "WHEN 'compilation' THEN 5 "
        "WHEN 'singles chart' THEN 6 "
        "WHEN 'soundtrack' THEN 7 "
        "WHEN 'remix' THEN 8 "
        "ELSE 9 END"
    ).arg(typeCol, compCol);
}

QString orderByForAlbums(SortOrder order) {
    switch (order) {
    case SortOrder::AlbumArtistYear:
        return "ORDER BY LOWER(COALESCE(NULLIF(album_artist, ''), artist)), year ASC, LOWER(album)";
    case SortOrder::AlbumTitle:
        return "ORDER BY LOWER(album), LOWER(COALESCE(NULLIF(album_artist, ''), artist))";
    case SortOrder::YearDesc:
        return "ORDER BY year DESC, LOWER(album)";
    case SortOrder::RecentlyAdded:
        return "ORDER BY MAX(date_added) DESC, LOWER(album)";
    case SortOrder::TrackTitle:
        return "ORDER BY LOWER(album)";
    case SortOrder::AlbumArtistTypeYear:
        return QString("ORDER BY LOWER(COALESCE(NULLIF(album_artist, ''), artist)), %1, LOWER(MAX(album_type)), year ASC, LOWER(album)")
            .arg(albumTypeRankExpr("MAX(album_type)", "MAX(is_compilation)"));
    case SortOrder::AlbumTypeYear:
        return QString("ORDER BY %1, LOWER(MAX(album_type)), LOWER(COALESCE(NULLIF(album_artist, ''), artist)), year ASC, LOWER(album)")
            .arg(albumTypeRankExpr("MAX(album_type)", "MAX(is_compilation)"));
    case SortOrder::RecentlyPlayed:
        return "ORDER BY MAX(last_played) DESC, LOWER(album)";
    }
    return "ORDER BY LOWER(album)";
}

QString orderByForTracks(SortOrder order) {
    switch (order) {
    case SortOrder::AlbumArtistYear:
        return "ORDER BY LOWER(COALESCE(NULLIF(album_artist, ''), artist)), year ASC, LOWER(album), disc_number ASC, track_number ASC, LOWER(title)";
    case SortOrder::AlbumTitle:
        return "ORDER BY LOWER(album), disc_number ASC, track_number ASC, LOWER(title)";
    case SortOrder::YearDesc:
        return "ORDER BY year DESC, LOWER(album), disc_number ASC, track_number ASC";
    case SortOrder::RecentlyAdded:
        return "ORDER BY date_added DESC, LOWER(album), disc_number ASC, track_number ASC";
    case SortOrder::TrackTitle:
        return "ORDER BY LOWER(title)";
    case SortOrder::AlbumArtistTypeYear:
        return QString("ORDER BY LOWER(COALESCE(NULLIF(album_artist, ''), artist)), %1, LOWER(album_type), year ASC, LOWER(album), disc_number ASC, track_number ASC, LOWER(title)")
            .arg(albumTypeRankExpr("album_type", "is_compilation"));
    case SortOrder::AlbumTypeYear:
        return QString("ORDER BY %1, LOWER(album_type), LOWER(COALESCE(NULLIF(album_artist, ''), artist)), year ASC, LOWER(album), disc_number ASC, track_number ASC, LOWER(title)")
            .arg(albumTypeRankExpr("album_type", "is_compilation"));
    case SortOrder::RecentlyPlayed:
        return "ORDER BY last_played DESC, LOWER(album), disc_number ASC, track_number ASC";
    }
    return "ORDER BY LOWER(album), disc_number ASC, track_number ASC";
}

} // namespace

Database::~Database() {
    close();
}

bool Database::open(const QString& dbPath) {
    QMutexLocker lock(&m_mutex);
    if (m_db) sqlite3_close(m_db);

    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    const QByteArray pathUtf8 = dbPath.toUtf8();
    if (sqlite3_open(pathUtf8.constData(), &m_db) != SQLITE_OK) {
        qWarning() << "Failed to open SQLite DB:" << sqlite3_errmsg(m_db);
        return false;
    }

    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    if (!initSchema()) return false;
    seedDefaultCollections();
    return true;
}

void Database::close() {
    QMutexLocker lock(&m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool Database::initSchema() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            title TEXT NOT NULL DEFAULT '',
            artist TEXT NOT NULL DEFAULT '',
            album_artist TEXT NOT NULL DEFAULT '',
            album TEXT NOT NULL DEFAULT '',
            genre TEXT NOT NULL DEFAULT '',
            year INTEGER NOT NULL DEFAULT 0,
            track_number INTEGER NOT NULL DEFAULT 0,
            disc_number INTEGER NOT NULL DEFAULT 1,
            duration_ms INTEGER NOT NULL DEFAULT 0,
            bitrate INTEGER NOT NULL DEFAULT 0,
            sample_rate INTEGER NOT NULL DEFAULT 0,
            format TEXT NOT NULL DEFAULT '',
            rating INTEGER NOT NULL DEFAULT 0,
            play_count INTEGER NOT NULL DEFAULT 0,
            date_added INTEGER NOT NULL DEFAULT 0,
            last_played INTEGER NOT NULL DEFAULT 0,
            is_compilation INTEGER NOT NULL DEFAULT 0,
            album_type TEXT NOT NULL DEFAULT '',
            art_hash TEXT NOT NULL DEFAULT ''
        );

        CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album, album_artist);
        CREATE INDEX IF NOT EXISTS idx_tracks_genre ON tracks(genre);
        CREATE INDEX IF NOT EXISTS idx_tracks_year ON tracks(year);

        CREATE TABLE IF NOT EXISTS folders (
            path TEXT UNIQUE NOT NULL
        );

        CREATE TABLE IF NOT EXISTS collections (
            id TEXT PRIMARY KEY,
            sort_index INTEGER NOT NULL DEFAULT 0,
            json_data TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS playlists (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            sort_index INTEGER NOT NULL DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS playlist_tracks (
            playlist_id TEXT NOT NULL,
            position INTEGER NOT NULL,
            track_path TEXT NOT NULL,
            PRIMARY KEY (playlist_id, position)
        );

        CREATE TABLE IF NOT EXISTS radio_stations (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            url TEXT NOT NULL,
            genre TEXT
        );

        CREATE TABLE IF NOT EXISTS podcast_feeds (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            url TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS bookmarks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            track_path TEXT NOT NULL,
            position_sec REAL NOT NULL,
            label TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS resume_positions (
            track_path TEXT PRIMARY KEY,
            position_sec REAL NOT NULL
        );
    )SQL";

    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        qWarning() << "Schema init error:" << (err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }

    // Migrate existing databases that lack album_type or is_inbox
    bool hasAlbumType = false;
    bool hasIsInbox = false;
    sqlite3_stmt* pragmaStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(tracks)", -1, &pragmaStmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(pragmaStmt) == SQLITE_ROW) {
            const QString col = colText(pragmaStmt, 1);
            if (col == "album_type") hasAlbumType = true;
            else if (col == "is_inbox") hasIsInbox = true;
        }
        sqlite3_finalize(pragmaStmt);
    }
    if (!hasAlbumType) {
        sqlite3_exec(m_db, "ALTER TABLE tracks ADD COLUMN album_type TEXT NOT NULL DEFAULT '';", nullptr, nullptr, nullptr);
    }
    if (!hasIsInbox) {
        sqlite3_exec(m_db, "ALTER TABLE tracks ADD COLUMN is_inbox INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, nullptr);
    }
    sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_tracks_album_type ON tracks(album_type);", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_tracks_is_inbox ON tracks(is_inbox);", nullptr, nullptr, nullptr);

    return true;
}

void Database::seedDefaultCollections() {
    auto insertCol = [&](int idx, const Collection& col) {
        const QByteArray json = QJsonDocument(col.toJson()).toJson(QJsonDocument::Compact);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "INSERT OR IGNORE INTO collections (id, sort_index, json_data) VALUES (?, ?, ?)", -1, &stmt, nullptr) == SQLITE_OK) {
            const QByteArray idUtf8 = col.id.toUtf8();
            sqlite3_bind_text(stmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, idx);
            sqlite3_bind_text(stmt, 3, json.constData(), json.size(), SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    };

    sqlite3_stmt* checkStmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM collections", -1, &checkStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            count = sqlite3_column_int(checkStmt, 0);
        }
        sqlite3_finalize(checkStmt);
    }

    if (count == 0) {
        Collection allMusic;
        allMusic.id = "sys_all_music";
        allMusic.name = "All Music";
        allMusic.isSystem = true;
        allMusic.viewMode = ViewMode::AlbumCovers;
        allMusic.artworkSize = 200;
        insertCol(0, allMusic);

        Collection variousArtists;
        variousArtists.id = "sys_various_artists";
        variousArtists.name = "Various Artists";
        variousArtists.isSystem = true;
        variousArtists.viewMode = ViewMode::AlbumCovers;
        variousArtists.rules.conditions.append({FilterField::IsCompilation, FilterOp::IsTrue, ""});
        insertCol(1, variousArtists);

        Collection recent;
        recent.id = "sys_recent";
        recent.name = "Recently Added";
        recent.isSystem = true;
        recent.viewMode = ViewMode::AlbumCovers;
        recent.sortOrder = SortOrder::RecentlyAdded;
        recent.rules.conditions.append({FilterField::DateAddedDaysAgo, FilterOp::LessOrEqual, "30"});
        insertCol(2, recent);
    }

    Collection recentlyPlayed;
    recentlyPlayed.id = "sys_recently_played";
    recentlyPlayed.name = "Recently Played";
    recentlyPlayed.isSystem = true;
    recentlyPlayed.viewMode = ViewMode::TracksTable;
    recentlyPlayed.sortOrder = SortOrder::RecentlyPlayed;
    recentlyPlayed.rules.conditions.append({FilterField::PlayCount, FilterOp::GreaterThan, "0"});
    insertCol(3, recentlyPlayed);
}

QStringList Database::libraryFolders() const {
    QMutexLocker lock(&m_mutex);
    QStringList out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT path FROM folders ORDER BY path", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            out.append(colText(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

bool Database::addLibraryFolder(const QString& path) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT OR IGNORE INTO folders (path) VALUES (?)", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const QByteArray p = path.toUtf8();
    sqlite3_bind_text(stmt, 1, p.constData(), p.size(), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::removeLibraryFolder(const QString& path) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM folders WHERE path = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const QByteArray p = path.toUtf8();
    sqlite3_bind_text(stmt, 1, p.constData(), p.size(), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::upsertTracks(const QVector<Track>& tracks) {
    if (tracks.isEmpty()) return true;
    QMutexLocker lock(&m_mutex);

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = R"SQL(
        INSERT INTO tracks (
            path, title, artist, album_artist, album, genre, year,
            track_number, disc_number, duration_ms, bitrate, sample_rate,
            format, date_added, is_compilation, album_type, art_hash
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            title = excluded.title,
            artist = excluded.artist,
            album_artist = excluded.album_artist,
            album = excluded.album,
            genre = excluded.genre,
            year = excluded.year,
            track_number = excluded.track_number,
            disc_number = excluded.disc_number,
            duration_ms = excluded.duration_ms,
            bitrate = excluded.bitrate,
            sample_rate = excluded.sample_rate,
            format = excluded.format,
            is_compilation = excluded.is_compilation,
            album_type = CASE WHEN excluded.album_type != '' THEN excluded.album_type ELSE tracks.album_type END,
            art_hash = CASE WHEN excluded.art_hash != '' THEN excluded.art_hash ELSE tracks.art_hash END;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    const int64_t now = QDateTime::currentSecsSinceEpoch();
    for (const auto& t : tracks) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        auto bindStr = [&](int idx, const QString& s) {
            const QByteArray utf8 = s.toUtf8();
            sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
        };

        bindStr(1, t.path);
        bindStr(2, t.title);
        bindStr(3, t.artist);
        bindStr(4, t.albumArtist);
        bindStr(5, t.album);
        bindStr(6, t.genre);
        sqlite3_bind_int(stmt, 7, t.year);
        sqlite3_bind_int(stmt, 8, t.trackNumber);
        sqlite3_bind_int(stmt, 9, t.discNumber);
        sqlite3_bind_int64(stmt, 10, t.durationMs);
        sqlite3_bind_int(stmt, 11, t.bitrate);
        sqlite3_bind_int(stmt, 12, t.sampleRate);
        bindStr(13, t.format);
        sqlite3_bind_int64(stmt, 14, t.dateAdded > 0 ? t.dateAdded : now);
        sqlite3_bind_int(stmt, 15, t.isCompilation ? 1 : 0);
        bindStr(16, t.albumType);
        bindStr(17, t.artHash);

        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::updateTracksTags(const QVector<Track>& tracks) {
    if (tracks.isEmpty()) return true;
    QMutexLocker lock(&m_mutex);

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = R"SQL(
        UPDATE tracks SET
            title = ?,
            artist = ?,
            album_artist = ?,
            album = ?,
            genre = ?,
            year = ?,
            track_number = ?,
            disc_number = ?,
            is_compilation = ?,
            album_type = ?,
            art_hash = ?
        WHERE id = ?;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    for (const auto& t : tracks) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        auto bindStr = [&](int idx, const QString& s) {
            const QByteArray utf8 = s.toUtf8();
            sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
        };

        bindStr(1, t.title);
        bindStr(2, t.artist);
        bindStr(3, t.albumArtist);
        bindStr(4, t.album);
        bindStr(5, t.genre);
        sqlite3_bind_int(stmt, 6, t.year);
        sqlite3_bind_int(stmt, 7, t.trackNumber);
        sqlite3_bind_int(stmt, 8, t.discNumber);
        sqlite3_bind_int(stmt, 9, t.isCompilation ? 1 : 0);
        bindStr(10, t.albumType);
        bindStr(11, t.artHash);
        sqlite3_bind_int64(stmt, 12, t.id);

        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

int Database::pruneMissingTracks(const QSet<QString>& seenPaths,
                                 const QStringList& configuredFolders,
                                 const QStringList& scannedRoots) {
    QMutexLocker lock(&m_mutex);

    QSet<QString> scannedClean;
    for (const QString& root : scannedRoots) {
        scannedClean.insert(QDir::cleanPath(root));
    }

    QStringList unmountedFolders;
    for (const QString& folder : configuredFolders) {
        const QString clean = QDir::cleanPath(folder);
        if (!scannedClean.contains(clean)) {
            unmountedFolders.append(clean);
        }
    }

    auto isUnderUnmounted = [&](const QString& trackPath) -> bool {
        for (const QString& folder : unmountedFolders) {
            if (folder == "/" || trackPath == folder || trackPath.startsWith(folder + '/')) {
                return true;
            }
        }
        return false;
    };

    QVector<int64_t> toDelete;
    sqlite3_stmt* selectStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT id, path FROM tracks", -1, &selectStmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    while (sqlite3_step(selectStmt) == SQLITE_ROW) {
        const int64_t id = sqlite3_column_int64(selectStmt, 0);
        const QString path = colText(selectStmt, 1);
        if (!seenPaths.contains(path) && !isUnderUnmounted(path)) {
            toDelete.append(id);
        }
    }
    sqlite3_finalize(selectStmt);

    if (toDelete.isEmpty()) return 0;

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* delStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM tracks WHERE id = ?", -1, &delStmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return 0;
    }

    int deletedCount = 0;
    for (int64_t id : toDelete) {
        sqlite3_reset(delStmt);
        sqlite3_bind_int64(delStmt, 1, id);
        if (sqlite3_step(delStmt) == SQLITE_DONE) {
            ++deletedCount;
        }
    }

    sqlite3_finalize(delStmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return deletedCount;
}

int Database::trackCount() const {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM tracks WHERE is_inbox = 0", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

void Database::incrementPlayCount(const QString& path) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE tracks SET play_count = play_count + 1, last_played = ? WHERE path = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, QDateTime::currentSecsSinceEpoch());
        const QByteArray p = path.toUtf8();
        sqlite3_bind_text(stmt, 2, p.constData(), p.size(), SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::setTrackRating(int64_t trackId, int rating) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE tracks SET rating = ? WHERE id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, qBound(0, rating, 5));
        sqlite3_bind_int64(stmt, 2, trackId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

bool Database::setAlbumType(const QString& album,
                            const QString& albumArtist,
                            bool isCompilation,
                            const QString& albumType) {
    QMutexLocker lock(&m_mutex);
    QString sql = "UPDATE tracks SET album_type = ? WHERE LOWER(COALESCE(NULLIF(album, ''), 'Unknown Album')) = LOWER(?)";
    if (isCompilation || albumArtist.compare("Various Artists", Qt::CaseInsensitive) == 0) {
        sql += " AND (is_compilation = 1 OR LOWER(album_artist) = 'various artists')";
    } else {
        sql += " AND LOWER(COALESCE(NULLIF(album_artist, ''), NULLIF(artist, ''), 'Unknown Artist')) = LOWER(?)";
    }

    sqlite3_stmt* stmt = nullptr;
    const QByteArray sqlUtf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), sqlUtf8.size(), &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    const QByteArray typeUtf8 = albumType.trimmed().toUtf8();
    const QByteArray albUtf8 = album.toUtf8();
    sqlite3_bind_text(stmt, 1, typeUtf8.constData(), typeUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, albUtf8.constData(), albUtf8.size(), SQLITE_TRANSIENT);
    if (!isCompilation && albumArtist.compare("Various Artists", Qt::CaseInsensitive) != 0) {
        const QByteArray artUtf8 = albumArtist.toUtf8();
        sqlite3_bind_text(stmt, 3, artUtf8.constData(), artUtf8.size(), SQLITE_TRANSIENT);
    }

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

QStringList Database::distinctAlbumTypes() const {
    QMutexLocker lock(&m_mutex);
    QStringList out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT DISTINCT album_type FROM tracks WHERE album_type != '' ORDER BY LOWER(album_type)", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const QString t = colText(stmt, 0);
            if (!t.isEmpty()) out.append(t);
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

QString Database::buildSearchSql(const QString& searchText, QVariantList& bindValues) const {
    const QString trimmed = searchText.trimmed();
    if (trimmed.isEmpty()) return "1=1";

    QString escaped = trimmed;
    escaped.replace('\\', "\\\\");
    escaped.replace('%', "\\%");
    escaped.replace('_', "\\_");
    const QString pat = "%" + escaped + "%";

    for (int i = 0; i < 6; ++i) bindValues.append(pat);
    return "(title LIKE ? ESCAPE '\\' OR artist LIKE ? ESCAPE '\\' OR album_artist LIKE ? ESCAPE '\\' OR album LIKE ? ESCAPE '\\' OR album_type LIKE ? ESCAPE '\\' OR genre LIKE ? ESCAPE '\\')";
}

QVector<Album> Database::queryAlbums(const RuleGroup& rules,
                                     const QString& searchText,
                                     SortOrder sortOrder) const {
    QMutexLocker lock(&m_mutex);
    QVector<Album> albums;
    QVariantList bindValues;

    const QString ruleSql = rules.toSql(bindValues);
    const QString searchSql = buildSearchSql(searchText, bindValues);

    const QString sql = QString(
        "SELECT "
        "  COALESCE(NULLIF(album, ''), 'Unknown Album') AS alb, "
        "  CASE WHEN (is_compilation = 1 OR LOWER(album_artist) = 'various artists') THEN 'Various Artists' "
        "       ELSE COALESCE(NULLIF(album_artist, ''), NULLIF(artist, ''), 'Unknown Artist') END AS alb_artist, "
        "  MAX(year) AS yr, "
        "  MAX(genre) AS gnr, "
        "  MAX(is_compilation) AS comp, "
        "  MAX(album_type) AS alb_type, "
        "  COUNT(*) AS cnt, "
        "  SUM(duration_ms) AS dur, "
        "  MAX(art_hash) AS art, "
        "  MIN(path) AS first_path, "
        "  MAX(disc_number) AS discs "
        "FROM tracks "
        "WHERE is_inbox = 0 AND (%1) AND (%2) "
        "GROUP BY LOWER(alb), LOWER(alb_artist) "
        "%3"
    ).arg(ruleSql, searchSql, orderByForAlbums(sortOrder));

    sqlite3_stmt* stmt = nullptr;
    const QByteArray sqlUtf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), sqlUtf8.size(), &stmt, nullptr) == SQLITE_OK) {
        bindVariantList(stmt, bindValues);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Album a;
            a.album = colText(stmt, 0);
            a.albumArtist = colText(stmt, 1);
            a.year = sqlite3_column_int(stmt, 2);
            a.genre = colText(stmt, 3);
            a.isCompilation = sqlite3_column_int(stmt, 4) != 0;
            a.albumType = colText(stmt, 5);
            if (a.albumType.isEmpty() && a.isCompilation) {
                a.albumType = "Compilation";
            }
            a.trackCount = sqlite3_column_int(stmt, 6);
            a.totalDurationMs = sqlite3_column_int64(stmt, 7);
            a.artHash = colText(stmt, 8);
            a.firstTrackPath = colText(stmt, 9);
            a.discCount = qMax(1, sqlite3_column_int(stmt, 10));
            albums.append(a);
        }
        sqlite3_finalize(stmt);
    } else {
        qWarning() << "queryAlbums SQL error:" << sqlite3_errmsg(m_db);
    }

    return albums;
}

QVector<Track> Database::queryTracks(const RuleGroup& rules,
                                     const QString& searchText,
                                     SortOrder sortOrder,
                                     const QString& albumFilter,
                                     const QString& albumArtistFilter,
                                     int limitCount,
                                     LimitOrder limitOrder) const {
    QMutexLocker lock(&m_mutex);
    QVector<Track> tracks;
    QVariantList bindValues;

    const QString ruleSql = rules.toSql(bindValues);
    const QString searchSql = buildSearchSql(searchText, bindValues);

    QString extraSql = "1=1";
    if (!albumFilter.isEmpty()) {
        extraSql = "LOWER(COALESCE(NULLIF(album, ''), 'Unknown Album')) = LOWER(?)";
        bindValues.append(albumFilter);
        if (!albumArtistFilter.isEmpty() && albumArtistFilter != "Various Artists") {
            extraSql += " AND LOWER(COALESCE(NULLIF(album_artist, ''), NULLIF(artist, ''), 'Unknown Artist')) = LOWER(?)";
            bindValues.append(albumArtistFilter);
        } else if (albumArtistFilter == "Various Artists") {
            extraSql += " AND (is_compilation = 1 OR LOWER(album_artist) = 'various artists')";
        }
    }

    QString orderSql = orderByForTracks(sortOrder);
    if (limitCount > 0) {
        switch (limitOrder) {
        case LimitOrder::Random: orderSql = "ORDER BY RANDOM()"; break;
        case LimitOrder::HighestRated: orderSql = "ORDER BY rating DESC, play_count DESC"; break;
        case LimitOrder::MostPlayed: orderSql = "ORDER BY play_count DESC"; break;
        case LimitOrder::LeastPlayed: orderSql = "ORDER BY play_count ASC, RANDOM()"; break;
        case LimitOrder::RecentlyAdded: orderSql = "ORDER BY date_added DESC"; break;
        case LimitOrder::Default: break;
        }
        orderSql += QString(" LIMIT %1").arg(limitCount);
    }

    const QString sql = QString(
        "SELECT id, path, title, artist, album_artist, album, genre, year, "
        "       track_number, disc_number, duration_ms, bitrate, sample_rate, "
        "       format, rating, play_count, date_added, last_played, is_compilation, album_type, art_hash "
        "FROM tracks "
        "WHERE is_inbox = 0 AND (%1) AND (%2) AND (%3) "
        "%4"
    ).arg(ruleSql, searchSql, extraSql, orderSql);

    sqlite3_stmt* stmt = nullptr;
    const QByteArray sqlUtf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), sqlUtf8.size(), &stmt, nullptr) == SQLITE_OK) {
        bindVariantList(stmt, bindValues);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Track t;
            t.id = sqlite3_column_int64(stmt, 0);
            t.path = colText(stmt, 1);
            t.title = colText(stmt, 2);
            t.artist = colText(stmt, 3);
            t.albumArtist = colText(stmt, 4);
            t.album = colText(stmt, 5);
            t.genre = colText(stmt, 6);
            t.year = sqlite3_column_int(stmt, 7);
            t.trackNumber = sqlite3_column_int(stmt, 8);
            t.discNumber = sqlite3_column_int(stmt, 9);
            t.durationMs = sqlite3_column_int64(stmt, 10);
            t.bitrate = sqlite3_column_int(stmt, 11);
            t.sampleRate = sqlite3_column_int(stmt, 12);
            t.format = colText(stmt, 13);
            t.rating = sqlite3_column_int(stmt, 14);
            t.playCount = sqlite3_column_int(stmt, 15);
            t.dateAdded = sqlite3_column_int64(stmt, 16);
            t.lastPlayed = sqlite3_column_int64(stmt, 17);
            t.isCompilation = sqlite3_column_int(stmt, 18) != 0;
            t.albumType = colText(stmt, 19);
            t.artHash = colText(stmt, 20);
            tracks.append(t);
        }
        sqlite3_finalize(stmt);
    } else {
        qWarning() << "queryTracks SQL error:" << sqlite3_errmsg(m_db);
    }

    return tracks;
}

QVector<Collection> Database::loadCollections() const {
    QMutexLocker lock(&m_mutex);
    QVector<Collection> out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT json_data FROM collections ORDER BY sort_index ASC, id ASC", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const QByteArray raw = colText(stmt, 0).toUtf8();
            const auto doc = QJsonDocument::fromJson(raw);
            if (doc.isObject()) {
                out.append(Collection::fromJson(doc.object()));
            }
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

bool Database::saveCollection(const Collection& col) {
    QMutexLocker lock(&m_mutex);
    const QByteArray json = QJsonDocument(col.toJson()).toJson(QJsonDocument::Compact);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"SQL(
        INSERT INTO collections (id, sort_index, json_data)
        VALUES (?, (SELECT COALESCE(MAX(sort_index), 0) + 1 FROM collections), ?)
        ON CONFLICT(id) DO UPDATE SET json_data = excluded.json_data;
    )SQL";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    const QByteArray idUtf8 = col.id.toUtf8();
    sqlite3_bind_text(stmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, json.constData(), json.size(), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteCollection(const QString& id) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM collections WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const QByteArray idUtf8 = id.toUtf8();
    sqlite3_bind_text(stmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

QVector<Database::Playlist> Database::loadPlaylists() const {
    QMutexLocker lock(&m_mutex);
    QVector<Playlist> out;
    const char* sql = R"SQL(
        SELECT p.id, p.name, COUNT(pt.track_path)
        FROM playlists p
        LEFT JOIN playlist_tracks pt ON pt.playlist_id = p.id
        GROUP BY p.id, p.name
        ORDER BY p.sort_index ASC, LOWER(p.name) ASC;
    )SQL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Playlist pl;
            pl.id = colText(stmt, 0);
            pl.name = colText(stmt, 1);
            pl.trackCount = sqlite3_column_int(stmt, 2);
            out.append(pl);
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

bool Database::createPlaylist(const QString& id, const QString& name) {
    QMutexLocker lock(&m_mutex);
    const char* sql = R"SQL(
        INSERT INTO playlists (id, name, sort_index)
        VALUES (?, ?, (SELECT COALESCE(MAX(sort_index), 0) + 1 FROM playlists));
    )SQL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    const QByteArray idUtf8 = id.toUtf8();
    const QByteArray nameUtf8 = name.trimmed().toUtf8();
    sqlite3_bind_text(stmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, nameUtf8.constData(), nameUtf8.size(), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::renamePlaylist(const QString& id, const QString& newName) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE playlists SET name = ? WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const QByteArray nameUtf8 = newName.trimmed().toUtf8();
    const QByteArray idUtf8 = id.toUtf8();
    sqlite3_bind_text(stmt, 1, nameUtf8.constData(), nameUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deletePlaylist(const QString& id) {
    QMutexLocker lock(&m_mutex);
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const QByteArray idUtf8 = id.toUtf8();

    sqlite3_stmt* s1 = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM playlist_tracks WHERE playlist_id = ?", -1, &s1, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(s1, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        sqlite3_step(s1);
        sqlite3_finalize(s1);
    }

    bool ok = false;
    sqlite3_stmt* s2 = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM playlists WHERE id = ?", -1, &s2, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(s2, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        ok = (sqlite3_step(s2) == SQLITE_DONE);
        sqlite3_finalize(s2);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return ok;
}

bool Database::addTracksToPlaylist(const QString& playlistId, const QStringList& trackPaths) {
    if (trackPaths.isEmpty()) return true;
    QMutexLocker lock(&m_mutex);
    const QByteArray idUtf8 = playlistId.toUtf8();

    int nextPos = 0;
    sqlite3_stmt* posStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_tracks WHERE playlist_id = ?", -1, &posStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(posStmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        if (sqlite3_step(posStmt) == SQLITE_ROW) {
            nextPos = sqlite3_column_int(posStmt, 0);
        }
        sqlite3_finalize(posStmt);
    }

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* insStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO playlist_tracks (playlist_id, position, track_path) VALUES (?, ?, ?)", -1, &insStmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    for (const QString& path : trackPaths) {
        sqlite3_reset(insStmt);
        sqlite3_clear_bindings(insStmt);
        const QByteArray pUtf8 = path.toUtf8();
        sqlite3_bind_text(insStmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(insStmt, 2, nextPos++);
        sqlite3_bind_text(insStmt, 3, pUtf8.constData(), pUtf8.size(), SQLITE_TRANSIENT);
        sqlite3_step(insStmt);
    }

    sqlite3_finalize(insStmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::removeTrackFromPlaylist(const QString& playlistId, int position) {
    QMutexLocker lock(&m_mutex);
    const QByteArray idUtf8 = playlistId.toUtf8();

    QStringList paths;
    sqlite3_stmt* sel = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT track_path FROM playlist_tracks WHERE playlist_id = ? ORDER BY position ASC", -1, &sel, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(sel, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        while (sqlite3_step(sel) == SQLITE_ROW) {
            paths.append(colText(sel, 0));
        }
        sqlite3_finalize(sel);
    }

    if (position < 0 || position >= paths.size()) return false;
    paths.removeAt(position);

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM playlist_tracks WHERE playlist_id = ?", -1, &del, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(del, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        sqlite3_step(del);
        sqlite3_finalize(del);
    }

    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO playlist_tracks (playlist_id, position, track_path) VALUES (?, ?, ?)", -1, &ins, nullptr) == SQLITE_OK) {
        for (int i = 0; i < paths.size(); ++i) {
            sqlite3_reset(ins);
            sqlite3_clear_bindings(ins);
            const QByteArray pUtf8 = paths[i].toUtf8();
            sqlite3_bind_text(ins, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, i);
            sqlite3_bind_text(ins, 3, pUtf8.constData(), pUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_step(ins);
        }
        sqlite3_finalize(ins);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::movePlaylistTrack(const QString& playlistId, int fromPos, int toPos) {
    QMutexLocker lock(&m_mutex);
    const QByteArray idUtf8 = playlistId.toUtf8();

    QStringList paths;
    sqlite3_stmt* sel = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT track_path FROM playlist_tracks WHERE playlist_id = ? ORDER BY position ASC", -1, &sel, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(sel, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        while (sqlite3_step(sel) == SQLITE_ROW) {
            paths.append(colText(sel, 0));
        }
        sqlite3_finalize(sel);
    }

    if (fromPos < 0 || fromPos >= paths.size() || toPos < 0 || toPos >= paths.size() || fromPos == toPos) {
        return false;
    }
    paths.move(fromPos, toPos);

    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM playlist_tracks WHERE playlist_id = ?", -1, &del, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(del, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
        sqlite3_step(del);
        sqlite3_finalize(del);
    }

    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO playlist_tracks (playlist_id, position, track_path) VALUES (?, ?, ?)", -1, &ins, nullptr) == SQLITE_OK) {
        for (int i = 0; i < paths.size(); ++i) {
            sqlite3_reset(ins);
            sqlite3_clear_bindings(ins);
            const QByteArray pUtf8 = paths[i].toUtf8();
            sqlite3_bind_text(ins, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(ins, 2, i);
            sqlite3_bind_text(ins, 3, pUtf8.constData(), pUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_step(ins);
        }
        sqlite3_finalize(ins);
    }

    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

QVector<Track> Database::queryPlaylistTracks(const QString& playlistId, const QString& searchText) const {
    QMutexLocker lock(&m_mutex);
    QVector<Track> tracks;
    QVariantList bindValues;
    bindValues.append(playlistId);
    const QString searchSql = buildSearchSql(searchText, bindValues);

    const QString sql = QString(
        "SELECT t.id, pt.track_path, t.title, t.artist, t.album_artist, t.album, t.genre, t.year, "
        "       t.track_number, t.disc_number, t.duration_ms, t.bitrate, t.sample_rate, "
        "       t.format, t.rating, t.play_count, t.date_added, t.last_played, t.is_compilation, t.album_type, t.art_hash "
        "FROM playlist_tracks pt "
        "LEFT JOIN tracks t ON t.path = pt.track_path "
        "WHERE pt.playlist_id = ? AND (%1) "
        "ORDER BY pt.position ASC"
    ).arg(searchText.trimmed().isEmpty() ? "1=1" : searchSql);

    sqlite3_stmt* stmt = nullptr;
    const QByteArray sqlUtf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), sqlUtf8.size(), &stmt, nullptr) == SQLITE_OK) {
        bindVariantList(stmt, bindValues);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Track t;
            t.id = sqlite3_column_int64(stmt, 0);
            t.path = colText(stmt, 1);
            t.title = colText(stmt, 2);
            if (t.title.isEmpty()) {
                t.title = QFileInfo(t.path).completeBaseName();
            }
            t.artist = colText(stmt, 3);
            t.albumArtist = colText(stmt, 4);
            t.album = colText(stmt, 5);
            t.genre = colText(stmt, 6);
            t.year = sqlite3_column_int(stmt, 7);
            t.trackNumber = sqlite3_column_int(stmt, 8);
            t.discNumber = sqlite3_column_int(stmt, 9);
            t.durationMs = sqlite3_column_int64(stmt, 10);
            t.bitrate = sqlite3_column_int(stmt, 11);
            t.sampleRate = sqlite3_column_int(stmt, 12);
            t.format = colText(stmt, 13);
            t.rating = sqlite3_column_int(stmt, 14);
            t.playCount = sqlite3_column_int(stmt, 15);
            t.dateAdded = sqlite3_column_int64(stmt, 16);
            t.lastPlayed = sqlite3_column_int64(stmt, 17);
            t.isCompilation = sqlite3_column_int(stmt, 18) != 0;
            t.albumType = colText(stmt, 19);
            t.artHash = colText(stmt, 20);
            tracks.append(t);
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

QVector<Database::RadioStation> Database::loadRadioStations() const {
    QMutexLocker lock(&m_mutex);
    QVector<RadioStation> list;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT id, name, url, genre FROM radio_stations ORDER BY name COLLATE NOCASE",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            RadioStation s;
            s.id = colText(stmt, 0);
            s.name = colText(stmt, 1);
            s.url = colText(stmt, 2);
            s.genre = colText(stmt, 3);
            list.append(s);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::saveRadioStation(const RadioStation& st) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO radio_stations (id, name, url, genre) VALUES (?, ?, ?, ?) "
                           "ON CONFLICT(id) DO UPDATE SET name=excluded.name, url=excluded.url, genre=excluded.genre",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, st.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, st.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, st.url.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, st.genre.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteRadioStation(const QString& id) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM radio_stations WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVector<Database::PodcastFeed> Database::loadPodcastFeeds() const {
    QMutexLocker lock(&m_mutex);
    QVector<PodcastFeed> list;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT id, title, url FROM podcast_feeds ORDER BY title COLLATE NOCASE",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PodcastFeed f;
            f.id = colText(stmt, 0);
            f.title = colText(stmt, 1);
            f.url = colText(stmt, 2);
            list.append(f);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::savePodcastFeed(const PodcastFeed& feed) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO podcast_feeds (id, title, url) VALUES (?, ?, ?) "
                           "ON CONFLICT(id) DO UPDATE SET title=excluded.title, url=excluded.url",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, feed.id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, feed.title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, feed.url.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deletePodcastFeed(const QString& id) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM podcast_feeds WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::upsertTracksToInbox(const QVector<Track>& tracks) {
    if (!upsertTracks(tracks)) return false;
    QMutexLocker lock(&m_mutex);
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE tracks SET is_inbox = 1 WHERE path = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        for (const Track& t : tracks) {
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, t.path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

int Database::inboxCount() const {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM tracks WHERE is_inbox = 1", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

bool Database::promoteInboxTracks(const QStringList& paths) {
    if (paths.isEmpty()) return true;
    QMutexLocker lock(&m_mutex);
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "UPDATE tracks SET is_inbox = 0 WHERE path = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        for (const QString& p : paths) {
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, p.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

bool Database::removeTracksByPath(const QStringList& paths) {
    if (paths.isEmpty()) return true;
    QMutexLocker lock(&m_mutex);
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM tracks WHERE path = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        for (const QString& p : paths) {
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, p.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

int Database::removeTracksUnderFolder(const QString& folderPath) {
    QMutexLocker lock(&m_mutex);
    QString prefix = QDir::cleanPath(folderPath);
    if (!prefix.endsWith('/')) prefix += '/';
    sqlite3_stmt* stmt = nullptr;
    int removed = 0;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM tracks WHERE path LIKE ?", -1, &stmt, nullptr) == SQLITE_OK) {
        const QString pattern = prefix + "%";
        sqlite3_bind_text(stmt, 1, pattern.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            removed = sqlite3_changes(m_db);
        }
        sqlite3_finalize(stmt);
    }
    return removed;
}

QVector<Track> Database::queryInboxTracks(const QString& searchText) const {
    QMutexLocker lock(&m_mutex);
    QVector<Track> tracks;
    QVariantList bindValues;
    const QString searchSql = buildSearchSql(searchText, bindValues);
    const QString sql = QString(
        "SELECT id, path, title, artist, album_artist, album, genre, year, "
        "       track_number, disc_number, duration_ms, bitrate, sample_rate, "
        "       format, rating, play_count, date_added, last_played, is_compilation, album_type, art_hash "
        "FROM tracks "
        "WHERE is_inbox = 1 AND (%1) "
        "ORDER BY date_added DESC, LOWER(album), disc_number ASC, track_number ASC, LOWER(title)"
    ).arg(searchSql);

    sqlite3_stmt* stmt = nullptr;
    const QByteArray sqlUtf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(m_db, sqlUtf8.constData(), sqlUtf8.size(), &stmt, nullptr) == SQLITE_OK) {
        bindVariantList(stmt, bindValues);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Track t;
            t.id = sqlite3_column_int64(stmt, 0);
            t.path = colText(stmt, 1);
            t.title = colText(stmt, 2);
            t.artist = colText(stmt, 3);
            t.albumArtist = colText(stmt, 4);
            t.album = colText(stmt, 5);
            t.genre = colText(stmt, 6);
            t.year = sqlite3_column_int(stmt, 7);
            t.trackNumber = sqlite3_column_int(stmt, 8);
            t.discNumber = sqlite3_column_int(stmt, 9);
            t.durationMs = sqlite3_column_int64(stmt, 10);
            t.bitrate = sqlite3_column_int(stmt, 11);
            t.sampleRate = sqlite3_column_int(stmt, 12);
            t.format = colText(stmt, 13);
            t.rating = sqlite3_column_int(stmt, 14);
            t.playCount = sqlite3_column_int(stmt, 15);
            t.dateAdded = sqlite3_column_int64(stmt, 16);
            t.lastPlayed = sqlite3_column_int64(stmt, 17);
            t.isCompilation = sqlite3_column_int(stmt, 18) != 0;
            t.albumType = colText(stmt, 19);
            t.artHash = colText(stmt, 20);
            tracks.append(t);
        }
        sqlite3_finalize(stmt);
    }
    return tracks;
}

bool Database::addBookmark(const QString& trackPath, double positionSec, const QString& label) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO bookmarks (track_path, position_sec, label) VALUES (?, ?, ?)",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, trackPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, positionSec);
    sqlite3_bind_text(stmt, 3, label.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

QVector<Database::Bookmark> Database::loadBookmarks(const QString& trackPath) const {
    QMutexLocker lock(&m_mutex);
    QVector<Bookmark> list;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT id, track_path, position_sec, label FROM bookmarks WHERE track_path = ? ORDER BY position_sec ASC",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, trackPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Bookmark b;
            b.id = sqlite3_column_int64(stmt, 0);
            b.trackPath = colText(stmt, 1);
            b.positionSec = sqlite3_column_double(stmt, 2);
            b.label = colText(stmt, 3);
            list.append(b);
        }
        sqlite3_finalize(stmt);
    }
    return list;
}

bool Database::deleteBookmark(int64_t bookmarkId) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM bookmarks WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, bookmarkId);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

void Database::saveResumePosition(const QString& trackPath, double positionSec) {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (positionSec <= 2.0) {
        if (sqlite3_prepare_v2(m_db, "DELETE FROM resume_positions WHERE track_path = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, trackPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return;
    }
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO resume_positions (track_path, position_sec) VALUES (?, ?) "
                           "ON CONFLICT(track_path) DO UPDATE SET position_sec = excluded.position_sec",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, trackPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, positionSec);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

double Database::loadResumePosition(const QString& trackPath) const {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    double pos = 0.0;
    if (sqlite3_prepare_v2(m_db, "SELECT position_sec FROM resume_positions WHERE track_path = ?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, trackPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            pos = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return pos;
}

QVector<Album> Database::queryFoldersAsAlbums(const RuleGroup& rules,
                                              const QString& searchText,
                                              SortOrder sortOrder) const {
    const QVector<Track> tracks = queryTracks(rules, searchText, SortOrder::AlbumArtistYear);
    QHash<QString, Album> byDir;
    QHash<QString, QSet<QString>> artistsPerDir;

    for (const Track& t : tracks) {
        const QFileInfo fi(t.path);
        const QString dirPath = fi.absolutePath();
        auto it = byDir.find(dirPath);
        if (it == byDir.end()) {
            Album a;
            a.folderPath = dirPath;
            a.album = QFileInfo(dirPath).fileName();
            if (a.album.isEmpty()) a.album = dirPath;
            a.albumArtist = !t.albumArtist.isEmpty() ? t.albumArtist : t.artist;
            a.year = t.year;
            a.genre = t.genre;
            a.isCompilation = true;
            a.albumType = t.albumType.isEmpty() ? QString("Singles Chart") : t.albumType;
            a.trackCount = 1;
            a.discCount = 1;
            a.totalDurationMs = t.durationMs;
            a.artHash = t.artHash;
            a.firstTrackPath = t.path;
            byDir.insert(dirPath, a);
        } else {
            Album& a = it.value();
            a.trackCount += 1;
            a.totalDurationMs += t.durationMs;
            if (t.year > a.year) a.year = t.year;
            if (a.artHash.isEmpty() && !t.artHash.isEmpty()) a.artHash = t.artHash;
            if (a.albumType == "Singles Chart" && !t.albumType.isEmpty()) a.albumType = t.albumType;
        }
        if (!t.artist.isEmpty()) {
            artistsPerDir[dirPath].insert(t.artist.toLower());
        }
    }

    QVector<Album> result;
    result.reserve(byDir.size());
    for (auto it = byDir.begin(); it != byDir.end(); ++it) {
        Album a = it.value();
        if (artistsPerDir[it.key()].size() > 1) {
            a.albumArtist = "Various Artists";
        }
        result.append(a);
    }

    std::sort(result.begin(), result.end(), [sortOrder](const Album& a, const Album& b) {
        if (sortOrder == SortOrder::YearDesc) {
            if (a.year != b.year) return a.year > b.year;
        }
        return a.album.compare(b.album, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QVector<Track> Database::queryTracksInFolder(const RuleGroup& rules,
                                             const QString& searchText,
                                             const QString& folderPath) const {
    const QVector<Track> all = queryTracks(rules, searchText, SortOrder::AlbumArtistYear);
    const QString cleanDir = QDir::cleanPath(folderPath);
    QVector<Track> out;
    for (const Track& t : all) {
        if (QDir::cleanPath(QFileInfo(t.path).absolutePath()) == cleanDir) {
            out.append(t);
        }
    }
    std::sort(out.begin(), out.end(), [](const Track& x, const Track& y) {
        if (x.discNumber != y.discNumber) return x.discNumber < y.discNumber;
        if (x.trackNumber != y.trackNumber) return x.trackNumber < y.trackNumber;
        return QFileInfo(x.path).fileName().compare(QFileInfo(y.path).fileName(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

} // namespace honeycomb

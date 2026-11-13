#include "Database.h"

#include <QDateTime>
#include <QDebug>
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
    )SQL";

    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        qWarning() << "Schema init error:" << (err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }
    return true;
}

void Database::seedDefaultCollections() {
    sqlite3_stmt* checkStmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM collections", -1, &checkStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            count = sqlite3_column_int(checkStmt, 0);
        }
        sqlite3_finalize(checkStmt);
    }
    if (count > 0) return;

    auto insertCol = [&](int idx, const Collection& col) {
        const QByteArray json = QJsonDocument(col.toJson()).toJson(QJsonDocument::Compact);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "INSERT INTO collections (id, sort_index, json_data) VALUES (?, ?, ?)", -1, &stmt, nullptr) == SQLITE_OK) {
            const QByteArray idUtf8 = col.id.toUtf8();
            sqlite3_bind_text(stmt, 1, idUtf8.constData(), idUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, idx);
            sqlite3_bind_text(stmt, 3, json.constData(), json.size(), SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    };

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
            format, date_added, is_compilation, art_hash
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
        bindStr(16, t.artHash);

        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

int Database::trackCount() const {
    QMutexLocker lock(&m_mutex);
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM tracks", -1, &stmt, nullptr) == SQLITE_OK) {
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

QString Database::buildSearchSql(const QString& searchText, QVariantList& bindValues) const {
    const QString trimmed = searchText.trimmed();
    if (trimmed.isEmpty()) return "1=1";

    QString escaped = trimmed;
    escaped.replace('\\', "\\\\");
    escaped.replace('%', "\\%");
    escaped.replace('_', "\\_");
    const QString pat = "%" + escaped + "%";

    for (int i = 0; i < 5; ++i) bindValues.append(pat);
    return "(title LIKE ? ESCAPE '\\' OR artist LIKE ? ESCAPE '\\' OR album_artist LIKE ? ESCAPE '\\' OR album LIKE ? ESCAPE '\\' OR genre LIKE ? ESCAPE '\\')";
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
        "  COUNT(*) AS cnt, "
        "  SUM(duration_ms) AS dur, "
        "  MAX(art_hash) AS art, "
        "  MIN(path) AS first_path, "
        "  MAX(disc_number) AS discs "
        "FROM tracks "
        "WHERE (%1) AND (%2) "
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
            a.trackCount = sqlite3_column_int(stmt, 5);
            a.totalDurationMs = sqlite3_column_int64(stmt, 6);
            a.artHash = colText(stmt, 7);
            a.firstTrackPath = colText(stmt, 8);
            a.discCount = qMax(1, sqlite3_column_int(stmt, 9));
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
        "       format, rating, play_count, date_added, last_played, is_compilation, art_hash "
        "FROM tracks "
        "WHERE (%1) AND (%2) AND (%3) "
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
            t.artHash = colText(stmt, 19);
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

} // namespace honeycomb

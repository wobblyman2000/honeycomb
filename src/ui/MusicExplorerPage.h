#pragma once

#include "core/Track.h"
#include "db/Database.h"
#include "ui/ArtCache.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVector>
#include <QWidget>

namespace honeycomb {

class MusicExplorerPage : public QWidget {
    Q_OBJECT
public:
    explicit MusicExplorerPage(Database* db, ArtCache* artCache, QWidget* parent = nullptr);

    void exploreArtist(const QString& artistName);
    void onNowPlayingTrackChanged(const Track& track);

signals:
    void playTracksRequested(const QVector<Track>& tracks, int startIndex);
    void findArtistInLibraryRequested(const QString& artist);

private:
    void fetchWikipediaSummary(const QString& artist);
    void fetchMusicBrainzArtist(const QString& artist);
    void fetchMusicBrainzDiscography(const QString& mbid);
    void fetchLastFmInfo(const QString& artist);
    void loadLocalLibrarySection(const QString& artist);

    Database* m_db = nullptr;
    ArtCache* m_artCache = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QString m_currentArtist;

    QLineEdit* m_searchEdit = nullptr;
    QCheckBox* m_followPlayingChk = nullptr;
    QLabel* m_artistTitle = nullptr;
    QLabel* m_artistMeta = nullptr;
    QLabel* m_artistPhoto = nullptr;
    QTextBrowser* m_bioBrowser = nullptr;
    QListWidget* m_similarList = nullptr;
    QListWidget* m_localAlbumsList = nullptr;
    QTableWidget* m_topTracksTable = nullptr;
    QTableWidget* m_discogTable = nullptr;

    QVector<Album> m_localAlbums;
    QVector<Track> m_localTracks;
};

} // namespace honeycomb

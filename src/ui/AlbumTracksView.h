#pragma once

#include "core/Track.h"
#include "ui/ArtCache.h"

#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace honeycomb {

class AlbumTracksView : public QWidget {
    Q_OBJECT
public:
    explicit AlbumTracksView(ArtCache* artCache, QWidget* parent = nullptr);

    void setData(const QVector<Album>& albums, const QVector<Track>& allTracks);

signals:
    void trackActivated(const QVector<honeycomb::Track>& queue, int startIndex);
    void trackPlayNextRequested(const honeycomb::Track& track);
    void trackQueueRequested(const honeycomb::Track& track);
    void findArtistRequested(const QString& artist);
    void findTrackRequested(const QString& title);

private:
    void showTracksForAlbum(int albumRow);

    ArtCache* m_artCache = nullptr;
    QSplitter* m_splitter = nullptr;
    QListWidget* m_albumList = nullptr;
    QLabel* m_headerArt = nullptr;
    QLabel* m_headerTitle = nullptr;
    QLabel* m_headerSub = nullptr;
    QTableWidget* m_trackTable = nullptr;

    QVector<Album> m_albums;
    QVector<Track> m_allTracks;
    QVector<Track> m_currentAlbumTracks;
    QVector<int> m_rowToTrackIndex;
};

} // namespace honeycomb

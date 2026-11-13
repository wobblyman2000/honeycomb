#pragma once

#include "core/Track.h"

#include <QListWidget>
#include <QString>
#include <QVector>
#include <QWidget>

namespace honeycomb {

class ColumnBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ColumnBrowser(QWidget* parent = nullptr);

    void setTracks(const QVector<Track>& tracks);
    void clearSelection();

    QString selectedGenre() const { return m_selectedGenre; }
    QString selectedAlbumArtist() const { return m_selectedAlbumArtist; }
    QString selectedAlbum() const { return m_selectedAlbum; }

signals:
    void filterChanged();

private:
    void rebuildGenres();
    void rebuildAlbumArtists();
    void rebuildAlbums();

    QListWidget* m_genreList = nullptr;
    QListWidget* m_artistList = nullptr;
    QListWidget* m_albumList = nullptr;

    QVector<Track> m_tracks;
    QString m_selectedGenre;
    QString m_selectedAlbumArtist;
    QString m_selectedAlbum;
};

} // namespace honeycomb

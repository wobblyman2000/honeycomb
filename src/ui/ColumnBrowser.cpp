#include "ColumnBrowser.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

namespace honeycomb {

namespace {

QString normalizedAlbumArtist(const Track& t) {
    if (t.isCompilation || t.albumArtist.compare("Various Artists", Qt::CaseInsensitive) == 0)
        return "Various Artists";
    if (!t.albumArtist.isEmpty()) return t.albumArtist;
    if (!t.artist.isEmpty()) return t.artist;
    return "Unknown Artist";
}

QWidget* makeColumnPane(const QString& title, QListWidget* list, QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto* lbl = new QLabel(title, box);
    lbl->setStyleSheet("font-size: 10px; font-weight: 700; color: #8c92a4; padding-left: 4px;");
    v->addWidget(lbl);
    v->addWidget(list, 1);
    return box;
}

} // namespace

ColumnBrowser::ColumnBrowser(QWidget* parent) : QWidget(parent) {
    setMaximumHeight(190);
    setMinimumHeight(140);

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 4);
    h->setSpacing(6);

    m_genreList = new QListWidget(this);
    m_artistList = new QListWidget(this);
    m_albumList = new QListWidget(this);

    h->addWidget(makeColumnPane("GENRE", m_genreList, this), 1);
    h->addWidget(makeColumnPane("ALBUM ARTIST", m_artistList, this), 1);
    h->addWidget(makeColumnPane("ALBUM", m_albumList, this), 1);

    connect(m_genreList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_selectedGenre = (row <= 0 || !m_genreList->item(row))
            ? QString()
            : m_genreList->item(row)->data(Qt::UserRole).toString();
        m_selectedAlbumArtist.clear();
        m_selectedAlbum.clear();
        rebuildAlbumArtists();
        rebuildAlbums();
        emit filterChanged();
    });

    connect(m_artistList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_selectedAlbumArtist = (row <= 0 || !m_artistList->item(row))
            ? QString()
            : m_artistList->item(row)->data(Qt::UserRole).toString();
        m_selectedAlbum.clear();
        rebuildAlbums();
        emit filterChanged();
    });

    connect(m_albumList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_selectedAlbum = (row <= 0 || !m_albumList->item(row))
            ? QString()
            : m_albumList->item(row)->data(Qt::UserRole).toString();
        emit filterChanged();
    });
}

void ColumnBrowser::setTracks(const QVector<Track>& tracks) {
    m_tracks = tracks;
    m_selectedGenre.clear();
    m_selectedAlbumArtist.clear();
    m_selectedAlbum.clear();
    rebuildGenres();
    rebuildAlbumArtists();
    rebuildAlbums();
}

void ColumnBrowser::clearSelection() {
    m_selectedGenre.clear();
    m_selectedAlbumArtist.clear();
    m_selectedAlbum.clear();
    m_genreList->blockSignals(true);
    m_genreList->setCurrentRow(0);
    m_genreList->blockSignals(false);
    rebuildAlbumArtists();
    rebuildAlbums();
    emit filterChanged();
}

void ColumnBrowser::rebuildGenres() {
    m_genreList->blockSignals(true);
    m_genreList->clear();

    QSet<QString> seen;
    QStringList genres;
    for (const Track& t : m_tracks) {
        const QString g = t.genre.isEmpty() ? "Unknown Genre" : t.genre;
        if (!seen.contains(g.toLower())) {
            seen.insert(g.toLower());
            genres.append(g);
        }
    }
    std::sort(genres.begin(), genres.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    m_genreList->addItem(QString("All Genres (%1)").arg(genres.size()));
    for (const QString& g : genres) {
        auto* item = new QListWidgetItem(g);
        item->setData(Qt::UserRole, g);
        m_genreList->addItem(item);
    }
    m_genreList->setCurrentRow(0);
    m_genreList->blockSignals(false);
}

void ColumnBrowser::rebuildAlbumArtists() {
    m_artistList->blockSignals(true);
    m_artistList->clear();

    QSet<QString> seen;
    QStringList artists;
    for (const Track& t : m_tracks) {
        if (!m_selectedGenre.isEmpty()) {
            const QString g = t.genre.isEmpty() ? "Unknown Genre" : t.genre;
            if (g.compare(m_selectedGenre, Qt::CaseInsensitive) != 0) continue;
        }
        const QString aa = normalizedAlbumArtist(t);
        if (!seen.contains(aa.toLower())) {
            seen.insert(aa.toLower());
            artists.append(aa);
        }
    }
    std::sort(artists.begin(), artists.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    m_artistList->addItem(QString("All Artists (%1)").arg(artists.size()));
    for (const QString& a : artists) {
        auto* item = new QListWidgetItem(a);
        item->setData(Qt::UserRole, a);
        m_artistList->addItem(item);
    }
    m_artistList->setCurrentRow(0);
    m_artistList->blockSignals(false);
}

void ColumnBrowser::rebuildAlbums() {
    m_albumList->blockSignals(true);
    m_albumList->clear();

    QSet<QString> seen;
    QStringList albums;
    for (const Track& t : m_tracks) {
        if (!m_selectedGenre.isEmpty()) {
            const QString g = t.genre.isEmpty() ? "Unknown Genre" : t.genre;
            if (g.compare(m_selectedGenre, Qt::CaseInsensitive) != 0) continue;
        }
        if (!m_selectedAlbumArtist.isEmpty()) {
            if (normalizedAlbumArtist(t).compare(m_selectedAlbumArtist, Qt::CaseInsensitive) != 0) continue;
        }
        const QString alb = t.album.isEmpty() ? "Unknown Album" : t.album;
        if (!seen.contains(alb.toLower())) {
            seen.insert(alb.toLower());
            albums.append(alb);
        }
    }
    std::sort(albums.begin(), albums.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    m_albumList->addItem(QString("All Albums (%1)").arg(albums.size()));
    for (const QString& alb : albums) {
        auto* item = new QListWidgetItem(alb);
        item->setData(Qt::UserRole, alb);
        m_albumList->addItem(item);
    }
    m_albumList->setCurrentRow(0);
    m_albumList->blockSignals(false);
}

} // namespace honeycomb

#pragma once

#include "core/Track.h"
#include "ui/ArtCache.h"

#include <QAbstractListModel>
#include <QListView>
#include <QStyledItemDelegate>
#include <QVector>

namespace honeycomb {

class AlbumGridModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        AlbumTitleRole = Qt::UserRole + 1,
        AlbumArtistRole,
        YearRole,
        TrackCountRole,
        DiscCountRole,
        ArtHashRole,
        IsCompilationRole,
        AlbumTypeRole
    };

    explicit AlbumGridModel(ArtCache* artCache, QObject* parent = nullptr);

    void setAlbums(const QVector<Album>& albums);
    const Album* albumAt(int row) const;
    void setArtworkSize(int sizePx);
    int artworkSize() const { return m_artSize; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    ArtCache* m_artCache = nullptr;
    QVector<Album> m_albums;
    int m_artSize = 200;
};

class AlbumGridDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit AlbumGridDelegate(ArtCache* artCache, int* artSizePtr, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    ArtCache* m_artCache = nullptr;
    int* m_artSizePtr = nullptr;
};

class AlbumGridView : public QListView {
    Q_OBJECT
public:
    explicit AlbumGridView(ArtCache* artCache, QWidget* parent = nullptr);

    void setAlbums(const QVector<Album>& albums);
    void setArtworkSize(int sizePx);
    int artworkSize() const { return m_artSize; }
    void setAvailableAlbumTypes(const QStringList& types) { m_customAlbumTypes = types; }
    void setAvailablePlaylists(const QVector<QPair<QString, QString>>& playlists) { m_playlists = playlists; }

signals:
    void albumClicked(const honeycomb::Album& album);
    void albumActivated(const honeycomb::Album& album);
    void albumPlayNextRequested(const honeycomb::Album& album);
    void albumQueueRequested(const honeycomb::Album& album);
    void findArtistRequested(const QString& artist);
    void albumTypeChangeRequested(const honeycomb::Album& album, const QString& albumType);
    void editAlbumTagsRequested(const honeycomb::Album& album);
    void albumAddToPlaylistRequested(const honeycomb::Album& album, const QString& playlistId);
    void albumAddToNewPlaylistRequested(const honeycomb::Album& album);
    void artworkSizeChanged(int newSizePx);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    ArtCache* m_artCache = nullptr;
    AlbumGridModel* m_model = nullptr;
    int m_artSize = 200;
    QStringList m_customAlbumTypes;
    QVector<QPair<QString, QString>> m_playlists;
};

} // namespace honeycomb

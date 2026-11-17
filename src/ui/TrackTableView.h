#pragma once

#include "core/Track.h"

#include <QAbstractTableModel>
#include <QTableView>
#include <QVector>

namespace honeycomb {

class TrackTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TrackTableModel(QObject* parent = nullptr);

    void setTracks(const QVector<Track>& tracks);
    const QVector<Track>& tracks() const { return m_tracks; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QVector<Track> m_tracks;
};

class TrackTableView : public QTableView {
    Q_OBJECT
public:
    explicit TrackTableView(QWidget* parent = nullptr);

    void setTracks(const QVector<Track>& tracks);
    void updateTrackRating(int64_t trackId, int rating);
    void setAvailablePlaylists(const QVector<QPair<QString, QString>>& playlists) { m_playlists = playlists; }
    void setActiveStaticPlaylistId(const QString& playlistId) { m_activePlaylistId = playlistId; }
    void setInboxMode(bool inbox) { m_inboxMode = inbox; }
    QStringList selectedTrackPaths() const;

signals:
    void trackActivated(const QVector<honeycomb::Track>& queue, int startIndex);
    void trackPlayNextRequested(const honeycomb::Track& track);
    void trackQueueRequested(const honeycomb::Track& track);
    void trackRatingChanged(int64_t trackId, int rating);
    void findArtistRequested(const QString& artist);
    void findTrackRequested(const QString& title);
    void editTagsRequested(const QVector<honeycomb::Track>& tracks);
    void autoTagTracksRequested(const QVector<honeycomb::Track>& tracks);
    void addToPlaylistRequested(const QString& playlistId, const QStringList& trackPaths);
    void addToNewPlaylistRequested(const QStringList& trackPaths);
    void removeFromPlaylistRequested(const QString& playlistId, int row);
    void movePlaylistTrackRequested(const QString& playlistId, int fromRow, int toRow);
    void promoteFromInboxRequested(const QStringList& trackPaths);
    void removeFromInboxRequested(const QStringList& trackPaths);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    TrackTableModel* m_model = nullptr;
    QVector<QPair<QString, QString>> m_playlists;
    QString m_activePlaylistId;
    bool m_inboxMode = false;
};

} // namespace honeycomb

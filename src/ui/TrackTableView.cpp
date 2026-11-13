#include "TrackTableView.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>

namespace honeycomb {

namespace {

QString formatDuration(int64_t ms) {
    const int64_t secs = ms / 1000;
    return QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
}

} // namespace

TrackTableModel::TrackTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void TrackTableModel::setTracks(const QVector<Track>& tracks) {
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

int TrackTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_tracks.size();
}

int TrackTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 9;
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) return {};
    if (role != Qt::DisplayRole) return {};

    const Track& t = m_tracks[index.row()];
    switch (index.column()) {
    case 0:
        if (t.discNumber > 1 && t.trackNumber > 0)
            return QString("%1-%2").arg(t.discNumber).arg(t.trackNumber, 2, 10, QChar('0'));
        return t.trackNumber > 0 ? QString::number(t.trackNumber) : "";
    case 1: return t.title;
    case 2: return t.artist;
    case 3: return t.album;
    case 4: return t.albumArtist;
    case 5: return t.genre;
    case 6: return t.year > 0 ? QString::number(t.year) : "";
    case 7: return t.rating > 0 ? QString("★").repeated(t.rating) : "";
    case 8: return formatDuration(t.durationMs);
    default: return {};
    }
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case 0: return "#";
    case 1: return "Title";
    case 2: return "Artist";
    case 3: return "Album";
    case 4: return "Album Artist";
    case 5: return "Genre";
    case 6: return "Year";
    case 7: return "Rating";
    case 8: return "Time";
    default: return {};
    }
}

TrackTableView::TrackTableView(QWidget* parent) : QTableView(parent) {
    m_model = new TrackTableModel(this);
    setModel(m_model);

    verticalHeader()->setVisible(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setShowGrid(false);
    setAlternatingRowColors(true);

    horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive);
    horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);

    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (idx.isValid()) {
            emit trackActivated(m_model->tracks(), idx.row());
        }
    });
}

void TrackTableView::setTracks(const QVector<Track>& tracks) {
    m_model->setTracks(tracks);
}

void TrackTableView::updateTrackRating(int64_t trackId, int rating) {
    auto tracks = m_model->tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == trackId) {
            tracks[i].rating = rating;
            m_model->setTracks(tracks);
            break;
        }
    }
}

void TrackTableView::contextMenuEvent(QContextMenuEvent* event) {
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid()) return;
    const Track& t = m_model->tracks().at(idx.row());

    QMenu menu(this);
    QAction* playAct = menu.addAction("Play Track");
    QAction* playNextAct = menu.addAction("Play Next");
    QAction* queueAct = menu.addAction("Add to Queue");
    QMenu* rateMenu = menu.addMenu("Set Rating");
    QAction* rateActs[6]{};
    rateActs[0] = rateMenu->addAction("Clear Rating");
    for (int r = 1; r <= 5; ++r) {
        rateActs[r] = rateMenu->addAction(QString("★").repeated(r));
    }
    menu.addSeparator();
    QAction* findArtistAct = !t.artist.isEmpty() ? menu.addAction(QString("Find Artist: %1").arg(t.artist)) : nullptr;
    QAction* findTrackAct = !t.title.isEmpty() ? menu.addAction(QString("Find Track: %1").arg(t.title)) : nullptr;

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == playAct) {
        emit trackActivated(m_model->tracks(), idx.row());
    } else if (chosen == playNextAct) {
        emit trackPlayNextRequested(t);
    } else if (chosen == queueAct) {
        emit trackQueueRequested(t);
    } else if (findArtistAct && chosen == findArtistAct) {
        emit findArtistRequested(t.artist);
    } else if (findTrackAct && chosen == findTrackAct) {
        emit findTrackRequested(t.title);
    } else {
        for (int r = 0; r <= 5; ++r) {
            if (chosen == rateActs[r]) {
                emit trackRatingChanged(t.id, r);
                updateTrackRating(t.id, r);
                break;
            }
        }
    }
}

} // namespace honeycomb

#include "AlbumGridView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

namespace honeycomb {

AlbumGridModel::AlbumGridModel(ArtCache* artCache, QObject* parent)
    : QAbstractListModel(parent), m_artCache(artCache) {
    connect(m_artCache, &ArtCache::artworkLoaded, this, [this](const QString&, int) {
        if (!m_albums.isEmpty()) {
            emit dataChanged(index(0, 0), index(m_albums.size() - 1, 0), {Qt::DecorationRole});
        }
    });
}

void AlbumGridModel::setAlbums(const QVector<Album>& albums) {
    beginResetModel();
    m_albums = albums;
    endResetModel();
}

const Album* AlbumGridModel::albumAt(int row) const {
    if (row < 0 || row >= m_albums.size()) return nullptr;
    return &m_albums[row];
}

void AlbumGridModel::setArtworkSize(int sizePx) {
    m_artSize = sizePx;
    if (!m_albums.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_albums.size() - 1, 0));
    }
}

int AlbumGridModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_albums.size();
}

QVariant AlbumGridModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_albums.size()) return {};
    const Album& a = m_albums[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case AlbumTitleRole: return a.album;
    case AlbumArtistRole: return a.albumArtist;
    case YearRole: return a.year;
    case TrackCountRole: return a.trackCount;
    case DiscCountRole: return a.discCount;
    case ArtHashRole: return a.artHash;
    case IsCompilationRole: return a.isCompilation;
    case Qt::DecorationRole: return m_artCache->getArtwork(a.artHash, m_artSize);
    default: return {};
    }
}

AlbumGridDelegate::AlbumGridDelegate(ArtCache* artCache, int* artSizePtr, QObject* parent)
    : QStyledItemDelegate(parent), m_artCache(artCache), m_artSizePtr(artSizePtr) {}

QSize AlbumGridDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    const int art = *m_artSizePtr;
    return QSize(art + 20, art + 58);
}

void AlbumGridDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const int artSize = *m_artSizePtr;
    const QRect cardRect = option.rect.adjusted(4, 4, -4, -4);

    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool selected = option.state & QStyle::State_Selected;

    if (selected) {
        painter->setBrush(QColor("#2c2920"));
        painter->setPen(QPen(QColor("#f5a623"), 1.5));
        painter->drawRoundedRect(cardRect, 10, 10);
    } else if (hovered) {
        painter->setBrush(QColor("#1e2028"));
        painter->setPen(QPen(QColor("#343746"), 1.0));
        painter->drawRoundedRect(cardRect, 10, 10);
    }

    const QRect artRect(cardRect.left() + (cardRect.width() - artSize) / 2,
                        cardRect.top() + 6,
                        artSize,
                        artSize);

    const QString artHash = index.data(AlbumGridModel::ArtHashRole).toString();
    const QPixmap pm = m_artCache->getArtwork(artHash, artSize);

    QPainterPath clipPath;
    clipPath.addRoundedRect(artRect, 8, 8);
    painter->save();
    painter->setClipPath(clipPath);
    painter->drawPixmap(artRect, pm);
    painter->restore();

    // Subtle border around artwork
    painter->setPen(QPen(QColor(255, 255, 255, 22), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(artRect, 8, 8);

    const int discCount = index.data(AlbumGridModel::DiscCountRole).toInt();
    if (discCount > 1) {
        const QString badge = QString("%1 CDs").arg(discCount);
        QFont bFont = option.font;
        bFont.setPointSizeF(bFont.pointSizeF() * 0.78);
        bFont.setBold(true);
        painter->setFont(bFont);
        const int bw = QFontMetrics(bFont).horizontalAdvance(badge) + 12;
        const QRect bRect(artRect.right() - bw - 6, artRect.top() + 6, bw, 18);
        painter->setBrush(QColor(16, 17, 22, 210));
        painter->setPen(QPen(QColor("#f5a623"), 1));
        painter->drawRoundedRect(bRect, 5, 5);
        painter->setPen(QColor("#f5a623"));
        painter->drawText(bRect, Qt::AlignCenter, badge);
    }

    // Title
    const QString title = index.data(AlbumGridModel::AlbumTitleRole).toString();
    const QString artist = index.data(AlbumGridModel::AlbumArtistRole).toString();
    const int year = index.data(AlbumGridModel::YearRole).toInt();
    const QString sub = year > 0 ? QString("%1 • %2").arg(artist).arg(year) : artist;

    QFont titleFont = option.font;
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 0.95);
    painter->setFont(titleFont);
    painter->setPen(QColor("#f0f1f5"));

    const QRect titleRect(artRect.left(), artRect.bottom() + 6, artSize, 18);
    const QString elidedTitle = QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, titleRect.width());
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

    QFont subFont = option.font;
    subFont.setPointSizeF(subFont.pointSizeF() * 0.85);
    painter->setFont(subFont);
    painter->setPen(QColor("#9aa0b0"));

    const QRect subRect(artRect.left(), titleRect.bottom() + 1, artSize, 16);
    const QString elidedSub = QFontMetrics(subFont).elidedText(sub, Qt::ElideRight, subRect.width());
    painter->drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter, elidedSub);

    painter->restore();
}

AlbumGridView::AlbumGridView(ArtCache* artCache, QWidget* parent)
    : QListView(parent), m_artCache(artCache) {
    m_model = new AlbumGridModel(artCache, this);
    setModel(m_model);
    setItemDelegate(new AlbumGridDelegate(artCache, &m_artSize, this));

    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setMouseTracking(true);
    setSpacing(8);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    connect(this, &QListView::clicked, this, [this](const QModelIndex& idx) {
        if (const Album* a = m_model->albumAt(idx.row())) {
            emit albumClicked(*a);
        }
    });

    connect(this, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (const Album* a = m_model->albumAt(idx.row())) {
            emit albumActivated(*a);
        }
    });
}

void AlbumGridView::setAlbums(const QVector<Album>& albums) {
    m_model->setAlbums(albums);
}

void AlbumGridView::setArtworkSize(int sizePx) {
    const int clamped = qBound(120, sizePx, 360);
    if (m_artSize == clamped) return;
    m_artSize = clamped;
    m_model->setArtworkSize(clamped);
    scheduleDelayedItemsLayout();
    emit artworkSizeChanged(clamped);
}

void AlbumGridView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y() > 0 ? 20 : -20;
        setArtworkSize(m_artSize + delta);
        event->accept();
        return;
    }
    QListView::wheelEvent(event);
}

void AlbumGridView::contextMenuEvent(QContextMenuEvent* event) {
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid()) return;
    const Album* a = m_model->albumAt(idx.row());
    if (!a) return;

    QMenu menu(this);
    QAction* playAct = menu.addAction("Play Album");
    QAction* playNextAct = menu.addAction("Play Album Next");
    QAction* queueAct = menu.addAction("Add Album to Queue");
    QAction* findArtistAct = nullptr;
    if (!a->albumArtist.isEmpty() && a->albumArtist != "Various Artists") {
        menu.addSeparator();
        findArtistAct = menu.addAction(QString("Find Artist: %1").arg(a->albumArtist));
    }
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == playAct) {
        emit albumActivated(*a);
    } else if (chosen == playNextAct) {
        emit albumPlayNextRequested(*a);
    } else if (chosen == queueAct) {
        emit albumQueueRequested(*a);
    } else if (findArtistAct && chosen == findArtistAct) {
        emit findArtistRequested(a->albumArtist);
    }
}

} // namespace honeycomb

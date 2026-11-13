#include "AlbumTracksView.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QVBoxLayout>

namespace honeycomb {

namespace {

QString formatMs(int64_t ms) {
    const int64_t secs = ms / 1000;
    const int64_t m = secs / 60;
    const int64_t s = secs % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

} // namespace

AlbumTracksView::AlbumTracksView(ArtCache* artCache, QWidget* parent)
    : QWidget(parent), m_artCache(artCache) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(m_splitter);

    m_albumList = new QListWidget(m_splitter);
    m_albumList->setIconSize(QSize(56, 56));
    m_albumList->setSpacing(2);
    m_albumList->setMinimumWidth(260);
    m_splitter->addWidget(m_albumList);

    auto* rightPane = new QWidget(m_splitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(12, 8, 8, 8);
    rightLayout->setSpacing(10);

    auto* headerRow = new QHBoxLayout();
    m_headerArt = new QLabel(rightPane);
    m_headerArt->setFixedSize(110, 110);
    m_headerArt->setScaledContents(true);

    auto* headerText = new QVBoxLayout();
    m_headerTitle = new QLabel("Select an Album", rightPane);
    QFont tf = m_headerTitle->font();
    tf.setPointSize(tf.pointSize() + 5);
    tf.setBold(true);
    m_headerTitle->setFont(tf);

    m_headerSub = new QLabel("", rightPane);
    m_headerSub->setStyleSheet("color: #9aa0b0;");

    headerText->addStretch();
    headerText->addWidget(m_headerTitle);
    headerText->addWidget(m_headerSub);
    headerText->addStretch();

    headerRow->addWidget(m_headerArt);
    headerRow->addSpacing(12);
    headerRow->addLayout(headerText, 1);
    rightLayout->addLayout(headerRow);

    m_trackTable = new QTableWidget(0, 5, rightPane);
    m_trackTable->setHorizontalHeaderLabels({"#", "Title", "Artist", "Format", "Time"});
    m_trackTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_trackTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_trackTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_trackTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_trackTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_trackTable->verticalHeader()->setVisible(false);
    m_trackTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trackTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trackTable->setShowGrid(false);
    m_trackTable->setAlternatingRowColors(true);
    m_trackTable->setContextMenuPolicy(Qt::CustomContextMenu);
    rightLayout->addWidget(m_trackTable, 1);

    m_splitter->addWidget(rightPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);

    connect(m_albumList, &QListWidget::currentRowChanged, this, &AlbumTracksView::showTracksForAlbum);
    connect(m_albumList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (!m_currentAlbumTracks.isEmpty()) {
            emit trackActivated(m_currentAlbumTracks, 0);
        }
    });

    connect(m_trackTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_rowToTrackIndex.size()) {
            const int idx = m_rowToTrackIndex[row];
            if (idx >= 0 && idx < m_currentAlbumTracks.size()) {
                emit trackActivated(m_currentAlbumTracks, idx);
            }
        }
    });

    connect(m_trackTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = m_trackTable->rowAt(pos.y());
        if (row < 0 || row >= m_rowToTrackIndex.size()) return;
        const int idx = m_rowToTrackIndex[row];
        if (idx < 0 || idx >= m_currentAlbumTracks.size()) return;
        const Track& t = m_currentAlbumTracks[idx];
        QMenu menu(this);
        QAction* playAct = menu.addAction("Play Track");
        QAction* playNextAct = menu.addAction("Play Next");
        QAction* queueAct = menu.addAction("Add to Queue");
        menu.addSeparator();
        QAction* findArtistAct = !t.artist.isEmpty() ? menu.addAction(QString("Find Artist: %1").arg(t.artist)) : nullptr;
        QAction* findTrackAct = !t.title.isEmpty() ? menu.addAction(QString("Find Track: %1").arg(t.title)) : nullptr;
        QAction* chosen = menu.exec(m_trackTable->viewport()->mapToGlobal(pos));
        if (chosen == playAct) {
            emit trackActivated(m_currentAlbumTracks, idx);
        } else if (chosen == playNextAct) {
            emit trackPlayNextRequested(t);
        } else if (chosen == queueAct) {
            emit trackQueueRequested(t);
        } else if (findArtistAct && chosen == findArtistAct) {
            emit findArtistRequested(t.artist);
        } else if (findTrackAct && chosen == findTrackAct) {
            emit findTrackRequested(t.title);
        }
    });
}

void AlbumTracksView::setData(const QVector<Album>& albums, const QVector<Track>& allTracks) {
    m_albums = albums;
    m_allTracks = allTracks;
    m_albumList->clear();

    for (const Album& a : m_albums) {
        const QString label = a.year > 0
            ? QString("%1\n%2 (%3)").arg(a.album, a.albumArtist).arg(a.year)
            : QString("%1\n%2").arg(a.album, a.albumArtist);
        auto* item = new QListWidgetItem(QIcon(m_artCache->getArtwork(a.artHash, 56)), label);
        m_albumList->addItem(item);
    }

    if (!m_albums.isEmpty()) {
        m_albumList->setCurrentRow(0);
    } else {
        m_currentAlbumTracks.clear();
        m_trackTable->setRowCount(0);
        m_headerTitle->setText("No Albums");
        m_headerSub->setText("");
        m_headerArt->clear();
    }
}

void AlbumTracksView::showTracksForAlbum(int albumRow) {
    if (albumRow < 0 || albumRow >= m_albums.size()) return;
    const Album& a = m_albums[albumRow];

    m_headerArt->setPixmap(m_artCache->getArtwork(a.artHash, 110));
    m_headerTitle->setText(a.album);
    const QString yearStr = a.year > 0 ? QString::number(a.year) : "";
    const QString discStr = a.discCount > 1 ? QString("  •  %1 discs").arg(a.discCount) : "";
    m_headerSub->setText(QString("%1  •  %2 tracks%3%4")
                             .arg(a.albumArtist)
                             .arg(a.trackCount)
                             .arg(discStr)
                             .arg(yearStr.isEmpty() ? "" : "  •  " + yearStr));

    m_currentAlbumTracks.clear();
    for (const Track& t : m_allTracks) {
        const QString tAlbum = t.album.isEmpty() ? "Unknown Album" : t.album;
        if (tAlbum.compare(a.album, Qt::CaseInsensitive) != 0) continue;

        if (a.isCompilation) {
            if (t.isCompilation || t.albumArtist.compare("Various Artists", Qt::CaseInsensitive) == 0) {
                m_currentAlbumTracks.append(t);
            }
        } else {
            const QString tArtist = !t.albumArtist.isEmpty() ? t.albumArtist
                                  : (!t.artist.isEmpty() ? t.artist : "Unknown Artist");
            if (tArtist.compare(a.albumArtist, Qt::CaseInsensitive) == 0) {
                m_currentAlbumTracks.append(t);
            }
        }
    }

    std::sort(m_currentAlbumTracks.begin(), m_currentAlbumTracks.end(), [](const Track& x, const Track& y) {
        if (x.discNumber != y.discNumber) return x.discNumber < y.discNumber;
        if (x.trackNumber != y.trackNumber) return x.trackNumber < y.trackNumber;
        return x.title.compare(y.title, Qt::CaseInsensitive) < 0;
    });

    bool multiDisc = a.discCount > 1;
    if (!multiDisc && !m_currentAlbumTracks.isEmpty()) {
        const int firstDisc = m_currentAlbumTracks.first().discNumber;
        for (const Track& t : m_currentAlbumTracks) {
            if (t.discNumber != firstDisc || t.discNumber > 1) {
                multiDisc = true;
                break;
            }
        }
    }

    m_trackTable->clearSpans();
    m_trackTable->setRowCount(0);
    m_rowToTrackIndex.clear();

    int lastDisc = -1;
    for (int i = 0; i < m_currentAlbumTracks.size(); ++i) {
        const Track& t = m_currentAlbumTracks[i];
        if (multiDisc && t.discNumber != lastDisc) {
            lastDisc = t.discNumber;
            const int headerRow = m_trackTable->rowCount();
            m_trackTable->insertRow(headerRow);
            m_rowToTrackIndex.append(-1);

            auto* hdr = new QTableWidgetItem(QString("Disc %1").arg(qMax(1, t.discNumber)));
            hdr->setFlags(Qt::ItemIsEnabled);
            hdr->setForeground(QColor("#f5a623"));
            hdr->setBackground(QColor("#1a1c24"));
            QFont hf = hdr->font();
            hf.setBold(true);
            hdr->setFont(hf);
            m_trackTable->setItem(headerRow, 0, hdr);
            m_trackTable->setSpan(headerRow, 0, 1, 5);
        }

        const int row = m_trackTable->rowCount();
        m_trackTable->insertRow(row);
        m_rowToTrackIndex.append(i);

        m_trackTable->setItem(row, 0, new QTableWidgetItem(t.trackNumber > 0 ? QString::number(t.trackNumber) : "-"));
        m_trackTable->setItem(row, 1, new QTableWidgetItem(t.title));
        m_trackTable->setItem(row, 2, new QTableWidgetItem(t.artist));
        m_trackTable->setItem(row, 3, new QTableWidgetItem(t.format));
        m_trackTable->setItem(row, 4, new QTableWidgetItem(formatMs(t.durationMs)));
    }
}

} // namespace honeycomb

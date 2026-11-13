#include "MainWindow.h"
#include "AppIcon.h"
#include "RuleBuilderDialog.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QRandomGenerator>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace honeycomb {

namespace {

QString formatSec(double secs) {
    if (secs < 0) secs = 0;
    const int s = static_cast<int>(secs);
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString formatMs(int64_t ms) {
    const int64_t secs = ms / 1000;
    return QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Honeycomb");
    resize(1420, 880);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/art";
    QDir().mkpath(dataDir);
    QDir().mkpath(cacheDir);

    m_db.open(dataDir + "/honeycomb.db");
    m_artCache = new ArtCache(cacheDir, this);
    m_scanner = new LibraryScanner(&m_db, cacheDir, this);
    m_audio = new AudioEngine(this);

    setupUi();
    setupTrayAndMpris(cacheDir);
    loadCollectionsSidebar();

    if (!m_collections.isEmpty()) {
        activateCollection(m_collections.first().id);
    }

    // Scanner signals
    connect(m_scanner, &LibraryScanner::scanStarted, this, [this]() {
        m_scanStatusLabel->setText("Scanning library...");
    });
    connect(m_scanner, &LibraryScanner::scanProgress, this, [this](int count, const QString& file) {
        m_scanStatusLabel->setText(QString("Scanned %1: %2").arg(count).arg(file));
    });
    connect(m_scanner, &LibraryScanner::scanFinished, this, [this](int total) {
        m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
        Q_UNUSED(total);
        refreshCurrentView(true);
    });

    // Audio signals
    connect(m_audio, &AudioEngine::positionChanged, this, [this](double pos) {
        if (!m_userSeeking && m_currentDurationSec > 0.0) {
            m_seekSlider->blockSignals(true);
            m_seekSlider->setValue(static_cast<int>((pos / m_currentDurationSec) * 1000.0));
            m_seekSlider->blockSignals(false);
        }
        m_timeLabel->setText(QString("%1 / %2").arg(formatSec(pos), formatSec(m_currentDurationSec)));
        if (m_mpris) m_mpris->setPositionSeconds(pos);
    });
    connect(m_audio, &AudioEngine::durationChanged, this, [this](double dur) {
        m_currentDurationSec = dur;
    });
    connect(m_audio, &AudioEngine::stateChanged, this, [this](AudioEngine::State st) {
        m_playPauseBtn->setText(st == AudioEngine::State::Playing ? "⏸" : "▶");
        if (m_mpris) m_mpris->setPlaybackState(st);
    });
    connect(m_audio, &AudioEngine::trackFinished, this, &MainWindow::playNext);

    m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, central);
    rootLayout->addWidget(mainSplitter, 1);

    // --- LEFT SIDEBAR ---
    auto* leftPane = new QWidget(mainSplitter);
    leftPane->setMinimumWidth(220);
    leftPane->setMaximumWidth(300);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(10, 10, 8, 10);
    leftLayout->setSpacing(8);

    auto* brandLabel = new QLabel("HONEYCOMB", leftPane);
    brandLabel->setStyleSheet("font-size: 15px; font-weight: 800; letter-spacing: 2px; color: #f5a623;");
    leftLayout->addWidget(brandLabel);

    m_sidebarList = new QListWidget(leftPane);
    m_sidebarList->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLayout->addWidget(m_sidebarList, 1);

    auto* newColBtn = new QPushButton("+ New Collection / Smart Playlist", leftPane);
    auto* addFolderBtn = new QPushButton("+ Add Music Folder", leftPane);
    auto* rescanBtn = new QPushButton("Rescan Library", leftPane);
    leftLayout->addWidget(newColBtn);
    leftLayout->addWidget(addFolderBtn);
    leftLayout->addWidget(rescanBtn);

    m_scanStatusLabel = new QLabel("0 tracks", leftPane);
    m_scanStatusLabel->setStyleSheet("color: #8c92a4; font-size: 11px;");
    m_scanStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_scanStatusLabel);

    mainSplitter->addWidget(leftPane);

    // --- CENTER MAIN PANEL ---
    auto* centerPane = new QWidget(mainSplitter);
    auto* centerLayout = new QVBoxLayout(centerPane);
    centerLayout->setContentsMargins(8, 10, 8, 8);
    centerLayout->setSpacing(8);

    auto* topBar = new QHBoxLayout();
    m_searchEdit = new QLineEdit(centerPane);
    m_searchEdit->setPlaceholderText("Search albums, artists, genres, titles...");
    m_searchEdit->setClearButtonEnabled(true);

    m_colBrowserBtn = new QPushButton("Browser", centerPane);
    m_colBrowserBtn->setCheckable(true);
    m_colBrowserBtn->setToolTip("Toggle Column Browser (Genre → Album Artist → Album)");

    m_viewModeCombo = new QComboBox(centerPane);
    m_viewModeCombo->addItem("Album Covers", static_cast<int>(ViewMode::AlbumCovers));
    m_viewModeCombo->addItem("Album & Tracks", static_cast<int>(ViewMode::AlbumAndTracks));
    m_viewModeCombo->addItem("Tracks Table", static_cast<int>(ViewMode::TracksTable));

    m_sortCombo = new QComboBox(centerPane);
    m_sortCombo->addItem("Sort: Artist / Year", static_cast<int>(SortOrder::AlbumArtistYear));
    m_sortCombo->addItem("Sort: Album", static_cast<int>(SortOrder::AlbumTitle));
    m_sortCombo->addItem("Sort: Year (Newest)", static_cast<int>(SortOrder::YearDesc));
    m_sortCombo->addItem("Sort: Recently Added", static_cast<int>(SortOrder::RecentlyAdded));
    m_sortCombo->addItem("Sort: Track Title", static_cast<int>(SortOrder::TrackTitle));

    m_artZoomSlider = new QSlider(Qt::Horizontal, centerPane);
    m_artZoomSlider->setRange(120, 340);
    m_artZoomSlider->setValue(200);
    m_artZoomSlider->setFixedWidth(100);
    m_artZoomSlider->setToolTip("Artwork Size (or Ctrl+Wheel)");

    m_editColBtn = new QPushButton("Edit Rules", centerPane);

    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_colBrowserBtn);
    topBar->addWidget(m_viewModeCombo);
    topBar->addWidget(m_sortCombo);
    topBar->addWidget(m_artZoomSlider);
    topBar->addWidget(m_editColBtn);
    centerLayout->addLayout(topBar);

    // Column Browser (collapsible per collection)
    m_colBrowser = new ColumnBrowser(centerPane);
    m_colBrowser->setVisible(false);
    centerLayout->addWidget(m_colBrowser);

    // Center splitter for Main View + Inline Album Track Drawer
    auto* centerSplitter = new QSplitter(Qt::Vertical, centerPane);
    centerLayout->addWidget(centerSplitter, 1);

    m_viewStack = new QStackedWidget(centerSplitter);
    m_albumGrid = new AlbumGridView(m_artCache, m_viewStack);
    m_albumTracks = new AlbumTracksView(m_artCache, m_viewStack);
    m_trackTable = new TrackTableView(m_viewStack);
    m_viewStack->addWidget(m_albumGrid);
    m_viewStack->addWidget(m_albumTracks);
    m_viewStack->addWidget(m_trackTable);
    centerSplitter->addWidget(m_viewStack);

    // Inline Album Track Drawer (shown when clicking an album in AlbumCovers)
    m_albumDrawer = new QWidget(centerSplitter);
    m_albumDrawer->setVisible(false);
    m_albumDrawer->setMinimumHeight(210);
    m_albumDrawer->setStyleSheet("QWidget#albumDrawer { background: #181a22; border-top: 2px solid #f5a623; }");
    m_albumDrawer->setObjectName("albumDrawer");
    auto* drawerLayout = new QVBoxLayout(m_albumDrawer);
    drawerLayout->setContentsMargins(10, 8, 10, 8);
    drawerLayout->setSpacing(6);

    auto* dHdr = new QHBoxLayout();
    m_drawerArt = new QLabel(m_albumDrawer);
    m_drawerArt->setFixedSize(56, 56);
    m_drawerArt->setScaledContents(true);

    auto* dText = new QVBoxLayout();
    m_drawerTitle = new QLabel("", m_albumDrawer);
    m_drawerTitle->setStyleSheet("font-size: 14px; font-weight: 700;");
    m_drawerSub = new QLabel("", m_albumDrawer);
    m_drawerSub->setStyleSheet("color: #9aa0b0; font-size: 12px;");
    dText->addWidget(m_drawerTitle);
    dText->addWidget(m_drawerSub);

    auto* dPlayBtn = new QPushButton("▶ Play Album", m_albumDrawer);
    auto* dQueueBtn = new QPushButton("+ Queue", m_albumDrawer);
    auto* dCloseBtn = new QPushButton("×", m_albumDrawer);
    dCloseBtn->setFixedWidth(28);

    dHdr->addWidget(m_drawerArt);
    dHdr->addSpacing(8);
    dHdr->addLayout(dText, 1);
    dHdr->addWidget(dPlayBtn);
    dHdr->addWidget(dQueueBtn);
    dHdr->addWidget(dCloseBtn);
    drawerLayout->addLayout(dHdr);

    m_drawerTable = new QTableWidget(0, 5, m_albumDrawer);
    m_drawerTable->setHorizontalHeaderLabels({"#", "Title", "Artist", "Format", "Time"});
    m_drawerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_drawerTable->verticalHeader()->setVisible(false);
    m_drawerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_drawerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_drawerTable->setShowGrid(false);
    m_drawerTable->setAlternatingRowColors(true);
    m_drawerTable->setContextMenuPolicy(Qt::CustomContextMenu);
    drawerLayout->addWidget(m_drawerTable, 1);

    centerSplitter->addWidget(m_albumDrawer);
    centerSplitter->setStretchFactor(0, 3);
    centerSplitter->setStretchFactor(1, 2);

    mainSplitter->addWidget(centerPane);

    // --- RIGHT SIDEBAR (NOW PLAYING + QUEUE) ---
    auto* rightPane = new QWidget(mainSplitter);
    rightPane->setMinimumWidth(260);
    rightPane->setMaximumWidth(360);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(8, 10, 10, 10);
    rightLayout->setSpacing(6);

    auto* npHeader = new QLabel("NOW PLAYING", rightPane);
    npHeader->setStyleSheet("font-size: 11px; font-weight: 700; letter-spacing: 1.5px; color: #8c92a4;");
    rightLayout->addWidget(npHeader);

    m_npArtLabel = new QLabel(rightPane);
    m_npArtLabel->setFixedSize(260, 260);
    m_npArtLabel->setAlignment(Qt::AlignCenter);
    m_npArtLabel->setStyleSheet("background: #1b1c22; border-radius: 8px;");
    m_npArtLabel->setPixmap(m_artCache->getFullArtwork("", 260));
    rightLayout->addWidget(m_npArtLabel, 0, Qt::AlignHCenter);

    m_npTitleLabel = new QLabel("Not Playing", rightPane);
    m_npTitleLabel->setStyleSheet("font-size: 15px; font-weight: 700;");
    m_npTitleLabel->setWordWrap(true);
    m_npArtistLabel = new QLabel("", rightPane);
    m_npArtistLabel->setStyleSheet("color: #c4c8d4;");
    m_npAlbumLabel = new QLabel("", rightPane);
    m_npAlbumLabel->setStyleSheet("color: #8c92a4; font-size: 12px;");
    m_npFormatLabel = new QLabel("", rightPane);
    m_npFormatLabel->setStyleSheet("color: #f5a623; font-size: 11px; font-weight: 600;");
    m_npRating = new StarRatingWidget(rightPane);

    rightLayout->addWidget(m_npTitleLabel);
    rightLayout->addWidget(m_npArtistLabel);
    rightLayout->addWidget(m_npAlbumLabel);
    rightLayout->addWidget(m_npFormatLabel);
    rightLayout->addWidget(m_npRating);

    auto* qHeaderRow = new QHBoxLayout();
    auto* qHeader = new QLabel("PLAYING QUEUE", rightPane);
    qHeader->setStyleSheet("font-size: 11px; font-weight: 700; letter-spacing: 1.5px; color: #8c92a4;");
    m_autoDjBtn = new QPushButton("Auto-DJ", rightPane);
    m_autoDjBtn->setCheckable(true);
    m_autoDjBtn->setToolTip("Automatically queue tracks from the active collection when queue ends");
    auto* clearQBtn = new QPushButton("Clear", rightPane);
    clearQBtn->setFlat(true);
    qHeaderRow->addWidget(qHeader);
    qHeaderRow->addStretch();
    qHeaderRow->addWidget(m_autoDjBtn);
    qHeaderRow->addWidget(clearQBtn);
    rightLayout->addLayout(qHeaderRow);

    m_queueList = new QListWidget(rightPane);
    m_queueList->setContextMenuPolicy(Qt::CustomContextMenu);
    rightLayout->addWidget(m_queueList, 1);

    mainSplitter->addWidget(rightPane);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);

    // --- BOTTOM TRANSPORT BAR ---
    auto* transportBar = new QWidget(central);
    transportBar->setFixedHeight(64);
    transportBar->setStyleSheet("background: #101116; border-top: 1px solid #262833;");
    auto* tLayout = new QHBoxLayout(transportBar);
    tLayout->setContentsMargins(14, 6, 14, 6);
    tLayout->setSpacing(10);

    auto* prevBtn = new QPushButton("⏮", transportBar);
    m_playPauseBtn = new QPushButton("▶", transportBar);
    m_playPauseBtn->setFixedSize(40, 36);
    m_playPauseBtn->setStyleSheet("background: #f5a623; color: #111; font-weight: bold; border-radius: 18px;");
    auto* nextBtn = new QPushButton("⏭", transportBar);
    m_shuffleBtn = new QPushButton("🔀", transportBar);
    m_shuffleBtn->setCheckable(true);
    m_repeatBtn = new QPushButton("🔁", transportBar);
    m_repeatBtn->setCheckable(true);

    m_seekSlider = new QSlider(Qt::Horizontal, transportBar);
    m_seekSlider->setRange(0, 1000);
    m_timeLabel = new QLabel("0:00 / 0:00", transportBar);
    m_timeLabel->setMinimumWidth(90);

    m_volumeSlider = new QSlider(Qt::Horizontal, transportBar);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(100);
    m_audio->setVolume(80);

    tLayout->addWidget(m_shuffleBtn);
    tLayout->addWidget(prevBtn);
    tLayout->addWidget(m_playPauseBtn);
    tLayout->addWidget(nextBtn);
    tLayout->addWidget(m_repeatBtn);
    tLayout->addWidget(m_seekSlider, 1);
    tLayout->addWidget(m_timeLabel);
    tLayout->addWidget(new QLabel("🔊", transportBar));
    tLayout->addWidget(m_volumeSlider);

    rootLayout->addWidget(transportBar);

    // --- CONNECTIONS ---
    connect(newColBtn, &QPushButton::clicked, this, &MainWindow::onNewCollection);
    connect(addFolderBtn, &QPushButton::clicked, this, &MainWindow::onAddMusicFolder);
    connect(rescanBtn, &QPushButton::clicked, this, [this]() {
        const QStringList folders = m_db.libraryFolders();
        if (folders.isEmpty()) onAddMusicFolder();
        else m_scanner->startScan(folders);
    });
    connect(m_editColBtn, &QPushButton::clicked, this, &MainWindow::onEditCurrentCollection);

    connect(m_sidebarList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty()) activateCollection(id);
    });

    connect(m_sidebarList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_sidebarList->itemAt(pos);
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;

        for (const auto& c : m_collections) {
            if (c.id == id && !c.isSystem) {
                QMenu menu(this);
                QAction* editAct = menu.addAction("Edit Collection / Rules...");
                QAction* delAct = menu.addAction("Delete Collection");
                QAction* chosen = menu.exec(m_sidebarList->viewport()->mapToGlobal(pos));
                if (chosen == editAct) {
                    activateCollection(id);
                    onEditCurrentCollection();
                } else if (chosen == delAct) {
                    m_db.deleteCollection(id);
                    loadCollectionsSidebar();
                    if (!m_collections.isEmpty()) activateCollection(m_collections.first().id);
                }
                break;
            }
        }
    });

    auto* searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(180);
    connect(m_searchEdit, &QLineEdit::textChanged, searchTimer, qOverload<>(&QTimer::start));
    connect(searchTimer, &QTimer::timeout, this, [this]() { refreshCurrentView(true); });

    connect(m_colBrowserBtn, &QPushButton::toggled, this, [this](bool on) {
        m_colBrowser->setVisible(on);
        if (!on) m_colBrowser->clearSelection();
        saveCurrentCollectionViewState();
        refreshCurrentView(on);
    });
    connect(m_colBrowser, &ColumnBrowser::filterChanged, this, [this]() {
        refreshCurrentView(false);
    });

    connect(m_viewModeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        saveCurrentCollectionViewState();
        refreshCurrentView(false);
    });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        saveCurrentCollectionViewState();
        refreshCurrentView(false);
    });
    connect(m_artZoomSlider, &QSlider::valueChanged, this, [this](int val) {
        m_albumGrid->setArtworkSize(val);
        saveCurrentCollectionViewState();
    });
    connect(m_albumGrid, &AlbumGridView::artworkSizeChanged, this, [this](int val) {
        m_artZoomSlider->blockSignals(true);
        m_artZoomSlider->setValue(val);
        m_artZoomSlider->blockSignals(false);
        saveCurrentCollectionViewState();
    });

    // Album Grid Click (Drawer) & Double-Click (Play)
    connect(m_albumGrid, &AlbumGridView::albumClicked, this, &MainWindow::openAlbumDrawer);
    connect(m_albumGrid, &AlbumGridView::albumActivated, this, [this](const Album& alb) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                playQueue(tracks, 0);
                break;
            }
        }
    });
    connect(m_albumGrid, &AlbumGridView::albumPlayNextRequested, this, [this](const Album& alb) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                playNextTracks(tracks);
                break;
            }
        }
    });
    connect(m_albumGrid, &AlbumGridView::albumQueueRequested, this, [this](const Album& alb) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                for (const auto& t : tracks) enqueueTrack(t);
                break;
            }
        }
    });
    connect(m_albumGrid, &AlbumGridView::findArtistRequested, this, &MainWindow::findArtist);

    // Drawer controls
    connect(dCloseBtn, &QPushButton::clicked, this, &MainWindow::closeAlbumDrawer);
    connect(dPlayBtn, &QPushButton::clicked, this, [this]() {
        if (!m_drawerTracks.isEmpty()) playQueue(m_drawerTracks, 0);
    });
    connect(dQueueBtn, &QPushButton::clicked, this, [this]() {
        for (const auto& t : m_drawerTracks) enqueueTrack(t);
    });
    connect(m_drawerTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_drawerRowToTrack.size()) {
            const int idx = m_drawerRowToTrack[row];
            if (idx >= 0 && idx < m_drawerTracks.size()) {
                playQueue(m_drawerTracks, idx);
            }
        }
    });
    connect(m_drawerTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = m_drawerTable->rowAt(pos.y());
        if (row < 0 || row >= m_drawerRowToTrack.size()) return;
        const int idx = m_drawerRowToTrack[row];
        if (idx < 0 || idx >= m_drawerTracks.size()) return;
        const Track& t = m_drawerTracks[idx];

        QMenu menu(this);
        QAction* playAct = menu.addAction("Play Track");
        QAction* playNextAct = menu.addAction("Play Next");
        QAction* queueAct = menu.addAction("Add to Queue");
        menu.addSeparator();
        QAction* findArtistAct = !t.artist.isEmpty() ? menu.addAction(QString("Find Artist: %1").arg(t.artist)) : nullptr;
        QAction* findTrackAct = !t.title.isEmpty() ? menu.addAction(QString("Find Track: %1").arg(t.title)) : nullptr;
        QAction* chosen = menu.exec(m_drawerTable->viewport()->mapToGlobal(pos));
        if (chosen == playAct) {
            playQueue(m_drawerTracks, idx);
        } else if (chosen == playNextAct) {
            playNextTrack(t);
        } else if (chosen == queueAct) {
            enqueueTrack(t);
        } else if (findArtistAct && chosen == findArtistAct) {
            findArtist(t.artist);
        } else if (findTrackAct && chosen == findTrackAct) {
            findTrack(t.title);
        }
    });

    connect(m_albumTracks, &AlbumTracksView::trackActivated, this, &MainWindow::playQueue);
    connect(m_albumTracks, &AlbumTracksView::trackPlayNextRequested, this, &MainWindow::playNextTrack);
    connect(m_albumTracks, &AlbumTracksView::trackQueueRequested, this, &MainWindow::enqueueTrack);
    connect(m_albumTracks, &AlbumTracksView::findArtistRequested, this, &MainWindow::findArtist);
    connect(m_albumTracks, &AlbumTracksView::findTrackRequested, this, &MainWindow::findTrack);

    connect(m_trackTable, &TrackTableView::trackActivated, this, &MainWindow::playQueue);
    connect(m_trackTable, &TrackTableView::trackPlayNextRequested, this, &MainWindow::playNextTrack);
    connect(m_trackTable, &TrackTableView::trackQueueRequested, this, &MainWindow::enqueueTrack);
    connect(m_trackTable, &TrackTableView::findArtistRequested, this, &MainWindow::findArtist);
    connect(m_trackTable, &TrackTableView::findTrackRequested, this, &MainWindow::findTrack);
    connect(m_trackTable, &TrackTableView::trackRatingChanged, this, [this](int64_t trackId, int rating) {
        m_db.setTrackRating(trackId, rating);
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size() && m_queue[m_queueIndex].id == trackId) {
            m_queue[m_queueIndex].rating = rating;
            m_npRating->setRating(rating);
        }
    });

    connect(m_npRating, &StarRatingWidget::ratingChanged, this, [this](int newRating) {
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
            m_queue[m_queueIndex].rating = newRating;
            m_db.setTrackRating(m_queue[m_queueIndex].id, newRating);
            m_trackTable->updateTrackRating(m_queue[m_queueIndex].id, newRating);
        }
    });

    connect(m_autoDjBtn, &QPushButton::toggled, this, [this](bool on) { m_autoDj = on; });

    connect(m_queueList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        playTrackAtIndex(m_queueList->row(item));
    });
    connect(m_queueList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = m_queueList->row(m_queueList->itemAt(pos));
        if (row < 0 || row >= m_queue.size()) return;
        const Track t = m_queue[row];

        QMenu menu(this);
        QAction* playAct = menu.addAction("Play Now");
        QAction* removeAct = menu.addAction("Remove from Queue");
        menu.addSeparator();
        QAction* findArtistAct = !t.artist.isEmpty() ? menu.addAction(QString("Find Artist: %1").arg(t.artist)) : nullptr;
        QAction* findTrackAct = !t.title.isEmpty() ? menu.addAction(QString("Find Track: %1").arg(t.title)) : nullptr;

        QAction* chosen = menu.exec(m_queueList->viewport()->mapToGlobal(pos));
        if (chosen == playAct) {
            playTrackAtIndex(row);
        } else if (chosen == removeAct) {
            m_queue.removeAt(row);
            if (row < m_queueIndex) {
                --m_queueIndex;
            } else if (row == m_queueIndex) {
                if (m_queue.isEmpty()) {
                    m_queueIndex = -1;
                    m_audio->stop();
                } else {
                    playTrackAtIndex(qMin(m_queueIndex, m_queue.size() - 1));
                    return;
                }
            }
            refreshQueueWidget();
        } else if (findArtistAct && chosen == findArtistAct) {
            findArtist(t.artist);
        } else if (findTrackAct && chosen == findTrackAct) {
            findTrack(t.title);
        }
    });
    connect(clearQBtn, &QPushButton::clicked, this, [this]() {
        m_queue.clear();
        m_queueIndex = -1;
        m_audio->stop();
        refreshQueueWidget();
    });

    connect(m_playPauseBtn, &QPushButton::clicked, m_audio, &AudioEngine::togglePause);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::playNext);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::playPrev);
    connect(m_shuffleBtn, &QPushButton::toggled, this, [this](bool on) { m_shuffle = on; });
    connect(m_repeatBtn, &QPushButton::toggled, this, [this](bool on) { m_repeat = on; });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        m_audio->setVolume(val);
        if (m_mpris) m_mpris->setVolume(val / 100.0);
    });

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_userSeeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_userSeeking = false;
        if (m_currentDurationSec > 0.0) {
            const double target = (m_seekSlider->value() / 1000.0) * m_currentDurationSec;
            m_audio->seekSeconds(target);
        }
    });
}

void MainWindow::loadCollectionsSidebar() {
    m_collections = m_db.loadCollections();
    m_sidebarList->clear();

    auto addHeader = [&](const QString& text) {
        auto* h = new QListWidgetItem(text);
        h->setFlags(Qt::NoItemFlags);
        h->setForeground(QColor("#788096"));
        QFont f = h->font();
        f.setPointSize(8);
        f.setBold(true);
        h->setFont(f);
        m_sidebarList->addItem(h);
    };

    addHeader("LIBRARY");
    for (const auto& c : m_collections) {
        if (c.isSystem) {
            auto* item = new QListWidgetItem("  " + c.name);
            item->setData(Qt::UserRole, c.id);
            m_sidebarList->addItem(item);
        }
    }

    addHeader("COLLECTIONS");
    for (const auto& c : m_collections) {
        if (!c.isSystem && !c.isSmartPlaylist) {
            auto* item = new QListWidgetItem("  ◆ " + c.name);
            item->setData(Qt::UserRole, c.id);
            m_sidebarList->addItem(item);
        }
    }

    addHeader("SMART PLAYLISTS");
    for (const auto& c : m_collections) {
        if (!c.isSystem && c.isSmartPlaylist) {
            auto* item = new QListWidgetItem("  ♫ " + c.name);
            item->setData(Qt::UserRole, c.id);
            m_sidebarList->addItem(item);
        }
    }
}

void MainWindow::activateCollection(const QString& id) {
    m_activeCollectionId = id;
    closeAlbumDrawer();

    for (int i = 0; i < m_sidebarList->count(); ++i) {
        auto* item = m_sidebarList->item(i);
        if (item->data(Qt::UserRole).toString() == id) {
            m_sidebarList->setCurrentItem(item);
            break;
        }
    }

    for (const auto& c : m_collections) {
        if (c.id == id) {
            m_viewModeCombo->blockSignals(true);
            m_sortCombo->blockSignals(true);
            m_artZoomSlider->blockSignals(true);
            m_colBrowserBtn->blockSignals(true);

            m_viewModeCombo->setCurrentIndex(static_cast<int>(c.viewMode));
            m_sortCombo->setCurrentIndex(static_cast<int>(c.sortOrder));
            m_artZoomSlider->setValue(c.artworkSize);
            m_albumGrid->setArtworkSize(c.artworkSize);
            m_colBrowserBtn->setChecked(c.showColumnBrowser);
            m_colBrowser->setVisible(c.showColumnBrowser);
            m_editColBtn->setEnabled(!c.isSystem);

            m_viewModeCombo->blockSignals(false);
            m_sortCombo->blockSignals(false);
            m_artZoomSlider->blockSignals(false);
            m_colBrowserBtn->blockSignals(false);
            break;
        }
    }

    refreshCurrentView(true);
}

void MainWindow::saveCurrentCollectionViewState() {
    for (auto& c : m_collections) {
        if (c.id == m_activeCollectionId) {
            c.viewMode = static_cast<ViewMode>(m_viewModeCombo->currentData().toInt());
            c.sortOrder = static_cast<SortOrder>(m_sortCombo->currentData().toInt());
            c.artworkSize = m_artZoomSlider->value();
            c.showColumnBrowser = m_colBrowserBtn->isChecked();
            m_db.saveCollection(c);
            break;
        }
    }
}

RuleGroup MainWindow::effectiveRules(const Collection& active) const {
    RuleGroup combined;
    combined.logic = RuleGroup::Logic::All;
    if (!active.rules.isEmpty()) {
        combined.subGroups.append(active.rules);
    }

    if (m_colBrowser->isVisible()) {
        const QString g = m_colBrowser->selectedGenre();
        if (!g.isEmpty()) {
            combined.conditions.append({FilterField::Genre, FilterOp::Is, g == "Unknown Genre" ? "" : g});
        }
        const QString aa = m_colBrowser->selectedAlbumArtist();
        if (!aa.isEmpty()) {
            if (aa == "Various Artists") {
                combined.conditions.append({FilterField::IsCompilation, FilterOp::IsTrue, ""});
            } else {
                RuleGroup artistOr;
                artistOr.logic = RuleGroup::Logic::Any;
                artistOr.conditions.append({FilterField::AlbumArtist, FilterOp::Is, aa});
                artistOr.conditions.append({FilterField::Artist, FilterOp::Is, aa});
                combined.subGroups.append(artistOr);
            }
        }
        const QString alb = m_colBrowser->selectedAlbum();
        if (!alb.isEmpty()) {
            combined.conditions.append({FilterField::Album, FilterOp::Is, alb == "Unknown Album" ? "" : alb});
        }
    }
    return combined;
}

void MainWindow::refreshCurrentView(bool resetColumnBrowser) {
    Collection active;
    bool found = false;
    for (const auto& c : m_collections) {
        if (c.id == m_activeCollectionId) {
            active = c;
            found = true;
            break;
        }
    }
    if (!found) return;

    const QString search = m_searchEdit->text();
    const auto mode = static_cast<ViewMode>(m_viewModeCombo->currentData().toInt());
    const auto sort = static_cast<SortOrder>(m_sortCombo->currentData().toInt());

    if (resetColumnBrowser && m_colBrowser->isVisible()) {
        m_colBrowser->setTracks(m_db.queryTracks(active.rules, search, sort));
    }

    const RuleGroup rules = effectiveRules(active);
    m_artZoomSlider->setVisible(mode == ViewMode::AlbumCovers);
    if (mode != ViewMode::AlbumCovers) {
        closeAlbumDrawer();
    }

    if (mode == ViewMode::AlbumCovers) {
        m_viewStack->setCurrentWidget(m_albumGrid);
        m_albumGrid->setAlbums(m_db.queryAlbums(rules, search, sort));
    } else if (mode == ViewMode::AlbumAndTracks) {
        m_viewStack->setCurrentWidget(m_albumTracks);
        const auto albums = m_db.queryAlbums(rules, search, sort);
        const auto tracks = m_db.queryTracks(rules, search, sort, QString(), QString(), active.limitCount, active.limitOrder);
        m_albumTracks->setData(albums, tracks);
    } else {
        m_viewStack->setCurrentWidget(m_trackTable);
        m_trackTable->setTracks(m_db.queryTracks(rules, search, sort, QString(), QString(), active.limitCount, active.limitOrder));
    }
}

void MainWindow::openAlbumDrawer(const Album& album) {
    Collection active;
    for (const auto& c : m_collections) {
        if (c.id == m_activeCollectionId) {
            active = c;
            break;
        }
    }

    m_drawerAlbum = album;
    m_drawerTracks = m_db.queryTracks(effectiveRules(active), m_searchEdit->text(),
                                      SortOrder::AlbumArtistYear, album.album, album.albumArtist);
    std::sort(m_drawerTracks.begin(), m_drawerTracks.end(), [](const Track& x, const Track& y) {
        if (x.discNumber != y.discNumber) return x.discNumber < y.discNumber;
        if (x.trackNumber != y.trackNumber) return x.trackNumber < y.trackNumber;
        return x.title.compare(y.title, Qt::CaseInsensitive) < 0;
    });

    m_drawerArt->setPixmap(m_artCache->getArtwork(album.artHash, 56));
    m_drawerTitle->setText(album.album);
    const QString discStr = album.discCount > 1 ? QString("  •  %1 discs").arg(album.discCount) : "";
    const QString yearStr = album.year > 0 ? QString("  •  %1").arg(album.year) : "";
    m_drawerSub->setText(QString("%1  •  %2 tracks%3%4").arg(album.albumArtist).arg(m_drawerTracks.size()).arg(discStr, yearStr));

    bool multiDisc = album.discCount > 1;
    if (!multiDisc && !m_drawerTracks.isEmpty()) {
        const int firstDisc = m_drawerTracks.first().discNumber;
        for (const Track& t : m_drawerTracks) {
            if (t.discNumber != firstDisc || t.discNumber > 1) {
                multiDisc = true;
                break;
            }
        }
    }

    m_drawerTable->clearSpans();
    m_drawerTable->setRowCount(0);
    m_drawerRowToTrack.clear();

    int lastDisc = -1;
    for (int i = 0; i < m_drawerTracks.size(); ++i) {
        const Track& t = m_drawerTracks[i];
        if (multiDisc && t.discNumber != lastDisc) {
            lastDisc = t.discNumber;
            const int hdrRow = m_drawerTable->rowCount();
            m_drawerTable->insertRow(hdrRow);
            m_drawerRowToTrack.append(-1);
            auto* hdr = new QTableWidgetItem(QString("Disc %1").arg(qMax(1, t.discNumber)));
            hdr->setFlags(Qt::ItemIsEnabled);
            hdr->setForeground(QColor("#f5a623"));
            hdr->setBackground(QColor("#1a1c24"));
            QFont hf = hdr->font();
            hf.setBold(true);
            hdr->setFont(hf);
            m_drawerTable->setItem(hdrRow, 0, hdr);
            m_drawerTable->setSpan(hdrRow, 0, 1, 5);
        }
        const int row = m_drawerTable->rowCount();
        m_drawerTable->insertRow(row);
        m_drawerRowToTrack.append(i);
        m_drawerTable->setItem(row, 0, new QTableWidgetItem(t.trackNumber > 0 ? QString::number(t.trackNumber) : "-"));
        m_drawerTable->setItem(row, 1, new QTableWidgetItem(t.title));
        m_drawerTable->setItem(row, 2, new QTableWidgetItem(t.artist));
        m_drawerTable->setItem(row, 3, new QTableWidgetItem(t.format));
        m_drawerTable->setItem(row, 4, new QTableWidgetItem(formatMs(t.durationMs)));
    }

    m_albumDrawer->setVisible(true);
}

void MainWindow::closeAlbumDrawer() {
    m_albumDrawer->setVisible(false);
}

void MainWindow::playQueue(const QVector<Track>& tracks, int startIndex) {
    if (tracks.isEmpty()) return;
    m_queue = tracks;
    playTrackAtIndex(qBound(0, startIndex, m_queue.size() - 1));
}

void MainWindow::playNextTrack(const Track& track) {
    if (m_queue.isEmpty() || m_queueIndex < 0) {
        enqueueTrack(track);
        return;
    }
    m_queue.insert(m_queueIndex + 1, track);
    refreshQueueWidget();
}

void MainWindow::playNextTracks(const QVector<Track>& tracks) {
    if (tracks.isEmpty()) return;
    if (m_queue.isEmpty() || m_queueIndex < 0) {
        playQueue(tracks, 0);
        return;
    }
    int insertPos = m_queueIndex + 1;
    for (const Track& t : tracks) {
        m_queue.insert(insertPos++, t);
    }
    refreshQueueWidget();
}

void MainWindow::enqueueTrack(const Track& track) {
    m_queue.append(track);
    refreshQueueWidget();
    if (m_queueIndex < 0) {
        playTrackAtIndex(0);
    }
}

void MainWindow::findArtist(const QString& artist) {
    if (artist.isEmpty()) return;
    closeAlbumDrawer();
    m_searchEdit->setText(artist);
    refreshCurrentView(true);
}

void MainWindow::findTrack(const QString& title) {
    if (title.isEmpty()) return;
    closeAlbumDrawer();
    m_searchEdit->setText(title);
    refreshCurrentView(true);
}

void MainWindow::setupTrayAndMpris(const QString& artCacheDir) {
    m_mpris = new MprisService(artCacheDir, this);
    connect(m_mpris, &MprisService::playRequested, this, [this]() { m_audio->setPaused(false); });
    connect(m_mpris, &MprisService::pauseRequested, this, [this]() { m_audio->setPaused(true); });
    connect(m_mpris, &MprisService::playPauseRequested, m_audio, &AudioEngine::togglePause);
    connect(m_mpris, &MprisService::stopRequested, m_audio, &AudioEngine::stop);
    connect(m_mpris, &MprisService::nextRequested, this, &MainWindow::playNext);
    connect(m_mpris, &MprisService::previousRequested, this, &MainWindow::playPrev);
    connect(m_mpris, &MprisService::seekAbsoluteRequested, m_audio, &AudioEngine::seekSeconds);
    connect(m_mpris, &MprisService::seekRelativeRequested, this, [this](double delta) {
        const double cur = (m_seekSlider->value() / 1000.0) * m_currentDurationSec;
        m_audio->seekSeconds(qMax(0.0, cur + delta));
    });
    connect(m_mpris, &MprisService::volumeRequested, this, [this](double vol) {
        const int v = qBound(0, static_cast<int>(vol * 100.0), 100);
        m_volumeSlider->setValue(v);
    });
    connect(m_mpris, &MprisService::raiseRequested, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    connect(m_mpris, &MprisService::quitRequested, qApp, &QApplication::quit);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(createHoneycombIcon(), this);
        m_trayIcon->setToolTip("Honeycomb");
        auto* trayMenu = new QMenu(this);
        m_trayNowPlayingAct = trayMenu->addAction("Not Playing");
        m_trayNowPlayingAct->setEnabled(false);
        trayMenu->addSeparator();
        trayMenu->addAction("Play / Pause", m_audio, &AudioEngine::togglePause);
        trayMenu->addAction("Stop", m_audio, &AudioEngine::stop);
        trayMenu->addAction("Next", this, &MainWindow::playNext);
        trayMenu->addAction("Previous", this, &MainWindow::playPrev);
        trayMenu->addSeparator();
        trayMenu->addAction("Show / Hide", this, [this]() {
            if (isVisible()) hide();
            else { showNormal(); raise(); activateWindow(); }
        });
        trayMenu->addAction("Quit", qApp, &QApplication::quit);
        m_trayIcon->setContextMenu(trayMenu);
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                if (isVisible()) hide();
                else { showNormal(); raise(); activateWindow(); }
            }
        });
        m_trayIcon->show();
    }
}

void MainWindow::playTrackAtIndex(int index) {
    if (index < 0 || index >= m_queue.size()) return;
    m_queueIndex = index;
    const Track& t = m_queue[m_queueIndex];
    m_audio->playFile(t.path);
    m_db.incrementPlayCount(t.path);
    updateNowPlayingPanel(t);
    refreshQueueWidget();
    if (m_mpris) m_mpris->setCurrentTrack(t);
    if (m_trayIcon) m_trayIcon->setToolTip(QString("Honeycomb — %1 - %2").arg(t.title, t.artist));
    if (m_trayNowPlayingAct) m_trayNowPlayingAct->setText(QString("▶ %1 — %2").arg(t.title, t.artist));
}

void MainWindow::playNext() {
    if (m_queue.isEmpty()) return;
    if (m_shuffle && m_queue.size() > 1) {
        const int next = QRandomGenerator::global()->bounded(m_queue.size());
        playTrackAtIndex(next);
        return;
    }
    if (m_queueIndex + 1 < m_queue.size()) {
        playTrackAtIndex(m_queueIndex + 1);
    } else if (m_autoDj) {
        Collection active;
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                active = c;
                break;
            }
        }
        const auto pool = m_db.queryTracks(effectiveRules(active), m_searchEdit->text(), SortOrder::TrackTitle);
        if (!pool.isEmpty()) {
            const int startNext = m_queue.size();
            const int count = qMin(5, pool.size());
            for (int i = 0; i < count; ++i) {
                m_queue.append(pool[QRandomGenerator::global()->bounded(pool.size())]);
            }
            playTrackAtIndex(startNext);
            return;
        }
    } else if (m_repeat) {
        playTrackAtIndex(0);
    } else {
        m_audio->stop();
    }
}

void MainWindow::playPrev() {
    if (m_queue.isEmpty()) return;
    if (m_queueIndex > 0) {
        playTrackAtIndex(m_queueIndex - 1);
    } else if (m_repeat) {
        playTrackAtIndex(m_queue.size() - 1);
    }
}

void MainWindow::updateNowPlayingPanel(const Track& t) {
    m_npArtLabel->setPixmap(m_artCache->getFullArtwork(t.artHash, 260));
    m_npTitleLabel->setText(t.title);
    m_npArtistLabel->setText(t.artist);
    m_npAlbumLabel->setText(t.year > 0 ? QString("%1 (%2)").arg(t.album).arg(t.year) : t.album);
    m_npFormatLabel->setText(QString("%1  •  %2 kbps").arg(t.format).arg(t.bitrate));
    m_npRating->setRating(t.rating);
}

void MainWindow::refreshQueueWidget() {
    m_queueList->clear();
    for (int i = 0; i < m_queue.size(); ++i) {
        const Track& t = m_queue[i];
        const QString prefix = (i == m_queueIndex) ? "▶ " : "   ";
        QString numStr;
        if (t.trackNumber > 0) {
            numStr = (t.discNumber > 1)
                ? QString("%1-%2. ").arg(t.discNumber).arg(t.trackNumber, 2, 10, QChar('0'))
                : QString("%1. ").arg(t.trackNumber, 2, 10, QChar('0'));
        } else {
            numStr = QString("%1. ").arg(i + 1, 2, 10, QChar('0'));
        }
        auto* item = new QListWidgetItem(prefix + numStr + t.title + " — " + t.artist);
        if (i == m_queueIndex) {
            item->setForeground(QColor("#f5a623"));
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_queueList->addItem(item);
    }
    if (m_queueIndex >= 0 && m_queueIndex < m_queueList->count()) {
        m_queueList->scrollToItem(m_queueList->item(m_queueIndex));
    }
}

void MainWindow::onAddMusicFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select Music Folder",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    if (dir.isEmpty()) return;

    m_db.addLibraryFolder(dir);
    m_scanner->startScan(m_db.libraryFolders());
}

void MainWindow::onNewCollection() {
    Collection blank;
    RuleBuilderDialog dlg(blank, this);
    if (dlg.exec() == QDialog::Accepted) {
        Collection created = dlg.resultCollection();
        m_db.saveCollection(created);
        loadCollectionsSidebar();
        activateCollection(created.id);
    }
}

void MainWindow::onEditCurrentCollection() {
    for (const auto& c : m_collections) {
        if (c.id == m_activeCollectionId && !c.isSystem) {
            RuleBuilderDialog dlg(c, this);
            if (dlg.exec() == QDialog::Accepted) {
                Collection updated = dlg.resultCollection();
                m_db.saveCollection(updated);
                loadCollectionsSidebar();
                activateCollection(updated.id);
            }
            break;
        }
    }
}

void MainWindow::onDeleteCurrentCollection() {
    for (const auto& c : m_collections) {
        if (c.id == m_activeCollectionId && !c.isSystem) {
            m_db.deleteCollection(c.id);
            loadCollectionsSidebar();
            if (!m_collections.isEmpty()) activateCollection(m_collections.first().id);
            break;
        }
    }
}

} // namespace honeycomb

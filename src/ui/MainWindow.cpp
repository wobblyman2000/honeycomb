#include "MainWindow.h"
#include "AppIcon.h"
#include "AutoTagDialog.h"
#include "RuleBuilderDialog.h"
#include "TagEditorDialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
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
    m_dataDir = dataDir;
    m_artCacheDir = cacheDir;
    QDir().mkpath(dataDir);
    QDir().mkpath(cacheDir);

    loadLibraryProfiles();
    QString activeDb = dataDir + "/honeycomb.db";
    for (const auto& lib : m_libraries) {
        if (lib.id == m_activeLibraryId) {
            activeDb = lib.dbPath;
            break;
        }
    }
    m_db.open(activeDb);
    m_autoDjConfig = AutoDjConfig::load();
    m_artCache = new ArtCache(cacheDir, this);
    m_scanner = new LibraryScanner(&m_db, cacheDir, this);
    m_audio = new AudioEngine(this);
    m_castDiscovery = new CastDiscovery(this);
    m_cast = new CastController(this);
    m_scrobble = new ScrobbleService(this);
    m_sleepTimer = new QTimer(this);
    m_sleepTimer->setSingleShot(true);
    connect(m_sleepTimer, &QTimer::timeout, this, [this]() {
        activeStop();
        m_scanStatusLabel->setText("Sleep timer stopped playback.");
    });

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
        loadCollectionsSidebar();
        refreshCurrentView(true);
    });

    auto onPosition = [this](double pos) {
        m_currentPosSec = pos;
        if (!m_userSeeking && m_currentDurationSec > 0.0) {
            m_seekSlider->blockSignals(true);
            m_seekSlider->setValue(static_cast<int>((pos / m_currentDurationSec) * 1000.0));
            m_seekSlider->blockSignals(false);
        }
        m_timeLabel->setText(QString("%1 / %2").arg(formatSec(pos), formatSec(m_currentDurationSec)));
        if (m_mpris) m_mpris->setPositionSeconds(pos);
        if (m_theater) m_theater->setPositionSeconds(pos, m_currentDurationSec);
        if (m_miniPlayer) m_miniPlayer->setPositionSeconds(pos, m_currentDurationSec);
        if (m_waveform) m_waveform->setPositionSeconds(pos);
        updateSidebarLyrics(static_cast<int64_t>(pos * 1000.0));

        if (!m_scrobbledCurrent && m_queueIndex >= 0 && m_queueIndex < m_queue.size() &&
            m_currentDurationSec > 30.0 && (pos >= m_currentDurationSec * 0.5 || pos >= 240.0)) {
            m_scrobbledCurrent = true;
            m_scrobble->sendScrobble(m_queue[m_queueIndex], m_trackStartedEpoch);
        }
    };
    auto onDuration = [this](double dur) {
        m_currentDurationSec = dur;
        if (m_waveform) m_waveform->setDurationSeconds(dur);
    };
    auto onState = [this](AudioEngine::State st) {
        m_playPauseBtn->setText(st == AudioEngine::State::Playing ? "⏸" : "▶");
        if (m_mpris) m_mpris->setPlaybackState(st);
        if (m_theater) m_theater->setPlaybackState(st);
        if (m_miniPlayer) m_miniPlayer->setPlaybackState(st);
    };

    auto onTrackDone = [this]() {
        if (m_sleepAfterTrack) {
            m_sleepAfterTrack = false;
            activeStop();
            m_scanStatusLabel->setText("Sleep timer stopped playback.");
            return;
        }
        playNext();
    };

    // Local Audio signals
    connect(m_audio, &AudioEngine::positionChanged, this, [this, onPosition](double pos) {
        if (!m_castingActive) onPosition(pos);
    });
    connect(m_audio, &AudioEngine::durationChanged, this, [this, onDuration](double dur) {
        if (!m_castingActive) onDuration(dur);
    });
    connect(m_audio, &AudioEngine::stateChanged, this, [this, onState](AudioEngine::State st) {
        if (!m_castingActive) onState(st);
    });
    connect(m_audio, &AudioEngine::trackFinished, this, [this, onTrackDone]() {
        if (!m_castingActive) onTrackDone();
    });

    // Google Cast signals
    connect(m_castDiscovery, &CastDiscovery::devicesUpdated, this, [this](const QVector<CastDevice>& devs) {
        m_castDevices = devs;
        rebuildCastMenu();
    });
    connect(m_cast, &CastController::connectedChanged, this, [this](bool connected, const QString& devName) {
        if (connected && m_castingActive) {
            m_castBtn->setText(QString("Cast: %1 ▾").arg(devName));
            m_castBtn->setStyleSheet("background: #2b4c7e; color: #fff; font-weight: bold;");
        } else if (!connected) {
            m_castingActive = false;
            m_castBtn->setText("Cast ▾");
            m_castBtn->setStyleSheet("");
        }
        rebuildCastMenu();
    });
    connect(m_cast, &CastController::positionChanged, this, [this, onPosition](double pos) {
        if (m_castingActive) onPosition(pos);
    });
    connect(m_cast, &CastController::durationChanged, this, [this, onDuration](double dur) {
        if (m_castingActive) onDuration(dur);
    });
    connect(m_cast, &CastController::stateChanged, this, [this, onState](AudioEngine::State st) {
        if (m_castingActive) onState(st);
    });
    connect(m_cast, &CastController::trackFinished, this, [this]() {
        if (m_castingActive) playNext();
    });
    connect(m_cast, &CastController::errorOccurred, this, [this](const QString& err) {
        m_scanStatusLabel->setText(QString("Cast error: %1").arg(err));
        if (m_castingActive) {
            stopCasting(false);
        }
    });

    m_castDiscovery->refresh();

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
    m_leftPane = new QWidget(mainSplitter);
    m_leftPane->setMinimumWidth(220);
    m_leftPane->setMaximumWidth(300);
    auto* leftLayout = new QVBoxLayout(m_leftPane);
    leftLayout->setContentsMargins(10, 10, 8, 10);
    leftLayout->setSpacing(8);

    auto* brandLabel = new QLabel("HONEYCOMB", m_leftPane);
    brandLabel->setStyleSheet("font-size: 15px; font-weight: 800; letter-spacing: 2px; color: #f5a623;");
    leftLayout->addWidget(brandLabel);

    m_libraryBtn = new QPushButton("Main Library ▾", m_leftPane);
    m_libraryBtn->setToolTip("Switch between separate music libraries or manage library folders");
    m_libraryMenu = new QMenu(m_libraryBtn);
    m_libraryBtn->setMenu(m_libraryMenu);
    rebuildLibraryMenu();
    leftLayout->addWidget(m_libraryBtn);

    m_sidebarList = new QListWidget(m_leftPane);
    m_sidebarList->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLayout->addWidget(m_sidebarList, 1);

    auto* newColBtn = new QPushButton("+ New Collection / Smart Playlist", m_leftPane);
    auto* plBtnRow = new QHBoxLayout();
    auto* newPlBtn = new QPushButton("+ New Playlist", m_leftPane);
    auto* importM3uBtn = new QPushButton("Import M3U...", m_leftPane);
    plBtnRow->addWidget(newPlBtn);
    plBtnRow->addWidget(importM3uBtn);
    auto* folderBtnRow = new QHBoxLayout();
    auto* addFolderBtn = new QPushButton("Library Folders...", m_leftPane);
    auto* inboxScanBtn = new QPushButton("Scan to Inbox...", m_leftPane);
    folderBtnRow->addWidget(addFolderBtn);
    folderBtnRow->addWidget(inboxScanBtn);
    auto* rescanBtn = new QPushButton("Rescan Library", m_leftPane);
    auto* autoWatchChk = new QCheckBox("Auto-watch folders", m_leftPane);
    {
        QSettings s("Honeycomb", "Honeycomb");
        const bool watchOn = s.value("library/autoWatch", true).toBool();
        autoWatchChk->setChecked(watchOn);
        m_scanner->setAutoWatchEnabled(watchOn);
        m_scanner->watchFolders(m_db.libraryFolders());
    }
    connect(autoWatchChk, &QCheckBox::toggled, this, [this](bool on) {
        QSettings s("Honeycomb", "Honeycomb");
        s.setValue("library/autoWatch", on);
        m_scanner->setAutoWatchEnabled(on);
        if (on) m_scanner->watchFolders(m_db.libraryFolders());
    });
    connect(newPlBtn, &QPushButton::clicked, this, [this]() { onNewPlaylist(); });
    connect(importM3uBtn, &QPushButton::clicked, this, &MainWindow::onImportM3U);
    connect(inboxScanBtn, &QPushButton::clicked, this, &MainWindow::onScanFolderToInbox);

    leftLayout->addWidget(newColBtn);
    leftLayout->addLayout(plBtnRow);
    leftLayout->addLayout(folderBtnRow);
    leftLayout->addWidget(rescanBtn);
    leftLayout->addWidget(autoWatchChk);

    m_scanStatusLabel = new QLabel("0 tracks", m_leftPane);
    m_scanStatusLabel->setStyleSheet("color: #8c92a4; font-size: 11px;");
    m_scanStatusLabel->setWordWrap(true);
    leftLayout->addWidget(m_scanStatusLabel);

    mainSplitter->addWidget(m_leftPane);

    // --- CENTER MAIN PANEL ---
    auto* centerPane = new QWidget(mainSplitter);
    auto* centerLayout = new QVBoxLayout(centerPane);
    centerLayout->setContentsMargins(8, 10, 8, 8);
    centerLayout->setSpacing(8);

    auto* topBar = new QHBoxLayout();
    m_backBtn = new QPushButton("◀", centerPane);
    m_backBtn->setFixedWidth(28);
    m_backBtn->setEnabled(false);
    m_backBtn->setToolTip("Back");
    m_fwdBtn = new QPushButton("▶", centerPane);
    m_fwdBtn->setFixedWidth(28);
    m_fwdBtn->setEnabled(false);
    m_fwdBtn->setToolTip("Forward");

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
    m_sortCombo->addItem("Sort: Artist / Type / Year", static_cast<int>(SortOrder::AlbumArtistTypeYear));
    m_sortCombo->addItem("Sort: Album Type / Year", static_cast<int>(SortOrder::AlbumTypeYear));
    m_sortCombo->addItem("Sort: Album", static_cast<int>(SortOrder::AlbumTitle));
    m_sortCombo->addItem("Sort: Year (Newest)", static_cast<int>(SortOrder::YearDesc));
    m_sortCombo->addItem("Sort: Recently Added", static_cast<int>(SortOrder::RecentlyAdded));
    m_sortCombo->addItem("Sort: Recently Played", static_cast<int>(SortOrder::RecentlyPlayed));
    m_sortCombo->addItem("Sort: Track Title", static_cast<int>(SortOrder::TrackTitle));

    m_artZoomSlider = new QSlider(Qt::Horizontal, centerPane);
    m_artZoomSlider->setRange(120, 340);
    m_artZoomSlider->setValue(200);
    m_artZoomSlider->setFixedWidth(100);
    m_artZoomSlider->setToolTip("Artwork Size (or Ctrl+Wheel)");

    m_editColBtn = new QPushButton("Edit Rules", centerPane);

    m_panelsBtn = new QPushButton("Panels ▾", centerPane);
    m_panelsBtn->setToolTip("Show or hide UI panels");

    topBar->addWidget(m_backBtn);
    topBar->addWidget(m_fwdBtn);
    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_colBrowserBtn);
    topBar->addWidget(m_viewModeCombo);
    topBar->addWidget(m_sortCombo);
    topBar->addWidget(m_artZoomSlider);
    topBar->addWidget(m_editColBtn);
    topBar->addWidget(m_panelsBtn);
    centerLayout->addLayout(topBar);

    // Inbox Action Bar (shown only when viewing Inbox)
    m_inboxBar = new QWidget(centerPane);
    m_inboxBar->setVisible(false);
    m_inboxBar->setStyleSheet("background: #1b1e28; border: 1px solid #2b2f3f; border-radius: 4px;");
    auto* inboxLayout = new QHBoxLayout(m_inboxBar);
    inboxLayout->setContentsMargins(10, 6, 10, 6);
    inboxLayout->setSpacing(8);
    auto* inboxInfo = new QLabel("Inbox — Edit tags & cover art here before moving tracks into your Main Library", m_inboxBar);
    inboxInfo->setStyleSheet("color: #f5a623; font-size: 12px; font-weight: 600;");
    auto* inboxAddBtn = new QPushButton("+ Scan Folder to Inbox...", m_inboxBar);
    auto* inboxPromoteSelBtn = new QPushButton("Move Selected to Library", m_inboxBar);
    auto* inboxPromoteAllBtn = new QPushButton("Move All to Library", m_inboxBar);
    inboxLayout->addWidget(inboxInfo, 1);
    inboxLayout->addWidget(inboxAddBtn);
    inboxLayout->addWidget(inboxPromoteSelBtn);
    inboxLayout->addWidget(inboxPromoteAllBtn);
    centerLayout->addWidget(m_inboxBar);

    connect(inboxAddBtn, &QPushButton::clicked, this, &MainWindow::onScanFolderToInbox);
    connect(inboxPromoteSelBtn, &QPushButton::clicked, this, [this]() {
        const QStringList paths = m_trackTable->selectedTrackPaths();
        if (paths.isEmpty()) return;
        m_db.promoteInboxTracks(paths);
        loadCollectionsSidebar();
        refreshCurrentView(false);
        m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
    });
    connect(inboxPromoteAllBtn, &QPushButton::clicked, this, [this]() {
        const auto tracks = m_db.queryInboxTracks();
        QStringList paths;
        for (const Track& t : tracks) paths.append(t.path);
        if (paths.isEmpty()) return;
        m_db.promoteInboxTracks(paths);
        loadCollectionsSidebar();
        refreshCurrentView(false);
        m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
    });

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
    m_radioPage = new RadioPage(&m_db, m_viewStack);
    m_podcastPage = new PodcastPage(&m_db, m_viewStack);
    m_viewStack->addWidget(m_radioPage);
    m_viewStack->addWidget(m_podcastPage);
    connect(m_radioPage, &RadioPage::streamPlayRequested, this, [this](const Track& t) {
        playQueue({t}, 0);
    });
    connect(m_podcastPage, &PodcastPage::streamPlayRequested, this, [this](const Track& t) {
        playQueue({t}, 0);
    });
    centerSplitter->addWidget(m_viewStack);

    // Inline Album Track Drawer (shown when clicking an album in AlbumCovers)
    m_albumDrawer = new QWidget(centerSplitter);
    m_albumDrawer->setVisible(false);
    m_albumDrawer->setMinimumHeight(220);
    m_albumDrawer->setStyleSheet("QWidget#albumDrawer { background: #181a22; border-top: 2px solid #f5a623; }");
    m_albumDrawer->setObjectName("albumDrawer");
    auto* drawerOuter = new QHBoxLayout(m_albumDrawer);
    drawerOuter->setContentsMargins(12, 8, 12, 8);
    drawerOuter->setSpacing(12);

    auto* drawerLeft = new QVBoxLayout();
    drawerLeft->setSpacing(6);

    auto* dHdr = new QHBoxLayout();
    auto* dCloseBtn = new QPushButton("×", m_albumDrawer);
    dCloseBtn->setFixedWidth(26);
    dCloseBtn->setToolTip("Close album drawer");

    auto* dText = new QVBoxLayout();
    m_drawerTitle = new QLabel("", m_albumDrawer);
    m_drawerTitle->setStyleSheet("font-size: 15px; font-weight: 700;");
    m_drawerSub = new QLabel("", m_albumDrawer);
    m_drawerSub->setStyleSheet("color: #9aa0b0; font-size: 12px;");
    dText->addWidget(m_drawerTitle);
    dText->addWidget(m_drawerSub);

    auto* dPlayBtn = new QPushButton("▶ Play Album", m_albumDrawer);
    auto* dQueueBtn = new QPushButton("+ Queue", m_albumDrawer);

    dHdr->addWidget(dCloseBtn);
    dHdr->addSpacing(4);
    dHdr->addLayout(dText, 1);
    dHdr->addWidget(dPlayBtn);
    dHdr->addWidget(dQueueBtn);
    drawerLeft->addLayout(dHdr);

    m_drawerTable = new QTableWidget(0, 6, m_albumDrawer);
    m_drawerTable->setHorizontalHeaderLabels({"#", "Title", "Artist", "Format", "★", "Time"});
    m_drawerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_drawerTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_drawerTable->verticalHeader()->setVisible(false);
    m_drawerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_drawerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_drawerTable->setShowGrid(false);
    m_drawerTable->setAlternatingRowColors(true);
    m_drawerTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_drawerTable2 = new QTableWidget(0, 4, m_albumDrawer);
    m_drawerTable2->setVisible(false);
    drawerLeft->addWidget(m_drawerTable, 1);

    m_drawerArt = new QLabel(m_albumDrawer);
    m_drawerArt->setFixedSize(160, 160);
    m_drawerArt->setScaledContents(true);
    m_drawerArt->setStyleSheet("background: #1b1c22; border-radius: 6px;");

    drawerOuter->addLayout(drawerLeft, 1);
    drawerOuter->addWidget(m_drawerArt, 0, Qt::AlignVCenter);

    centerSplitter->addWidget(m_albumDrawer);
    centerSplitter->setStretchFactor(0, 3);
    centerSplitter->setStretchFactor(1, 2);

    mainSplitter->addWidget(centerPane);

    // --- RIGHT SIDEBAR (NOW PLAYING + QUEUE) ---
    m_rightPane = new QWidget(mainSplitter);
    m_rightPane->setMinimumWidth(260);
    m_rightPane->setMaximumWidth(360);
    auto* rightLayout = new QVBoxLayout(m_rightPane);
    rightLayout->setContentsMargins(8, 10, 10, 10);
    rightLayout->setSpacing(6);

    auto* npHeader = new QLabel("NOW PLAYING", m_rightPane);
    npHeader->setStyleSheet("font-size: 11px; font-weight: 700; letter-spacing: 1.5px; color: #8c92a4;");
    rightLayout->addWidget(npHeader);

    m_npArtLabel = new QLabel(m_rightPane);
    m_npArtLabel->setFixedSize(260, 260);
    m_npArtLabel->setAlignment(Qt::AlignCenter);
    m_npArtLabel->setStyleSheet("background: #1b1c22; border-radius: 8px;");
    m_npArtLabel->setPixmap(m_artCache->getFullArtwork("", 260));
    rightLayout->addWidget(m_npArtLabel, 0, Qt::AlignHCenter);

    m_npTitleLabel = new QLabel("Not Playing", m_rightPane);
    m_npTitleLabel->setStyleSheet("font-size: 15px; font-weight: 700;");
    m_npTitleLabel->setWordWrap(true);
    m_npArtistLabel = new QLabel("", m_rightPane);
    m_npArtistLabel->setStyleSheet("color: #c4c8d4;");
    m_npAlbumLabel = new QLabel("", m_rightPane);
    m_npAlbumLabel->setStyleSheet("color: #8c92a4; font-size: 12px;");
    m_npFormatLabel = new QLabel("", m_rightPane);
    m_npFormatLabel->setStyleSheet("color: #f5a623; font-size: 11px; font-weight: 600;");
    m_npRating = new StarRatingWidget(m_rightPane);

    rightLayout->addWidget(m_npTitleLabel);
    rightLayout->addWidget(m_npArtistLabel);
    rightLayout->addWidget(m_npAlbumLabel);
    rightLayout->addWidget(m_npFormatLabel);
    rightLayout->addWidget(m_npRating);

    m_waveform = new WaveformWidget(m_rightPane);
    connect(m_waveform, &WaveformWidget::seekRequested, this, &MainWindow::activeSeekSeconds);
    rightLayout->addWidget(m_waveform);

    m_rightTabs = new QTabWidget(m_rightPane);
    auto* queueTab = new QWidget(m_rightTabs);
    auto* queueTabLayout = new QVBoxLayout(queueTab);
    queueTabLayout->setContentsMargins(0, 6, 0, 0);
    queueTabLayout->setSpacing(4);

    auto* qHeaderRow = new QHBoxLayout();
    m_autoDjBtn = new QPushButton("Auto-DJ", queueTab);
    m_autoDjBtn->setCheckable(true);
    m_autoDjBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    m_autoDjBtn->setToolTip("Toggle Auto-DJ (right-click or click ⚙ to configure rules)");
    auto* autoDjSettingsBtn = new QPushButton("⚙", queueTab);
    autoDjSettingsBtn->setFixedWidth(28);
    autoDjSettingsBtn->setToolTip("Auto-DJ Settings & Rules...");
    connect(autoDjSettingsBtn, &QPushButton::clicked, this, &MainWindow::openAutoDjSettings);
    connect(m_autoDjBtn, &QPushButton::customContextMenuRequested, this, [this](const QPoint&) {
        openAutoDjSettings();
    });
    auto* clearQBtn = new QPushButton("Clear", queueTab);
    clearQBtn->setFlat(true);
    qHeaderRow->addStretch();
    qHeaderRow->addWidget(m_autoDjBtn);
    qHeaderRow->addWidget(autoDjSettingsBtn);
    qHeaderRow->addWidget(clearQBtn);
    queueTabLayout->addLayout(qHeaderRow);

    m_queueList = new QListWidget(queueTab);
    m_queueList->setContextMenuPolicy(Qt::CustomContextMenu);
    queueTabLayout->addWidget(m_queueList, 1);

    m_sidebarLyricsList = new QListWidget(m_rightTabs);
    m_sidebarLyricsList->setWordWrap(true);
    m_sidebarLyricsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_sidebarLyricsList->setFocusPolicy(Qt::NoFocus);

    auto* detailsScroll = new QScrollArea(m_rightTabs);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    m_detailsLabel = new QLabel("No track playing.", detailsScroll);
    m_detailsLabel->setWordWrap(true);
    m_detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailsLabel->setStyleSheet("color: #c4c8d4; font-size: 12px; padding: 8px;");
    detailsScroll->setWidget(m_detailsLabel);

    m_rightTabs->addTab(queueTab, "Queue");
    m_rightTabs->addTab(m_sidebarLyricsList, "Lyrics");
    m_rightTabs->addTab(detailsScroll, "Details");
    rightLayout->addWidget(m_rightTabs, 1);

    mainSplitter->addWidget(m_rightPane);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);

    // --- SELECTION & QUEUE STATUS BAR ---
    m_statusBar = new QWidget(central);
    m_statusBar->setFixedHeight(22);
    m_statusBar->setStyleSheet("background: #14161e; border-top: 1px solid #222530;");
    auto* stLayout = new QHBoxLayout(m_statusBar);
    stLayout->setContentsMargins(12, 0, 12, 0);
    m_statusLeftLabel = new QLabel("", m_statusBar);
    m_statusLeftLabel->setStyleSheet("color: #8c92a4; font-size: 11px;");
    m_statusRightLabel = new QLabel("queued: 0:00", m_statusBar);
    m_statusRightLabel->setStyleSheet("color: #8c92a4; font-size: 11px;");
    stLayout->addWidget(m_statusLeftLabel);
    stLayout->addStretch();
    stLayout->addWidget(m_statusRightLabel);
    rootLayout->addWidget(m_statusBar);

    // Panels / Layout Settings Menu
    {
        QSettings uiSettings("Honeycomb", "Honeycomb");
        const bool showLeft = uiSettings.value("ui/panels/leftSidebar", true).toBool();
        const bool showRight = uiSettings.value("ui/panels/rightSidebar", true).toBool();
        const bool showNpArt = uiSettings.value("ui/panels/npArt", true).toBool();
        const bool showWaveform = uiSettings.value("ui/panels/waveform", true).toBool();
        const bool showQueueLyrics = uiSettings.value("ui/panels/queueLyrics", true).toBool();
        const bool showStatusBar = uiSettings.value("ui/panels/statusBar", true).toBool();

        m_leftPane->setVisible(showLeft);
        m_rightPane->setVisible(showRight);
        m_npArtLabel->setVisible(showNpArt);
        m_waveform->setVisible(showWaveform);
        m_rightTabs->setVisible(showQueueLyrics);
        m_statusBar->setVisible(showStatusBar);

        auto* panelsMenu = new QMenu(m_panelsBtn);
        m_actLeftSidebar = panelsMenu->addAction("Left Sidebar (Collections & Playlists)");
        m_actLeftSidebar->setCheckable(true);
        m_actLeftSidebar->setChecked(showLeft);
        connect(m_actLeftSidebar, &QAction::toggled, this, [this](bool on) {
            m_leftPane->setVisible(on);
            QSettings("Honeycomb", "Honeycomb").setValue("ui/panels/leftSidebar", on);
        });

        m_actRightSidebar = panelsMenu->addAction("Right Sidebar (Now Playing & Queue)");
        m_actRightSidebar->setCheckable(true);
        m_actRightSidebar->setChecked(showRight);
        connect(m_actRightSidebar, &QAction::toggled, this, [this](bool on) {
            m_rightPane->setVisible(on);
            QSettings("Honeycomb", "Honeycomb").setValue("ui/panels/rightSidebar", on);
        });

        m_actColBrowser = panelsMenu->addAction("Column Browser");
        m_actColBrowser->setCheckable(true);
        connect(m_actColBrowser, &QAction::toggled, this, [this](bool on) {
            m_colBrowserBtn->setChecked(on);
        });

        m_actStatusBar = panelsMenu->addAction("Status Bar (Selection & Queue)");
        m_actStatusBar->setCheckable(true);
        m_actStatusBar->setChecked(showStatusBar);
        connect(m_actStatusBar, &QAction::toggled, this, [this](bool on) {
            m_statusBar->setVisible(on);
            QSettings("Honeycomb", "Honeycomb").setValue("ui/panels/statusBar", on);
        });

        panelsMenu->addSeparator();

        m_actNpArt = panelsMenu->addAction("Now Playing Cover Art");
        m_actNpArt->setCheckable(true);
        m_actNpArt->setChecked(showNpArt);
        connect(m_actNpArt, &QAction::toggled, this, [this](bool on) {
            m_npArtLabel->setVisible(on);
            QSettings("Honeycomb", "Honeycomb").setValue("ui/panels/npArt", on);
        });

        m_actWaveform = panelsMenu->addAction("Waveform Seekbar");
        m_actWaveform->setCheckable(true);
        m_actWaveform->setChecked(showWaveform);
        connect(m_actWaveform, &QAction::toggled, this, [this](bool on) {
            m_waveform->setVisible(on);
            QSettings("Honeycomb", "Honeycomb").setValue("ui/panels/waveform", on);
        });

        m_actQueueLyrics = panelsMenu->addAction("Queue / Lyrics / Details Tabs");
        m_actQueueLyrics->setCheckable(true);
        m_actQueueLyrics->setChecked(showQueueLyrics);
        connect(m_actQueueLyrics, &QAction::toggled, this, [this](bool on) {
            m_rightTabs->setVisible(on);
            QSettings("Honeycomb", "Honeycomb").setValue("ui/panels/queueLyrics", on);
        });

        panelsMenu->addSeparator();

        QAction* resetLayoutAct = panelsMenu->addAction("Reset Default Layout");
        connect(resetLayoutAct, &QAction::triggered, this, [this]() {
            m_actLeftSidebar->setChecked(true);
            m_actRightSidebar->setChecked(true);
            m_actNpArt->setChecked(true);
            m_actWaveform->setChecked(true);
            m_actQueueLyrics->setChecked(true);
            m_actStatusBar->setChecked(true);
        });

        connect(panelsMenu, &QMenu::aboutToShow, this, [this]() {
            m_actColBrowser->blockSignals(true);
            m_actColBrowser->setChecked(m_colBrowser->isVisible());
            m_actColBrowser->blockSignals(false);
        });

        m_panelsBtn->setMenu(panelsMenu);
    }

    // --- BOTTOM TRANSPORT BAR ---
    auto* transportBar = new QWidget(central);
    transportBar->setFixedHeight(64);
    transportBar->setStyleSheet("background: #101116; border-top: 1px solid #262833;");
    auto* tLayout = new QHBoxLayout(transportBar);
    tLayout->setContentsMargins(14, 6, 14, 6);
    tLayout->setSpacing(10);

    auto* prevBtn = new QPushButton("⏮", transportBar);
    m_playPauseBtn = new QPushButton("▶", transportBar);
    m_playPauseBtn->setFlat(true);
    m_playPauseBtn->setFixedSize(36, 36);
    m_playPauseBtn->setStyleSheet("QPushButton { background: transparent; border: none; color: #f5a623; font-size: 20px; font-weight: bold; } QPushButton:hover { color: #ffd27d; }");
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

    auto* audioBtn = new QPushButton("Audio ▾", transportBar);
    audioBtn->setToolTip("ReplayGain & Crossfade settings");
    auto* audioMenu = new QMenu(audioBtn);

    QSettings audioSettings("Honeycomb", "Honeycomb");
    const auto savedRg = static_cast<AudioEngine::ReplayGainMode>(
        audioSettings.value("audio/replayGain", static_cast<int>(AudioEngine::ReplayGainMode::Album)).toInt());
    const int savedCf = audioSettings.value("audio/crossfadeSec", 0).toInt();
    m_audio->setReplayGain(savedRg);
    m_audio->setCrossfadeSeconds(savedCf);

    QMenu* rgMenu = audioMenu->addMenu("ReplayGain");
    auto* rgGroup = new QActionGroup(rgMenu);
    auto addRgOpt = [&](const QString& label, AudioEngine::ReplayGainMode mode) {
        QAction* act = rgMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(savedRg == mode);
        rgGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, mode]() {
            m_audio->setReplayGain(mode);
            QSettings("Honeycomb", "Honeycomb").setValue("audio/replayGain", static_cast<int>(mode));
        });
    };
    addRgOpt("Off", AudioEngine::ReplayGainMode::Off);
    addRgOpt("Track", AudioEngine::ReplayGainMode::Track);
    addRgOpt("Album", AudioEngine::ReplayGainMode::Album);

    QMenu* cfMenu = audioMenu->addMenu("Crossfade");
    auto* cfGroup = new QActionGroup(cfMenu);
    for (int sec : {0, 2, 4, 6, 8}) {
        QAction* act = cfMenu->addAction(sec == 0 ? "Off" : QString("%1 seconds").arg(sec));
        act->setCheckable(true);
        act->setChecked(savedCf == sec);
        cfGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, sec]() {
            m_audio->setCrossfadeSeconds(sec);
            QSettings("Honeycomb", "Honeycomb").setValue("audio/crossfadeSec", sec);
        });
    }
    audioMenu->addSeparator();
    QMenu* sleepMenu = audioMenu->addMenu("Sleep Timer");
    auto* sleepGroup = new QActionGroup(sleepMenu);
    auto addSleepOpt = [&](const QString& label, int mins, bool afterTrack) {
        QAction* act = sleepMenu->addAction(label);
        act->setCheckable(true);
        if (mins == 0 && !afterTrack) act->setChecked(true);
        sleepGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, mins, afterTrack]() {
            setSleepTimerMinutes(mins, afterTrack);
        });
    };
    addSleepOpt("Off", 0, false);
    addSleepOpt("15 minutes", 15, false);
    addSleepOpt("30 minutes", 30, false);
    addSleepOpt("45 minutes", 45, false);
    addSleepOpt("60 minutes", 60, false);
    addSleepOpt("After Current Track", 0, true);

    QAction* scrobbleAct = audioMenu->addAction("Scrobbling (ListenBrainz / Last.fm)...");
    connect(scrobbleAct, &QAction::triggered, this, &MainWindow::openScrobbleSettings);

    audioBtn->setMenu(audioMenu);

    m_castBtn = new QPushButton("Cast ▾", transportBar);
    m_castBtn->setToolTip("Cast audio to Google / Chromecast speakers");
    m_castMenu = new QMenu(m_castBtn);
    m_castBtn->setMenu(m_castMenu);
    connect(m_castMenu, &QMenu::aboutToShow, this, [this]() {
        m_castDiscovery->refresh();
        rebuildCastMenu();
    });
    rebuildCastMenu();

    m_transportTrackLabel = new QLabel("Not Playing", transportBar);
    m_transportTrackLabel->setStyleSheet("color: #e0e3ec; font-size: 12px; font-weight: 600;");
    m_transportTrackLabel->setMaximumWidth(220);
    m_transportRating = new StarRatingWidget(transportBar);

    auto* theaterBtn = new QPushButton("Theater", transportBar);
    theaterBtn->setToolTip("Full-Screen Theater Mode with Lyrics (Esc to exit)");
    auto* miniBtn = new QPushButton("Mini", transportBar);
    miniBtn->setToolTip("Compact Floating Mini Player");

    tLayout->addWidget(m_shuffleBtn);
    tLayout->addWidget(prevBtn);
    tLayout->addWidget(m_playPauseBtn);
    tLayout->addWidget(nextBtn);
    tLayout->addWidget(m_repeatBtn);
    tLayout->addWidget(m_transportTrackLabel);
    tLayout->addWidget(m_seekSlider, 1);
    tLayout->addWidget(m_timeLabel);
    tLayout->addWidget(m_transportRating);
    tLayout->addWidget(new QLabel("🔊", transportBar));
    tLayout->addWidget(m_volumeSlider);
    tLayout->addWidget(audioBtn);
    tLayout->addWidget(m_castBtn);
    tLayout->addWidget(theaterBtn);
    tLayout->addWidget(miniBtn);

    rootLayout->addWidget(transportBar);

    connect(theaterBtn, &QPushButton::clicked, this, &MainWindow::enterTheaterMode);
    connect(miniBtn, &QPushButton::clicked, this, &MainWindow::enterMiniPlayer);
    connect(m_backBtn, &QPushButton::clicked, this, &MainWindow::navigateBack);
    connect(m_fwdBtn, &QPushButton::clicked, this, &MainWindow::navigateForward);

    // --- CONNECTIONS ---
    connect(newColBtn, &QPushButton::clicked, this, &MainWindow::onNewCollection);
    connect(addFolderBtn, &QPushButton::clicked, this, &MainWindow::openLibraryFoldersDialog);
    connect(rescanBtn, &QPushButton::clicked, this, [this]() {
        const QStringList folders = m_db.libraryFolders();
        if (folders.isEmpty()) openLibraryFoldersDialog();
        else m_scanner->startScan(folders);
    });
    connect(m_editColBtn, &QPushButton::clicked, this, &MainWindow::onEditCurrentCollection);

    connect(m_sidebarList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        if (id == "__inbox__") {
            activateInbox();
            return;
        }
        if (id == "__radio__") {
            m_viewingInbox = false;
            if (m_inboxBar) m_inboxBar->setVisible(false);
            if (m_trackTable) m_trackTable->setInboxMode(false);
            m_activePlaylistId.clear();
            m_activeCollectionId.clear();
            closeAlbumDrawer();
            m_viewStack->setCurrentWidget(m_radioPage);
            updateStatusSelection("Internet Radio");
            return;
        }
        if (id == "__podcasts__") {
            m_viewingInbox = false;
            if (m_inboxBar) m_inboxBar->setVisible(false);
            if (m_trackTable) m_trackTable->setInboxMode(false);
            m_activePlaylistId.clear();
            m_activeCollectionId.clear();
            closeAlbumDrawer();
            m_viewStack->setCurrentWidget(m_podcastPage);
            updateStatusSelection("Podcasts");
            return;
        }
        if (item->data(Qt::UserRole + 1).toBool()) {
            activatePlaylist(id);
        } else {
            activateCollection(id);
        }
    });

    connect(m_sidebarList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_sidebarList->itemAt(pos);
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;

        if (item->data(Qt::UserRole + 1).toBool()) {
            QMenu menu(this);
            QAction* playAct = menu.addAction("Play Playlist");
            QAction* renameAct = menu.addAction("Rename Playlist...");
            QAction* exportAct = menu.addAction("Export M3U8...");
            menu.addSeparator();
            QAction* delAct = menu.addAction("Delete Playlist");
            QAction* chosen = menu.exec(m_sidebarList->viewport()->mapToGlobal(pos));
            if (!chosen) return;
            if (chosen == playAct) {
                const auto tracks = m_db.queryPlaylistTracks(id);
                if (!tracks.isEmpty()) playQueue(tracks, 0);
            } else if (chosen == renameAct) {
                QString curName;
                for (const auto& pl : m_playlists) {
                    if (pl.id == id) { curName = pl.name; break; }
                }
                bool ok = false;
                const QString newName = QInputDialog::getText(
                    this, "Rename Playlist", "Playlist name:", QLineEdit::Normal, curName, &ok);
                if (ok && !newName.trimmed().isEmpty()) {
                    m_db.renamePlaylist(id, newName.trimmed());
                    loadCollectionsSidebar();
                    if (m_activePlaylistId == id) activatePlaylist(id);
                }
            } else if (chosen == exportAct) {
                onExportM3U(id);
            } else if (chosen == delAct) {
                m_db.deletePlaylist(id);
                if (m_activePlaylistId == id) {
                    m_activePlaylistId.clear();
                    loadCollectionsSidebar();
                    if (!m_collections.isEmpty()) activateCollection(m_collections.first().id);
                } else {
                    loadCollectionsSidebar();
                }
            }
            return;
        }

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
    connect(m_albumGrid, &AlbumGridView::albumTypeChangeRequested, this, [this](const Album& alb, const QString& albumType) {
        if (m_db.setAlbumType(alb.album, alb.albumArtist, alb.isCompilation, albumType)) {
            refreshCurrentView(true);
        }
    });
    connect(m_albumGrid, &AlbumGridView::editAlbumTagsRequested, this, [this](const Album& alb) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                openTagEditor(tracks);
                break;
            }
        }
    });
    connect(m_albumGrid, &AlbumGridView::autoTagAlbumRequested, this, [this](const Album& alb) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                openAutoTagger(tracks);
                break;
            }
        }
    });
    connect(m_albumGrid, &AlbumGridView::albumAddToPlaylistRequested, this, [this](const Album& alb, const QString& plId) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                QStringList paths;
                for (const Track& t : tracks) paths.append(t.path);
                m_db.addTracksToPlaylist(plId, paths);
                loadCollectionsSidebar();
                break;
            }
        }
    });
    connect(m_albumGrid, &AlbumGridView::albumAddToNewPlaylistRequested, this, [this](const Album& alb) {
        for (const auto& c : m_collections) {
            if (c.id == m_activeCollectionId) {
                const auto tracks = m_db.queryTracks(effectiveRules(c), m_searchEdit->text(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
                QStringList paths;
                for (const Track& t : tracks) paths.append(t.path);
                onNewPlaylist(paths);
                break;
            }
        }
    });

    // Drawer controls
    connect(dCloseBtn, &QPushButton::clicked, this, &MainWindow::closeAlbumDrawer);
    connect(dPlayBtn, &QPushButton::clicked, this, [this]() {
        if (!m_drawerTracks.isEmpty()) playQueue(m_drawerTracks, 0);
    });
    connect(dQueueBtn, &QPushButton::clicked, this, [this]() {
        for (const auto& t : m_drawerTracks) enqueueTrack(t);
    });
    auto connectDrawerTable = [this](QTableWidget* tbl, const QVector<int>& rowMap) {
        connect(tbl, &QTableWidget::cellDoubleClicked, this, [this, &rowMap](int row, int) {
            if (row >= 0 && row < rowMap.size()) {
                const int idx = rowMap[row];
                if (idx >= 0 && idx < m_drawerTracks.size()) {
                    playQueue(m_drawerTracks, idx);
                }
            }
        });
        connect(tbl, &QTableWidget::customContextMenuRequested, this, [this, tbl, &rowMap](const QPoint& pos) {
            const int row = tbl->rowAt(pos.y());
            if (row < 0 || row >= rowMap.size()) return;
            const int idx = rowMap[row];
            if (idx < 0 || idx >= m_drawerTracks.size()) return;
            const Track& t = m_drawerTracks[idx];

            QMenu menu(this);
            QAction* playAct = menu.addAction("Play Track");
            QAction* playNextAct = menu.addAction("Play Next");
            QAction* queueAct = menu.addAction("Add to Queue");
            menu.addSeparator();
            QAction* findArtistAct = !t.artist.isEmpty() ? menu.addAction(QString("Find Artist: %1").arg(t.artist)) : nullptr;
            QAction* findTrackAct = !t.title.isEmpty() ? menu.addAction(QString("Find Track: %1").arg(t.title)) : nullptr;
            QAction* chosen = menu.exec(tbl->viewport()->mapToGlobal(pos));
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
    };
    connectDrawerTable(m_drawerTable, m_drawerRowToTrack);
    connectDrawerTable(m_drawerTable2, m_drawerRowToTrack2);

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
    connect(m_trackTable, &TrackTableView::promoteFromInboxRequested, this, [this](const QStringList& paths) {
        m_db.promoteInboxTracks(paths);
        loadCollectionsSidebar();
        refreshCurrentView(false);
        m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
    });
    connect(m_trackTable, &TrackTableView::removeFromInboxRequested, this, [this](const QStringList& paths) {
        m_db.removeTracksByPath(paths);
        loadCollectionsSidebar();
        refreshCurrentView(false);
    });
    connect(m_trackTable, &TrackTableView::editTagsRequested, this, &MainWindow::openTagEditor);
    connect(m_trackTable, &TrackTableView::autoTagTracksRequested, this, &MainWindow::openAutoTagger);
    connect(m_trackTable, &TrackTableView::addToPlaylistRequested, this, [this](const QString& plId, const QStringList& paths) {
        m_db.addTracksToPlaylist(plId, paths);
        loadCollectionsSidebar();
        if (m_activePlaylistId == plId) refreshCurrentView(false);
    });
    connect(m_trackTable, &TrackTableView::addToNewPlaylistRequested, this, [this](const QStringList& paths) {
        onNewPlaylist(paths);
    });
    connect(m_trackTable, &TrackTableView::removeFromPlaylistRequested, this, [this](const QString& plId, int row) {
        m_db.removeTrackFromPlaylist(plId, row);
        loadCollectionsSidebar();
        if (m_activePlaylistId == plId) refreshCurrentView(false);
    });
    connect(m_trackTable, &TrackTableView::movePlaylistTrackRequested, this, [this](const QString& plId, int fromRow, int toRow) {
        m_db.movePlaylistTrack(plId, fromRow, toRow);
        if (m_activePlaylistId == plId) refreshCurrentView(false);
    });
    connect(m_trackTable, &TrackTableView::trackRatingChanged, this, [this](int64_t trackId, int rating) {
        m_db.setTrackRating(trackId, rating);
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size() && m_queue[m_queueIndex].id == trackId) {
            m_queue[m_queueIndex].rating = rating;
            m_npRating->setRating(rating);
            m_transportRating->setRating(rating);
        }
    });

    auto applyNowPlayingRating = [this](int newRating) {
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
            m_queue[m_queueIndex].rating = newRating;
            m_db.setTrackRating(m_queue[m_queueIndex].id, newRating);
            m_trackTable->updateTrackRating(m_queue[m_queueIndex].id, newRating);
            m_npRating->setRating(newRating);
            m_transportRating->setRating(newRating);
        }
    };
    connect(m_npRating, &StarRatingWidget::ratingChanged, this, applyNowPlayingRating);
    connect(m_transportRating, &StarRatingWidget::ratingChanged, this, applyNowPlayingRating);

    connect(m_autoDjBtn, &QPushButton::toggled, this, [this](bool on) {
        m_autoDj = on;
        if (on) {
            fillAutoDjQueue(true);
        }
    });

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
                    activeStop();
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
        activeStop();
        refreshQueueWidget();
    });

    connect(m_playPauseBtn, &QPushButton::clicked, this, &MainWindow::activeTogglePause);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::playNext);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::playPrev);
    connect(m_shuffleBtn, &QPushButton::toggled, this, [this](bool on) { m_shuffle = on; });
    connect(m_repeatBtn, &QPushButton::toggled, this, [this](bool on) { m_repeat = on; });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        activeSetVolume(val);
        if (m_mpris) m_mpris->setVolume(val / 100.0);
    });

    connect(m_seekSlider, &QSlider::sliderPressed, this, [this]() { m_userSeeking = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        m_userSeeking = false;
        if (m_currentDurationSec > 0.0) {
            const double target = (m_seekSlider->value() / 1000.0) * m_currentDurationSec;
            activeSeekSeconds(target);
        }
    });
}

void MainWindow::loadCollectionsSidebar() {
    m_collections = m_db.loadCollections();
    m_playlists = m_db.loadPlaylists();
    m_sidebarList->clear();

    QVector<QPair<QString, QString>> plPairs;
    for (const auto& pl : m_playlists) {
        plPairs.append({pl.id, pl.name});
    }
    if (m_albumGrid) m_albumGrid->setAvailablePlaylists(plPairs);
    if (m_trackTable) m_trackTable->setAvailablePlaylists(plPairs);

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
            item->setData(Qt::UserRole + 1, false);
            m_sidebarList->addItem(item);
        }
    }
    {
        const int inCnt = m_db.inboxCount();
        auto* inboxItem = new QListWidgetItem(
            inCnt > 0 ? QString("  📥 Inbox (%1)").arg(inCnt) : QString("  📥 Inbox"));
        inboxItem->setData(Qt::UserRole, "__inbox__");
        inboxItem->setData(Qt::UserRole + 1, false);
        m_sidebarList->addItem(inboxItem);
    }

    addHeader("COLLECTIONS");
    for (const auto& c : m_collections) {
        if (!c.isSystem && !c.isSmartPlaylist) {
            auto* item = new QListWidgetItem("  ◆ " + c.name);
            item->setData(Qt::UserRole, c.id);
            item->setData(Qt::UserRole + 1, false);
            m_sidebarList->addItem(item);
        }
    }

    addHeader("SMART PLAYLISTS");
    for (const auto& c : m_collections) {
        if (!c.isSystem && c.isSmartPlaylist) {
            auto* item = new QListWidgetItem("  ♫ " + c.name);
            item->setData(Qt::UserRole, c.id);
            item->setData(Qt::UserRole + 1, false);
            m_sidebarList->addItem(item);
        }
    }

    addHeader("PLAYLISTS");
    for (const auto& pl : m_playlists) {
        auto* item = new QListWidgetItem(QString("  ♪ %1 (%2)").arg(pl.name).arg(pl.trackCount));
        item->setData(Qt::UserRole, pl.id);
        item->setData(Qt::UserRole + 1, true);
        m_sidebarList->addItem(item);
    }

    addHeader("RADIO & PODCASTS");
    {
        auto* rItem = new QListWidgetItem("  📻 Internet Radio");
        rItem->setData(Qt::UserRole, "__radio__");
        rItem->setData(Qt::UserRole + 1, false);
        m_sidebarList->addItem(rItem);

        auto* pItem = new QListWidgetItem("  🎙 Podcasts");
        pItem->setData(Qt::UserRole, "__podcasts__");
        pItem->setData(Qt::UserRole + 1, false);
        m_sidebarList->addItem(pItem);
    }
}

void MainWindow::activateCollection(const QString& id) {
    m_viewingInbox = false;
    if (m_inboxBar) m_inboxBar->setVisible(false);
    if (m_trackTable) m_trackTable->setInboxMode(false);
    m_activePlaylistId.clear();
    if (m_trackTable) m_trackTable->setActiveStaticPlaylistId(QString());
    m_activeCollectionId = id;
    closeAlbumDrawer();

    for (int i = 0; i < m_sidebarList->count(); ++i) {
        auto* item = m_sidebarList->item(i);
        if (!item->data(Qt::UserRole + 1).toBool() && item->data(Qt::UserRole).toString() == id) {
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
    pushNavHistory();
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

void MainWindow::activatePlaylist(const QString& id) {
    m_viewingInbox = false;
    if (m_inboxBar) m_inboxBar->setVisible(false);
    if (m_trackTable) m_trackTable->setInboxMode(false);
    m_activePlaylistId = id;
    if (m_trackTable) m_trackTable->setActiveStaticPlaylistId(id);
    closeAlbumDrawer();
    m_colBrowser->setVisible(false);
    m_artZoomSlider->setVisible(false);

    for (int i = 0; i < m_sidebarList->count(); ++i) {
        auto* item = m_sidebarList->item(i);
        if (item->data(Qt::UserRole + 1).toBool() && item->data(Qt::UserRole).toString() == id) {
            m_sidebarList->setCurrentItem(item);
            break;
        }
    }

    refreshCurrentView(false);
}

void MainWindow::refreshCurrentView(bool resetColumnBrowser) {
    if (m_viewingInbox) {
        m_artZoomSlider->setVisible(false);
        closeAlbumDrawer();
        m_viewStack->setCurrentWidget(m_trackTable);
        const auto inTracks = m_db.queryInboxTracks(m_searchEdit->text());
        m_trackTable->setTracks(inTracks);
        updateStatusSelection(QString("%1 tracks in Inbox").arg(inTracks.size()));
        return;
    }

    if (!m_activePlaylistId.isEmpty()) {
        m_artZoomSlider->setVisible(false);
        closeAlbumDrawer();
        m_viewStack->setCurrentWidget(m_trackTable);
        const auto plTracks = m_db.queryPlaylistTracks(m_activePlaylistId, m_searchEdit->text());
        m_trackTable->setTracks(plTracks);
        updateStatusSelection(QString("%1 tracks in playlist").arg(plTracks.size()));
        return;
    }

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
        m_albumGrid->setAvailableAlbumTypes(m_db.distinctAlbumTypes());
        const auto albums = m_db.queryAlbums(rules, search, sort);
        m_albumGrid->setAlbums(albums);
        updateStatusSelection(QString("%1 albums").arg(albums.size()));
    } else if (mode == ViewMode::AlbumAndTracks) {
        m_viewStack->setCurrentWidget(m_albumTracks);
        const auto albums = m_db.queryAlbums(rules, search, sort);
        const auto tracks = m_db.queryTracks(rules, search, sort, QString(), QString(), active.limitCount, active.limitOrder);
        m_albumTracks->setData(albums, tracks);
        updateStatusSelection(QString("%1 albums, %2 tracks").arg(albums.size()).arg(tracks.size()));
    } else {
        m_viewStack->setCurrentWidget(m_trackTable);
        const auto tracks = m_db.queryTracks(rules, search, sort, QString(), QString(), active.limitCount, active.limitOrder);
        m_trackTable->setTracks(tracks);
        updateStatusSelection(QString("%1 tracks").arg(tracks.size()));
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

    m_drawerArt->setPixmap(m_artCache->getFullArtwork(album.artHash, 160));
    m_drawerTitle->setText(album.album);
    const QString discStr = album.discCount > 1 ? QString("  •  %1 discs").arg(album.discCount) : "";
    const QString yearStr = album.year > 0 ? QString("  •  %1").arg(album.year) : "";
    m_drawerSub->setText(QString("%1  •  %2 tracks%3%4").arg(album.albumArtist).arg(m_drawerTracks.size()).arg(discStr, yearStr));

    qint64 totalBytes = 0;
    qint64 totalMs = 0;
    for (const Track& t : m_drawerTracks) {
        totalBytes += QFileInfo(t.path).size();
        totalMs += t.durationMs;
    }
    const double mb = totalBytes / (1024.0 * 1024.0);
    updateStatusSelection(QString("selected: 1 album, %1 tracks, %2 MB, %3")
                              .arg(m_drawerTracks.size())
                              .arg(mb, 0, 'f', 1)
                              .arg(formatMs(totalMs)));

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

    auto starsText = [](int r) {
        if (r <= 0) return QString();
        return QString(r, QChar(0x2605));
    };

    auto appendTrackRow = [&](QTableWidget* tbl, QVector<int>& rowMap, int trackIdx) {
        const Track& t = m_drawerTracks[trackIdx];
        const int row = tbl->rowCount();
        tbl->insertRow(row);
        rowMap.append(trackIdx);
        tbl->setItem(row, 0, new QTableWidgetItem(t.trackNumber > 0 ? QString::number(t.trackNumber) : "-"));
        tbl->setItem(row, 1, new QTableWidgetItem(t.title));
        tbl->setItem(row, 2, new QTableWidgetItem(t.artist));
        tbl->setItem(row, 3, new QTableWidgetItem(t.format));
        auto* starItem = new QTableWidgetItem(starsText(t.rating));
        starItem->setForeground(QColor("#f5a623"));
        tbl->setItem(row, 4, starItem);
        tbl->setItem(row, 5, new QTableWidgetItem(formatMs(t.durationMs)));
    };

    m_drawerTable->clearSpans();
    m_drawerTable->setRowCount(0);
    m_drawerRowToTrack.clear();
    m_drawerTable2->setVisible(false);

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
            m_drawerTable->setSpan(hdrRow, 0, 1, 6);
        }
        appendTrackRow(m_drawerTable, m_drawerRowToTrack, i);
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

void MainWindow::openTagEditor(const QVector<Track>& tracks) {
    if (tracks.isEmpty()) return;
    TagEditorDialog dlg(tracks, &m_db, m_artCache, m_artCacheDir, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasChanges()) {
        const auto& updated = dlg.updatedTracks();
        QHash<int64_t, Track> byId;
        for (const Track& t : updated) {
            byId.insert(t.id, t);
        }
        for (Track& q : m_queue) {
            auto it = byId.find(q.id);
            if (it != byId.end()) {
                q = it.value();
            }
        }
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size() && byId.contains(m_queue[m_queueIndex].id)) {
            const Track& cur = m_queue[m_queueIndex];
            updateNowPlayingPanel(cur);
            if (m_mpris) m_mpris->setCurrentTrack(cur);
            if (m_trayIcon) m_trayIcon->setToolTip(QString("Honeycomb — %1 - %2").arg(cur.title, cur.artist));
            if (m_trayNowPlayingAct) m_trayNowPlayingAct->setText(QString("▶ %1 — %2").arg(cur.title, cur.artist));
        }
        refreshQueueWidget();
        refreshCurrentView(true);
    }
}

void MainWindow::setupTrayAndMpris(const QString& artCacheDir) {
    m_mpris = new MprisService(artCacheDir, this);
    connect(m_mpris, &MprisService::playRequested, this, [this]() { activeSetPaused(false); });
    connect(m_mpris, &MprisService::pauseRequested, this, [this]() { activeSetPaused(true); });
    connect(m_mpris, &MprisService::playPauseRequested, this, &MainWindow::activeTogglePause);
    connect(m_mpris, &MprisService::stopRequested, this, &MainWindow::activeStop);
    connect(m_mpris, &MprisService::nextRequested, this, &MainWindow::playNext);
    connect(m_mpris, &MprisService::previousRequested, this, &MainWindow::playPrev);
    connect(m_mpris, &MprisService::seekAbsoluteRequested, this, &MainWindow::activeSeekSeconds);
    connect(m_mpris, &MprisService::seekRelativeRequested, this, [this](double delta) {
        const double cur = (m_seekSlider->value() / 1000.0) * m_currentDurationSec;
        activeSeekSeconds(qMax(0.0, cur + delta));
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
        trayMenu->addAction("Play / Pause", this, &MainWindow::activeTogglePause);
        trayMenu->addAction("Stop", this, &MainWindow::activeStop);
        trayMenu->addAction("Next", this, &MainWindow::playNext);
        trayMenu->addAction("Previous", this, &MainWindow::playPrev);
        trayMenu->addSeparator();
        trayMenu->addAction("Theater Mode", this, &MainWindow::enterTheaterMode);
        trayMenu->addAction("Mini Player", this, &MainWindow::enterMiniPlayer);
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
    m_currentPosSec = 0.0;
    if (m_castingActive) {
        m_cast->playTrack(t, artFileForHash(t.artHash), 0.0);
    } else {
        m_audio->playFile(t.path);
    }
    m_db.incrementPlayCount(t.path);
    m_trackStartedEpoch = QDateTime::currentSecsSinceEpoch();
    m_scrobbledCurrent = false;
    if (m_scrobble) m_scrobble->sendNowPlaying(t);
    updateNowPlayingPanel(t);
    if (m_autoDj) {
        fillAutoDjQueue(false);
    }
    refreshQueueWidget();
    if (m_mpris) m_mpris->setCurrentTrack(t);
    if (m_trayIcon) m_trayIcon->setToolTip(QString("Honeycomb — %1 - %2").arg(t.title, t.artist));
    if (m_trayNowPlayingAct) m_trayNowPlayingAct->setText(QString("▶ %1 — %2").arg(t.title, t.artist));
}

void MainWindow::fillAutoDjQueue(bool autoStartIfEmpty) {
    if (!m_autoDj) return;

    const int targetUpcoming = qMax(1, m_autoDjConfig.queueDepth);
    const int upcoming = (m_queueIndex < 0)
                             ? m_queue.size()
                             : qMax(0, m_queue.size() - 1 - m_queueIndex);
    if (upcoming >= targetUpcoming) return;

    Collection active;
    for (const auto& c : m_collections) {
        if (c.id == m_activeCollectionId) {
            active = c;
            break;
        }
    }

    const auto picked = pickAutoDjTracks(
        m_autoDjConfig,
        m_db,
        m_collections,
        m_activeCollectionId,
        m_searchEdit->text(),
        effectiveRules(active),
        m_queue,
        m_queueIndex,
        targetUpcoming - upcoming);

    if (picked.isEmpty()) return;
    for (const Track& t : picked) {
        m_queue.append(t);
    }

    refreshQueueWidget();

    if (autoStartIfEmpty && m_queueIndex < 0 && !m_queue.isEmpty()) {
        playTrackAtIndex(0);
    }
}

void MainWindow::openAutoDjSettings() {
    Collection active;
    for (const auto& c : m_collections) {
        if (c.id == m_activeCollectionId) {
            active = c;
            break;
        }
    }

    AutoDjDialog dlg(m_autoDjConfig, &m_db, m_collections, m_activeCollectionId,
                     m_searchEdit->text(), effectiveRules(active), m_queue, m_queueIndex, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_autoDjConfig = dlg.resultConfig();
    m_autoDjConfig.save();

    if (dlg.startRequested() || m_autoDj) {
        // Refresh upcoming queue tracks using the updated rules
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
            m_queue.resize(m_queueIndex + 1);
        } else {
            m_queue.clear();
            m_queueIndex = -1;
        }
        if (!m_autoDj) {
            m_autoDjBtn->setChecked(true); // triggers fillAutoDjQueue(true)
        } else {
            fillAutoDjQueue(true);
            refreshQueueWidget();
        }
    }
}

void MainWindow::playNext() {
    if (m_queue.isEmpty()) {
        if (m_autoDj) {
            fillAutoDjQueue(true);
        }
        return;
    }
    if (m_shuffle && m_queue.size() > 1) {
        const int next = QRandomGenerator::global()->bounded(m_queue.size());
        playTrackAtIndex(next);
        return;
    }
    if (m_queueIndex + 1 < m_queue.size()) {
        playTrackAtIndex(m_queueIndex + 1);
    } else if (m_autoDj) {
        fillAutoDjQueue(false);
        if (m_queueIndex + 1 < m_queue.size()) {
            playTrackAtIndex(m_queueIndex + 1);
        }
    } else if (m_repeat) {
        playTrackAtIndex(0);
    } else {
        activeStop();
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
    if (m_transportTrackLabel) {
        m_transportTrackLabel->setText(QString("%1 — %2").arg(t.artist, t.title));
        m_transportTrackLabel->setToolTip(QString("%1 — %2 (%3)").arg(t.artist, t.title, t.album));
    }
    if (m_transportRating) m_transportRating->setRating(t.rating);
    if (m_waveform) m_waveform->setTrack(t.path, t.durationMs / 1000.0);
    if (m_detailsLabel) {
        const qint64 bytes = QFileInfo(t.path).size();
        const double mb = bytes / (1024.0 * 1024.0);
        m_detailsLabel->setText(QString(
            "<b>Title:</b> %1<br>"
            "<b>Artist:</b> %2<br>"
            "<b>Album:</b> %3<br>"
            "<b>Album Artist:</b> %4<br>"
            "<b>Genre:</b> %5<br>"
            "<b>Year:</b> %6<br>"
            "<b>Album Type:</b> %7<br>"
            "<b>Format:</b> %8 (%9 kbps)<br>"
            "<b>Duration:</b> %10<br>"
            "<b>File Size:</b> %11 MB<br>"
            "<b>Plays:</b> %12<br>"
            "<b>Path:</b> %13")
            .arg(t.title.toHtmlEscaped(),
                 t.artist.toHtmlEscaped(),
                 t.album.toHtmlEscaped(),
                 t.albumArtist.toHtmlEscaped(),
                 t.genre.toHtmlEscaped(),
                 t.year > 0 ? QString::number(t.year) : "-",
                 t.albumType.isEmpty() ? QString("-") : t.albumType.toHtmlEscaped(),
                 t.format.toHtmlEscaped())
            .arg(t.bitrate)
            .arg(formatMs(t.durationMs))
            .arg(mb, 0, 'f', 2)
            .arg(t.playCount)
            .arg(t.path.toHtmlEscaped()));
    }

    m_currentLyrics = loadLyricsForTrack(t.path);
    m_sidebarActiveLyric = -1;
    m_sidebarLyricsList->clear();
    if (m_currentLyrics.lines.isEmpty()) {
        auto* item = new QListWidgetItem("No lyrics found.\n\nPlace a .lrc file next to the track or embed a LYRICS tag.");
        item->setForeground(QColor("#8c92a4"));
        item->setTextAlignment(Qt::AlignCenter);
        m_sidebarLyricsList->addItem(item);
    } else {
        for (const auto& l : m_currentLyrics.lines) {
            auto* item = new QListWidgetItem(l.text.isEmpty() ? " " : l.text);
            item->setForeground(m_currentLyrics.isSynced ? QColor("#8c92a4") : QColor("#e8eaf0"));
            m_sidebarLyricsList->addItem(item);
        }
    }

    if (m_theater) m_theater->setTrack(t, m_currentLyrics);
    if (m_miniPlayer) m_miniPlayer->setTrack(t);
}

void MainWindow::updateSidebarLyrics(int64_t posMs) {
    if (!m_currentLyrics.isSynced || m_currentLyrics.lines.isEmpty()) return;

    int active = -1;
    for (int i = 0; i < m_currentLyrics.lines.size(); ++i) {
        if (m_currentLyrics.lines[i].timeMs <= posMs) active = i;
        else break;
    }
    if (active == m_sidebarActiveLyric) return;

    if (m_sidebarActiveLyric >= 0 && m_sidebarActiveLyric < m_sidebarLyricsList->count()) {
        auto* prev = m_sidebarLyricsList->item(m_sidebarActiveLyric);
        prev->setForeground(QColor("#8c92a4"));
        QFont f = prev->font();
        f.setBold(false);
        prev->setFont(f);
    }
    m_sidebarActiveLyric = active;
    if (m_sidebarActiveLyric >= 0 && m_sidebarActiveLyric < m_sidebarLyricsList->count()) {
        auto* cur = m_sidebarLyricsList->item(m_sidebarActiveLyric);
        cur->setForeground(QColor("#f5a623"));
        QFont f = cur->font();
        f.setBold(true);
        cur->setFont(f);
        m_sidebarLyricsList->scrollToItem(cur, QAbstractItemView::PositionAtCenter);
    }
}

void MainWindow::enterTheaterMode() {
    if (!m_theater) {
        m_theater = new TheaterWindow(m_artCache, nullptr);
        m_theater->setWindowIcon(createHoneycombIcon());
        connect(m_theater, &TheaterWindow::playPauseRequested, this, &MainWindow::activeTogglePause);
        connect(m_theater, &TheaterWindow::nextRequested, this, &MainWindow::playNext);
        connect(m_theater, &TheaterWindow::prevRequested, this, &MainWindow::playPrev);
        connect(m_theater, &TheaterWindow::seekRequested, this, &MainWindow::activeSeekSeconds);
        connect(m_theater, &TheaterWindow::exitRequested, this, &MainWindow::exitTheaterMode);
    }
    if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
        m_theater->setTrack(m_queue[m_queueIndex], m_currentLyrics);
    }
    m_theater->setPlaybackState(activePlaybackState());
    m_theater->showFullScreen();
    m_theater->raise();
    m_theater->activateWindow();
}

void MainWindow::exitTheaterMode() {
    if (m_theater) m_theater->hide();
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::enterMiniPlayer() {
    if (!m_miniPlayer) {
        m_miniPlayer = new MiniPlayerWindow(m_artCache, nullptr);
        m_miniPlayer->setWindowIcon(createHoneycombIcon());
        connect(m_miniPlayer, &MiniPlayerWindow::playPauseRequested, this, &MainWindow::activeTogglePause);
        connect(m_miniPlayer, &MiniPlayerWindow::nextRequested, this, &MainWindow::playNext);
        connect(m_miniPlayer, &MiniPlayerWindow::prevRequested, this, &MainWindow::playPrev);
        connect(m_miniPlayer, &MiniPlayerWindow::expandRequested, this, &MainWindow::exitMiniPlayer);
    }
    if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
        m_miniPlayer->setTrack(m_queue[m_queueIndex]);
    }
    m_miniPlayer->setPlaybackState(activePlaybackState());
    hide();
    m_miniPlayer->show();
    m_miniPlayer->raise();
}

void MainWindow::exitMiniPlayer() {
    if (m_miniPlayer) m_miniPlayer->hide();
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::refreshQueueWidget() {
    m_queueList->clear();
    qint64 queuedMs = 0;
    for (int i = 0; i < m_queue.size(); ++i) {
        const Track& t = m_queue[i];
        queuedMs += t.durationMs;
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
    if (m_statusRightLabel) {
        const qint64 totalSec = queuedMs / 1000;
        const qint64 hrs = totalSec / 3600;
        const qint64 mins = (totalSec % 3600) / 60;
        const qint64 secs = totalSec % 60;
        const QString durText = (hrs > 0)
            ? QString("%1:%2:%3").arg(hrs).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'))
            : QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
        m_statusRightLabel->setText(QString("queued: %1 (%2 tracks)").arg(durText).arg(m_queue.size()));
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

void MainWindow::onNewPlaylist(const QStringList& initialPaths) {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "New Playlist", "Playlist name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString id = "pl_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_db.createPlaylist(id, name.trimmed())) {
        if (!initialPaths.isEmpty()) {
            m_db.addTracksToPlaylist(id, initialPaths);
        }
        loadCollectionsSidebar();
        activatePlaylist(id);
    }
}

void MainWindow::onImportM3U() {
    const QString file = QFileDialog::getOpenFileName(
        this, "Import M3U / M3U8 Playlist", QString(), "Playlists (*.m3u *.m3u8)");
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Playlist", "Could not open playlist file.");
        return;
    }

    const QDir baseDir = QFileInfo(file).dir();
    QStringList paths;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        QFileInfo fi(line);
        if (fi.isRelative()) {
            fi = QFileInfo(baseDir.filePath(line));
        }
        if (fi.exists()) {
            paths.append(fi.absoluteFilePath());
        }
    }

    const QString defaultName = QFileInfo(file).completeBaseName();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Import Playlist", "Playlist name:", QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString id = "pl_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_db.createPlaylist(id, name.trimmed())) {
        m_db.addTracksToPlaylist(id, paths);
        loadCollectionsSidebar();
        activatePlaylist(id);
    }
}

void MainWindow::onExportM3U(const QString& playlistId) {
    QString plName = "playlist";
    for (const auto& pl : m_playlists) {
        if (pl.id == playlistId) { plName = pl.name; break; }
    }

    const QString dest = QFileDialog::getSaveFileName(
        this, "Export M3U8 Playlist", plName + ".m3u8", "M3U8 Playlist (*.m3u8);;M3U Playlist (*.m3u)");
    if (dest.isEmpty()) return;

    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Playlist", "Could not write playlist file.");
        return;
    }

    const auto tracks = m_db.queryPlaylistTracks(playlistId);
    QTextStream out(&f);
    out << "#EXTM3U\n";
    for (const Track& t : tracks) {
        const int secs = static_cast<int>(t.durationMs / 1000);
        out << "#EXTINF:" << secs << "," << t.artist << " - " << t.title << "\n";
        out << t.path << "\n";
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

QString MainWindow::artFileForHash(const QString& artHash) const {
    if (artHash.isEmpty() || m_artCacheDir.isEmpty()) return {};
    const QString jpg = m_artCacheDir + "/" + artHash + ".jpg";
    if (QFileInfo::exists(jpg)) return jpg;
    const QString png = m_artCacheDir + "/" + artHash + ".png";
    if (QFileInfo::exists(png)) return png;
    return {};
}

void MainWindow::rebuildCastMenu() {
    if (!m_castMenu) return;
    m_castMenu->clear();

    QAction* localAct = m_castMenu->addAction("This Computer (Local Audio)");
    localAct->setCheckable(true);
    localAct->setChecked(!m_castingActive);
    connect(localAct, &QAction::triggered, this, [this]() {
        if (m_castingActive) stopCasting(true);
    });

    m_castMenu->addSeparator();

    if (m_castDevices.isEmpty()) {
        QAction* emptyAct = m_castMenu->addAction(
            m_castDiscovery->isScanning() ? "Searching for speakers..." : "No Google Cast speakers found");
        emptyAct->setEnabled(false);
    } else {
        const QString activeId = m_castingActive ? m_cast->currentDevice().id : QString();
        for (const auto& dev : m_castDevices) {
            const QString label = dev.isGroup()
                                      ? QString("🔊 %1 (Group)").arg(dev.name)
                                      : QString("🔈 %1 (%2)").arg(dev.name, dev.model);
            QAction* act = m_castMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(m_castingActive && dev.id == activeId);
            connect(act, &QAction::triggered, this, [this, dev]() {
                startCastToDevice(dev);
            });
        }
    }

    m_castMenu->addSeparator();
    QAction* refreshAct = m_castMenu->addAction("Refresh Speakers");
    connect(refreshAct, &QAction::triggered, m_castDiscovery, &CastDiscovery::refresh);
}

void MainWindow::startCastToDevice(const CastDevice& dev) {
    const double resumePos = m_currentPosSec;
    const bool hadTrack = (m_queueIndex >= 0 && m_queueIndex < m_queue.size());

    m_audio->stop();
    m_castingActive = true;
    m_castBtn->setText(QString("Cast: %1… ▾").arg(dev.name));
    m_cast->setVolume(m_volumeSlider->value());
    m_cast->connectToDevice(dev);

    if (hadTrack) {
        const Track& t = m_queue[m_queueIndex];
        m_cast->playTrack(t, artFileForHash(t.artHash), resumePos);
    }
    rebuildCastMenu();
}

void MainWindow::stopCasting(bool resumeLocal) {
    const double resumePos = m_currentPosSec;
    const bool wasPlaying = (m_cast->state() == AudioEngine::State::Playing);

    m_castingActive = false;
    m_cast->disconnectFromDevice();
    m_castBtn->setText("Cast ▾");
    m_castBtn->setStyleSheet("");
    rebuildCastMenu();

    if (resumeLocal && wasPlaying && m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
        m_audio->playFile(m_queue[m_queueIndex].path);
        if (resumePos > 1.0) {
            QTimer::singleShot(350, this, [this, resumePos]() {
                if (!m_castingActive) m_audio->seekSeconds(resumePos);
            });
        }
    }
}

void MainWindow::activeTogglePause() {
    if (m_castingActive) m_cast->togglePause();
    else m_audio->togglePause();
}

void MainWindow::activeSetPaused(bool paused) {
    if (m_castingActive) m_cast->setPaused(paused);
    else m_audio->setPaused(paused);
}

void MainWindow::activeStop() {
    if (m_castingActive) m_cast->stop();
    else m_audio->stop();
}

void MainWindow::activeSeekSeconds(double pos) {
    if (m_castingActive) m_cast->seekSeconds(pos);
    else m_audio->seekSeconds(pos);
}

void MainWindow::activeSetVolume(int vol) {
    if (m_castingActive) m_cast->setVolume(vol);
    else m_audio->setVolume(vol);
}

AudioEngine::State MainWindow::activePlaybackState() const {
    return m_castingActive ? m_cast->state() : m_audio->state();
}

void MainWindow::updateStatusSelection(const QString& text) {
    if (m_statusLeftLabel) {
        m_statusLeftLabel->setText(text);
    }
}

void MainWindow::pushNavHistory() {
    if (m_navigating) return;
    NavState st;
    st.collectionId = m_activeCollectionId;
    st.playlistId = m_activePlaylistId;
    st.searchText = m_searchEdit ? m_searchEdit->text() : QString();
    st.viewMode = m_viewModeCombo ? m_viewModeCombo->currentIndex() : 0;
    st.sortOrder = m_sortCombo ? m_sortCombo->currentIndex() : 0;

    if (m_navIndex >= 0 && m_navIndex < m_navHistory.size()) {
        const NavState& cur = m_navHistory[m_navIndex];
        if (cur.collectionId == st.collectionId && cur.playlistId == st.playlistId &&
            cur.searchText == st.searchText) {
            return;
        }
        m_navHistory.resize(m_navIndex + 1);
    }
    m_navHistory.append(st);
    m_navIndex = m_navHistory.size() - 1;
    if (m_backBtn) m_backBtn->setEnabled(m_navIndex > 0);
    if (m_fwdBtn) m_fwdBtn->setEnabled(false);
}

void MainWindow::navigateBack() {
    if (m_navIndex <= 0) return;
    --m_navIndex;
    applyNavState(m_navHistory[m_navIndex]);
}

void MainWindow::navigateForward() {
    if (m_navIndex + 1 >= m_navHistory.size()) return;
    ++m_navIndex;
    applyNavState(m_navHistory[m_navIndex]);
}

void MainWindow::applyNavState(const NavState& st) {
    m_navigating = true;
    if (m_searchEdit) {
        m_searchEdit->blockSignals(true);
        m_searchEdit->setText(st.searchText);
        m_searchEdit->blockSignals(false);
    }
    if (!st.playlistId.isEmpty()) {
        activatePlaylist(st.playlistId);
    } else if (!st.collectionId.isEmpty()) {
        activateCollection(st.collectionId);
    }
    m_navigating = false;
    if (m_backBtn) m_backBtn->setEnabled(m_navIndex > 0);
    if (m_fwdBtn) m_fwdBtn->setEnabled(m_navIndex + 1 < m_navHistory.size());
}

void MainWindow::openAutoTagger(const QVector<Track>& tracks) {
    if (tracks.isEmpty()) return;
    AutoTagDialog dlg(tracks, &m_db, m_artCacheDir, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasChanges()) {
        refreshCurrentView(true);
    }
}

void MainWindow::setSleepTimerMinutes(int minutes, bool afterTrack) {
    m_sleepTimer->stop();
    m_sleepAfterTrack = afterTrack;
    if (afterTrack) {
        m_scanStatusLabel->setText("Sleep timer: stop after current track");
    } else if (minutes > 0) {
        m_sleepTimer->start(minutes * 60 * 1000);
        m_scanStatusLabel->setText(QString("Sleep timer: %1 min").arg(minutes));
    } else {
        m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
    }
}

void MainWindow::openScrobbleSettings() {
    ScrobbleConfig cfg = m_scrobble->config();

    QDialog dlg(this);
    dlg.setWindowTitle("Scrobbling Settings (ListenBrainz & Last.fm)");
    dlg.resize(460, 340);

    auto* root = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout();

    auto* lbEnable = new QCheckBox("Enable ListenBrainz scrobbling", &dlg);
    lbEnable->setChecked(cfg.listenBrainzEnabled);
    auto* lbToken = new QLineEdit(cfg.listenBrainzToken, &dlg);
    lbToken->setPlaceholderText("Paste ListenBrainz User Token");
    lbToken->setEchoMode(QLineEdit::PasswordEchoOnEdit);

    auto* lfmEnable = new QCheckBox("Enable Last.fm scrobbling", &dlg);
    lfmEnable->setChecked(cfg.lastFmEnabled);
    auto* lfmKey = new QLineEdit(cfg.lastFmApiKey, &dlg);
    auto* lfmSecret = new QLineEdit(cfg.lastFmApiSecret, &dlg);
    lfmSecret->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    auto* lfmSession = new QLineEdit(cfg.lastFmSessionKey, &dlg);
    lfmSession->setPlaceholderText("Session key (auto-filled below or pasted)");

    form->addRow(lbEnable);
    form->addRow("ListenBrainz Token:", lbToken);
    form->addRow(new QLabel(" ", &dlg));
    form->addRow(lfmEnable);
    form->addRow("Last.fm API Key:", lfmKey);
    form->addRow("Last.fm API Secret:", lfmSecret);
    form->addRow("Last.fm Session Key:", lfmSession);

    auto* authBtn = new QPushButton("Sign In to Last.fm (Get Session Key)...", &dlg);
    form->addRow("", authBtn);
    root->addLayout(form);

    connect(authBtn, &QPushButton::clicked, &dlg, [&]() {
        if (lfmKey->text().trimmed().isEmpty() || lfmSecret->text().trimmed().isEmpty()) {
            QMessageBox::information(&dlg, "Last.fm", "Enter your Last.fm API Key and Secret first.");
            return;
        }
        bool okUser = false;
        const QString user = QInputDialog::getText(&dlg, "Last.fm Sign In", "Last.fm Username:", QLineEdit::Normal, "", &okUser);
        if (!okUser || user.trimmed().isEmpty()) return;
        bool okPass = false;
        const QString pass = QInputDialog::getText(&dlg, "Last.fm Sign In", "Last.fm Password:", QLineEdit::Password, "", &okPass);
        if (!okPass || pass.isEmpty()) return;

        authBtn->setEnabled(false);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_scrobble, &ScrobbleService::lastFmSessionObtained, &dlg,
                        [&dlg, authBtn, lfmSession, conn](bool ok, const QString& val) {
                            QObject::disconnect(*conn);
                            authBtn->setEnabled(true);
                            if (ok) {
                                lfmSession->setText(val);
                                QMessageBox::information(&dlg, "Last.fm", "Signed in! Session key saved.");
                            } else {
                                QMessageBox::warning(&dlg, "Last.fm Sign In Failed", val);
                            }
                        });
        m_scrobble->fetchLastFmSession(lfmKey->text().trimmed(), lfmSecret->text().trimmed(), user.trimmed(), pass);
    });

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    root->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        cfg.listenBrainzEnabled = lbEnable->isChecked();
        cfg.listenBrainzToken = lbToken->text().trimmed();
        cfg.lastFmEnabled = lfmEnable->isChecked();
        cfg.lastFmApiKey = lfmKey->text().trimmed();
        cfg.lastFmApiSecret = lfmSecret->text().trimmed();
        cfg.lastFmSessionKey = lfmSession->text().trimmed();
        cfg.save();
        m_scrobble->setConfig(cfg);
    }
}

void MainWindow::loadLibraryProfiles() {
    m_libraries.clear();
    QSettings s("Honeycomb", "Honeycomb");
    const QByteArray raw = s.value("libraries/profiles").toByteArray();
    if (!raw.isEmpty()) {
        const QJsonArray arr = QJsonDocument::fromJson(raw).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            LibraryProfile p;
            p.id = o.value("id").toString();
            p.name = o.value("name").toString();
            p.dbPath = o.value("dbPath").toString();
            if (!p.id.isEmpty() && !p.name.isEmpty() && !p.dbPath.isEmpty()) {
                m_libraries.append(p);
            }
        }
    }

    bool hasDefault = false;
    for (const auto& p : m_libraries) {
        if (p.id == "default") {
            hasDefault = true;
            break;
        }
    }
    if (!hasDefault) {
        m_libraries.prepend({"default", "Main Library", m_dataDir + "/honeycomb.db"});
        saveLibraryProfiles();
    }

    m_activeLibraryId = s.value("libraries/activeId", "default").toString();
    bool validActive = false;
    for (const auto& p : m_libraries) {
        if (p.id == m_activeLibraryId) {
            validActive = true;
            break;
        }
    }
    if (!validActive) m_activeLibraryId = m_libraries.first().id;
}

void MainWindow::saveLibraryProfiles() const {
    QJsonArray arr;
    for (const auto& p : m_libraries) {
        arr.append(QJsonObject{
            {"id", p.id},
            {"name", p.name},
            {"dbPath", p.dbPath}
        });
    }
    QSettings s("Honeycomb", "Honeycomb");
    s.setValue("libraries/profiles", QJsonDocument(arr).toJson(QJsonDocument::Compact));
    s.setValue("libraries/activeId", m_activeLibraryId);
}

void MainWindow::rebuildLibraryMenu() {
    if (!m_libraryMenu || !m_libraryBtn) return;
    m_libraryMenu->clear();

    QString activeName = "Main Library";
    auto* grp = new QActionGroup(m_libraryMenu);
    for (const auto& lib : m_libraries) {
        QAction* act = m_libraryMenu->addAction("📚 " + lib.name);
        act->setCheckable(true);
        const bool isCur = (lib.id == m_activeLibraryId);
        act->setChecked(isCur);
        if (isCur) activeName = lib.name;
        grp->addAction(act);
        connect(act, &QAction::triggered, this, [this, id = lib.id]() {
            if (id != m_activeLibraryId) switchLibrary(id);
        });
    }
    m_libraryBtn->setText(activeName + " ▾");

    m_libraryMenu->addSeparator();
    connect(m_libraryMenu->addAction("New Library..."), &QAction::triggered, this, &MainWindow::onNewLibrary);
    connect(m_libraryMenu->addAction("Rename Current Library..."), &QAction::triggered, this, &MainWindow::onRenameLibrary);
    QAction* delAct = m_libraryMenu->addAction("Delete Current Library");
    delAct->setEnabled(m_activeLibraryId != "default");
    connect(delAct, &QAction::triggered, this, &MainWindow::onDeleteLibrary);
    m_libraryMenu->addSeparator();
    connect(m_libraryMenu->addAction("Manage Library Folders..."), &QAction::triggered, this, &MainWindow::openLibraryFoldersDialog);
}

void MainWindow::switchLibrary(const QString& id) {
    QString targetDb;
    QString targetName;
    for (const auto& p : m_libraries) {
        if (p.id == id) {
            targetDb = p.dbPath;
            targetName = p.name;
            break;
        }
    }
    if (targetDb.isEmpty()) return;

    if (m_scanner->isScanning()) {
        m_scanner->cancel();
    }

    m_activeLibraryId = id;
    saveLibraryProfiles();

    m_db.open(targetDb);
    m_viewingInbox = false;
    if (m_inboxBar) m_inboxBar->setVisible(false);
    if (m_trackTable) m_trackTable->setInboxMode(false);
    m_activePlaylistId.clear();
    m_navHistory.clear();
    m_navIndex = -1;
    if (m_backBtn) m_backBtn->setEnabled(false);
    if (m_fwdBtn) m_fwdBtn->setEnabled(false);

    rebuildLibraryMenu();
    loadCollectionsSidebar();
    if (m_radioPage) m_radioPage->reload();
    if (m_podcastPage) m_podcastPage->reload();
    m_scanner->watchFolders(m_db.libraryFolders());

    if (!m_collections.isEmpty()) {
        activateCollection(m_collections.first().id);
    } else {
        refreshCurrentView(true);
    }
    m_scanStatusLabel->setText(QString("%1 — %2 tracks").arg(targetName).arg(m_db.trackCount()));
}

void MainWindow::onNewLibrary() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Create New Library",
        "Library Name (e.g., Soundtracks, Christmas Albums):",
        QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    const QString id = QUuid::createUuid().toString(QUuid::Id128).left(12);
    LibraryProfile p;
    p.id = id;
    p.name = name;
    p.dbPath = m_dataDir + "/honeycomb_" + id + ".db";
    m_libraries.append(p);
    saveLibraryProfiles();
    switchLibrary(id);

    const auto reply = QMessageBox::question(
        this, "Add Folders",
        QString("Created library \"%1\". Would you like to add music folders to it now?").arg(name));
    if (reply == QMessageBox::Yes) {
        openLibraryFoldersDialog();
    }
}

void MainWindow::onRenameLibrary() {
    for (auto& p : m_libraries) {
        if (p.id == m_activeLibraryId) {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, "Rename Library", "Library Name:", QLineEdit::Normal, p.name, &ok).trimmed();
            if (ok && !name.isEmpty()) {
                p.name = name;
                saveLibraryProfiles();
                rebuildLibraryMenu();
            }
            break;
        }
    }
}

void MainWindow::onDeleteLibrary() {
    if (m_activeLibraryId == "default") return;
    QString curName;
    QString curDb;
    int idx = -1;
    for (int i = 0; i < m_libraries.size(); ++i) {
        if (m_libraries[i].id == m_activeLibraryId) {
            curName = m_libraries[i].name;
            curDb = m_libraries[i].dbPath;
            idx = i;
            break;
        }
    }
    if (idx < 0) return;

    const auto ans = QMessageBox::question(
        this, "Delete Library",
        QString("Delete library \"%1\"? (Your audio files on disk will NOT be deleted.)").arg(curName));
    if (ans != QMessageBox::Yes) return;

    m_libraries.removeAt(idx);
    switchLibrary("default");
    if (!curDb.isEmpty() && curDb != m_dataDir + "/honeycomb.db") {
        QFile::remove(curDb);
        QFile::remove(curDb + "-wal");
        QFile::remove(curDb + "-shm");
    }
}

void MainWindow::openLibraryFoldersDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Manage Library Folders");
    dlg.resize(540, 340);

    auto* root = new QVBoxLayout(&dlg);
    auto* info = new QLabel("Add folders from any drive or mount point. All folders below are scanned into the active library.", &dlg);
    info->setWordWrap(true);
    info->setStyleSheet("color: #9aa0b0; font-size: 12px;");
    root->addWidget(info);

    auto* list = new QListWidget(&dlg);
    auto reloadList = [&]() {
        list->clear();
        for (const QString& f : m_db.libraryFolders()) {
            list->addItem(f);
        }
    };
    reloadList();
    root->addWidget(list, 1);

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton("+ Add Folder...", &dlg);
    auto* remBtn = new QPushButton("Remove Selected", &dlg);
    auto* scanBtn = new QPushButton("Rescan All", &dlg);
    auto* closeBtn = new QPushButton("Close", &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addStretch();
    btnRow->addWidget(scanBtn);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    bool changed = false;
    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(
            &dlg, "Select Music Folder",
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
        if (dir.isEmpty()) return;
        m_db.addLibraryFolder(dir);
        reloadList();
        changed = true;
    });
    connect(remBtn, &QPushButton::clicked, &dlg, [&]() {
        auto* item = list->currentItem();
        if (!item) return;
        const QString folder = item->text();
        m_db.removeLibraryFolder(folder);
        m_db.removeTracksUnderFolder(folder);
        reloadList();
        changed = true;
    });
    connect(scanBtn, &QPushButton::clicked, &dlg, [&]() {
        const QStringList folders = m_db.libraryFolders();
        if (!folders.isEmpty()) m_scanner->startScan(folders);
        dlg.accept();
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
    if (changed) {
        m_scanner->watchFolders(m_db.libraryFolders());
        const QStringList folders = m_db.libraryFolders();
        if (!folders.isEmpty()) {
            m_scanner->startScan(folders);
        } else {
            loadCollectionsSidebar();
            refreshCurrentView(true);
            m_scanStatusLabel->setText(QString("%1 tracks in library").arg(m_db.trackCount()));
        }
    }
}

void MainWindow::activateInbox() {
    m_viewingInbox = true;
    m_activePlaylistId.clear();
    m_activeCollectionId.clear();
    if (m_inboxBar) m_inboxBar->setVisible(true);
    if (m_trackTable) {
        m_trackTable->setActiveStaticPlaylistId(QString());
        m_trackTable->setInboxMode(true);
    }
    closeAlbumDrawer();
    m_colBrowser->setVisible(false);
    m_artZoomSlider->setVisible(false);

    for (int i = 0; i < m_sidebarList->count(); ++i) {
        auto* item = m_sidebarList->item(i);
        if (item->data(Qt::UserRole).toString() == "__inbox__") {
            m_sidebarList->setCurrentItem(item);
            break;
        }
    }
    refreshCurrentView(false);
}

void MainWindow::onScanFolderToInbox() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select Folder to Scan into Inbox",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    if (dir.isEmpty()) return;

    activateInbox();
    m_scanner->startScan({dir}, true);
}

} // namespace honeycomb

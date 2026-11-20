#pragma once

#include "audio/AudioEngine.h"
#include "cast/CastController.h"
#include "cast/CastDiscovery.h"
#include "core/Collection.h"
#include "db/Database.h"
#include "linux/MprisService.h"
#include "scanner/LibraryScanner.h"
#include "services/ScrobbleService.h"
#include "ui/AlbumGridView.h"
#include "ui/AlbumTracksView.h"
#include "ui/ArtCache.h"
#include "ui/AutoDjDialog.h"
#include "ui/ColumnBrowser.h"
#include "ui/LyricsReader.h"
#include "ui/MiniPlayerWindow.h"
#include "ui/MusicExplorerPage.h"
#include "ui/RadioPodcastPages.h"
#include "ui/StarRatingWidget.h"
#include "ui/TheaterWindow.h"
#include "ui/TrackTableView.h"
#include "ui/WaveformWidget.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVector>

namespace honeycomb {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupTrayAndMpris(const QString& artCacheDir);
    void loadCollectionsSidebar();
    void activateCollection(const QString& id);
    void activatePlaylist(const QString& id);
    void refreshCurrentView(bool resetColumnBrowser = true);
    void saveCurrentCollectionViewState();
    RuleGroup effectiveRules(const Collection& active) const;
    QVector<Track> queryTracksForAlbum(const Collection& active, const Album& album) const;
    void groupTracksAsSinglesChart(const QVector<Track>& tracks);

    void openAlbumDrawer(const Album& album);
    void closeAlbumDrawer();

    void enterTheaterMode();
    void exitTheaterMode();
    void enterMiniPlayer();
    void exitMiniPlayer();
    void updateSidebarLyrics(int64_t posMs);

    void playQueue(const QVector<Track>& tracks, int startIndex);
    void playNextTrack(const Track& track);
    void playNextTracks(const QVector<Track>& tracks);
    void enqueueTrack(const Track& track);
    void findArtist(const QString& artist);
    void findTrack(const QString& title);
    void openTagEditor(const QVector<Track>& tracks);
    void openAutoTagger(const QVector<Track>& tracks);
    void openReplayGainScanner(const QVector<Track>& tracks);
    void openDeviceSyncDialog();
    void openShortcutsDialog();
    void setupShortcuts();
    void reloadShortcuts();
    void addBookmarkAtCurrentPos();
    void rebuildBookmarksMenu(QMenu* menu);
    void openScrobbleSettings();
    void setSleepTimerMinutes(int minutes, bool afterTrack);

    struct BrowseTabState {
        QString title = "All Music";
        QString collectionId;
        QString playlistId;
        bool isInbox = false;
        bool isRadio = false;
        bool isPodcasts = false;
        bool isExplorer = false;
        QString searchText;
    };
    void openNewBrowseTab(const QString& title = "All Music");
    void closeBrowseTab(int index);
    void saveCurrentTabState();
    void restoreBrowseTab(int index);
    void updateCurrentTabTitle(const QString& title);

    void playTrackAtIndex(int index);
    void playNext();
    bool playNextAlbumInCollection();
    void playPrev();
    void fillAutoDjQueue(bool autoStartIfEmpty = false);
    void openAutoDjSettings();
    void updateNowPlayingPanel(const Track& t);
    void refreshQueueWidget();

    void onAddMusicFolder();
    void openLibraryFoldersDialog();
    void onNewCollection();
    void onNewPlaylist(const QStringList& initialPaths = QStringList());
    void onImportM3U();
    void onExportM3U(const QString& playlistId);
    void onEditCurrentCollection();
    void onDeleteCurrentCollection();

    struct LibraryProfile {
        QString id;
        QString name;
        QString dbPath;
    };
    void loadLibraryProfiles();
    void saveLibraryProfiles() const;
    void rebuildLibraryMenu();
    void switchLibrary(const QString& id);
    void onNewLibrary();
    void onRenameLibrary();
    void onDeleteLibrary();
    void activateInbox();
    void onScanFolderToInbox();

    void rebuildCastMenu();
    void startCastToDevice(const CastDevice& dev);
    void stopCasting(bool resumeLocal);
    QString artFileForHash(const QString& artHash) const;
    void activeTogglePause();
    void activeSetPaused(bool paused);
    void activeStop();
    void activeSeekSeconds(double pos);
    void activeSetVolume(int vol);
    AudioEngine::State activePlaybackState() const;

    struct NavState {
        QString collectionId;
        QString playlistId;
        QString searchText;
        int viewMode = 0;
        int sortOrder = 0;
    };
    void pushNavHistory();
    void navigateBack();
    void navigateForward();
    void applyNavState(const NavState& st);
    void updateStatusSelection(const QString& text);

    Database m_db;
    QString m_dataDir;
    QString m_artCacheDir;
    QVector<LibraryProfile> m_libraries;
    QString m_activeLibraryId;
    bool m_viewingInbox = false;
    ArtCache* m_artCache = nullptr;
    LibraryScanner* m_scanner = nullptr;
    AudioEngine* m_audio = nullptr;
    CastDiscovery* m_castDiscovery = nullptr;
    CastController* m_cast = nullptr;
    QVector<CastDevice> m_castDevices;
    bool m_castingActive = false;
    ScrobbleService* m_scrobble = nullptr;
    qint64 m_trackStartedEpoch = 0;
    bool m_scrobbledCurrent = false;
    QTimer* m_sleepTimer = nullptr;
    bool m_sleepAfterTrack = false;
    MprisService* m_mpris = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QAction* m_trayNowPlayingAct = nullptr;
    TheaterWindow* m_theater = nullptr;
    MiniPlayerWindow* m_miniPlayer = nullptr;

    QVector<Collection> m_collections;
    QString m_activeCollectionId;
    QVector<Database::Playlist> m_playlists;
    QString m_activePlaylistId;

    // Navigation history
    QVector<NavState> m_navHistory;
    int m_navIndex = -1;
    bool m_navigating = false;

    // Left Sidebar
    QWidget* m_leftPane = nullptr;
    QPushButton* m_libraryBtn = nullptr;
    QMenu* m_libraryMenu = nullptr;
    QListWidget* m_sidebarList = nullptr;
    QLabel* m_scanStatusLabel = nullptr;
    QWidget* m_inboxBar = nullptr;

    // Center Top Bar, Column Browser & Views
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_fwdBtn = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_colBrowserBtn = nullptr;
    QPushButton* m_groupByFolderBtn = nullptr;
    QComboBox* m_viewModeCombo = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QSlider* m_artZoomSlider = nullptr;
    QPushButton* m_editColBtn = nullptr;
    QPushButton* m_panelsBtn = nullptr;
    QAction* m_actLeftSidebar = nullptr;
    QAction* m_actRightSidebar = nullptr;
    QAction* m_actColBrowser = nullptr;
    QAction* m_actNpArt = nullptr;
    QAction* m_actWaveform = nullptr;
    QAction* m_actQueueLyrics = nullptr;
    QAction* m_actStatusBar = nullptr;
    QAction* m_actAlphaBar = nullptr;

    QWidget* m_alphaBar = nullptr;
    QString m_alphaFilter; // "", "#", or "A".."Z"
    QVector<QPushButton*> m_alphaButtons;

    ColumnBrowser* m_colBrowser = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    AlbumGridView* m_albumGrid = nullptr;
    AlbumTracksView* m_albumTracks = nullptr;
    TrackTableView* m_trackTable = nullptr;
    RadioPage* m_radioPage = nullptr;
    PodcastPage* m_podcastPage = nullptr;
    MusicExplorerPage* m_explorerPage = nullptr;

    // Album Grid Inline Track Drawer
    QWidget* m_albumDrawer = nullptr;
    QLabel* m_drawerArt = nullptr;
    QLabel* m_drawerTitle = nullptr;
    QLabel* m_drawerSub = nullptr;
    QTableWidget* m_drawerTable = nullptr;
    QTableWidget* m_drawerTable2 = nullptr;
    Album m_drawerAlbum;
    QVector<Track> m_drawerTracks;
    QVector<int> m_drawerRowToTrack;
    QVector<int> m_drawerRowToTrack2;

    // Right Sidebar (Now Playing + Queue / Lyrics / Details)
    QWidget* m_rightPane = nullptr;
    QTabWidget* m_rightTabs = nullptr;
    QLabel* m_npArtLabel = nullptr;
    QLabel* m_npTitleLabel = nullptr;
    QLabel* m_npArtistLabel = nullptr;
    QLabel* m_npAlbumLabel = nullptr;
    QLabel* m_npFormatLabel = nullptr;
    StarRatingWidget* m_npRating = nullptr;
    WaveformWidget* m_waveform = nullptr;
    QPushButton* m_autoDjBtn = nullptr;
    QListWidget* m_queueList = nullptr;
    QListWidget* m_sidebarLyricsList = nullptr;
    QLabel* m_detailsLabel = nullptr;
    LyricsData m_currentLyrics;
    int m_sidebarActiveLyric = -1;

    // Selection & Queue Status Bar
    QWidget* m_statusBar = nullptr;
    QLabel* m_statusLeftLabel = nullptr;
    QLabel* m_statusRightLabel = nullptr;

    // Bottom Transport Bar
    QPushButton* m_playPauseBtn = nullptr;
    QPushButton* m_shuffleBtn = nullptr;
    QPushButton* m_repeatBtn = nullptr;
    QLabel* m_transportTrackLabel = nullptr;
    StarRatingWidget* m_transportRating = nullptr;
    QSlider* m_seekSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QPushButton* m_castBtn = nullptr;
    QMenu* m_castMenu = nullptr;

    // Playback Queue State
    QVector<Track> m_queue;
    int m_queueIndex = -1;
    double m_currentPosSec = 0.0;
    double m_currentDurationSec = 0.0;
    bool m_userSeeking = false;
    bool m_shuffle = false;
    bool m_repeat = false;
    bool m_autoDj = false;
    bool m_continueNextAlbum = false;
    AutoDjConfig m_autoDjConfig;

    // Multi-Tab Browsing & Shortcuts & Resume
    QTabBar* m_browseTabs = nullptr;
    QVector<BrowseTabState> m_tabStates;
    bool m_switchingTab = false;
    QMap<QString, QShortcut*> m_shortcuts;
    double m_lastResumeSavedSec = 0.0;
};

} // namespace honeycomb

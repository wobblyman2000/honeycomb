#pragma once

#include "audio/AudioEngine.h"
#include "core/Collection.h"
#include "db/Database.h"
#include "linux/MprisService.h"
#include "scanner/LibraryScanner.h"
#include "ui/AlbumGridView.h"
#include "ui/AlbumTracksView.h"
#include "ui/ArtCache.h"
#include "ui/AutoDjDialog.h"
#include "ui/ColumnBrowser.h"
#include "ui/LyricsReader.h"
#include "ui/MiniPlayerWindow.h"
#include "ui/StarRatingWidget.h"
#include "ui/TheaterWindow.h"
#include "ui/TrackTableView.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QSystemTrayIcon>
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
    void playTrackAtIndex(int index);
    void playNext();
    void playPrev();
    void fillAutoDjQueue(bool autoStartIfEmpty = false);
    void openAutoDjSettings();
    void updateNowPlayingPanel(const Track& t);
    void refreshQueueWidget();

    void onAddMusicFolder();
    void onNewCollection();
    void onNewPlaylist(const QStringList& initialPaths = QStringList());
    void onImportM3U();
    void onExportM3U(const QString& playlistId);
    void onEditCurrentCollection();
    void onDeleteCurrentCollection();

    Database m_db;
    QString m_artCacheDir;
    ArtCache* m_artCache = nullptr;
    LibraryScanner* m_scanner = nullptr;
    AudioEngine* m_audio = nullptr;
    MprisService* m_mpris = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QAction* m_trayNowPlayingAct = nullptr;
    TheaterWindow* m_theater = nullptr;
    MiniPlayerWindow* m_miniPlayer = nullptr;

    QVector<Collection> m_collections;
    QString m_activeCollectionId;
    QVector<Database::Playlist> m_playlists;
    QString m_activePlaylistId;

    // Left Sidebar
    QListWidget* m_sidebarList = nullptr;
    QLabel* m_scanStatusLabel = nullptr;

    // Center Top Bar, Column Browser & Views
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_colBrowserBtn = nullptr;
    QComboBox* m_viewModeCombo = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QSlider* m_artZoomSlider = nullptr;
    QPushButton* m_editColBtn = nullptr;

    ColumnBrowser* m_colBrowser = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    AlbumGridView* m_albumGrid = nullptr;
    AlbumTracksView* m_albumTracks = nullptr;
    TrackTableView* m_trackTable = nullptr;

    // Album Grid Inline Track Drawer
    QWidget* m_albumDrawer = nullptr;
    QLabel* m_drawerArt = nullptr;
    QLabel* m_drawerTitle = nullptr;
    QLabel* m_drawerSub = nullptr;
    QTableWidget* m_drawerTable = nullptr;
    Album m_drawerAlbum;
    QVector<Track> m_drawerTracks;
    QVector<int> m_drawerRowToTrack;

    // Right Sidebar (Now Playing + Queue / Lyrics)
    QLabel* m_npArtLabel = nullptr;
    QLabel* m_npTitleLabel = nullptr;
    QLabel* m_npArtistLabel = nullptr;
    QLabel* m_npAlbumLabel = nullptr;
    QLabel* m_npFormatLabel = nullptr;
    StarRatingWidget* m_npRating = nullptr;
    QPushButton* m_autoDjBtn = nullptr;
    QListWidget* m_queueList = nullptr;
    QListWidget* m_sidebarLyricsList = nullptr;
    LyricsData m_currentLyrics;
    int m_sidebarActiveLyric = -1;

    // Bottom Transport Bar
    QPushButton* m_playPauseBtn = nullptr;
    QPushButton* m_shuffleBtn = nullptr;
    QPushButton* m_repeatBtn = nullptr;
    QSlider* m_seekSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QSlider* m_volumeSlider = nullptr;

    // Playback Queue State
    QVector<Track> m_queue;
    int m_queueIndex = -1;
    double m_currentDurationSec = 0.0;
    bool m_userSeeking = false;
    bool m_shuffle = false;
    bool m_repeat = false;
    bool m_autoDj = false;
    AutoDjConfig m_autoDjConfig;
};

} // namespace honeycomb

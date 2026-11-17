cmake_minimum_required(VERSION 3.20)
project(Honeycomb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent DBus Network)
find_package(PkgConfig REQUIRED)

pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_executable(honeycomb
    src/main.cpp
    src/core/FilterRule.cpp
    src/db/Database.cpp
    src/scanner/LibraryScanner.cpp
    src/audio/AudioEngine.cpp
    src/cast/CastDevice.h
    src/cast/CastDiscovery.h
    src/cast/CastDiscovery.cpp
    src/cast/CastMediaServer.h
    src/cast/CastMediaServer.cpp
    src/cast/CastController.h
    src/cast/CastController.cpp
    src/services/ScrobbleService.h
    src/services/ScrobbleService.cpp
    src/linux/MprisService.h
    src/linux/MprisService.cpp
    src/ui/ArtCache.cpp
    src/ui/ColumnBrowser.cpp
    src/ui/StarRatingWidget.h
    src/ui/WaveformWidget.h
    src/ui/WaveformWidget.cpp
    src/ui/LyricsReader.h
    src/ui/TheaterWindow.h
    src/ui/TheaterWindow.cpp
    src/ui/MiniPlayerWindow.h
    src/ui/MiniPlayerWindow.cpp
    src/ui/AlbumGridView.cpp
    src/ui/AlbumTracksView.cpp
    src/ui/TrackTableView.cpp
    src/ui/RuleBuilderDialog.cpp
    src/ui/AutoDjDialog.h
    src/ui/AutoDjDialog.cpp
    src/ui/TagEditorDialog.h
    src/ui/TagEditorDialog.cpp
    src/ui/AutoTagDialog.h
    src/ui/AutoTagDialog.cpp
    src/ui/RadioPodcastPages.h
    src/ui/RadioPodcastPages.cpp
    src/ui/MainWindow.cpp
)

target_include_directories(honeycomb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(honeycomb PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::DBus
    Qt6::Network
    PkgConfig::TAGLIB
    PkgConfig::SQLITE3
    PkgConfig::MPV
)

target_compile_options(honeycomb PRIVATE -Wall -Wextra -Wpedantic)

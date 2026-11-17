#pragma once

#include "core/Track.h"
#include "db/Database.h"

#include <QListWidget>
#include <QNetworkAccessManager>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace honeycomb {

class RadioPage : public QWidget {
    Q_OBJECT
public:
    explicit RadioPage(Database* db, QWidget* parent = nullptr);
    void reload();

signals:
    void streamPlayRequested(const Track& streamTrack);

private:
    void onAddStation();
    void onRemoveSelected();
    void playRow(int row);

    Database* m_db = nullptr;
    QTableWidget* m_table = nullptr;
    QVector<Database::RadioStation> m_stations;
};

class PodcastPage : public QWidget {
    Q_OBJECT
public:
    explicit PodcastPage(Database* db, QWidget* parent = nullptr);
    void reload();

signals:
    void streamPlayRequested(const Track& episodeTrack);

private:
    struct Episode {
        QString title;
        QString pubDate;
        QString duration;
        QString audioUrl;
    };

    void onSubscribe();
    void onRemoveFeed();
    void fetchSelectedFeed();
    void playEpisodeRow(int row);

    Database* m_db = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QListWidget* m_feedList = nullptr;
    QTableWidget* m_epTable = nullptr;
    QVector<Database::PodcastFeed> m_feeds;
    QVector<Episode> m_episodes;
    QString m_activeFeedTitle;
};

} // namespace honeycomb

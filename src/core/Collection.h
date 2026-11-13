#pragma once

#include "FilterRule.h"
#include <QJsonObject>
#include <QString>

namespace honeycomb {

enum class ViewMode {
    AlbumCovers,
    AlbumAndTracks,
    TracksTable
};

enum class SortOrder {
    AlbumArtistYear,
    AlbumTitle,
    YearDesc,
    RecentlyAdded,
    TrackTitle
};

enum class LimitOrder {
    Default,
    Random,
    HighestRated,
    MostPlayed,
    LeastPlayed,
    RecentlyAdded
};

struct Collection {
    QString id;
    QString name;
    bool isSystem = false;
    bool isSmartPlaylist = false; // false = Collection, true = Smart Playlist
    RuleGroup rules;

    // Per-Collection View Settings
    ViewMode viewMode = ViewMode::AlbumCovers;
    int artworkSize = 200;
    SortOrder sortOrder = SortOrder::AlbumArtistYear;
    bool showColumnBrowser = false;
    bool showRightSidebar = true;

    // Smart Playlist Limits (0 = unlimited)
    int limitCount = 0;
    LimitOrder limitOrder = LimitOrder::Default;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["isSystem"] = isSystem;
        obj["isSmartPlaylist"] = isSmartPlaylist;
        obj["rules"] = rules.toJson();
        obj["viewMode"] = static_cast<int>(viewMode);
        obj["artworkSize"] = artworkSize;
        obj["sortOrder"] = static_cast<int>(sortOrder);
        obj["showColumnBrowser"] = showColumnBrowser;
        obj["showRightSidebar"] = showRightSidebar;
        obj["limitCount"] = limitCount;
        obj["limitOrder"] = static_cast<int>(limitOrder);
        return obj;
    }

    static Collection fromJson(const QJsonObject& obj) {
        Collection c;
        c.id = obj["id"].toString();
        c.name = obj["name"].toString();
        c.isSystem = obj["isSystem"].toBool(false);
        c.isSmartPlaylist = obj["isSmartPlaylist"].toBool(false);
        c.rules = RuleGroup::fromJson(obj["rules"].toObject());
        c.viewMode = static_cast<ViewMode>(obj["viewMode"].toInt(0));
        c.artworkSize = obj["artworkSize"].toInt(200);
        c.sortOrder = static_cast<SortOrder>(obj["sortOrder"].toInt(0));
        c.showColumnBrowser = obj["showColumnBrowser"].toBool(false);
        c.showRightSidebar = obj["showRightSidebar"].toBool(true);
        c.limitCount = obj["limitCount"].toInt(0);
        c.limitOrder = static_cast<LimitOrder>(obj["limitOrder"].toInt(0));
        return c;
    }
};

} // namespace honeycomb

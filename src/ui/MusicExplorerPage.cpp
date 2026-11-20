#include "ui/MusicExplorerPage.h"
#include "services/ScrobbleService.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSplitter>
#include <QTabWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>

namespace honeycomb {

namespace {
constexpr const char* kUserAgent = "Honeycomb/0.1 (https://github.com/wobblyman2000/honeycomb)";

QString formatMs(int64_t ms) {
    const int64_t secs = ms / 1000;
    return QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
}
} // namespace

MusicExplorerPage::MusicExplorerPage(Database* db, ArtCache* artCache, QWidget* parent)
    : QWidget(parent), m_db(db), m_artCache(artCache) {
    m_nam = new QNetworkAccessManager(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // Search + Follow bar
    auto* topRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Explore any artist (biography, discography, top tracks, similar artists)...");
    m_searchEdit->setClearButtonEnabled(true);
    auto* goBtn = new QPushButton("Explore", this);
    m_followPlayingChk = new QCheckBox("Follow Now Playing", this);
    m_followPlayingChk->setChecked(true);
    auto* showInLibBtn = new QPushButton("Show in Library", this);
    topRow->addWidget(m_searchEdit, 1);
    topRow->addWidget(goBtn);
    topRow->addWidget(m_followPlayingChk);
    topRow->addWidget(showInLibBtn);
    root->addLayout(topRow);

    // Artist Banner (Photo + Title + Bio)
    auto* banner = new QWidget(this);
    banner->setStyleSheet("background: #181a22; border: 1px solid #262936; border-radius: 6px;");
    auto* bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(12, 12, 12, 12);
    bannerLayout->setSpacing(14);

    m_artistPhoto = new QLabel(banner);
    m_artistPhoto->setFixedSize(136, 136);
    m_artistPhoto->setAlignment(Qt::AlignCenter);
    m_artistPhoto->setStyleSheet("background: #12141b; border-radius: 6px; color: #7a8094;");
    m_artistPhoto->setText("Artist");
    bannerLayout->addWidget(m_artistPhoto, 0, Qt::AlignTop);

    auto* bioCol = new QVBoxLayout();
    bioCol->setSpacing(4);
    m_artistTitle = new QLabel("Music Explorer", banner);
    m_artistTitle->setStyleSheet("font-size: 20px; font-weight: 800; color: #f5a623; border: none;");
    m_artistMeta = new QLabel("Play a track or search an artist above to explore.", banner);
    m_artistMeta->setStyleSheet("color: #9aa0b0; font-size: 12px; border: none;");
    m_bioBrowser = new QTextBrowser(banner);
    m_bioBrowser->setOpenExternalLinks(true);
    m_bioBrowser->setMaximumHeight(110);
    m_bioBrowser->setStyleSheet("background: transparent; border: none; color: #d7dae4; font-size: 12px;");
    bioCol->addWidget(m_artistTitle);
    bioCol->addWidget(m_artistMeta);
    bioCol->addWidget(m_bioBrowser, 1);
    bannerLayout->addLayout(bioCol, 1);
    root->addWidget(banner);

    // Lower Splitter: Left (In Library Albums + Similar Artists) | Right (Top Tracks / Discography Tabs)
    auto* split = new QSplitter(Qt::Horizontal, this);

    auto* leftPane = new QWidget(split);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    leftLayout->addWidget(new QLabel("In Your Library (Albums):", leftPane));
    m_localAlbumsList = new QListWidget(leftPane);
    m_localAlbumsList->setIconSize(QSize(48, 48));
    leftLayout->addWidget(m_localAlbumsList, 1);

    leftLayout->addWidget(new QLabel("Similar / Related Artists (double-click to explore):", leftPane));
    m_similarList = new QListWidget(leftPane);
    leftLayout->addWidget(m_similarList, 1);

    auto* rightTabs = new QTabWidget(split);

    m_topTracksTable = new QTableWidget(0, 4, rightTabs);
    m_topTracksTable->setHorizontalHeaderLabels({"Track", "Album", "Plays / ★", "Time"});
    m_topTracksTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_topTracksTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_topTracksTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_topTracksTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_topTracksTable->verticalHeader()->setVisible(false);
    m_topTracksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_topTracksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_topTracksTable->setShowGrid(false);
    m_topTracksTable->setAlternatingRowColors(true);

    m_discogTable = new QTableWidget(0, 4, rightTabs);
    m_discogTable->setHorizontalHeaderLabels({"Year", "Release Title", "Type", "Status"});
    m_discogTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_discogTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_discogTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_discogTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_discogTable->verticalHeader()->setVisible(false);
    m_discogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_discogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_discogTable->setShowGrid(false);
    m_discogTable->setAlternatingRowColors(true);

    rightTabs->addTab(m_topTracksTable, "Library Top Tracks");
    rightTabs->addTab(m_discogTable, "Full Discography (MusicBrainz)");

    split->addWidget(leftPane);
    split->addWidget(rightTabs);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    connect(goBtn, &QPushButton::clicked, this, [this]() {
        exploreArtist(m_searchEdit->text().trimmed());
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        exploreArtist(m_searchEdit->text().trimmed());
    });
    connect(showInLibBtn, &QPushButton::clicked, this, [this]() {
        if (!m_currentArtist.isEmpty()) emit findArtistInLibraryRequested(m_currentArtist);
    });
    connect(m_similarList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString name = item->data(Qt::UserRole).toString();
        if (!name.isEmpty()) exploreArtist(name);
    });
    connect(m_localAlbumsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const int idx = m_localAlbumsList->row(item);
        if (idx < 0 || idx >= m_localAlbums.size()) return;
        const Album& alb = m_localAlbums[idx];
        RuleGroup all;
        const auto tracks = m_db->queryTracks(all, QString(), SortOrder::AlbumArtistYear, alb.album, alb.albumArtist);
        if (!tracks.isEmpty()) emit playTracksRequested(tracks, 0);
    });
    connect(m_topTracksTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_localTracks.size()) {
            emit playTracksRequested(m_localTracks, row);
        }
    });
}

void MusicExplorerPage::onNowPlayingTrackChanged(const Track& track) {
    if (!m_followPlayingChk->isChecked()) return;
    const QString artist = !track.albumArtist.isEmpty() && track.albumArtist != "Various Artists"
                               ? track.albumArtist
                               : track.artist;
    if (!artist.isEmpty() && artist.compare(m_currentArtist, Qt::CaseInsensitive) != 0) {
        exploreArtist(artist);
    }
}

void MusicExplorerPage::exploreArtist(const QString& artistName) {
    const QString clean = artistName.trimmed();
    if (clean.isEmpty()) return;

    m_currentArtist = clean;
    m_searchEdit->setText(clean);
    m_artistTitle->setText(clean);
    m_artistMeta->setText("Loading artist profile...");
    m_bioBrowser->setPlainText("Fetching artist biography...");
    m_artistPhoto->setPixmap(QPixmap());
    m_artistPhoto->setText("Loading...");
    m_similarList->clear();
    m_discogTable->setRowCount(0);

    loadLocalLibrarySection(clean);
    fetchWikipediaSummary(clean);
    fetchMusicBrainzArtist(clean);
    fetchLastFmInfo(clean);
}

void MusicExplorerPage::loadLocalLibrarySection(const QString& artist) {
    RuleGroup artistRule;
    artistRule.logic = RuleGroup::Logic::Any;
    artistRule.conditions.append({FilterField::AlbumArtist, FilterOp::Is, artist});
    artistRule.conditions.append({FilterField::Artist, FilterOp::Is, artist});

    m_localAlbums = m_db->queryAlbums(artistRule, QString(), SortOrder::AlbumArtistTypeYear);
    m_localTracks = m_db->queryTracks(artistRule, QString(), SortOrder::AlbumArtistYear);

    std::sort(m_localTracks.begin(), m_localTracks.end(), [](const Track& a, const Track& b) {
        if (a.rating != b.rating) return a.rating > b.rating;
        if (a.playCount != b.playCount) return a.playCount > b.playCount;
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });

    m_localAlbumsList->clear();
    for (const Album& a : m_localAlbums) {
        const QString yr = a.year > 0 ? QString(" (%1)").arg(a.year) : "";
        const QString tp = !a.albumType.isEmpty() ? QString(" [%1]").arg(a.albumType) : "";
        auto* item = new QListWidgetItem(QString("%1%2%3").arg(a.album, yr, tp));
        if (m_artCache && !a.artHash.isEmpty()) {
            item->setIcon(QIcon(m_artCache->getArtwork(a.artHash, 48)));
        }
        m_localAlbumsList->addItem(item);
    }

    if (!m_localAlbums.isEmpty() && m_artCache && !m_localAlbums.first().artHash.isEmpty()) {
        m_artistPhoto->setPixmap(m_artCache->getFullArtwork(m_localAlbums.first().artHash, 136));
    }

    m_topTracksTable->setRowCount(0);
    for (const Track& t : m_localTracks) {
        const int r = m_topTracksTable->rowCount();
        m_topTracksTable->insertRow(r);
        m_topTracksTable->setItem(r, 0, new QTableWidgetItem(t.title));
        m_topTracksTable->setItem(r, 1, new QTableWidgetItem(t.album));
        const QString stars = t.rating > 0 ? QString(t.rating, QChar(0x2605)) : "";
        m_topTracksTable->setItem(r, 2, new QTableWidgetItem(QString("%1 plays %2").arg(t.playCount).arg(stars).trimmed()));
        m_topTracksTable->setItem(r, 3, new QTableWidgetItem(formatMs(t.durationMs)));
    }
}

void MusicExplorerPage::fetchWikipediaSummary(const QString& artist) {
    QString slug = artist;
    slug.replace(' ', '_');
    const QUrl url(QString("https://en.wikipedia.org/api/rest_v1/page/summary/%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(slug))));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, artist]() {
        if (m_currentArtist != artist) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            if (m_bioBrowser->toPlainText().startsWith("Fetching")) {
                m_bioBrowser->setPlainText("No Wikipedia biography found for this artist.");
            }
            reply->deleteLater();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        const QString extract = obj.value("extract").toString();
        if (!extract.isEmpty()) {
            m_bioBrowser->setPlainText(extract);
        }

        const QString thumbUrl = obj.value("thumbnail").toObject().value("source").toString();
        if (!thumbUrl.isEmpty()) {
            QNetworkRequest imgReq((QUrl(thumbUrl)));
            imgReq.setRawHeader("User-Agent", kUserAgent);
            QNetworkReply* imgReply = m_nam->get(imgReq);
            connect(imgReply, &QNetworkReply::finished, this, [this, imgReply, artist]() {
                if (m_currentArtist == artist && imgReply->error() == QNetworkReply::NoError) {
                    QPixmap px;
                    if (px.loadFromData(imgReply->readAll())) {
                        m_artistPhoto->setPixmap(px.scaled(136, 136, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    }
                }
                imgReply->deleteLater();
            });
        }
    });
}

void MusicExplorerPage::fetchMusicBrainzArtist(const QString& artist) {
    QUrl url("https://musicbrainz.org/ws/2/artist/");
    QUrlQuery q;
    q.addQueryItem("query", QString("artist:\"%1\"").arg(artist));
    q.addQueryItem("limit", "1");
    q.addQueryItem("fmt", "json");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, artist]() {
        if (m_currentArtist != artist) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_artistMeta->setText(QString("%1 albums in library").arg(m_localAlbums.size()));
            reply->deleteLater();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        const QJsonArray artists = root.value("artists").toArray();
        if (artists.isEmpty()) {
            m_artistMeta->setText(QString("%1 albums in library").arg(m_localAlbums.size()));
            return;
        }

        const QJsonObject a = artists.first().toObject();
        const QString mbid = a.value("id").toString();
        const QString country = a.value("country").toString();
        const QString begin = a.value("life-span").toObject().value("begin").toString().left(4);
        const QString disambig = a.value("disambiguation").toString();
        QStringList metaParts;
        if (!country.isEmpty()) metaParts << country;
        if (!begin.isEmpty()) metaParts << ("Since " + begin);
        if (!disambig.isEmpty()) metaParts << disambig;
        metaParts << QString("%1 albums in library").arg(m_localAlbums.size());
        m_artistMeta->setText(metaParts.join("  •  "));

        if (!mbid.isEmpty()) {
            fetchMusicBrainzDiscography(mbid);
        }
    });
}

void MusicExplorerPage::fetchMusicBrainzDiscography(const QString& mbid) {
    QUrl url(QString("https://musicbrainz.org/ws/2/release-group?artist=%1&limit=50&fmt=json").arg(mbid));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);

    const QString artistSnap = m_currentArtist;
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, artistSnap]() {
        if (m_currentArtist != artistSnap || reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            return;
        }
        const QJsonArray groups = QJsonDocument::fromJson(reply->readAll()).object().value("release-groups").toArray();
        reply->deleteLater();

        struct RgRow { QString year; QString title; QString type; bool inLib; };
        QVector<RgRow> rows;
        for (const QJsonValue& v : groups) {
            const QJsonObject g = v.toObject();
            const QString title = g.value("title").toString();
            const QString year = g.value("first-release-date").toString().left(4);
            const QString type = g.value("primary-type").toString();
            bool inLib = false;
            for (const Album& alb : m_localAlbums) {
                if (alb.album.compare(title, Qt::CaseInsensitive) == 0) {
                    inLib = true;
                    break;
                }
            }
            rows.append({year, title, type, inLib});
        }
        std::sort(rows.begin(), rows.end(), [](const RgRow& a, const RgRow& b) {
            return a.year < b.year;
        });

        m_discogTable->setRowCount(0);
        for (const RgRow& rg : rows) {
            const int r = m_discogTable->rowCount();
            m_discogTable->insertRow(r);
            m_discogTable->setItem(r, 0, new QTableWidgetItem(rg.year.isEmpty() ? "—" : rg.year));
            m_discogTable->setItem(r, 1, new QTableWidgetItem(rg.title));
            m_discogTable->setItem(r, 2, new QTableWidgetItem(rg.type));
            auto* stItem = new QTableWidgetItem(rg.inLib ? "In Library ✓" : "Not in Library");
            if (rg.inLib) stItem->setForeground(QColor("#f5a623"));
            m_discogTable->setItem(r, 3, stItem);
        }
    });
}

void MusicExplorerPage::fetchLastFmInfo(const QString& artist) {
    const ScrobbleConfig cfg = ScrobbleConfig::load();
    if (!cfg.lastFmApiKey.isEmpty()) {
        QUrl url("https://ws.audioscrobbler.com/2.0/");
        QUrlQuery q;
        q.addQueryItem("method", "artist.getSimilar");
        q.addQueryItem("artist", artist);
        q.addQueryItem("api_key", cfg.lastFmApiKey);
        q.addQueryItem("limit", "15");
        q.addQueryItem("format", "json");
        url.setQuery(q);

        QNetworkReply* reply = m_nam->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, artist]() {
            if (m_currentArtist == artist && reply->error() == QNetworkReply::NoError) {
                const QJsonArray arr = QJsonDocument::fromJson(reply->readAll())
                                           .object()
                                           .value("similarartists")
                                           .toObject()
                                           .value("artist")
                                           .toArray();
                for (const QJsonValue& v : arr) {
                    const QString name = v.toObject().value("name").toString();
                    if (!name.isEmpty()) {
                        auto* item = new QListWidgetItem("↗ " + name);
                        item->setData(Qt::UserRole, name);
                        m_similarList->addItem(item);
                    }
                }
            }
            reply->deleteLater();
        });
        return;
    }

    // Fallback without Last.fm key: suggest other artists in the same genre from local library
    QString genre;
    if (!m_localTracks.isEmpty()) genre = m_localTracks.first().genre;
    if (genre.isEmpty()) return;

    RuleGroup gRule;
    gRule.conditions.append({FilterField::Genre, FilterOp::Is, genre});
    const auto genreAlbums = m_db->queryAlbums(gRule, QString(), SortOrder::AlbumArtistYear);
    QSet<QString> seen;
    seen.insert(artist.toLower());
    for (const Album& a : genreAlbums) {
        const QString aa = a.albumArtist;
        if (aa.isEmpty() || aa == "Various Artists" || seen.contains(aa.toLower())) continue;
        seen.insert(aa.toLower());
        auto* item = new QListWidgetItem(QString("♪ %1 (%2)").arg(aa, genre));
        item->setData(Qt::UserRole, aa);
        m_similarList->addItem(item);
        if (m_similarList->count() >= 15) break;
    }
}

} // namespace honeycomb

#include "ui/AutoTagDialog.h"
#include "ui/TagEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace honeycomb {

namespace {
constexpr const char* kUserAgent = "Honeycomb/0.1 (https://github.com/wobblyman2000/honeycomb)";
}

AutoTagDialog::AutoTagDialog(const QVector<Track>& tracks,
                             Database* db,
                             const QString& artCacheDir,
                             QWidget* parent)
    : QDialog(parent), m_tracks(tracks), m_db(db), m_artCacheDir(artCacheDir) {
    setWindowTitle("Auto-Tag & Fetch Art (MusicBrainz)");
    resize(860, 580);

    m_nam = new QNetworkAccessManager(this);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto* searchRow = new QHBoxLayout();
    m_artistEdit = new QLineEdit(this);
    m_artistEdit->setPlaceholderText("Artist / Album Artist...");
    m_albumEdit = new QLineEdit(this);
    m_albumEdit->setPlaceholderText("Album Title...");
    if (!m_tracks.isEmpty()) {
        const Track& first = m_tracks.first();
        m_artistEdit->setText(!first.albumArtist.isEmpty() ? first.albumArtist : first.artist);
        m_albumEdit->setText(first.album);
    }
    m_searchBtn = new QPushButton("Search MusicBrainz", this);
    searchRow->addWidget(new QLabel("Artist:", this));
    searchRow->addWidget(m_artistEdit, 1);
    searchRow->addWidget(new QLabel("Album:", this));
    searchRow->addWidget(m_albumEdit, 1);
    searchRow->addWidget(m_searchBtn);
    root->addLayout(searchRow);

    m_statusLabel = new QLabel("Click Search MusicBrainz to find matching releases.", this);
    m_statusLabel->setStyleSheet("color: #9aa0b0; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* split = new QSplitter(Qt::Horizontal, this);

    auto* leftBox = new QWidget(split);
    auto* leftLayout = new QVBoxLayout(leftBox);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel("Matching Releases:", leftBox));
    m_releaseList = new QListWidget(leftBox);
    leftLayout->addWidget(m_releaseList, 1);

    auto* artRow = new QHBoxLayout();
    m_artPreview = new QLabel("No Cover", leftBox);
    m_artPreview->setFixedSize(140, 140);
    m_artPreview->setAlignment(Qt::AlignCenter);
    m_artPreview->setStyleSheet("background: #1b1c22; border: 1px solid #2b2e3b; border-radius: 6px; color: #7a8094;");
    m_applyArtChk = new QCheckBox("Embed & cache cover art", leftBox);
    m_applyArtChk->setChecked(true);
    artRow->addWidget(m_artPreview);
    artRow->addWidget(m_applyArtChk, 1, Qt::AlignVCenter);
    leftLayout->addLayout(artRow);

    auto* rightBox = new QWidget(split);
    auto* rightLayout = new QVBoxLayout(rightBox);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel("Track Tag Preview (Local → MusicBrainz):", rightBox));
    m_previewTable = new QTableWidget(0, 4, rightBox);
    m_previewTable->setHorizontalHeaderLabels({"#", "Current Title", "Matched Title", "Matched Artist"});
    m_previewTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_previewTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_previewTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_previewTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_previewTable->verticalHeader()->setVisible(false);
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_previewTable->setAlternatingRowColors(true);
    rightLayout->addWidget(m_previewTable, 1);

    split->addWidget(leftBox);
    split->addWidget(rightBox);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    m_applyBtn = new QPushButton("Apply Tags & Art", this);
    m_applyBtn->setEnabled(false);
    auto* cancelBtn = new QPushButton("Cancel", this);
    bottomRow->addWidget(m_applyBtn);
    bottomRow->addWidget(cancelBtn);
    root->addLayout(bottomRow);

    connect(m_searchBtn, &QPushButton::clicked, this, &AutoTagDialog::onSearch);
    connect(m_releaseList, &QListWidget::currentRowChanged, this, &AutoTagDialog::onReleaseSelected);
    connect(m_applyBtn, &QPushButton::clicked, this, &AutoTagDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    updatePreviewTable();
    if (!m_artistEdit->text().isEmpty() || !m_albumEdit->text().isEmpty()) {
        onSearch();
    }
}

void AutoTagDialog::onSearch() {
    const QString artist = m_artistEdit->text().trimmed();
    const QString album = m_albumEdit->text().trimmed();
    if (artist.isEmpty() && album.isEmpty()) return;

    QStringList parts;
    if (!artist.isEmpty()) parts << QString("artist:\"%1\"").arg(artist);
    if (!album.isEmpty()) parts << QString("release:\"%1\"").arg(album);

    QUrl url("https://musicbrainz.org/ws/2/release/");
    QUrlQuery q;
    q.addQueryItem("query", parts.join(" AND "));
    q.addQueryItem("limit", "15");
    q.addQueryItem("fmt", "json");
    url.setQuery(q);

    m_statusLabel->setText("Searching MusicBrainz...");
    m_searchBtn->setEnabled(false);
    m_releaseList->clear();
    m_hits.clear();
    m_applyBtn->setEnabled(false);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_searchBtn->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText(QString("MusicBrainz search failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        const QJsonArray releases = root.value("releases").toArray();
        for (const QJsonValue& v : releases) {
            const QJsonObject r = v.toObject();
            MbReleaseHit hit;
            hit.mbid = r.value("id").toString();
            hit.title = r.value("title").toString();
            hit.date = r.value("date").toString();
            hit.country = r.value("country").toString();
            hit.trackCount = r.value("track-count").toInt();
            hit.primaryType = r.value("release-group").toObject().value("primary-type").toString();

            const QJsonArray ac = r.value("artist-credit").toArray();
            QStringList artistParts;
            for (const QJsonValue& a : ac) {
                const QJsonObject ao = a.toObject();
                artistParts << ao.value("name").toString() + ao.value("joinphrase").toString();
            }
            hit.artist = artistParts.join("").trimmed();
            m_hits.append(hit);

            const QString year = hit.date.left(4);
            const QString label = QString("%1 — %2 (%3) [%4 tracks%5]")
                                      .arg(hit.title,
                                           hit.artist,
                                           year.isEmpty() ? "?" : year)
                                      .arg(hit.trackCount)
                                      .arg(hit.country.isEmpty() ? QString() : ", " + hit.country);
            m_releaseList->addItem(label);
        }

        m_statusLabel->setText(m_hits.isEmpty()
                                   ? "No matching MusicBrainz releases found."
                                   : QString("Found %1 releases. Select one to preview tags & cover art.").arg(m_hits.size()));
        if (!m_hits.isEmpty()) {
            m_releaseList->setCurrentRow(0);
        }
    });
}

void AutoTagDialog::onReleaseSelected(int row) {
    if (row < 0 || row >= m_hits.size()) return;
    m_selectedHit = m_hits[row];
    m_matchedTracks.clear();
    m_coverBytes.clear();
    m_coverMime.clear();
    m_artPreview->setPixmap(QPixmap());
    m_artPreview->setText("Loading art...");
    m_applyBtn->setEnabled(false);

    fetchReleaseDetails(m_selectedHit);
    fetchCoverArt(m_selectedHit.mbid);
}

void AutoTagDialog::fetchReleaseDetails(const MbReleaseHit& hit) {
    QUrl url(QString("https://musicbrainz.org/ws/2/release/%1?inc=recordings+artist-credits+release-groups&fmt=json")
                 .arg(hit.mbid));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);

    m_statusLabel->setText("Fetching tracklist from MusicBrainz...");
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hit]() {
        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText(QString("Failed to load release details: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        if (m_selectedHit.mbid != hit.mbid) return;

        const QString rgType = root.value("release-group").toObject().value("primary-type").toString();
        if (!rgType.isEmpty()) m_selectedHit.primaryType = rgType;
        if (m_selectedHit.date.isEmpty()) m_selectedHit.date = root.value("date").toString();

        m_matchedTracks.clear();
        const QJsonArray media = root.value("media").toArray();
        for (int m = 0; m < media.size(); ++m) {
            const QJsonObject med = media[m].toObject();
            const int discNo = med.value("position").toInt(m + 1);
            const QJsonArray tracks = med.value("tracks").toArray();
            for (int t = 0; t < tracks.size(); ++t) {
                const QJsonObject tr = tracks[t].toObject();
                MatchedTrack mt;
                mt.discNumber = discNo;
                mt.trackNumber = tr.value("position").toInt(t + 1);
                mt.title = tr.value("title").toString();
                if (mt.title.isEmpty()) {
                    mt.title = tr.value("recording").toObject().value("title").toString();
                }
                const QJsonArray ac = tr.value("artist-credit").toArray();
                QStringList ap;
                for (const QJsonValue& a : ac) {
                    const QJsonObject ao = a.toObject();
                    ap << ao.value("name").toString() + ao.value("joinphrase").toString();
                }
                mt.artist = ap.join("").trimmed();
                if (mt.artist.isEmpty()) mt.artist = m_selectedHit.artist;
                m_matchedTracks.append(mt);
            }
        }

        updatePreviewTable();
        m_applyBtn->setEnabled(!m_matchedTracks.isEmpty());
        m_statusLabel->setText(QString("Loaded %1 tracks from \"%2\".").arg(m_matchedTracks.size()).arg(m_selectedHit.title));
    });
}

void AutoTagDialog::fetchCoverArt(const QString& mbid) {
    QUrl url(QString("https://coverartarchive.org/release/%1/front-500").arg(mbid));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, mbid]() {
        if (m_selectedHit.mbid != mbid) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_artPreview->setText("No Cover");
            reply->deleteLater();
            return;
        }
        const QByteArray bytes = reply->readAll();
        const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        reply->deleteLater();

        QPixmap px;
        if (px.loadFromData(bytes)) {
            m_coverBytes = bytes;
            m_coverMime = ctype.contains("png", Qt::CaseInsensitive) ? "image/png" : "image/jpeg";
            m_artPreview->setPixmap(px.scaled(140, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_artPreview->setText("No Cover");
        }
    });
}

void AutoTagDialog::updatePreviewTable() {
    m_previewTable->setRowCount(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track& local = m_tracks[i];
        m_previewTable->setItem(i, 0, new QTableWidgetItem(local.trackNumber > 0 ? QString::number(local.trackNumber) : QString::number(i + 1)));
        m_previewTable->setItem(i, 1, new QTableWidgetItem(local.title));
        if (i < m_matchedTracks.size()) {
            m_previewTable->setItem(i, 2, new QTableWidgetItem(m_matchedTracks[i].title));
            m_previewTable->setItem(i, 3, new QTableWidgetItem(m_matchedTracks[i].artist));
        } else {
            m_previewTable->setItem(i, 2, new QTableWidgetItem("—"));
            m_previewTable->setItem(i, 3, new QTableWidgetItem("—"));
        }
    }
}

void AutoTagDialog::onApply() {
    if (m_matchedTracks.isEmpty()) return;

    const int year = m_selectedHit.date.left(4).toInt();
    QString newArtHash;
    if (m_applyArtChk->isChecked() && !m_coverBytes.isEmpty()) {
        newArtHash = saveArtBytesToCache(m_coverBytes, m_artCacheDir);
    }

    int failures = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
        Track& t = m_tracks[i];
        t.album = m_selectedHit.title;
        if (!m_selectedHit.artist.isEmpty()) t.albumArtist = m_selectedHit.artist;
        if (year > 0) t.year = year;
        if (!m_selectedHit.primaryType.isEmpty()) t.albumType = m_selectedHit.primaryType;

        if (i < m_matchedTracks.size()) {
            t.title = m_matchedTracks[i].title;
            t.artist = m_matchedTracks[i].artist;
            t.trackNumber = m_matchedTracks[i].trackNumber;
            t.discNumber = m_matchedTracks[i].discNumber;
        }
        if (!newArtHash.isEmpty()) {
            t.artHash = newArtHash;
            writeEmbeddedCoverArt(t.path, false, m_coverBytes, m_coverMime);
        }

        if (!writeTrackTagsToFile(t)) {
            ++failures;
        }
    }

    if (m_db) {
        m_db->upsertTracks(m_tracks);
    }

    if (failures > 0) {
        QMessageBox::warning(this, "Auto-Tag Warning",
                             QString("Updated database, but %1 file(s) could not be written on disk.").arg(failures));
    }
    m_saved = true;
    accept();
}

} // namespace honeycomb

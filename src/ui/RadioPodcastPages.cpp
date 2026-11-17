#include "ui/RadioPodcastPages.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSplitter>
#include <QUuid>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace honeycomb {

RadioPage::RadioPage(Database* db, QWidget* parent) : QWidget(parent), m_db(db) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* hdr = new QHBoxLayout();
    auto* title = new QLabel("Internet Radio", this);
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    auto* addBtn = new QPushButton("+ Add Station...", this);
    auto* playBtn = new QPushButton("▶ Play", this);
    auto* delBtn = new QPushButton("Remove", this);
    hdr->addWidget(title);
    hdr->addStretch();
    hdr->addWidget(addBtn);
    hdr->addWidget(playBtn);
    hdr->addWidget(delBtn);
    root->addLayout(hdr);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"Station", "Genre", "Stream URL"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    root->addWidget(m_table, 1);

    connect(addBtn, &QPushButton::clicked, this, &RadioPage::onAddStation);
    connect(delBtn, &QPushButton::clicked, this, &RadioPage::onRemoveSelected);
    connect(playBtn, &QPushButton::clicked, this, [this]() {
        playRow(m_table->currentRow());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        playRow(row);
    });

    reload();
}

void RadioPage::reload() {
    m_stations = m_db->loadRadioStations();
    m_table->setRowCount(0);
    for (const auto& s : m_stations) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(s.name));
        m_table->setItem(r, 1, new QTableWidgetItem(s.genre));
        m_table->setItem(r, 2, new QTableWidgetItem(s.url));
    }
}

void RadioPage::onAddStation() {
    QDialog dlg(this);
    dlg.setWindowTitle("Add Internet Radio Station");
    auto* form = new QFormLayout(&dlg);
    auto* nameEdit = new QLineEdit(&dlg);
    auto* genreEdit = new QLineEdit(&dlg);
    auto* urlEdit = new QLineEdit(&dlg);
    urlEdit->setPlaceholderText("https://... (.mp3, .aac, .m3u8, .pls)");
    form->addRow("Station Name:", nameEdit);
    form->addRow("Genre:", genreEdit);
    form->addRow("Stream URL:", urlEdit);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    const QString name = nameEdit->text().trimmed();
    const QString url = urlEdit->text().trimmed();
    if (name.isEmpty() || url.isEmpty()) return;

    Database::RadioStation st;
    st.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    st.name = name;
    st.genre = genreEdit->text().trimmed();
    st.url = url;
    m_db->saveRadioStation(st);
    reload();
}

void RadioPage::onRemoveSelected() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_stations.size()) return;
    m_db->deleteRadioStation(m_stations[row].id);
    reload();
}

void RadioPage::playRow(int row) {
    if (row < 0 || row >= m_stations.size()) return;
    const auto& s = m_stations[row];
    Track t;
    t.path = s.url;
    t.title = s.name;
    t.artist = s.genre.isEmpty() ? QString("Internet Radio") : s.genre;
    t.album = "Live Stream";
    t.format = "STREAM";
    emit streamPlayRequested(t);
}

PodcastPage::PodcastPage(Database* db, QWidget* parent) : QWidget(parent), m_db(db) {
    m_nam = new QNetworkAccessManager(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* hdr = new QHBoxLayout();
    auto* title = new QLabel("Podcasts", this);
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    auto* subBtn = new QPushButton("+ Subscribe RSS...", this);
    auto* refreshBtn = new QPushButton("Refresh", this);
    auto* playBtn = new QPushButton("▶ Play Episode", this);
    auto* delBtn = new QPushButton("Unsubscribe", this);
    hdr->addWidget(title);
    hdr->addStretch();
    hdr->addWidget(subBtn);
    hdr->addWidget(refreshBtn);
    hdr->addWidget(playBtn);
    hdr->addWidget(delBtn);
    root->addLayout(hdr);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_feedList = new QListWidget(split);
    m_feedList->setMaximumWidth(260);
    m_epTable = new QTableWidget(0, 3, split);
    m_epTable->setHorizontalHeaderLabels({"Episode", "Published", "Duration"});
    m_epTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_epTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_epTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_epTable->verticalHeader()->setVisible(false);
    m_epTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_epTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_epTable->setShowGrid(false);
    m_epTable->setAlternatingRowColors(true);
    split->addWidget(m_feedList);
    split->addWidget(m_epTable);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    connect(subBtn, &QPushButton::clicked, this, &PodcastPage::onSubscribe);
    connect(refreshBtn, &QPushButton::clicked, this, &PodcastPage::fetchSelectedFeed);
    connect(delBtn, &QPushButton::clicked, this, &PodcastPage::onRemoveFeed);
    connect(m_feedList, &QListWidget::currentRowChanged, this, [this](int) {
        fetchSelectedFeed();
    });
    connect(playBtn, &QPushButton::clicked, this, [this]() {
        playEpisodeRow(m_epTable->currentRow());
    });
    connect(m_epTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        playEpisodeRow(row);
    });

    reload();
}

void PodcastPage::reload() {
    m_feeds = m_db->loadPodcastFeeds();
    m_feedList->clear();
    for (const auto& f : m_feeds) {
        m_feedList->addItem(f.title);
    }
    if (!m_feeds.isEmpty()) {
        m_feedList->setCurrentRow(0);
    } else {
        m_epTable->setRowCount(0);
        m_episodes.clear();
    }
}

void PodcastPage::onSubscribe() {
    bool ok = false;
    const QString url = QInputDialog::getText(
        this, "Subscribe to Podcast", "Podcast RSS Feed URL:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || url.isEmpty()) return;

    Database::PodcastFeed feed;
    feed.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    feed.title = QUrl(url).host().isEmpty() ? url : QUrl(url).host();
    feed.url = url;
    m_db->savePodcastFeed(feed);
    reload();
    for (int i = 0; i < m_feeds.size(); ++i) {
        if (m_feeds[i].id == feed.id) {
            m_feedList->setCurrentRow(i);
            break;
        }
    }
}

void PodcastPage::onRemoveFeed() {
    const int row = m_feedList->currentRow();
    if (row < 0 || row >= m_feeds.size()) return;
    m_db->deletePodcastFeed(m_feeds[row].id);
    reload();
}

void PodcastPage::fetchSelectedFeed() {
    const int row = m_feedList->currentRow();
    if (row < 0 || row >= m_feeds.size()) return;

    Database::PodcastFeed feed = m_feeds[row];
    m_activeFeedTitle = feed.title;
    m_epTable->setRowCount(0);
    m_episodes.clear();

    QNetworkRequest req(QUrl(feed.url));
    req.setRawHeader("User-Agent", "Honeycomb/0.1");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, feed]() {
        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            return;
        }
        const QByteArray xmlBytes = reply->readAll();
        reply->deleteLater();

        QXmlStreamReader xml(xmlBytes);
        bool inItem = false;
        bool gotChannelTitle = false;
        QString channelTitle;
        Episode cur;

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QString name = xml.name().toString();
                if (name == "item") {
                    inItem = true;
                    cur = Episode{};
                } else if (!inItem && name == "title" && !gotChannelTitle) {
                    channelTitle = xml.readElementText().trimmed();
                    gotChannelTitle = true;
                } else if (inItem) {
                    if (name == "title") cur.title = xml.readElementText().trimmed();
                    else if (name == "pubDate") cur.pubDate = xml.readElementText().trimmed().left(16);
                    else if (name == "duration") cur.duration = xml.readElementText().trimmed();
                    else if (name == "enclosure") {
                        cur.audioUrl = xml.attributes().value("url").toString().trimmed();
                    }
                }
            } else if (xml.isEndElement() && xml.name().toString() == "item") {
                if (inItem && !cur.audioUrl.isEmpty()) {
                    m_episodes.append(cur);
                }
                inItem = false;
            }
        }

        if (!channelTitle.isEmpty() && channelTitle != feed.title) {
            Database::PodcastFeed updated = feed;
            updated.title = channelTitle;
            m_db->savePodcastFeed(updated);
            m_activeFeedTitle = channelTitle;
            const int curRow = m_feedList->currentRow();
            if (curRow >= 0 && curRow < m_feedList->count()) {
                m_feedList->item(curRow)->setText(channelTitle);
            }
        }

        m_epTable->setRowCount(0);
        for (const auto& ep : m_episodes) {
            const int r = m_epTable->rowCount();
            m_epTable->insertRow(r);
            m_epTable->setItem(r, 0, new QTableWidgetItem(ep.title));
            m_epTable->setItem(r, 1, new QTableWidgetItem(ep.pubDate));
            m_epTable->setItem(r, 2, new QTableWidgetItem(ep.duration));
        }
    });
}

void PodcastPage::playEpisodeRow(int row) {
    if (row < 0 || row >= m_episodes.size()) return;
    const auto& ep = m_episodes[row];
    Track t;
    t.path = ep.audioUrl;
    t.title = ep.title;
    t.artist = m_activeFeedTitle.isEmpty() ? QString("Podcast") : m_activeFeedTitle;
    t.album = "Podcast";
    t.format = "PODCAST";
    emit streamPlayRequested(t);
}

} // namespace honeycomb

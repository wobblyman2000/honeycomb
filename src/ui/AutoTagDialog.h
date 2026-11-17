#pragma once

#include "core/Track.h"
#include "db/Database.h"

#include <QByteArray>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QVector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace honeycomb {

class AutoTagDialog : public QDialog {
    Q_OBJECT
public:
    AutoTagDialog(const QVector<Track>& tracks,
                  Database* db,
                  const QString& artCacheDir,
                  QWidget* parent = nullptr);

    bool hasChanges() const { return m_saved; }

private slots:
    void onSearch();
    void onReleaseSelected(int row);
    void onApply();

private:
    struct MbReleaseHit {
        QString mbid;
        QString title;
        QString artist;
        QString date;
        QString country;
        QString primaryType;
        int trackCount = 0;
    };

    struct MatchedTrack {
        int trackNumber = 0;
        int discNumber = 1;
        QString title;
        QString artist;
    };

    void fetchReleaseDetails(const MbReleaseHit& hit);
    void fetchCoverArt(const QString& mbid);
    void updatePreviewTable();

    QVector<Track> m_tracks;
    Database* m_db = nullptr;
    QString m_artCacheDir;
    QNetworkAccessManager* m_nam = nullptr;
    bool m_saved = false;

    QLineEdit* m_artistEdit = nullptr;
    QLineEdit* m_albumEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QListWidget* m_releaseList = nullptr;
    QLabel* m_artPreview = nullptr;
    QTableWidget* m_previewTable = nullptr;
    QCheckBox* m_applyArtChk = nullptr;
    QPushButton* m_applyBtn = nullptr;

    QVector<MbReleaseHit> m_hits;
    MbReleaseHit m_selectedHit;
    QVector<MatchedTrack> m_matchedTracks;
    QByteArray m_coverBytes;
    QString m_coverMime;
};

} // namespace honeycomb

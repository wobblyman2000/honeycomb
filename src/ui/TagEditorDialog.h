#pragma once

#include "core/Track.h"
#include "db/Database.h"
#include "ui/ArtCache.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace honeycomb {

class TagEditorDialog : public QDialog {
    Q_OBJECT
public:
    TagEditorDialog(const QVector<Track>& tracks,
                    Database* db,
                    ArtCache* artCache,
                    const QString& artCacheDir,
                    QWidget* parent = nullptr);

    const QVector<Track>& updatedTracks() const { return m_tracks; }
    bool hasChanges() const { return m_saved; }

private slots:
    void onChooseCover();
    void onRemoveCover();
    void onSave();

private:
    enum class ArtAction { Keep, Replace, Remove };

    QVector<Track> m_tracks;
    Database* m_db = nullptr;
    ArtCache* m_artCache = nullptr;
    QString m_artCacheDir;
    bool m_batch = false;
    bool m_saved = false;

    ArtAction m_artAction = ArtAction::Keep;
    QByteArray m_newArtBytes;
    QString m_newArtMime;

    QLabel* m_artPreview = nullptr;
    QLabel* m_artStatusLabel = nullptr;

    QCheckBox* m_chkTitle = nullptr;
    QLineEdit* m_editTitle = nullptr;

    QCheckBox* m_chkArtist = nullptr;
    QLineEdit* m_editArtist = nullptr;

    QCheckBox* m_chkAlbumArtist = nullptr;
    QLineEdit* m_editAlbumArtist = nullptr;

    QCheckBox* m_chkAlbum = nullptr;
    QLineEdit* m_editAlbum = nullptr;

    QCheckBox* m_chkGenre = nullptr;
    QLineEdit* m_editGenre = nullptr;

    QCheckBox* m_chkYear = nullptr;
    QSpinBox* m_spinYear = nullptr;

    QCheckBox* m_chkTrack = nullptr;
    QSpinBox* m_spinTrack = nullptr;

    QCheckBox* m_chkDisc = nullptr;
    QSpinBox* m_spinDisc = nullptr;

    QCheckBox* m_chkAlbumType = nullptr;
    QComboBox* m_comboAlbumType = nullptr;

    QCheckBox* m_chkCompilation = nullptr;
    QCheckBox* m_editCompilation = nullptr;
};

} // namespace honeycomb

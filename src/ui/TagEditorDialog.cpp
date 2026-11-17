#include "TagEditorDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <taglib/attachedpictureframe.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/opusfile.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>

namespace honeycomb {

QString saveArtBytesToCache(const QByteArray& bytes, const QString& artCacheDir) {
    if (bytes.isEmpty()) return {};
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    const QString dest = artCacheDir + "/" + hash + ".img";
    if (!QFileInfo::exists(dest)) {
        QFile f(dest);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(bytes);
        }
    }
    return hash;
}

bool writeEmbeddedCoverArt(const QString& path,
                           bool removeOnly,
                           const QByteArray& artBytes,
                           const QString& mimeType) {
    const QByteArray pathUtf8 = path.toUtf8();
    const QString ext = QFileInfo(path).suffix().toLower();

    if (ext == "mp3") {
        TagLib::MPEG::File f(pathUtf8.constData());
        if (!f.isValid()) return false;
        TagLib::ID3v2::Tag* id3 = f.ID3v2Tag(true);
        if (!id3) return false;
        id3->removeFrames("APIC");
        if (!removeOnly && !artBytes.isEmpty()) {
            auto* frame = new TagLib::ID3v2::AttachedPictureFrame();
            frame->setMimeType(mimeType.toUtf8().constData());
            frame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
            frame->setPicture(TagLib::ByteVector(artBytes.constData(), static_cast<unsigned int>(artBytes.size())));
            id3->addFrame(frame);
        }
        return f.save();
    }

    if (ext == "flac") {
        TagLib::FLAC::File f(pathUtf8.constData());
        if (!f.isValid()) return false;
        f.removePictures();
        if (!removeOnly && !artBytes.isEmpty()) {
            auto* pic = new TagLib::FLAC::Picture();
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setMimeType(mimeType.toUtf8().constData());
            pic->setData(TagLib::ByteVector(artBytes.constData(), static_cast<unsigned int>(artBytes.size())));
            f.addPicture(pic);
        }
        return f.save();
    }

    if (ext == "m4a" || ext == "mp4" || ext == "aac" || ext == "alac") {
        TagLib::MP4::File f(pathUtf8.constData());
        if (!f.isValid() || !f.tag()) return false;
        f.tag()->removeItem("covr");
        if (!removeOnly && !artBytes.isEmpty()) {
            const auto fmt = mimeType.contains("png", Qt::CaseInsensitive)
                                 ? TagLib::MP4::CoverArt::PNG
                                 : TagLib::MP4::CoverArt::JPEG;
            TagLib::MP4::CoverArt art(fmt, TagLib::ByteVector(artBytes.constData(), static_cast<unsigned int>(artBytes.size())));
            TagLib::MP4::CoverArtList list;
            list.append(art);
            f.tag()->setItem("covr", list);
        }
        return f.save();
    }

    if (ext == "ogg" || ext == "oga") {
        TagLib::Ogg::Vorbis::File f(pathUtf8.constData());
        if (!f.isValid() || !f.tag()) return false;
        f.tag()->removeAllPictures();
        if (!removeOnly && !artBytes.isEmpty()) {
            auto* pic = new TagLib::FLAC::Picture();
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setMimeType(mimeType.toUtf8().constData());
            pic->setData(TagLib::ByteVector(artBytes.constData(), static_cast<unsigned int>(artBytes.size())));
            f.tag()->addPicture(pic);
        }
        return f.save();
    }

    if (ext == "opus") {
        TagLib::Ogg::Opus::File f(pathUtf8.constData());
        if (!f.isValid() || !f.tag()) return false;
        f.tag()->removeAllPictures();
        if (!removeOnly && !artBytes.isEmpty()) {
            auto* pic = new TagLib::FLAC::Picture();
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            pic->setMimeType(mimeType.toUtf8().constData());
            pic->setData(TagLib::ByteVector(artBytes.constData(), static_cast<unsigned int>(artBytes.size())));
            f.tag()->addPicture(pic);
        }
        return f.save();
    }

    return true;
}

bool writeTrackTagsToFile(const Track& t) {
    const QByteArray pathUtf8 = t.path.toUtf8();
    TagLib::FileRef ref(pathUtf8.constData());
    if (ref.isNull() || !ref.file() || !ref.tag()) return false;

    TagLib::Tag* tag = ref.tag();
    tag->setTitle(TagLib::String(t.title.toStdWString()));
    tag->setArtist(TagLib::String(t.artist.toStdWString()));
    tag->setAlbum(TagLib::String(t.album.toStdWString()));
    tag->setGenre(TagLib::String(t.genre.toStdWString()));
    tag->setYear(static_cast<unsigned int>(qMax(0, t.year)));
    tag->setTrack(static_cast<unsigned int>(qMax(0, t.trackNumber)));

    TagLib::PropertyMap props = ref.file()->properties();

    auto setOrErase = [&](const char* key, const QString& val) {
        if (val.trimmed().isEmpty()) {
            props.erase(key);
        } else {
            props.replace(key, TagLib::StringList(TagLib::String(val.trimmed().toStdWString())));
        }
    };

    setOrErase("ALBUMARTIST", t.albumArtist);
    setOrErase("DISCNUMBER", t.discNumber > 0 ? QString::number(t.discNumber) : QString());
    setOrErase("COMPILATION", t.isCompilation ? QString("1") : QString());
    setOrErase("RELEASETYPE", t.albumType);
    if (t.albumType.trimmed().isEmpty()) {
        props.erase("ALBUMTYPE");
        props.erase("MUSICBRAINZ_ALBUMTYPE");
    }

    ref.file()->setProperties(props);
    return ref.save();
}

namespace {

template <typename Getter>
bool allSameValue(const QVector<Track>& tracks, Getter getter) {
    if (tracks.isEmpty()) return true;
    const auto first = getter(tracks.first());
    for (int i = 1; i < tracks.size(); ++i) {
        if (getter(tracks[i]) != first) return false;
    }
    return true;
}

} // namespace

TagEditorDialog::TagEditorDialog(const QVector<Track>& tracks,
                                 Database* db,
                                 ArtCache* artCache,
                                 const QString& artCacheDir,
                                 QWidget* parent)
    : QDialog(parent),
      m_tracks(tracks),
      m_db(db),
      m_artCache(artCache),
      m_artCacheDir(artCacheDir),
      m_batch(tracks.size() > 1) {

    setWindowTitle(m_batch ? QString("Edit Tags (%1 tracks)").arg(m_tracks.size()) : "Edit Track Tags");
    setMinimumWidth(620);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);

    // Header (file path or batch summary)
    auto* headerLbl = new QLabel(this);
    headerLbl->setStyleSheet("color: #9ba1b4; font-size: 11px;");
    headerLbl->setWordWrap(true);
    if (m_batch) {
        headerLbl->setText(QString("Batch editing %1 tracks. Only checked fields will be updated.").arg(m_tracks.size()));
    } else if (!m_tracks.isEmpty()) {
        headerLbl->setText(m_tracks.first().path);
    }
    root->addWidget(headerLbl);

    auto* bodyRow = new QHBoxLayout();
    bodyRow->setSpacing(16);

    // Left: Cover Art
    auto* artCol = new QVBoxLayout();
    artCol->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_artPreview = new QLabel(this);
    m_artPreview->setFixedSize(160, 160);
    m_artPreview->setAlignment(Qt::AlignCenter);
    m_artPreview->setStyleSheet("background: #16171d; border: 1px solid #2e3240; border-radius: 6px;");

    QString initialArtHash;
    if (!m_tracks.isEmpty()) {
        initialArtHash = m_tracks.first().artHash;
        if (m_batch && !allSameValue(m_tracks, [](const Track& t) { return t.artHash; })) {
            initialArtHash.clear();
        }
    }
    if (m_artCache && !initialArtHash.isEmpty()) {
        m_artPreview->setPixmap(m_artCache->getFullArtwork(initialArtHash, 160));
    } else {
        m_artPreview->setText(m_batch ? "Mixed / No Art" : "No Cover Art");
    }

    m_artStatusLabel = new QLabel(this);
    m_artStatusLabel->setStyleSheet("color: #8c92a4; font-size: 11px;");
    m_artStatusLabel->setAlignment(Qt::AlignCenter);

    auto* chooseArtBtn = new QPushButton("Change Cover...", this);
    auto* removeArtBtn = new QPushButton("Remove Cover", this);
    connect(chooseArtBtn, &QPushButton::clicked, this, &TagEditorDialog::onChooseCover);
    connect(removeArtBtn, &QPushButton::clicked, this, &TagEditorDialog::onRemoveCover);

    artCol->addWidget(m_artPreview);
    artCol->addWidget(m_artStatusLabel);
    artCol->addWidget(chooseArtBtn);
    artCol->addWidget(removeArtBtn);
    artCol->addStretch();
    bodyRow->addLayout(artCol);

    // Right: Tag Fields Grid
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    int row = 0;
    auto addFieldRow = [&](const QString& label, QCheckBox*& chk, QWidget* editor, bool sameVal) {
        chk = new QCheckBox(label, this);
        if (m_batch) {
            chk->setChecked(sameVal);
        } else {
            chk->setChecked(true);
            chk->setEnabled(false);
        }
        grid->addWidget(chk, row, 0);
        grid->addWidget(editor, row, 1);
        ++row;
    };

    const Track first = m_tracks.isEmpty() ? Track{} : m_tracks.first();

    // Title
    const bool sameTitle = allSameValue(m_tracks, [](const Track& t) { return t.title; });
    m_editTitle = new QLineEdit(sameTitle ? first.title : QString(), this);
    if (!sameTitle) m_editTitle->setPlaceholderText("<keep mixed>");
    addFieldRow("Title", m_chkTitle, m_editTitle, sameTitle && !m_batch);
    connect(m_editTitle, &QLineEdit::textEdited, this, [this]() { m_chkTitle->setChecked(true); });

    // Artist
    const bool sameArtist = allSameValue(m_tracks, [](const Track& t) { return t.artist; });
    m_editArtist = new QLineEdit(sameArtist ? first.artist : QString(), this);
    if (!sameArtist) m_editArtist->setPlaceholderText("<keep mixed>");
    addFieldRow("Artist", m_chkArtist, m_editArtist, sameArtist);
    connect(m_editArtist, &QLineEdit::textEdited, this, [this]() { m_chkArtist->setChecked(true); });

    // Album Artist
    const bool sameAlbArtist = allSameValue(m_tracks, [](const Track& t) { return t.albumArtist; });
    m_editAlbumArtist = new QLineEdit(sameAlbArtist ? first.albumArtist : QString(), this);
    if (!sameAlbArtist) m_editAlbumArtist->setPlaceholderText("<keep mixed>");
    addFieldRow("Album Artist", m_chkAlbumArtist, m_editAlbumArtist, sameAlbArtist);
    connect(m_editAlbumArtist, &QLineEdit::textEdited, this, [this]() { m_chkAlbumArtist->setChecked(true); });

    // Album
    const bool sameAlbum = allSameValue(m_tracks, [](const Track& t) { return t.album; });
    m_editAlbum = new QLineEdit(sameAlbum ? first.album : QString(), this);
    if (!sameAlbum) m_editAlbum->setPlaceholderText("<keep mixed>");
    addFieldRow("Album", m_chkAlbum, m_editAlbum, sameAlbum);
    connect(m_editAlbum, &QLineEdit::textEdited, this, [this]() { m_chkAlbum->setChecked(true); });

    // Genre
    const bool sameGenre = allSameValue(m_tracks, [](const Track& t) { return t.genre; });
    m_editGenre = new QLineEdit(sameGenre ? first.genre : QString(), this);
    if (!sameGenre) m_editGenre->setPlaceholderText("<keep mixed>");
    addFieldRow("Genre", m_chkGenre, m_editGenre, sameGenre);
    connect(m_editGenre, &QLineEdit::textEdited, this, [this]() { m_chkGenre->setChecked(true); });

    // Year
    const bool sameYear = allSameValue(m_tracks, [](const Track& t) { return t.year; });
    m_spinYear = new QSpinBox(this);
    m_spinYear->setRange(0, 2999);
    m_spinYear->setSpecialValueText("Unset");
    m_spinYear->setValue(sameYear ? first.year : 0);
    addFieldRow("Year", m_chkYear, m_spinYear, sameYear);
    connect(m_spinYear, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { m_chkYear->setChecked(true); });

    // Track #
    const bool sameTrack = allSameValue(m_tracks, [](const Track& t) { return t.trackNumber; });
    m_spinTrack = new QSpinBox(this);
    m_spinTrack->setRange(0, 999);
    m_spinTrack->setSpecialValueText("Unset");
    m_spinTrack->setValue(sameTrack ? first.trackNumber : 0);
    addFieldRow("Track #", m_chkTrack, m_spinTrack, sameTrack && !m_batch);
    connect(m_spinTrack, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { m_chkTrack->setChecked(true); });

    // Disc #
    const bool sameDisc = allSameValue(m_tracks, [](const Track& t) { return t.discNumber; });
    m_spinDisc = new QSpinBox(this);
    m_spinDisc->setRange(1, 99);
    m_spinDisc->setValue(sameDisc ? qMax(1, first.discNumber) : 1);
    addFieldRow("Disc #", m_chkDisc, m_spinDisc, sameDisc);
    connect(m_spinDisc, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { m_chkDisc->setChecked(true); });

    // Album Type
    const bool sameType = allSameValue(m_tracks, [](const Track& t) { return t.albumType; });
    m_comboAlbumType = new QComboBox(this);
    m_comboAlbumType->setEditable(true);
    QStringList types = {"", "Studio", "Live", "EP", "Single", "Compilation", "Soundtrack", "Remix"};
    if (m_db) {
        for (const QString& t : m_db->distinctAlbumTypes()) {
            if (!types.contains(t, Qt::CaseInsensitive)) types.append(t);
        }
    }
    m_comboAlbumType->addItems(types);
    m_comboAlbumType->setCurrentText(sameType ? first.albumType : QString());
    addFieldRow("Album Type", m_chkAlbumType, m_comboAlbumType, sameType);
    connect(m_comboAlbumType, &QComboBox::editTextChanged, this, [this]() { m_chkAlbumType->setChecked(true); });

    // Compilation
    const bool sameComp = allSameValue(m_tracks, [](const Track& t) { return t.isCompilation; });
    m_editCompilation = new QCheckBox("Part of a compilation (Various Artists)", this);
    m_editCompilation->setChecked(sameComp ? first.isCompilation : false);
    addFieldRow("Compilation", m_chkCompilation, m_editCompilation, sameComp);
    connect(m_editCompilation, &QCheckBox::toggled, this, [this]() { m_chkCompilation->setChecked(true); });

    grid->setColumnStretch(1, 1);
    bodyRow->addLayout(grid, 1);
    root->addLayout(bodyRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TagEditorDialog::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void TagEditorDialog::onChooseCover() {
    const QString file = QFileDialog::getOpenFileName(
        this, "Select Cover Art", QString(), "Images (*.png *.jpg *.jpeg *.webp)");
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Cover Art", "Could not read the selected image file.");
        return;
    }

    const QByteArray bytes = f.readAll();
    QImage img;
    if (!img.loadFromData(bytes)) {
        QMessageBox::warning(this, "Cover Art", "The selected file is not a valid image.");
        return;
    }

    m_newArtBytes = bytes;
    const QString ext = QFileInfo(file).suffix().toLower();
    m_newArtMime = (ext == "png") ? "image/png" : "image/jpeg";
    m_artAction = ArtAction::Replace;

    m_artPreview->setPixmap(QPixmap::fromImage(img.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_artStatusLabel->setText("New cover pending save");
}

void TagEditorDialog::onRemoveCover() {
    m_newArtBytes.clear();
    m_newArtMime.clear();
    m_artAction = ArtAction::Remove;
    m_artPreview->setPixmap(QPixmap());
    m_artPreview->setText("Cover Removed");
    m_artStatusLabel->setText("Cover will be removed");
}

void TagEditorDialog::onSave() {
    QString newArtHash;
    if (m_artAction == ArtAction::Replace) {
        newArtHash = saveArtBytesToCache(m_newArtBytes, m_artCacheDir);
    }

    QStringList failedFiles;
    for (Track& t : m_tracks) {
        if (m_chkTitle->isChecked()) t.title = m_editTitle->text().trimmed();
        if (m_chkArtist->isChecked()) t.artist = m_editArtist->text().trimmed();
        if (m_chkAlbumArtist->isChecked()) t.albumArtist = m_editAlbumArtist->text().trimmed();
        if (m_chkAlbum->isChecked()) t.album = m_editAlbum->text().trimmed();
        if (m_chkGenre->isChecked()) t.genre = m_editGenre->text().trimmed();
        if (m_chkYear->isChecked()) t.year = m_spinYear->value();
        if (m_chkTrack->isChecked()) t.trackNumber = m_spinTrack->value();
        if (m_chkDisc->isChecked()) t.discNumber = m_spinDisc->value();
        if (m_chkAlbumType->isChecked()) t.albumType = m_comboAlbumType->currentText().trimmed();
        if (m_chkCompilation->isChecked()) t.isCompilation = m_editCompilation->isChecked();

        if (m_artAction == ArtAction::Replace) {
            t.artHash = newArtHash;
        } else if (m_artAction == ArtAction::Remove) {
            t.artHash.clear();
        }

        if (!writeTrackTagsToFile(t)) {
            failedFiles.append(QFileInfo(t.path).fileName());
        } else if (m_artAction != ArtAction::Keep) {
            writeEmbeddedCoverArt(t.path, m_artAction == ArtAction::Remove, m_newArtBytes, m_newArtMime);
        }
    }

    if (m_db) {
        m_db->updateTracksTags(m_tracks);
    }

    if (!failedFiles.isEmpty()) {
        QMessageBox::warning(
            this, "Tag Editor",
            QString("Some files could not be written to disk (read-only or unsupported format):\n%1")
                .arg(failedFiles.join(", ")));
    }

    m_saved = true;
    accept();
}

} // namespace honeycomb

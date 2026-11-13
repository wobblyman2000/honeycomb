#include "LibraryScanner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/audioproperties.h>

namespace honeycomb {

namespace {

bool isAudioExtension(const QString& ext) {
    static const QStringList exts = {
        "flac", "mp3", "ogg", "opus", "m4a", "mp4", "aac", "wav", "wv", "ape", "wma", "aiff"
    };
    return exts.contains(ext.toLower());
}

QString saveArtBytes(const QByteArray& bytes, const QString& artCacheDir) {
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

QString findFolderArt(const QString& dirPath, const QString& artCacheDir, QHash<QString, QString>& dirCache) {
    auto it = dirCache.find(dirPath);
    if (it != dirCache.end()) return it.value();

    QDir dir(dirPath);
    const QStringList candidates = {
        "cover.jpg", "cover.jpeg", "cover.png",
        "folder.jpg", "folder.jpeg", "folder.png",
        "front.jpg", "front.jpeg", "front.png",
        "album.jpg", "album.jpeg", "album.png"
    };

    QString foundPath;
    for (const QString& name : candidates) {
        const QString p = dir.filePath(name);
        if (QFileInfo::exists(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.isEmpty()) {
        const auto files = dir.entryInfoList({"*.jpg", "*.jpeg", "*.png", "*.webp"}, QDir::Files);
        if (!files.isEmpty()) {
            foundPath = files.first().absoluteFilePath();
        }
    }

    QString hash;
    if (!foundPath.isEmpty()) {
        QFile f(foundPath);
        if (f.open(QIODevice::ReadOnly)) {
            hash = saveArtBytes(f.readAll(), artCacheDir);
        }
    }
    dirCache.insert(dirPath, hash);
    return hash;
}

Track scanFile(const QFileInfo& fi, const QString& artCacheDir, QHash<QString, QString>& dirArtCache) {
    Track t;
    t.path = fi.absoluteFilePath();
    t.format = fi.suffix().toUpper();
    t.title = fi.completeBaseName();

    const QByteArray pathUtf8 = t.path.toUtf8();
    TagLib::FileRef ref(pathUtf8.constData());
    if (ref.isNull() || !ref.file()) return t;

    if (const TagLib::Tag* tag = ref.tag()) {
        if (!tag->title().isEmpty())
            t.title = QString::fromStdWString(tag->title().toWString()).trimmed();
        if (!tag->artist().isEmpty())
            t.artist = QString::fromStdWString(tag->artist().toWString()).trimmed();
        if (!tag->album().isEmpty())
            t.album = QString::fromStdWString(tag->album().toWString()).trimmed();
        if (!tag->genre().isEmpty())
            t.genre = QString::fromStdWString(tag->genre().toWString()).trimmed();
        t.year = static_cast<int>(tag->year());
        t.trackNumber = static_cast<int>(tag->track());
    }

    const TagLib::PropertyMap props = ref.file()->properties();
    if (props.contains("ALBUMARTIST")) {
        const auto& vals = props["ALBUMARTIST"];
        if (!vals.isEmpty())
            t.albumArtist = QString::fromStdWString(vals.front().toWString()).trimmed();
    } else if (props.contains("ALBUM ARTIST")) {
        const auto& vals = props["ALBUM ARTIST"];
        if (!vals.isEmpty())
            t.albumArtist = QString::fromStdWString(vals.front().toWString()).trimmed();
    }

    if (props.contains("COMPILATION")) {
        const QString val = QString::fromStdWString(props["COMPILATION"].front().toWString()).trimmed();
        t.isCompilation = (val == "1" || val.compare("true", Qt::CaseInsensitive) == 0);
    }
    if (!t.isCompilation && t.albumArtist.compare("Various Artists", Qt::CaseInsensitive) == 0) {
        t.isCompilation = true;
    }

    if (props.contains("DISCNUMBER")) {
        const QString discStr = QString::fromStdWString(props["DISCNUMBER"].front().toWString());
        const int slash = discStr.indexOf('/');
        const int d = (slash > 0 ? discStr.left(slash) : discStr).toInt();
        if (d > 0) t.discNumber = d;
    }

    if (const TagLib::AudioProperties* ap = ref.audioProperties()) {
        t.durationMs = static_cast<int64_t>(ap->lengthInMilliseconds());
        t.bitrate = ap->bitrate();
        t.sampleRate = ap->sampleRate();
    }

    // Embedded picture via TagLib 2.x complexProperties
    const auto pics = ref.file()->complexProperties("PICTURE");
    if (!pics.isEmpty()) {
        const auto& pic = pics.front();
        auto dataIt = pic.find("data");
        if (dataIt != pic.end()) {
            const TagLib::ByteVector bv = dataIt->second.toByteVector();
            if (!bv.isEmpty()) {
                t.artHash = saveArtBytes(QByteArray(bv.data(), static_cast<int>(bv.size())), artCacheDir);
            }
        }
    }

    if (t.artHash.isEmpty()) {
        t.artHash = findFolderArt(fi.absolutePath(), artCacheDir, dirArtCache);
    }

    return t;
}

} // namespace

LibraryScanner::LibraryScanner(Database* db, const QString& artCacheDir, QObject* parent)
    : QObject(parent), m_db(db), m_artCacheDir(artCacheDir) {
    QDir().mkpath(m_artCacheDir);
}

LibraryScanner::~LibraryScanner() {
    cancel();
}

void LibraryScanner::cancel() {
    m_cancel.store(true);
}

void LibraryScanner::startScan(const QStringList& folders) {
    if (m_scanning.exchange(true)) return;
    m_cancel.store(false);
    emit scanStarted();

    (void)QtConcurrent::run([this, folders]() {
        QVector<Track> batch;
        batch.reserve(100);
        int totalCount = 0;
        QHash<QString, QString> dirArtCache;

        for (const QString& root : folders) {
            if (m_cancel.load()) break;
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && !m_cancel.load()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (!isAudioExtension(fi.suffix())) continue;

                batch.append(scanFile(fi, m_artCacheDir, dirArtCache));
                ++totalCount;

                if (batch.size() >= 100) {
                    m_db->upsertTracks(batch);
                    batch.clear();
                    QMetaObject::invokeMethod(this, [this, totalCount, name = fi.fileName()]() {
                        emit scanProgress(totalCount, name);
                    }, Qt::QueuedConnection);
                }
            }
        }

        if (!batch.isEmpty()) {
            m_db->upsertTracks(batch);
        }

        m_scanning.store(false);
        QMetaObject::invokeMethod(this, [this, totalCount]() {
            emit scanFinished(totalCount);
        }, Qt::QueuedConnection);
    });
}

} // namespace honeycomb

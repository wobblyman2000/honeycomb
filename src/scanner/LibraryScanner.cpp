#include "LibraryScanner.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
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

    for (const char* key : {"RELEASETYPE", "ALBUMTYPE", "MUSICBRAINZ_ALBUMTYPE"}) {
        if (props.contains(key) && !props[key].isEmpty()) {
            const QString raw = QString::fromStdWString(props[key].front().toWString()).trimmed();
            if (!raw.isEmpty()) {
                const QString low = raw.toLower();
                if (low == "album" || low == "studio") t.albumType = "Studio";
                else if (low == "live" || low == "live album") t.albumType = "Live";
                else if (low == "ep") t.albumType = "EP";
                else if (low == "single") t.albumType = "Single";
                else if (low == "compilation") t.albumType = "Compilation";
                else if (low == "soundtrack") t.albumType = "Soundtrack";
                else if (low == "remix") t.albumType = "Remix";
                else t.albumType = raw;
                break;
            }
        }
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

    m_watcher = new QFileSystemWatcher(this);
    m_watchDebounceTimer = new QTimer(this);
    m_watchDebounceTimer->setSingleShot(true);
    m_watchDebounceTimer->setInterval(1500);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        if (m_autoWatchEnabled && !m_scanning.load() && !m_watchedFolders.isEmpty()) {
            m_watchDebounceTimer->start();
        }
    });
    connect(m_watchDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_autoWatchEnabled && !m_scanning.load() && !m_watchedFolders.isEmpty()) {
            startScan(m_watchedFolders);
        }
    });
}

LibraryScanner::~LibraryScanner() {
    cancel();
}

void LibraryScanner::cancel() {
    m_cancel.store(true);
}

void LibraryScanner::setAutoWatchEnabled(bool enabled) {
    m_autoWatchEnabled = enabled;
    refreshWatcher();
}

void LibraryScanner::watchFolders(const QStringList& folders) {
    m_watchedFolders = folders;
    refreshWatcher();
}

void LibraryScanner::refreshWatcher() {
    const QStringList existing = m_watcher->directories();
    if (!existing.isEmpty()) {
        m_watcher->removePaths(existing);
    }
    if (!m_autoWatchEnabled || m_watchedFolders.isEmpty()) return;

    QStringList dirsToWatch;
    const int maxDirs = 2000;
    for (const QString& root : m_watchedFolders) {
        if (!QDir(root).exists()) continue;
        dirsToWatch.append(root);
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && dirsToWatch.size() < maxDirs) {
            dirsToWatch.append(it.next());
        }
        if (dirsToWatch.size() >= maxDirs) break;
    }
    if (!dirsToWatch.isEmpty()) {
        m_watcher->addPaths(dirsToWatch);
    }
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
        QSet<QString> seenPaths;
        QStringList scannedRoots;

        for (const QString& root : folders) {
            if (m_cancel.load()) break;
            if (!QDir(root).exists()) continue;
            scannedRoots.append(root);

            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && !m_cancel.load()) {
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (!isAudioExtension(fi.suffix())) continue;

                seenPaths.insert(fi.absoluteFilePath());
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

        if (!m_cancel.load()) {
            m_db->pruneMissingTracks(seenPaths, folders, scannedRoots);
        }

        m_scanning.store(false);
        QMetaObject::invokeMethod(this, [this, totalCount, folders]() {
            watchFolders(folders);
            emit scanFinished(totalCount);
        }, Qt::QueuedConnection);
    });
}

} // namespace honeycomb

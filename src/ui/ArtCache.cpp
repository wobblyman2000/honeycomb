#include "ArtCache.h"

#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>
#include <QtConcurrent>

namespace honeycomb {

namespace {

QPixmap makePlaceholder(int sizePx) {
    const QString key = QString("placeholder_%1").arg(sizePx);
    QPixmap pm;
    if (QPixmapCache::find(key, &pm)) return pm;

    pm = QPixmap(sizePx, sizePx);
    pm.fill(QColor("#23242b"));

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#3a3c48"), 2));
    const int r = sizePx / 5;
    p.drawEllipse(QPoint(sizePx / 2, sizePx / 2), r, r);
    p.drawEllipse(QPoint(sizePx / 2, sizePx / 2), r / 3, r / 3);
    p.end();

    QPixmapCache::insert(key, pm);
    return pm;
}

} // namespace

ArtCache::ArtCache(const QString& artCacheDir, QObject* parent)
    : QObject(parent), m_artCacheDir(artCacheDir) {
    QPixmapCache::setCacheLimit(128 * 1024); // 128 MB
}

QPixmap ArtCache::getArtwork(const QString& artHash, int sizePx) {
    if (artHash.isEmpty()) {
        return makePlaceholder(sizePx);
    }

    const QString cacheKey = QString("%1_%2").arg(artHash).arg(sizePx);
    QPixmap pm;
    if (QPixmapCache::find(cacheKey, &pm)) {
        return pm;
    }

    if (!m_pending.contains(cacheKey)) {
        m_pending.insert(cacheKey);
        const QString filePath = m_artCacheDir + "/" + artHash + ".img";
        (void)QtConcurrent::run([this, filePath, artHash, sizePx, cacheKey]() {
            QImageReader reader(filePath);
            reader.setAutoTransform(true);
            QImage img = reader.read();
            if (!img.isNull()) {
                img = img.scaled(sizePx, sizePx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                // Center crop to square
                if (img.width() != sizePx || img.height() != sizePx) {
                    const int x = qMax(0, (img.width() - sizePx) / 2);
                    const int y = qMax(0, (img.height() - sizePx) / 2);
                    img = img.copy(x, y, qMin(sizePx, img.width()), qMin(sizePx, img.height()));
                }
            }
            QMetaObject::invokeMethod(this, [this, img, artHash, sizePx, cacheKey]() {
                m_pending.remove(cacheKey);
                if (!img.isNull()) {
                    QPixmapCache::insert(cacheKey, QPixmap::fromImage(img));
                    emit artworkLoaded(artHash, sizePx);
                }
            }, Qt::QueuedConnection);
        });
    }

    return makePlaceholder(sizePx);
}

QPixmap ArtCache::getFullArtwork(const QString& artHash, int targetSizePx) const {
    if (artHash.isEmpty()) return makePlaceholder(targetSizePx);
    const QString filePath = m_artCacheDir + "/" + artHash + ".img";
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) return makePlaceholder(targetSizePx);
    return QPixmap::fromImage(
        img.scaled(targetSizePx, targetSizePx, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace honeycomb

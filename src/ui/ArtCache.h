#pragma once

#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

namespace honeycomb {

class ArtCache : public QObject {
    Q_OBJECT
public:
    explicit ArtCache(const QString& artCacheDir, QObject* parent = nullptr);

    // Returns cached pixmap if ready, or placeholder while loading asynchronously.
    QPixmap getArtwork(const QString& artHash, int sizePx);

    // Loads full-resolution artwork synchronously (for Now Playing sidebar).
    QPixmap getFullArtwork(const QString& artHash, int targetSizePx) const;

signals:
    void artworkLoaded(const QString& artHash, int sizePx);

private:
    QString m_artCacheDir;
    QSet<QString> m_pending;
};

} // namespace honeycomb

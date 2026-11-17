#pragma once

#include "core/Track.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace honeycomb {

struct ScrobbleConfig {
    bool listenBrainzEnabled = false;
    QString listenBrainzToken;

    bool lastFmEnabled = false;
    QString lastFmApiKey;
    QString lastFmApiSecret;
    QString lastFmSessionKey;

    static ScrobbleConfig load();
    void save() const;
};

class ScrobbleService : public QObject {
    Q_OBJECT
public:
    explicit ScrobbleService(QObject* parent = nullptr);

    void setConfig(const ScrobbleConfig& cfg) { m_cfg = cfg; }
    ScrobbleConfig config() const { return m_cfg; }

    void sendNowPlaying(const Track& track);
    void sendScrobble(const Track& track, qint64 startedAtEpochSec);

    void fetchLastFmSession(const QString& apiKey,
                            const QString& apiSecret,
                            const QString& username,
                            const QString& password);

signals:
    void lastFmSessionObtained(bool ok, const QString& sessionKeyOrError);
    void scrobbleStatus(const QString& message);

private:
    void postListenBrainz(const QString& listenType, const Track& track, qint64 timestamp);
    void postLastFm(const QMap<QString, QString>& params);
    static QString signLastFm(const QMap<QString, QString>& params, const QString& secret);

    ScrobbleConfig m_cfg;
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace honeycomb

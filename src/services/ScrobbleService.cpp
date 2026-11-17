#include "services/ScrobbleService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrlQuery>

namespace honeycomb {

ScrobbleConfig ScrobbleConfig::load() {
    QSettings s("Honeycomb", "Honeycomb");
    ScrobbleConfig c;
    c.listenBrainzEnabled = s.value("scrobble/lbEnabled", false).toBool();
    c.listenBrainzToken = s.value("scrobble/lbToken").toString();
    c.lastFmEnabled = s.value("scrobble/lfmEnabled", false).toBool();
    c.lastFmApiKey = s.value("scrobble/lfmApiKey").toString();
    c.lastFmApiSecret = s.value("scrobble/lfmApiSecret").toString();
    c.lastFmSessionKey = s.value("scrobble/lfmSessionKey").toString();
    return c;
}

void ScrobbleConfig::save() const {
    QSettings s("Honeycomb", "Honeycomb");
    s.setValue("scrobble/lbEnabled", listenBrainzEnabled);
    s.setValue("scrobble/lbToken", listenBrainzToken);
    s.setValue("scrobble/lfmEnabled", lastFmEnabled);
    s.setValue("scrobble/lfmApiKey", lastFmApiKey);
    s.setValue("scrobble/lfmApiSecret", lastFmApiSecret);
    s.setValue("scrobble/lfmSessionKey", lastFmSessionKey);
}

ScrobbleService::ScrobbleService(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_cfg = ScrobbleConfig::load();
}

void ScrobbleService::sendNowPlaying(const Track& track) {
    if (track.title.isEmpty() || track.path.startsWith("http", Qt::CaseInsensitive)) return;

    if (m_cfg.listenBrainzEnabled && !m_cfg.listenBrainzToken.trimmed().isEmpty()) {
        postListenBrainz("playing_now", track, 0);
    }
    if (m_cfg.lastFmEnabled && !m_cfg.lastFmApiKey.isEmpty() &&
        !m_cfg.lastFmApiSecret.isEmpty() && !m_cfg.lastFmSessionKey.isEmpty()) {
        QMap<QString, QString> params;
        params["method"] = "track.updateNowPlaying";
        params["api_key"] = m_cfg.lastFmApiKey;
        params["sk"] = m_cfg.lastFmSessionKey;
        params["artist"] = track.artist;
        params["track"] = track.title;
        if (!track.album.isEmpty()) params["album"] = track.album;
        if (track.durationMs > 0) params["duration"] = QString::number(track.durationMs / 1000);
        postLastFm(params);
    }
}

void ScrobbleService::sendScrobble(const Track& track, qint64 startedAtEpochSec) {
    if (track.title.isEmpty() || track.path.startsWith("http", Qt::CaseInsensitive)) return;

    if (m_cfg.listenBrainzEnabled && !m_cfg.listenBrainzToken.trimmed().isEmpty()) {
        postListenBrainz("single", track, startedAtEpochSec);
    }
    if (m_cfg.lastFmEnabled && !m_cfg.lastFmApiKey.isEmpty() &&
        !m_cfg.lastFmApiSecret.isEmpty() && !m_cfg.lastFmSessionKey.isEmpty()) {
        QMap<QString, QString> params;
        params["method"] = "track.scrobble";
        params["api_key"] = m_cfg.lastFmApiKey;
        params["sk"] = m_cfg.lastFmSessionKey;
        params["artist"] = track.artist;
        params["track"] = track.title;
        params["timestamp"] = QString::number(startedAtEpochSec);
        if (!track.album.isEmpty()) params["album"] = track.album;
        if (track.durationMs > 0) params["duration"] = QString::number(track.durationMs / 1000);
        postLastFm(params);
    }
}

void ScrobbleService::postListenBrainz(const QString& listenType, const Track& track, qint64 timestamp) {
    QJsonObject meta{
        {"artist_name", track.artist},
        {"track_name", track.title}
    };
    if (!track.album.isEmpty()) meta["release_name"] = track.album;

    QJsonObject item{{"track_metadata", meta}};
    if (listenType == "single") {
        item["listened_at"] = static_cast<qint64>(timestamp);
    }

    QJsonObject root{
        {"listen_type", listenType},
        {"payload", QJsonArray{item}}
    };

    QNetworkRequest req(QUrl("https://api.listenbrainz.org/1/submit-listens"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Token " + m_cfg.listenBrainzToken.trimmed()).toUtf8());
    req.setRawHeader("User-Agent", "Honeycomb/0.1");

    QNetworkReply* reply = m_nam->post(req, QJsonDocument(root).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, listenType]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit scrobbleStatus(QString("ListenBrainz error: %1").arg(reply->errorString()));
        } else if (listenType == "single") {
            emit scrobbleStatus("Scrobbled to ListenBrainz");
        }
        reply->deleteLater();
    });
}

QString ScrobbleService::signLastFm(const QMap<QString, QString>& params, const QString& secret) {
    QString raw;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == "format" || it.key() == "callback") continue;
        raw += it.key() + it.value();
    }
    raw += secret;
    return QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex());
}

void ScrobbleService::postLastFm(const QMap<QString, QString>& params) {
    QMap<QString, QString> signedParams = params;
    signedParams["api_sig"] = signLastFm(params, m_cfg.lastFmApiSecret);
    signedParams["format"] = "json";

    QUrlQuery query;
    for (auto it = signedParams.constBegin(); it != signedParams.constEnd(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }

    QNetworkRequest req(QUrl("https://ws.audioscrobbler.com/2.0/"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setRawHeader("User-Agent", "Honeycomb/0.1");

    QNetworkReply* reply = m_nam->post(req, query.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit scrobbleStatus(QString("Last.fm error: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
    });
}

void ScrobbleService::fetchLastFmSession(const QString& apiKey,
                                           const QString& apiSecret,
                                           const QString& username,
                                           const QString& password) {
    QMap<QString, QString> params;
    params["method"] = "auth.getMobileSession";
    params["api_key"] = apiKey;
    params["username"] = username;
    params["password"] = password;
    params["api_sig"] = signLastFm(params, apiSecret);
    params["format"] = "json";

    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }

    QNetworkRequest req(QUrl("https://ws.audioscrobbler.com/2.0/"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply* reply = m_nam->post(req, query.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.contains("session")) {
            const QString sk = obj.value("session").toObject().value("key").toString();
            emit lastFmSessionObtained(!sk.isEmpty(), sk.isEmpty() ? "Empty session key" : sk);
        } else {
            const QString msg = obj.value("message").toString(reply->errorString());
            emit lastFmSessionObtained(false, msg);
        }
        reply->deleteLater();
    });
}

} // namespace honeycomb

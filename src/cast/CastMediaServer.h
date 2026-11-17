#pragma once

#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

namespace honeycomb {

class CastMediaServer : public QObject {
    Q_OBJECT
public:
    explicit CastMediaServer(QObject* parent = nullptr);
    ~CastMediaServer() override;

    bool ensureStarted();
    void stopServer();

    QString setMedia(const QString& trackPath,
                     const QString& artPath,
                     const QString& targetHost,
                     quint16 targetPort,
                     QString* outContentType,
                     QString* outArtUrl);

    static QString localAddressForTarget(const QString& targetHost, quint16 targetPort);
    static QString mimeTypeForPath(const QString& path);

private slots:
    void onNewConnection();

private:
    void handleHttpRequest(QTcpSocket* sock, const QByteArray& rawRequest);
    void sendFileResponse(QTcpSocket* sock,
                          const QString& filePath,
                          const QString& contentType,
                          const QByteArray& rangeHeader,
                          bool headOnly);
    void sendErrorResponse(QTcpSocket* sock, int code, const QByteArray& reason);

    QTcpServer* m_server = nullptr;
    QString m_token;
    QString m_trackPath;
    QString m_artPath;
    QString m_mimeType;
};

} // namespace honeycomb

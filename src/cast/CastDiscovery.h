#pragma once

#include "cast/CastDevice.h"

#include <QObject>
#include <QProcess>
#include <QVector>

namespace honeycomb {

class CastDiscovery : public QObject {
    Q_OBJECT
public:
    explicit CastDiscovery(QObject* parent = nullptr);
    ~CastDiscovery() override;

    void refresh();
    bool isScanning() const;
    QVector<CastDevice> devices() const { return m_devices; }

signals:
    void devicesUpdated(const QVector<CastDevice>& devices);
    void discoveryFailed(const QString& error);

private:
    void parseAvahiOutput(const QByteArray& output);

    QProcess* m_proc = nullptr;
    QVector<CastDevice> m_devices;
};

} // namespace honeycomb

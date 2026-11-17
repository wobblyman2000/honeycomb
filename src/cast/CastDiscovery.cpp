#include "cast/CastDiscovery.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace honeycomb {

CastDiscovery::CastDiscovery(QObject* parent) : QObject(parent) {}

CastDiscovery::~CastDiscovery() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

bool CastDiscovery::isScanning() const {
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void CastDiscovery::refresh() {
    if (isScanning()) return;

    if (m_proc) {
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && exitCode == 0) {
            parseAvahiOutput(m_proc->readAllStandardOutput());
            emit devicesUpdated(m_devices);
        } else {
            const QString err = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
            emit discoveryFailed(err.isEmpty() ? "avahi-browse failed" : err);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit discoveryFailed("avahi-browse is not available. Install avahi-utils to discover Cast speakers.");
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
    });

    m_proc->start("avahi-browse", {"-rt", "-p", "_googlecast._tcp"});
}

void CastDiscovery::parseAvahiOutput(const QByteArray& output) {
    QHash<QString, CastDevice> byId;
    const QString text = QString::fromUtf8(output);
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    static const QRegularExpression txtRe("\"([^\"]*)\"");

    for (const QString& line : lines) {
        if (!line.startsWith('=')) continue;
        const QStringList fields = line.split(';');
        if (fields.size() < 10) continue;
        if (fields[2] != "IPv4") continue;

        const QString address = fields[7].trimmed();
        bool okPort = false;
        const quint16 port = fields[8].trimmed().toUShort(&okPort);
        if (address.isEmpty() || !okPort || port == 0) continue;

        QString id;
        QString friendlyName;
        QString model;

        auto it = txtRe.globalMatch(fields[9]);
        while (it.hasNext()) {
            const QString kv = it.next().captured(1);
            const int eq = kv.indexOf('=');
            if (eq <= 0) continue;
            const QString k = kv.left(eq);
            const QString v = kv.mid(eq + 1);
            if (k == "id") id = v;
            else if (k == "fn") friendlyName = v;
            else if (k == "md") model = v;
        }

        if (id.isEmpty()) id = QString("%1:%2").arg(address).arg(port);
        if (friendlyName.isEmpty()) friendlyName = fields[3];

        CastDevice dev;
        dev.id = id;
        dev.name = friendlyName;
        dev.model = model;
        dev.address = address;
        dev.port = port;
        byId.insert(id, dev);
    }

    QVector<CastDevice> list = byId.values().toVector();
    std::sort(list.begin(), list.end(), [](const CastDevice& a, const CastDevice& b) {
        if (a.isGroup() != b.isGroup()) return a.isGroup() > b.isGroup();
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    m_devices = list;
}

} // namespace honeycomb

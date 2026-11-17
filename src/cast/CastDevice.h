#pragma once

#include <QString>
#include <QtGlobal>

namespace honeycomb {

struct CastDevice {
    QString id;
    QString name;
    QString model;
    QString address;
    quint16 port = 8009;

    bool isValid() const { return !address.isEmpty() && port > 0; }
    bool isGroup() const { return model.contains("Group", Qt::CaseInsensitive) || port != 8009; }
};

} // namespace honeycomb

#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QMap>
#include <QString>

class QKeySequenceEdit;

namespace honeycomb {

struct ShortcutEntry {
    QString id;
    QString label;
    QString defaultKey;
};

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);

    static QVector<ShortcutEntry> definitions();
    static QKeySequence loadShortcut(const QString& id, const QString& defaultKey);
    static void saveShortcut(const QString& id, const QKeySequence& seq);

private:
    QMap<QString, QKeySequenceEdit*> m_edits;
};

} // namespace honeycomb

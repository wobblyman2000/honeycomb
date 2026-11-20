#include "ui/ShortcutsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace honeycomb {

QVector<ShortcutEntry> ShortcutsDialog::definitions() {
    return {
        {"play_pause", "Play / Pause", "Space"},
        {"next_track", "Next Track", "Ctrl+Right"},
        {"prev_track", "Previous Track", "Ctrl+Left"},
        {"vol_up", "Volume Up", "Ctrl+Up"},
        {"vol_down", "Volume Down", "Ctrl+Down"},
        {"seek_fwd", "Seek Forward 10s", "Alt+Right"},
        {"seek_back", "Seek Backward 10s", "Alt+Left"},
        {"focus_search", "Focus Search Box", "Ctrl+F"},
        {"add_bookmark", "Add Bookmark at Current Time", "Ctrl+B"},
        {"new_tab", "New Browser Tab", "Ctrl+T"},
        {"close_tab", "Close Current Tab", "Ctrl+W"},
        {"theater_mode", "Theater Mode", "F11"},
        {"mini_player", "Mini Player", "Ctrl+M"},
    };
}

QKeySequence ShortcutsDialog::loadShortcut(const QString& id, const QString& defaultKey) {
    QSettings s("Honeycomb", "Honeycomb");
    const QString str = s.value("shortcuts/" + id, defaultKey).toString();
    return QKeySequence(str);
}

void ShortcutsDialog::saveShortcut(const QString& id, const QKeySequence& seq) {
    QSettings s("Honeycomb", "Honeycomb");
    s.setValue("shortcuts/" + id, seq.toString(QKeySequence::PortableText));
}

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Keyboard Shortcuts");
    resize(460, 440);

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    const auto defs = definitions();
    for (const auto& d : defs) {
        auto* edit = new QKeySequenceEdit(loadShortcut(d.id, d.defaultKey), this);
        m_edits.insert(d.id, edit);
        form->addRow(d.label + ":", edit);
    }
    root->addLayout(form, 1);

    auto* bottom = new QHBoxLayout();
    auto* resetBtn = new QPushButton("Reset Defaults", this);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    bottom->addWidget(resetBtn);
    bottom->addStretch();
    bottom->addWidget(btns);
    root->addLayout(bottom);

    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        for (const auto& d : definitions()) {
            if (m_edits.contains(d.id)) {
                m_edits[d.id]->setKeySequence(QKeySequence(d.defaultKey));
            }
        }
    });
    connect(btns, &QDialogButtonBox::accepted, this, [this]() {
        for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
            saveShortcut(it.key(), it.value()->keySequence());
        }
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace honeycomb

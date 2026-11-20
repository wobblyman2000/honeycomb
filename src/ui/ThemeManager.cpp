#include "ui/ThemeManager.h"

#include <QPalette>
#include <QSettings>

namespace honeycomb {

QString ThemeManager::currentThemeId() {
    return QSettings("Honeycomb", "Honeycomb").value("ui/themeId", "honeycomb_dark").toString();
}

QString ThemeManager::currentAccentHex() {
    return QSettings("Honeycomb", "Honeycomb").value("ui/accentHex", "#f5a623").toString();
}

void ThemeManager::applySavedTheme(QApplication* app) {
    applyTheme(app, currentThemeId(), currentAccentHex());
}

void ThemeManager::applyTheme(QApplication* app, const QString& themeId, const QString& accentHex) {
    if (!app) return;

    QSettings s("Honeycomb", "Honeycomb");
    s.setValue("ui/themeId", themeId);
    s.setValue("ui/accentHex", accentHex);

    const QColor accent(accentHex.isEmpty() ? "#f5a623" : accentHex);
    const QString acc = accent.name();

    QColor win("#14151a"), text("#e8eaf0"), base("#181920"), altBase("#1e2028"),
        btn("#22242e"), border("#2e3140"), hover("#2b2e3b"), hiText("#111111");

    if (themeId == "oled_black") {
        win = QColor("#050507");
        text = QColor("#eceef4");
        base = QColor("#090a0d");
        altBase = QColor("#101218");
        btn = QColor("#14161e");
        border = QColor("#232634");
        hover = QColor("#1d202b");
    } else if (themeId == "midnight_blue") {
        win = QColor("#0f1524");
        text = QColor("#e6ecf8");
        base = QColor("#131b2e");
        altBase = QColor("#19233b");
        btn = QColor("#1e2a46");
        border = QColor("#2c3c60");
        hover = QColor("#273759");
    } else if (themeId == "clean_light") {
        win = QColor("#f2f4f8");
        text = QColor("#1b1e26");
        base = QColor("#ffffff");
        altBase = QColor("#f6f8fb");
        btn = QColor("#e6eaf1");
        border = QColor("#cfd6e2");
        hover = QColor("#dce2ec");
        hiText = QColor("#ffffff");
    }

    QPalette pal;
    pal.setColor(QPalette::Window, win);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, altBase);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, btn);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, hiText);
    app->setPalette(pal);

    const QString css = QString(R"(
        QListWidget, QTableView, QTreeView {
            border: none;
            outline: none;
        }
        QLineEdit, QComboBox, QSpinBox {
            background: %1;
            border: 1px solid %2;
            border-radius: 5px;
            padding: 4px 8px;
            color: %3;
        }
        QPushButton {
            background: %4;
            border: 1px solid %2;
            border-radius: 5px;
            padding: 5px 10px;
            color: %3;
        }
        QPushButton:hover {
            background: %5;
            border-color: %6;
        }
        QPushButton:checked {
            background: %5;
            border-color: %6;
            color: %6;
        }
        QTabBar::tab {
            background: %4;
            color: %3;
            padding: 5px 12px;
            border: 1px solid %2;
            border-bottom: none;
            border-top-left-radius: 5px;
            border-top-right-radius: 5px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: %1;
            border-color: %6;
            color: %6;
            font-weight: bold;
        }
    )").arg(altBase.name(), border.name(), text.name(), btn.name(), hover.name(), acc);

    app->setStyleSheet(css);
}

} // namespace honeycomb

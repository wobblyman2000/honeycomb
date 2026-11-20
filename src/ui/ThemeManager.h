#pragma once

#include <QApplication>
#include <QColor>
#include <QString>

namespace honeycomb {

class ThemeManager {
public:
    static QString currentThemeId();
    static QString currentAccentHex();

    static void applyTheme(QApplication* app, const QString& themeId, const QString& accentHex);
    static void applySavedTheme(QApplication* app);
};

} // namespace honeycomb

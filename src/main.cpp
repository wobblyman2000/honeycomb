#include "ui/AppIcon.h"
#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Honeycomb");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Honeycomb");
    app.setDesktopFileName("honeycomb");
    app.setWindowIcon(honeycomb::createHoneycombIcon());
    app.setStyle(QStyleFactory::create("Fusion"));

    honeycomb::ThemeManager::applySavedTheme(&app);

    honeycomb::MainWindow w;
    w.show();
    return app.exec();
}

#include "ui/AppIcon.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Honeycomb");
    app.setOrganizationName("Honeycomb");
    app.setDesktopFileName("honeycomb");
    app.setWindowIcon(honeycomb::createHoneycombIcon());
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette dark;
    dark.setColor(QPalette::Window, QColor("#14151a"));
    dark.setColor(QPalette::WindowText, QColor("#e8eaf0"));
    dark.setColor(QPalette::Base, QColor("#181920"));
    dark.setColor(QPalette::AlternateBase, QColor("#1e2028"));
    dark.setColor(QPalette::ToolTipBase, QColor("#1e2028"));
    dark.setColor(QPalette::ToolTipText, QColor("#e8eaf0"));
    dark.setColor(QPalette::Text, QColor("#e8eaf0"));
    dark.setColor(QPalette::Button, QColor("#22242e"));
    dark.setColor(QPalette::ButtonText, QColor("#e8eaf0"));
    dark.setColor(QPalette::Highlight, QColor("#f5a623"));
    dark.setColor(QPalette::HighlightedText, QColor("#111111"));
    app.setPalette(dark);

    app.setStyleSheet(R"(
        QListWidget, QTableView, QTreeView {
            border: none;
            outline: none;
        }
        QLineEdit, QComboBox, QSpinBox {
            background: #1e2028;
            border: 1px solid #2e3140;
            border-radius: 5px;
            padding: 4px 8px;
        }
        QPushButton {
            background: #22242e;
            border: 1px solid #2e3140;
            border-radius: 5px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background: #2b2e3b;
            border-color: #f5a623;
        }
        QPushButton:checked {
            background: #3a2e1a;
            border-color: #f5a623;
            color: #f5a623;
        }
    )");

    honeycomb::MainWindow w;
    w.show();
    return app.exec();
}

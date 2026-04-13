#include "MainWindow.h"

#include <QtWidgets/QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Blueshot Editor"));
    app.setOrganizationName(QStringLiteral("Blueshot"));

    const QString initialFilePath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    MainWindow window(initialFilePath);
    window.show();
    return app.exec();
}

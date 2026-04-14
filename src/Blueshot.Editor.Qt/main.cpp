#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QCoreApplication::setApplicationName(QStringLiteral("Blueshot Editor"));
    QCoreApplication::setOrganizationName(QStringLiteral("Blueshot"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ae.humaidq.blueshot"));

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/win/applicationIcon/48.png")));

    const QString initialFilePath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    MainWindow window(initialFilePath);
    window.show();
    return app.exec();
}

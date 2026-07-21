#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char *argv[])
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("NickZhu"));
    QCoreApplication::setApplicationName(QStringLiteral("CacDoctorApp"));

    MainWindow w;
    w.show();
    return QApplication::exec();
}

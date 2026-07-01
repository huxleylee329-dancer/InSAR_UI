#include"QtGui"
#include<ColorBar.h>
#include <QtWidgets/QApplication>
#include"MainWindow.h"
#include"qheaderview.h"
#include <QPixmap>
//#include <QSplashScreen>
#include<string>
#include<icon_source.h>
#include <QFile>
#include <QSettings>
#include <QDialog>
//#include<QStyleFactory>
#include <QList>
#include <QVector>
#include <QPersistentModelIndex>
#include <QMetaType>
#include <QAbstractItemModel>
#include <vector>
#include "ImportTask.h"


// Global function to load QSS from file
QString loadStyleSheet(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to load stylesheet:" << fileName;
        return QString();
    }
    QString content = QString::fromUtf8(file.readAll());
    return content;
}

// Global function to apply theme
void applyTheme(const QString &theme = "light")
{
    // Load base styles from Qt resources
    QString appStyle = loadStyleSheet(":/SatExplorer/stylesheets/application.qss");
    QString widgetStyle = loadStyleSheet(":/SatExplorer/stylesheets/widgets.qss");
    QString dialogStyle = loadStyleSheet(":/SatExplorer/stylesheets/dialogs.qss");
    QString mainWindowStyle = loadStyleSheet(":/SatExplorer/stylesheets/mainwindow.qss");
    QString nodeEditorStyle = loadStyleSheet(":/SatExplorer/stylesheets/nodeeditor.qss");
    QString importNodesStyle = loadStyleSheet(":/SatExplorer/stylesheets/importnodes.qss");

    // Load theme variant
    QString themeStyle = loadStyleSheet(":/SatExplorer/stylesheets/themes/" + theme + ".qss");

    // Combine all styles
    QString fullStyle = appStyle + "\n" +
                       widgetStyle + "\n" +
                       dialogStyle + "\n" +
                       mainWindowStyle + "\n" +
                       nodeEditorStyle + "\n" +
                       importNodesStyle + "\n" +
                       themeStyle;

    qApp->setStyleSheet(fullStyle);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(APP_ICON));

    qRegisterMetaType<QList<QPersistentModelIndex>>("QList<QPersistentModelIndex>");
    qRegisterMetaType<QVector<int>>("QVector<int>");
    qRegisterMetaType<QAbstractItemModel::LayoutChangeHint>("QAbstractItemModel::LayoutChangeHint");
    qRegisterMetaType<std::vector<QString>>("std::vector<QString>");
    qRegisterMetaType<QList<double>>("QList<double>");
    qRegisterMetaType<ImportTask>("ImportTask");
    qRegisterMetaType<std::vector<ImportTask>>("std::vector<ImportTask>");


    // Load theme preference from Config.ini
    QSettings settings("Config.ini", QSettings::IniFormat);
    QString theme = settings.value("Appearance/Theme", "light").toString();

    // Apply default theme
    applyTheme(theme);

    QPixmap* k = new QPixmap(QString(CURSOR_UP_ICON));

    // Create MainWindow directly
    MainWindow* mainWindow = nullptr;
    if (argc == 2) {
        // Project file passed as command line argument
        mainWindow = new MainWindow(argv[1], nullptr);
    } else {
        // No project - MainWindow will show welcome screen
        mainWindow = new MainWindow(nullptr);
    }

    mainWindow->showMaximized();

    delete k;
    return a.exec();
}
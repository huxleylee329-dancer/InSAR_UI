#include"QtGui"
#include<ColorBar.h>
#include <QtWidgets/QApplication>
#include"MainWindow.h"
#include"WelcomeScreen.h"
#include"qheaderview.h"
#include <QPixmap>
//#include <QSplashScreen>
#include<string>
#include<icon_source.h>
#include <QFile>
#include <QSettings>
#include <QDialog>
//#include<QStyleFactory>

// Global function to load QSS from file
QString loadStyleSheet(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to load stylesheet:" << fileName;
        return QString();
    }
    QString content = QString::fromUtf8(file.readAll());
    qDebug() << "Loaded stylesheet:" << fileName << "Size:" << content.length() << "chars";
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
   /* QFile csv_test("E:/Urumqi2/SBAS.csv");
    QTextStream in(&csv_test);;
    csv_test.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            in << right<<qSetFieldWidth(6) << qSetRealNumberPrecision(5) << double(i) * 0.001 + double(j);

        }
        in << "\n";
    }
      */
    //csv_test.close();
    QApplication a(argc, argv);

    // Load theme preference from Config.ini
    QSettings settings("Config.ini", QSettings::IniFormat);
    QString theme = settings.value("Appearance/Theme", "light").toString();

    // Apply default theme
    applyTheme(theme);

    QPixmap* k = new QPixmap(QString(CURSOR_UP_ICON));

    // Show welcome screen dialog first
    WelcomeScreen *welcomeDialog = new WelcomeScreen(nullptr);
    welcomeDialog->setWindowFlag(Qt::Window);
    welcomeDialog->showMaximized();

    // Lambda to handle creation/opening of project and close welcome screen
    QString openedProjectPath;
    bool shouldProceed = false;

    auto proceedToMainWindow = [&](const QString &projectPath) {
        openedProjectPath = projectPath;
        shouldProceed = true;
        welcomeDialog->close();
    };

    QObject::connect(welcomeDialog, &WelcomeScreen::newProjectRequested, [proceedToMainWindow]() {
        proceedToMainWindow("");
    });
    QObject::connect(welcomeDialog, &WelcomeScreen::openProjectRequested, [&proceedToMainWindow, &a]() {
        QString filePath = QFileDialog::getOpenFileName(
            nullptr,
            QString::fromUtf8("打开项目"),
            QDir::currentPath(),
            QString::fromUtf8("InSAR Project (*.Insar);;All Files (*)")
        );
        if (!filePath.isEmpty()) {
            proceedToMainWindow(filePath);
        }
    });
    QObject::connect(welcomeDialog, &WelcomeScreen::recentProjectRequested, [proceedToMainWindow](const QString &filePath) {
        proceedToMainWindow(filePath);
    });

    // Enter event loop - WelcomeScreen handles user interaction
    // Wait until user chooses to proceed or closes the window
    while (!shouldProceed && welcomeDialog->isVisible()) {
        a.processEvents();
    }

    if (!shouldProceed) {
        // User closed window without opening project - exit
        delete welcomeDialog;
        delete k;
        return 0;
    }

    // Create MainWindow
    MainWindow* b = NULL;
    if (!openedProjectPath.isEmpty()) {
        b = new MainWindow(openedProjectPath, nullptr);
    }
    else if (argc == 2) {
        b = new MainWindow(argv[1], nullptr);
    }
    else {
        b = new MainWindow(nullptr);
    }

    b->showMaximized();

    delete welcomeDialog;
    delete k;
    return a.exec();
}
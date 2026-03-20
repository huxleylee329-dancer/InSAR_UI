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
    // Get application directory
    QString appDir = QCoreApplication::applicationDirPath();
    QString stylesDir = appDir + "/../stylesheets/";

    // Load base styles
    QString appStyle = loadStyleSheet(stylesDir + "application.qss");
    QString widgetStyle = loadStyleSheet(stylesDir + "widgets.qss");
    QString dialogStyle = loadStyleSheet(stylesDir + "dialogs.qss");
    QString mainWindowStyle = loadStyleSheet(stylesDir + "mainwindow.qss");
    QString nodeEditorStyle = loadStyleSheet(stylesDir + "nodeeditor.qss");
    QString importNodesStyle = loadStyleSheet(stylesDir + "importnodes.qss");

    // Load theme variant
    QString themeStyle = loadStyleSheet(stylesDir + "themes/" + theme + ".qss");

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
    /*开机启动画面*/
    /*
    char szFilePath[MAX_PATH + 1] = { 0 };
    GetModuleFileNameA(NULL, szFilePath, MAX_PATH);
    (strrchr(szFilePath, '\\'))[0] = 0;
    string exe_path(szFilePath);
    exe_path = exe_path.append("\\icon\\guide.png");
    QPixmap pixmap(exe_path.c_str());
    QSplashScreen splash(pixmap);
    splash.show();
    splash.showMessage(QStringLiteral("正在启动，请稍后......"), Qt::AlignHCenter | Qt::AlignBottom, Qt::white);
    QDateTime n = QDateTime::currentDateTime();
    QDateTime now;
    do {
        now = QDateTime::currentDateTime();
    } while (n.secsTo(now) <= 1);//3为需要延时的秒数
    */
    /*使程序在显示启动画面的同时仍能响应鼠标等其他事件*/
    a.processEvents();
    //if (argc == 3)
    //{
    //    MainWindow b(QString(argv[1]), nullptr);
    //    b.show();
    //    splash.finish(&b);
    //}
    //else
    //{
    //    MainWindow b;
    //    b.show();
    //    splash.finish(&b);
    //}
    MainWindow* b = NULL;
    if (argc == 2)
    {
        b = new MainWindow(argv[1], nullptr);
    }
    else
    {
        b = new MainWindow(nullptr);
    }
    //b->setAttribute(Qt::WA_QuitOnClose);
    b->show();
    //splash.finish(b);
    return a.exec();
}
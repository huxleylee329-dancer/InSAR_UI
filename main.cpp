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
#include <opencv2/core.hpp>
#include <QLoggingCategory>
#include <windows.h>
#include <stdio.h>
#include <iostream>
#include "NodeUtils.h"
#include "InSARLogManager.h"

/**
 * @brief 初始化调试控制台并重定向标准输出/标准错误
 * 移除了控制台的关闭按钮以防止误操作导致整个主程序闪退
 */
static void setupDebugConsole()
{
    if (AllocConsole()) {
        FILE* fpOut = nullptr;
        FILE* fpErr = nullptr;
        freopen_s(&fpOut, "CONOUT$", "w", stdout);
        freopen_s(&fpErr, "CONOUT$", "w", stderr);
        
        // 关闭输出缓冲区，确保实时输出
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        
        // 同步 C++ 标准流（std::cout, std::cerr）
        std::ios::sync_with_stdio();
        
        // 禁用控制台的快速编辑模式（QuickEdit Mode），防止用户误点控制台导致输出线程挂起
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        DWORD prev_mode;
        if (GetConsoleMode(hInput, &prev_mode)) {
            SetConsoleMode(hInput, prev_mode & ~ENABLE_QUICK_EDIT_MODE);
        }
        
        // 禁用控制台窗口的关闭按钮（X），防止误点导致进程终止
        HWND hwnd = GetConsoleWindow();
        if (hwnd != NULL) {
            HMENU hMenu = GetSystemMenu(hwnd, FALSE);
            if (hMenu != NULL) {
                DeleteMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);
            }
        }
        
        // 设置控制台标题
        SetConsoleTitleA("SatExplorer Debug Console");
    }
}

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
    // 禁用 Windows 平台的 TSF 输入法框架以防止输入法在 TextInputFramework.dll 中引发 MessagingValidationException 导致程序崩溃
    // 注释掉 QT_IM_MODULE="none" 以允许系统回退到 IMM32 框架输入中文，同时保持禁用 TSF 以防止崩溃
    // qputenv("QT_IM_MODULE", "none");
    qputenv("QT_DISABLE_TSF", "1");

    // 禁用 Intel IPP 优化以防止 OpenCV 在 Debug 模式或特定数据类型下引发 ipp::IwException 内部异常，避免调试器中断或闪退
    cv::ipp::setUseIPP(false);

    // 禁用 Qt 内部网络状态监视器报错输出（防止 Windows 底层网卡 GUID 获取失败时打印大量 nlansp_c.dll/qt.network.monitor 调试日志）
    QLoggingCategory::setFilterRules("qt.network.monitor.debug=false\n"
                                     "qt.network.monitor.warning=false");

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
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString theme = settings.value("Appearance/Theme", "light").toString();

    // Apply default theme
    applyTheme(theme);

    // 根据配置决定是否开启全局调试控制台
    bool showConsole = settings.value("Debug/ShowConsole", false).toBool();
    if (showConsole) {
        setupDebugConsole();
    }

    // Keep persisted diagnostic evidence independent of the temporary console.
    // Migrate the short-lived Debug key when present so existing developer
    // configurations retain their intended behavior.
    const QString diagnosticFileKey = QStringLiteral("Logging/DiagnosticFileEnabled");
    bool writeDiagnosticLog = false;
    if (settings.contains(diagnosticFileKey)) {
        writeDiagnosticLog = settings.value(diagnosticFileKey).toBool();
    } else {
        writeDiagnosticLog = settings.value("Debug/WriteDiagnosticLog", false).toBool();
        settings.setValue(diagnosticFileKey, writeDiagnosticLog);
    }
    InSARLogManager::instance().configureDiagnosticSinks(showConsole, writeDiagnosticLog);
    InSARLogManager::installQtMessageHandler();

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

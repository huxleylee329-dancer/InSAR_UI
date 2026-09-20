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
#include <atomic>
#include <thread>
#include <io.h>
#include <fcntl.h>
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

/**
 * @brief DLL 的 fprintf(stderr) 输出落盘
 *
 * 外部处理 DLL 的根因诊断（例如 SAR2UTM 的 "output grid rejected"）只走 stderr，
 * 且 CONOUT$ 重定向会把它们挡在持久化日志之外——控制台一关就再也无法复盘。
 * 这里把 stderr 接到管道，由读取线程按日志管理器的标准通道逐行转发（控制台 + 日志文件）。
 *
 * 前提：InSARLogManager 的控制台输出已改为直接写 CONOUT$ 句柄（writeEmergency），
 * 否则日志管理器自己的输出会被读取线程再次回灌，形成无限回环。
 * 因此本函数必须在 setupDebugConsole() 与 installQtMessageHandler() 之后调用。
 */
namespace {

std::atomic<bool> g_dllOutputCaptureActive{ false };

// 逐行转发到日志管理器。控制台回显不再由本线程手写：日志管理器的 DebugConsole
// 目标会经 writeEmergency 直写 CONOUT$ 完成，避免同一条输出走两条通道。
// targets 与 DemWorker 的 DemDLL 保持一致：控制台 + 诊断文件两个 sink。
void forwardExternalLine(const std::string& text)
{
    if (text.empty() || !g_dllOutputCaptureActive.load(std::memory_order_relaxed)) return;
    InSARLogManager::LogDiagnostic(InSARLogManager::LevelInfo, QStringLiteral("Stderr"),
        QString::fromLocal8Bit(text.c_str(), static_cast<int>(text.size())).left(2048),
        LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile,
        QStringLiteral("stderr"));
}

void dllOutputReaderLoop(int readFd)
{
    std::string pending;
    char buffer[4096];
    for (;;) {
        const int bytesRead = _read(readFd, buffer, static_cast<unsigned>(sizeof(buffer)));
        if (bytesRead <= 0) break;
        pending.append(buffer, static_cast<size_t>(bytesRead));

        size_t consumed = 0;
        for (;;) {
            const size_t newline = pending.find('\n', consumed);
            if (newline == std::string::npos) break;
            std::string line = pending.substr(consumed, newline - consumed);
            consumed = newline + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            forwardExternalLine(line);
        }
        pending.erase(0, consumed);

        // 长时间不换行的异常输出：截断转发，避免在内存里无限累积
        if (pending.size() > 65536) {
            forwardExternalLine(pending);
            pending.clear();
        }
    }
    _close(readFd);
}

void setupDllOutputCapture()
{
    int pipeFds[2] = { -1, -1 };
    if (_pipe(pipeFds, 64 * 1024, _O_BINARY) != 0) return;
    if (_dup2(pipeFds[1], 2) != 0) {
        _close(pipeFds[0]);
        _close(pipeFds[1]);
        return;
    }
    _close(pipeFds[1]);                       // 写端已由 fd 2 持有，见 _dup2 语义
    setvbuf(stderr, nullptr, _IONBF, 0);      // 逐行落盘，不让 CRT 缓冲拖住输出
    g_dllOutputCaptureActive.store(true);
    std::thread(dllOutputReaderLoop, pipeFds[0]).detach();
}

} // namespace

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
    // 必须在控制台与日志管理器都就绪之后再接管 stderr
    setupDllOutputCapture();

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
    const int exitCode = a.exec();
    // 退出阶段停止回灌，避免读取线程与日志管理器、静态析构互踩
    g_dllOutputCaptureActive.store(false);
    return exitCode;
}

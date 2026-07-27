#include "Sentinel1ImportWorker.h"
#include <FormatConversion.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <string>

namespace {

bool validateAcquisitionTimeRange(const QString& h5Path, QString& errorMessage)
{
    std::string startText;
    std::string stopText;
    QString readError;
    if (!NodeUtils::readStringFromH5(h5Path, "acquisition_start_time", startText, &readError) ||
        !NodeUtils::readStringFromH5(h5Path, "acquisition_stop_time", stopText, &readError)) {
        errorMessage = QStringLiteral("Unable to read Sentinel-1 acquisition time metadata from %1: %2")
            .arg(h5Path, readError);
        return false;
    }

    FormatConversion conversion;
    double startGps = 0.0;
    double stopGps = 0.0;
    if (conversion.utc2gps(startText.c_str(), &startGps) != 0 ||
        conversion.utc2gps(stopText.c_str(), &stopGps) != 0 ||
        stopGps <= startGps) {
        errorMessage = QStringLiteral("Invalid Sentinel-1 acquisition time range in %1: start=%2, stop=%3")
            .arg(h5Path)
            .arg(QString::fromStdString(startText))
            .arg(QString::fromStdString(stopText));
        return false;
    }

    InSARLogManager::LogInfo("Sentinel1ImportWorker",
        QStringLiteral("Validated Sentinel-1 acquisition time metadata: file=%1, start=%2 (%3), stop=%4 (%5)")
            .arg(h5Path)
            .arg(QString::fromStdString(startText))
            .arg(startGps, 0, 'f', 3)
            .arg(QString::fromStdString(stopText))
            .arg(stopGps, 0, 'f', 3));
    return true;
}

} // namespace

// 进度回调上下文
struct S1ProgressContext
{
    Sentinel1ImportWorker* worker;
    int progressMin;
    int progressMax;
};

// DLL进度回调：将DLL内部0-100映射到当前任务的进度区间
static bool onS1DllProgress(int percent, const char* message, void* userData)
{
    S1ProgressContext* ctx = static_cast<S1ProgressContext*>(userData);
    if (!ctx || !ctx->worker)
        return false;

    if (ctx->worker->isStopRequested())
    {
        return false;
    }

    int mapped = ctx->progressMin + (ctx->progressMax - ctx->progressMin) * percent / 100;
    QString msg = (message && message[0]) ? QString::fromUtf8(message) : QStringLiteral("正在导入...");
    ctx->worker->updateImportProgress(mapped, msg);
    return true;
}

Sentinel1ImportWorker::Sentinel1ImportWorker(QObject* parent)
    : BaseImportWorker("Sentinel1", parent)
{
}

Sentinel1ImportWorker::~Sentinel1ImportWorker()
{
}

bool Sentinel1ImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                       int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.size() < 3) return false;

    QString manifest_file = arguments[0];
    QString subswath      = arguments[1];
    QString polarization  = arguments[2];

    // 智能识别 arguments[3]：EOF 文件路径 vs burst 起始编号
    // EOF 路径包含 ".EOF" 扩展名或路径分隔符；burst 编号为纯数字字符串
    QString pod_file;
    int burstArgStart = 3;
    if (arguments.size() > 3 && arguments[3].contains(".EOF", Qt::CaseInsensitive)) {
        pod_file = arguments[3];
        burstArgStart = 4;
    }

    int start_burst = -1;
    int end_burst = -1;
    if (arguments.size() > burstArgStart + 1) {
        start_burst = arguments[burstArgStart].toInt();
        end_burst = arguments[burstArgStart + 1].toInt();
    }

    FormatConversion conversion;
    S1ProgressContext context;
    context.worker = this;
    context.progressMin = progressMin;
    context.progressMax = progressMax;

    int ret = conversion.import_sentinel(
        manifest_file.toStdString().c_str(),
        subswath.toStdString().c_str(),
        polarization.toStdString().c_str(),
        outputPath.toStdString().c_str(),
        pod_file.isEmpty() ? nullptr : pod_file.toStdString().c_str(),
        onS1DllProgress,
        &context,
        start_burst,
        end_burst
    );

    if (ret < 0) {
        outErrorMsg = QString("底转换 DLL 执行失败，错误码：%1。请核对爆块(Burst)范围或原始数据文件是否损坏。").arg(ret);
    }

    if (ret < 0) {
        return false;
    }

    return validateAcquisitionTimeRange(outputPath, outErrorMsg);
}

#include "Sentinel1ImportWorker.h"
#include <FormatConversion.h>

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
                                       int progressMin, int progressMax)
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

    return ret >= 0;
}

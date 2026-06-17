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
static void onS1DllProgress(int percent, const char* message, void* userData)
{
    S1ProgressContext* ctx = static_cast<S1ProgressContext*>(userData);
    if (!ctx || !ctx->worker)
        return;
    int mapped = ctx->progressMin + (ctx->progressMax - ctx->progressMin) * percent / 100;
    QString msg = (message && message[0]) ? QString::fromUtf8(message) : QStringLiteral("正在导入...");
    ctx->worker->updateImportProgress(mapped, msg);
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
    QString pod_file      = arguments.size() > 3 ? arguments[3] : "";

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
        &context
    );

    return ret >= 0;
}

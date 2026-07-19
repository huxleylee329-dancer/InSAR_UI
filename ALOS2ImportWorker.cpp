#include "ALOS2ImportWorker.h"
#include <FormatConversion.h>

// 进度回调上下文
struct Alos2ProgressContext
{
    ALOS2ImportWorker* worker;
    int progressMin;
    int progressMax;
};

// DLL进度回调
static bool onAlos2Progress(int percent, const char* message, void* userData)
{
    Alos2ProgressContext* ctx = static_cast<Alos2ProgressContext*>(userData);
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

ALOS2ImportWorker::ALOS2ImportWorker(QObject* parent)
    : BaseImportWorker("ALOS2", parent)
{
}

ALOS2ImportWorker::~ALOS2ImportWorker()
{
}

bool ALOS2ImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                   int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.size() < 2) return false;

    QString img_file = arguments[0];
    QString led_file = arguments[1];

    FormatConversion conversion;
    Alos2ProgressContext progressCtx;
    progressCtx.worker = this;
    progressCtx.progressMin = progressMin;
    progressCtx.progressMax = progressMax;

    int ret = conversion.ALOS2h5(
        img_file.toStdString().c_str(),
        led_file.toStdString().c_str(),
        outputPath.toStdString().c_str(),
        onAlos2Progress,
        &progressCtx
    );

    return ret >= 0;
}

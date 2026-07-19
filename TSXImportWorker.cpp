#include "TSXImportWorker.h"
#include <FormatConversion.h>

// 进度回调上下文
struct TsxProgressContext
{
    TSXImportWorker* worker;
    int progressMin;
    int progressMax;
};

// DLL进度回调：将DLL内部0-100映射到UI进度区间
static bool onTsxProgress(int percent, const char* message, void* userData)
{
    TsxProgressContext* ctx = static_cast<TsxProgressContext*>(userData);
    if (!ctx || !ctx->worker)
        return false;

    if (ctx->worker->isStopRequested())
    {
        return false;
    }

    int mapped = ctx->progressMin + (ctx->progressMax - ctx->progressMin) * percent / 100;
    QString msg = (message && message[0]) ? QString::fromUtf8(message) : QStringLiteral("正在导入数据，请耐心等待……");
    ctx->worker->updateImportProgress(mapped, msg);
    return true;
}

TSXImportWorker::TSXImportWorker(QObject* parent)
    : BaseImportWorker("TSX", parent)
{
}

TSXImportWorker::~TSXImportWorker()
{
}

bool TSXImportWorker::convertToH5(const QStringList& arguments, const QString& outputPath,
                                 int progressMin, int progressMax, QString& outErrorMsg)
{
    if (arguments.size() < 2) return false;

    QString xml_filename = arguments[0];
    QString polarization = arguments[1];

    FormatConversion conversion;
    TsxProgressContext progressCtx;
    progressCtx.worker = this;
    progressCtx.progressMin = progressMin;
    progressCtx.progressMax = progressMax;

    int ret = conversion.TSX2h5(
        xml_filename.toStdString().c_str(),
        outputPath.toStdString().c_str(),
        polarization.toStdString().c_str(),
        onTsxProgress,
        &progressCtx
    );

    return ret >= 0;
}

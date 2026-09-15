#include "UnwrapWorker.h"
#include "NodeUtils.h"
#include <Unwrap.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QDir>
#include <QFile>
#include <QThread>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QByteArray>
#include <QRegularExpression>
#include <cmath>
#include <climits>
#include <cstdint>
#include <limits>
#include <vector>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Unwrap_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Unwrap.lib")
#endif

thread_local UnwrapWorker* t_currentUnwrapWorker = nullptr;
thread_local int t_unwrapCurrentImageIndex = 0;
thread_local int t_unwrapTotalImagesCount = 1;
thread_local int t_unwrapLastLoggedProgress = -10;
thread_local int t_unwrapLastReportedProgress = -1;

namespace {

QString diagnosticString(const char* bytes, int capacity)
{
    const QByteArray value(bytes, capacity);
    const int terminator = value.indexOf('\0');
    return QString::fromUtf8(value.constData(), terminator >= 0 ? terminator : value.size()).trimmed();
}

QString eventString(const char* bytes, int capacity)
{
    return diagnosticString(bytes, capacity);
}

SnaphuRunOptionsV1 makeSnaphuRunOptions(const SnaphuUiOptions& uiOptions)
{
    SnaphuRunOptionsV1 options = {};
    options.structSize = sizeof(options);
    options.version = 1;
    options.tileRows = uiOptions.tileRows;
    options.tileCols = uiOptions.tileCols;
    options.rowOverlap = uiOptions.rowOverlap;
    options.colOverlap = uiOptions.colOverlap;
    // Windows SNAPHU currently serializes tile execution internally.
    options.requestedProcessCount = 1;
    options.wallTimeoutMilliseconds = uiOptions.wallTimeoutMilliseconds;
    options.heartbeatMilliseconds = 1000;
    options.statisticalCostMode = uiOptions.statisticalCostMode;
    if (uiOptions.keepArtifactsOnSuccess) {
        options.flags |= SNAPHU_RUN_OPTION_KEEP_ARTIFACTS_ON_SUCCESS;
    }
    return options;
}

QString diagnosticStageName(uint32_t stage)
{
    switch (stage) {
    case UNWRAP_DIAGNOSTIC_STAGE_INPUT: return QStringLiteral("input");
    case UNWRAP_DIAGNOSTIC_STAGE_PATH: return QStringLiteral("path");
    case UNWRAP_DIAGNOSTIC_STAGE_PREPARE: return QStringLiteral("prepare");
    case UNWRAP_DIAGNOSTIC_STAGE_LAUNCH: return QStringLiteral("launch");
    case UNWRAP_DIAGNOSTIC_STAGE_JOB: return QStringLiteral("job");
    case UNWRAP_DIAGNOSTIC_STAGE_PROCESS_EXIT: return QStringLiteral("process exit");
    case UNWRAP_DIAGNOSTIC_STAGE_OUTPUT: return QStringLiteral("output validation");
    case UNWRAP_DIAGNOSTIC_STAGE_COMPLETED: return QStringLiteral("completed");
    case UNWRAP_DIAGNOSTIC_STAGE_INTERNAL: return QStringLiteral("internal");
    default: return QStringLiteral("unknown");
    }
}

QString diagnosticFailureMessage(const QString& operation, const UnwrapDiagnostic& diagnostic, int result)
{
    const QString tool = diagnosticString(diagnostic.tool, sizeof(diagnostic.tool));
    const QString summary = diagnosticString(diagnostic.summary, sizeof(diagnostic.summary));
    const QString stderrTail = diagnosticString(diagnostic.stderrTail, sizeof(diagnostic.stderrTail)).trimmed();
    QString msg = QStringLiteral("%1 failed (%2, stage=%3, status=%4, win32Error=%5, exitCode=%6): %7")
        .arg(operation,
             tool.isEmpty() ? QStringLiteral("unwrap") : tool,
             diagnosticStageName(diagnostic.stage))
        .arg(result)
        .arg(diagnostic.win32Error)
        .arg(diagnostic.exitCode)
        .arg(summary.isEmpty() ? QStringLiteral("No diagnostic summary.") : summary);
    if (!stderrTail.isEmpty()) {
        QString cleanStderr = stderrTail;
        cleanStderr.replace(QLatin1Char('\r'), QLatin1String(""));
        cleanStderr.replace(QLatin1Char('\n'), QLatin1String(" | "));
        msg += QStringLiteral(" | stderr: %1").arg(cleanStderr);
    }
    return msg;
}

struct UnwrapAmplitudeStatus
{
    QString status = QStringLiteral("not_applicable");
    QString reason = QStringLiteral("method_not_snaphu");
    int masterRows = 0;
    int masterCols = 0;
    int slaveRows = 0;
    int slaveCols = 0;
    int expectedRows = 0;
    int expectedCols = 0;
    bool degraded = false;
};

UnwrapAmplitudeStatus parseSnaphuAmplitudeStatus(const QString& summary)
{
    UnwrapAmplitudeStatus result;
    result.status = QStringLiteral("unknown");
    result.reason = QStringLiteral("missing_amplitude_diagnostic");
    result.degraded = true;

    static const QRegularExpression usedExpression(
        QStringLiteral("amp=used\\((\\d+)x(\\d+)\\)"));
    static const QRegularExpression mismatchExpression(
        QStringLiteral("amp=omitted_dimension_mismatch\\((\\d+)x(\\d+),(\\d+)x(\\d+); expected=(\\d+)x(\\d+)\\)"));

    const QRegularExpressionMatch mismatchMatch = mismatchExpression.match(summary);
    if (mismatchMatch.hasMatch()) {
        result.status = QStringLiteral("omitted_dimension_mismatch");
        result.reason = QStringLiteral("source_amplitude_dimensions_do_not_match_phase");
        result.masterRows = mismatchMatch.captured(1).toInt();
        result.masterCols = mismatchMatch.captured(2).toInt();
        result.slaveRows = mismatchMatch.captured(3).toInt();
        result.slaveCols = mismatchMatch.captured(4).toInt();
        result.expectedRows = mismatchMatch.captured(5).toInt();
        result.expectedCols = mismatchMatch.captured(6).toInt();
        return result;
    }

    const QRegularExpressionMatch usedMatch = usedExpression.match(summary);
    if (usedMatch.hasMatch()) {
        result.status = QStringLiteral("used");
        result.reason = QStringLiteral("none");
        result.masterRows = usedMatch.captured(1).toInt();
        result.masterCols = usedMatch.captured(2).toInt();
        result.slaveRows = result.masterRows;
        result.slaveCols = result.masterCols;
        result.expectedRows = result.masterRows;
        result.expectedCols = result.masterCols;
        result.degraded = false;
        return result;
    }

    if (summary.contains(QStringLiteral("amp=unavailable"))) {
        result.status = QStringLiteral("unavailable");
        result.reason = QStringLiteral("source_amplitude_unavailable");
    }
    return result;
}

bool readOffset(const QString& h5Path, const char* dataset, int& offset, QString& error)
{
    cv::Mat value;
    if (!NodeUtils::readMatFromH5(h5Path, QString::fromLatin1(dataset), value) ||
        value.empty() || value.type() != CV_32SC1 || value.total() != 1) {
        error = QStringLiteral("Unable to read scalar %1 from %2.")
                    .arg(QString::fromLatin1(dataset), h5Path);
        return false;
    }
    offset = value.at<int>(0, 0);
    return true;
}

bool auditAndNormalizeMcfInputs(const cv::Mat& phase,
                                const cv::Mat& coherence,
                                cv::Mat& residue,
                                qint64& normalizedResidues,
                                qint64& nonZeroResidues,
                                QString& error)
{
    normalizedResidues = 0;
    nonZeroResidues = 0;
    if (phase.empty() || phase.type() != CV_64F || phase.channels() != 1 ||
        coherence.empty() || coherence.type() != CV_64F || coherence.channels() != 1 ||
        residue.empty() || residue.type() != CV_64F || residue.channels() != 1) {
        error = QStringLiteral("MCF 输入矩阵类型或尺寸无效");
        return false;
    }
    if (coherence.size() != phase.size() ||
        residue.rows != phase.rows - 1 || residue.cols != phase.cols - 1) {
        error = QStringLiteral("MCF 输入矩阵尺寸不匹配：phase=%1x%2, coherence=%3x%4, residue=%5x%6")
            .arg(phase.rows).arg(phase.cols)
            .arg(coherence.rows).arg(coherence.cols)
            .arg(residue.rows).arg(residue.cols);
        return false;
    }

    for (int row = 0; row < phase.rows; ++row) {
        const double* values = phase.ptr<double>(row);
        for (int column = 0; column < phase.cols; ++column) {
            if (!std::isfinite(values[column])) {
                error = QStringLiteral("MCF phase 包含非有限值：row=%1, col=%2")
                    .arg(row).arg(column);
                return false;
            }
        }
    }
    for (int row = 0; row < coherence.rows; ++row) {
        const double* values = coherence.ptr<double>(row);
        for (int column = 0; column < coherence.cols; ++column) {
            if (!std::isfinite(values[column]) || values[column] < 0.0) {
                error = QStringLiteral("MCF coherence 包含非法值：row=%1, col=%2, value=%3")
                    .arg(row).arg(column).arg(values[column], 0, 'g', 17);
                return false;
            }
        }
    }

    // Utils::write_DIMACS() serializes residue supplies as integers. The
    // residue calculation is mathematically integral, but can produce values
    // such as 1.0000000000000002 after floating-point wrapping.
    constexpr double kResidueIntegerTolerance = 1e-9;
    for (int row = 0; row < residue.rows; ++row) {
        double* values = residue.ptr<double>(row);
        for (int column = 0; column < residue.cols; ++column) {
            const double value = values[column];
            if (!std::isfinite(value)) {
                error = QStringLiteral("MCF residue 包含非有限值：row=%1, col=%2")
                    .arg(row).arg(column);
                return false;
            }
            if (std::fabs(value) <= 0.5) {
                continue;
            }
            ++nonZeroResidues;
            const double rounded = std::round(value);
            if (!std::isfinite(rounded) || std::fabs(value - rounded) > kResidueIntegerTolerance) {
                error = QStringLiteral("MCF residue 不是整数：row=%1, col=%2, value=%3")
                    .arg(row).arg(column).arg(value, 0, 'g', 17);
                return false;
            }
            if (value != rounded) {
                values[column] = rounded;
                ++normalizedResidues;
            }
        }
    }
    return true;
}

// Keep the UI gate aligned with Core's CS2 working-set estimate so a clearly
// unsupported network is rejected before the expensive coherence calculation.
struct UiCs2NodeLayoutEstimate
{
    int64_t excess;
    int64_t price;
    void* first;
    void* current;
    void* suspended;
    void* qNext;
    void* bucketNext;
    void* bucketPrevious;
    long rank;
    long input;
};

struct UiCs2ArcLayoutEstimate
{
    long residualCapacity;
    int64_t cost;
    void* head;
    void* sister;
    long sourceIndex;
};

struct UiCs2BucketLayoutEstimate
{
    void* first;
};

struct UiMcfInputArcLayoutEstimate
{
    long tail;
    long head;
    long lower;
    long upper;
    long long cost;
};

struct McfDimensionPreflight
{
    qint64 nodes = 0;
    qint64 arcs = 0;
    quint64 estimatedWorkingSetBytes = 0;
    QString failure;
};

constexpr quint64 kMcfWorkingSetBudgetBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;

bool checkedMcfAdd(quint64 left, quint64 right, quint64& output)
{
    if (right > (std::numeric_limits<quint64>::max)() - left) return false;
    output = left + right;
    return true;
}

bool checkedMcfMultiply(quint64 left, quint64 right, quint64& output)
{
    if (left != 0 && right > (std::numeric_limits<quint64>::max)() / left) return false;
    output = left * right;
    return true;
}

QString formatMcfBytes(quint64 bytes)
{
    return QStringLiteral("%1 GiB").arg(
        static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

bool preflightMcfDimensions(int phaseRows, int phaseCols, McfDimensionPreflight& report)
{
    report = McfDimensionPreflight{};
    if (phaseRows < 3 || phaseCols < 3) {
        report.failure = QStringLiteral("MCF phase 尺寸不支持：至少需要 3x3，当前为 %1x%2")
            .arg(phaseRows).arg(phaseCols);
        return false;
    }

    const quint64 residueRows = static_cast<quint64>(phaseRows - 1);
    const quint64 residueCols = static_cast<quint64>(phaseCols - 1);
    quint64 pixelCount = 0;
    quint64 nodes = 0;
    quint64 arcs = 0;
    quint64 term = 0;
    if (!checkedMcfMultiply(residueRows, residueCols, pixelCount) ||
        !checkedMcfAdd(pixelCount, 1, nodes) ||
        !checkedMcfMultiply(2, residueRows - 1, term) ||
        !checkedMcfMultiply(term, residueCols, term) ||
        !checkedMcfAdd(arcs, term, arcs) ||
        !checkedMcfMultiply(2, residueRows, term) ||
        !checkedMcfMultiply(term, residueCols - 1, term) ||
        !checkedMcfAdd(arcs, term, arcs) ||
        !checkedMcfMultiply(4, residueCols, term) ||
        !checkedMcfAdd(arcs, term, arcs) ||
        !checkedMcfMultiply(4, residueRows - 2, term) ||
        !checkedMcfAdd(arcs, term, arcs)) {
        report.failure = QStringLiteral("MCF 网络尺寸计算溢出");
        return false;
    }
    if (nodes > static_cast<quint64>((std::numeric_limits<qint64>::max)()) ||
        arcs > static_cast<quint64>((std::numeric_limits<qint64>::max)()) ||
        nodes > static_cast<quint64>(LONG_MAX) || arcs > static_cast<quint64>(LONG_MAX)) {
        report.nodes = nodes > static_cast<quint64>((std::numeric_limits<qint64>::max)())
            ? (std::numeric_limits<qint64>::max)() : static_cast<qint64>(nodes);
        report.arcs = arcs > static_cast<quint64>((std::numeric_limits<qint64>::max)())
            ? (std::numeric_limits<qint64>::max)() : static_cast<qint64>(arcs);
        report.failure = QStringLiteral("MCF 网络规模超过 DIMACS/solver 整数限制：nodes=%1, arcs=%2")
            .arg(QString::number(nodes)).arg(QString::number(arcs));
        return false;
    }

    report.nodes = static_cast<qint64>(nodes);
    report.arcs = static_cast<qint64>(arcs);
    quint64 directedArcSlots = 0;
    quint64 twoArcSlots = 0;
    quint64 bucketCount = 0;
    quint64 count = 0;
    if (!checkedMcfMultiply(2, arcs, twoArcSlots) ||
        !checkedMcfAdd(twoArcSlots, 1, directedArcSlots) ||
        !checkedMcfAdd(nodes, 1, bucketCount) ||
        !checkedMcfMultiply(bucketCount, 12, bucketCount) ||
        !checkedMcfAdd(bucketCount, 2, bucketCount) ||
        !checkedMcfAdd(nodes, 2, count)) {
        report.failure = QStringLiteral("MCF 工作集估算溢出");
        return false;
    }

    quint64 bytes = 0;
    const auto addAllocation = [&bytes](quint64 itemCount, size_t itemSize) {
        quint64 allocation = 0;
        quint64 next = 0;
        return checkedMcfMultiply(itemCount, static_cast<quint64>(itemSize), allocation) &&
            checkedMcfAdd(bytes, allocation, next) && (bytes = next, true);
    };
    if (!addAllocation(count, sizeof(UiCs2NodeLayoutEstimate)) ||
        !addAllocation(directedArcSlots, sizeof(UiCs2ArcLayoutEstimate)) ||
        !addAllocation(twoArcSlots, sizeof(long)) ||
        !addAllocation(twoArcSlots, sizeof(long)) ||
        !addAllocation(count, sizeof(long)) ||
        !addAllocation(bucketCount, sizeof(UiCs2BucketLayoutEstimate)) ||
        !addAllocation(nodes, sizeof(int64_t)) ||
        !addAllocation(arcs, sizeof(UiMcfInputArcLayoutEstimate)) ||
        !addAllocation(nodes, sizeof(long long)) ||
        !addAllocation(arcs, sizeof(long))) {
        report.failure = QStringLiteral("MCF 工作集估算溢出");
        return false;
    }
    report.estimatedWorkingSetBytes = bytes;
    if (bytes > kMcfWorkingSetBudgetBytes) {
        report.failure = QStringLiteral(
            "MCF 网络规模不受当前 CS2 实现支持；建议使用 SNAPHU tiled 或 SPD Guided");
        return false;
    }
    return true;
}

} // namespace

static bool __stdcall snaphuRunEventCallback(const SnaphuRunEventV1* event, void* userData)
{
    UnwrapWorker* worker = static_cast<UnwrapWorker*>(userData);
    if (!worker) {
        return false;
    }

    // SnaphuRunEventV1 is borrowed by Core. Copy all fields before crossing Qt threads.
    SnaphuRunEventInfo copied;
    if (event) {
        copied.type = event->type;
        copied.effectiveProcessCount = event->effectiveProcessCount;
        copied.metricAvailability = event->metricAvailability;
        copied.elapsedMilliseconds = event->elapsedMilliseconds;
        copied.totalCpuMilliseconds = event->totalCpuMilliseconds;
        copied.peakJobMemoryBytes = event->peakJobMemoryBytes;
        copied.readBytes = event->readBytes;
        copied.writeBytes = event->writeBytes;
        copied.taskDirectory = eventString(event->taskDirectory, sizeof(event->taskDirectory));
        copied.configPath = eventString(event->configPath, sizeof(event->configPath));
        copied.message = eventString(event->message, sizeof(event->message));
        Q_EMIT worker->snaphuRunEvent(copied);
    }

    return !worker->thread()->isInterruptionRequested() && !worker->isStopRequested();
}

static bool __stdcall unwrapProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer s_cbTimer;
    thread_local bool s_timerStarted = false;
    if (!s_timerStarted) {
        s_cbTimer.start();
        s_timerStarted = true;
    }
    if (progress != 0 && progress != 100 && s_cbTimer.elapsed() < 100) {
        return true;
    }
    s_cbTimer.restart();

    if (t_currentUnwrapWorker)
    {
        if (t_currentUnwrapWorker->thread()->isInterruptionRequested() || t_currentUnwrapWorker->isStopRequested())
        {
            return false;
        }

        const int boundedProgress = qBound(0, progress, 100);
        if (boundedProgress <= t_unwrapLastReportedProgress) {
            return true;
        }

        const int start_prog = 10 + t_unwrapCurrentImageIndex * 80 / t_unwrapTotalImagesCount;
        const int end_prog = 10 + (t_unwrapCurrentImageIndex + 1) * 80 / t_unwrapTotalImagesCount;
        const int mapped_prog = start_prog + boundedProgress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_currentUnwrapWorker->updateProcess(mapped_prog, QStringLiteral("第%1幅图像解缠中：%2% (%3)")
            .arg(t_unwrapCurrentImageIndex + 1).arg(boundedProgress).arg(msgStr));

        if (boundedProgress == 0 || boundedProgress == 100 ||
            (boundedProgress - t_unwrapLastLoggedProgress) >= 10)
        {
            InSARLogManager::LogInfo("UnwrapWorker", QString("Unwrap progress: %1% (Total: %2%) - %3")
                .arg(boundedProgress).arg(mapped_prog).arg(msgStr));
            t_unwrapLastLoggedProgress = boundedProgress;
        }
        t_unwrapLastReportedProgress = boundedProgress;
    }
    return true;
}

struct UnwrapThreadLocalGuard {
    UnwrapThreadLocalGuard(UnwrapWorker* worker, int total) {
        t_currentUnwrapWorker = worker;
        t_unwrapCurrentImageIndex = 0;
        t_unwrapTotalImagesCount = total;
        t_unwrapLastLoggedProgress = -10;
        t_unwrapLastReportedProgress = -1;
    }
    ~UnwrapThreadLocalGuard() {
        t_currentUnwrapWorker = nullptr;
        t_unwrapCurrentImageIndex = 0;
        t_unwrapTotalImagesCount = 1;
        t_unwrapLastLoggedProgress = -10;
        t_unwrapLastReportedProgress = -1;
    }
};

UnwrapWorker::UnwrapWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<UnwrapFileResult>("UnwrapFileResult");
    qRegisterMetaType<SnaphuUiOptions>("SnaphuUiOptions");
    qRegisterMetaType<SnaphuRunEventInfo>("SnaphuRunEventInfo");
}

UnwrapWorker::~UnwrapWorker()
{
}

void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_path, QString file_name,
                          QStringList phasePaths, SnaphuUiOptions snaphuOptions)
{
    const auto finishCancelled = [this]() {
        InSARLogManager::LogInfo("UnwrapWorker", "Unwrap cancelled by user.");
        Q_EMIT cancelled();
    };
    UnwrapThreadLocalGuard guard(this, qMax(1, phasePaths.size()));
    InSARLogManager::LogInfo("UnwrapWorker", QString("Unwrap task started. Output folder: %1, Method: %2").arg(file_name).arg(method));

    if (save_path.isEmpty() || file_name.isEmpty() || phasePaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }


    QString absolute_path = save_path + "/" + file_name;
    QDir target_dir(absolute_path);
    if (!target_dir.exists() && !QDir(save_path).mkdir(file_name))
    {
        emit errorProcess(QStringLiteral("无法创建解缠输出目录: %1").arg(absolute_path));
        return;
    }

    QList<QString> phase_name;
    QList<QString> phase_path;
    QList<QString> unwrap_name;
    QList<QString> relative_unwrap_path;
    QList<QString> absolute_unwrap_path;
    emit updateProcess(10, QStringLiteral("准备数据……"));

    for (const QString& path : phasePaths) {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists() || fileInfo.baseName().isEmpty()) {
            emit errorProcess(QStringLiteral("Invalid phase input path: %1").arg(path));
            return;
        }
        const QString originName = fileInfo.baseName();
        const QString changeName = originName + "_unwrapped";
        phase_name.append(originName);
        phase_path.append(fileInfo.absoluteFilePath());
        unwrap_name.append(changeName);
        relative_unwrap_path.append("/" + file_name + "/" + changeName + ".h5");
        absolute_unwrap_path.append(QDir(save_path).filePath(file_name + "/" + changeName + ".h5"));
    }
    int image_number = phase_name.size();
    if (image_number == 0) {
        emit errorProcess(QStringLiteral("没有可解缠的干涉图像"));
        return;
    }
    t_unwrapTotalImagesCount = image_number;
    std::vector<UnwrapAmplitudeStatus> amplitudeStatuses(static_cast<size_t>(image_number));

    ::Unwrap unwrap;
    FormatConversion FC;
    Utils util;
    
    int ret = 0;

    std::vector<int> offset_rows(image_number, 0);
    std::vector<int> offset_cols(image_number, 0);
    std::vector<bool> process_success(image_number, false);

    const auto failImage = [this, image_number, method](int index, const QString& stage, int code) {
        QString methodName;
        switch (method) {
        case 1: methodName = QStringLiteral("SPD Guided"); break;
        case 2: methodName = QStringLiteral("MCF"); break;
        case 3: methodName = QStringLiteral("SNAPHU"); break;
        case 4: methodName = QStringLiteral("Quality Guided MCF"); break;
        default: methodName = QStringLiteral("Unknown"); break;
        }
        emit errorProcess(QStringLiteral("%1 failed for image %2/%3 (%4, code=%5).")
                              .arg(methodName)
                              .arg(index + 1)
                              .arg(image_number)
                              .arg(stage)
                              .arg(code));
    };

    const auto failDiagnostic = [this, &finishCancelled](const QString& operation,
                                                         const UnwrapDiagnostic& diagnostic,
                                                         int result) {
        if (diagnostic.cancelled != 0 ||
            QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            finishCancelled();
            return;
        }
        emit errorProcess(diagnosticFailureMessage(operation, diagnostic, result));
    };

    QString metadataError;
    auto copyH5Metadata = [&](int idx) -> bool {
        NodeUtils::Hdf5Locker locker;
        /*写入h5*/
        ret = FC.creat_new_h5(absolute_unwrap_path.at(idx).toStdString().c_str());
        if (ret < 0) {
            metadataError = QStringLiteral("无法创建解缠输出 H5");
            return false;
        }

        string tmp_str;
        Mat tmp;
        QString phaseH5 = phase_path.at(idx);
        QString unwrapH5 = absolute_unwrap_path.at(idx);
        
        PathResolver::Resolution masterResolution;
        {
            NodeUtils::Hdf5Locker locker;
            FormatConversion FC;
            if (!NodeUtils::readStringFromH5(phaseH5, "source_1", tmp_str)) {
                metadataError = QStringLiteral("无法读取 source_1");
                return false;
            }
            PathResolver::Error pathError = PathResolver::Error::None;
            const QByteArray projectRootUtf8 = save_path.toUtf8();
            if (!PathResolver::resolve(tmp_str, projectRootUtf8.toStdString(), masterResolution, &pathError)) {
                metadataError = QStringLiteral("无法解析 source_1: %1")
                                    .arg(QString::fromLatin1(PathResolver::errorMessage(pathError)));
                return false;
            }
            if (FC.write_str_to_h5(unwrapH5.toStdString().c_str(), "source_1", tmp_str.c_str()) != 0) {
                metadataError = QStringLiteral("无法写入 source_1");
                return false;
            }
        }
        const QString masterPath = QString::fromUtf8(masterResolution.utf8.data(),
                                                     static_cast<int>(masterResolution.utf8.size()));

        {
            NodeUtils::Hdf5Locker locker;
            FormatConversion FC;
            if (!NodeUtils::readStringFromH5(phaseH5, "source_2", tmp_str) ||
                FC.write_str_to_h5(unwrapH5.toStdString().c_str(), "source_2", tmp_str.c_str()) != 0) {
                metadataError = QStringLiteral("无法复制 source_2");
                return false;
            }
        }
        QString sourcePathMetadataError;
        if (!NodeUtils::copySourcePathMetadata(phaseH5, unwrapH5, &sourcePathMetadataError)) {
            metadataError = sourcePathMetadataError;
            return false;
        }
        {
            NodeUtils::Hdf5Locker locker;
            const auto copyRequiredMatrix = [&](const QString& dataset) {
                QString matrixError;
                if (!NodeUtils::readMatFromH5(phaseH5, dataset, tmp, -1, &matrixError) || tmp.empty()) {
                    metadataError = QStringLiteral("无法读取必需元数据 %1: %2")
                                        .arg(dataset, matrixError.isEmpty() ? QStringLiteral("empty dataset") : matrixError);
                    return false;
                }
                if (!NodeUtils::writeMatToH5(unwrapH5, dataset, tmp)) {
                    metadataError = QStringLiteral("无法写入必需元数据 %1").arg(dataset);
                    return false;
                }
                return true;
            };
            const auto copyOptionalMatrix = [&](const QString& dataset) {
                QString matrixError;
                if (!NodeUtils::readMatFromH5(phaseH5, dataset, tmp, -1, &matrixError)) {
                    return true;
                }
                if (tmp.empty() || !NodeUtils::writeMatToH5(unwrapH5, dataset, tmp)) {
                    metadataError = QStringLiteral("无法写入可选元数据 %1").arg(dataset);
                    return false;
                }
                return true;
            };

            if (!copyRequiredMatrix(QStringLiteral("range_len")) ||
                !copyRequiredMatrix(QStringLiteral("azimuth_len")) ||
                !copyRequiredMatrix(QStringLiteral("multilook_rg")) ||
                !copyRequiredMatrix(QStringLiteral("multilook_az")) ||
                !copyOptionalMatrix(QStringLiteral("mapped_lon")) ||
                !copyOptionalMatrix(QStringLiteral("mapped_lat"))) {
                return false;
            }
        }
        if (!NodeUtils::copyPhaseProcessingMetadata(phaseH5, unwrapH5, &metadataError) ||
            !NodeUtils::copyDenoiseFilterSupportContract(phaseH5, unwrapH5, &metadataError)) {
            return false;
        }

        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            return false;
        }

        /*行列偏移量*/
        {
            NodeUtils::Hdf5Locker locker;
            if (!readOffset(masterPath, "offset_row", offset_rows[idx], metadataError) ||
                !readOffset(masterPath, "offset_col", offset_cols[idx], metadataError)) {
                return false;
            }
        }

        return true;
    };

    auto writeOutputPhase = [&](int idx, Mat& phase_unwrap,
                                const Mat* connectedComponent = nullptr) -> bool {
        if (phase_unwrap.type() != CV_32F) {
            phase_unwrap.convertTo(phase_unwrap, CV_32F);
        }
		if (connectedComponent != nullptr) {
			if (connectedComponent->type() != CV_32S || connectedComponent->size() != phase_unwrap.size()) {
				metadataError = QStringLiteral("解缠连通分量类型或网格不一致");
				return false;
			}
			for (int row = 0; row < phase_unwrap.rows; ++row) {
				const int* componentRow = connectedComponent->ptr<int>(row);
				float* phaseRow = phase_unwrap.ptr<float>(row);
				for (int column = 0; column < phase_unwrap.cols; ++column) {
					if (componentRow[column] > 0) {
						if (!std::isfinite(phaseRow[column])) {
							metadataError = QStringLiteral("解缠有效连通分量包含非有限相位");
							return false;
						}
					} else if (componentRow[column] == 0) {
						phaseRow[column] = std::numeric_limits<float>::quiet_NaN();
					} else {
						metadataError = QStringLiteral("解缠连通分量包含负编号");
						return false;
					}
				}
			}
		}

        const QString& outputPath = absolute_unwrap_path.at(idx);
        if (!NodeUtils::writeMatToH5(outputPath, "phase", phase_unwrap)) {
            return false;
        }
		if (connectedComponent != nullptr &&
			!NodeUtils::writeMatToH5(outputPath, "unwrap_connected_component", *connectedComponent)) {
			metadataError = QStringLiteral("无法写入解缠连通分量");
			return false;
		}

        if (!NodeUtils::writeScalarToH5(outputPath, "unwrap_method", method) ||
            !NodeUtils::writeScalarToH5(outputPath, "unwrap_coherence_threshold", coherence_threshold)) {
            metadataError = QStringLiteral("无法写入解缠参数元数据");
            return false;
        }

        const UnwrapAmplitudeStatus& amplitude = amplitudeStatuses.at(static_cast<size_t>(idx));
        const auto writeString = [&](const QString& dataset, const QString& value) {
            QString writeError;
            if (!NodeUtils::writeStringToH5(outputPath, dataset, value.toStdString(), &writeError)) {
                metadataError = QStringLiteral("无法写入 %1: %2").arg(dataset, writeError);
                return false;
            }
            return true;
        };
        const auto writeInt = [&](const QString& dataset, int value) {
            QString writeError;
            if (!NodeUtils::writeScalarToH5(outputPath, dataset, value, &writeError)) {
                metadataError = QStringLiteral("无法写入 %1: %2").arg(dataset, writeError);
                return false;
            }
            return true;
        };
        if (!writeString(QStringLiteral("unwrap_amplitude_status"), amplitude.status) ||
            !writeString(QStringLiteral("unwrap_amplitude_reason"), amplitude.reason) ||
            !writeInt(QStringLiteral("unwrap_amplitude_degraded"), amplitude.degraded ? 1 : 0) ||
            !writeInt(QStringLiteral("unwrap_amplitude_master_rows"), amplitude.masterRows) ||
            !writeInt(QStringLiteral("unwrap_amplitude_master_cols"), amplitude.masterCols) ||
            !writeInt(QStringLiteral("unwrap_amplitude_slave_rows"), amplitude.slaveRows) ||
            !writeInt(QStringLiteral("unwrap_amplitude_slave_cols"), amplitude.slaveCols) ||
            !writeInt(QStringLiteral("unwrap_amplitude_expected_rows"), amplitude.expectedRows) ||
            !writeInt(QStringLiteral("unwrap_amplitude_expected_cols"), amplitude.expectedCols)) {
            return false;
        }
        return true;
    };

    const auto finishMetadataFailure = [&](int idx) {
		QFile::remove(absolute_unwrap_path.at(idx));
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
            finishCancelled();
        } else {
            emit errorProcess(QStringLiteral("创建或复制解缠输出元数据失败: %1")
                                  .arg(metadataError.isEmpty() ? QStringLiteral("unknown error") : metadataError));
        }
    };

    for (int i = 0; i < image_number; ++i) {
        QString phaseValidityError;
        const bool supportsMaskedPhase = method == 1 || method == 3;
        if (!NodeUtils::validatePhaseValidityContract(phase_path.at(i), !supportsMaskedPhase, &phaseValidityError)) {
            emit errorProcess(phaseValidityError);
            return;
        }
		if (!NodeUtils::validateDenoiseFilterSupportContract(phase_path.at(i), &phaseValidityError)) {
			emit errorProcess(phaseValidityError);
			return;
		}
    }

    if (method == 1)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            t_unwrapLastReportedProgress = -1;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase, CV_64F) ? 0 : -1;
            if (ret < 0) {
                failImage(i, QStringLiteral("reading phase"), ret);
                return;
            }

			Mat validMask;
			const bool hasV2ValidMask = NodeUtils::readMatFromH5(phase_path.at(i), "phase_valid_mask",
				validMask, CV_8U);
			if (!hasV2ValidMask) {
				validMask = Mat::ones(phase.size(), CV_8U);
			}
			Mat phase_unwrap;
			Mat connectedComponent;
			ret = unwrap.SPD_Guided_Unwrap_Masked(phase, validMask, phase_unwrap,
				connectedComponent, unwrapProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) {
                failImage(i, QStringLiteral("unwrapping"), ret);
                return;
            }

			if (!copyH5Metadata(i)) {
				finishMetadataFailure(i);
                return;
            }
			const Mat* outputComponent = hasV2ValidMask ? &connectedComponent : nullptr;
			if (!writeOutputPhase(i, phase_unwrap, outputComponent)) {
                QFile::remove(absolute_unwrap_path.at(i));
                failImage(i, QStringLiteral("writing output"), -1);
                return;
            }
            process_success[i] = true;
        }
    }
    else if (method == 2)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            t_unwrapLastReportedProgress = -1;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase, CV_64F) ? 0 : -1;
            if (ret < 0) {
                failImage(i, QStringLiteral("reading phase"), ret);
                return;
            }

            McfDimensionPreflight dimensionPreflight;
            if (!preflightMcfDimensions(phase.rows, phase.cols, dimensionPreflight)) {
                const QString detail = QStringLiteral(
                    "%1：phase=%2x%3, nodes=%4, arcs=%5, estimatedWorkingSet=%6, budget=%7")
                    .arg(dimensionPreflight.failure)
                    .arg(phase.rows).arg(phase.cols)
                    .arg(dimensionPreflight.nodes).arg(dimensionPreflight.arcs)
                    .arg(formatMcfBytes(dimensionPreflight.estimatedWorkingSetBytes))
                    .arg(formatMcfBytes(kMcfWorkingSetBudgetBytes));
                InSARLogManager::LogDebug("UnwrapWorker",
                    QStringLiteral("MCF dimension preflight rejected: %1").arg(detail),
                    "unwrap.mcf.preflight");
                emit errorProcess(QStringLiteral("MCF 网络规模预检失败：%1").arg(detail));
                return;
            }
            InSARLogManager::LogDebug("UnwrapWorker",
                QStringLiteral("MCF dimension preflight passed: phase=%1x%2, nodes=%3, arcs=%4, estimatedWorkingSet=%5, budget=%6")
                    .arg(phase.rows).arg(phase.cols)
                    .arg(dimensionPreflight.nodes).arg(dimensionPreflight.arcs)
                    .arg(formatMcfBytes(dimensionPreflight.estimatedWorkingSetBytes))
                    .arg(formatMcfBytes(kMcfWorkingSetBudgetBytes)),
                "unwrap.mcf.preflight");

            Mat phase_unwrap;
            Mat coherence, residue;
            InSARLogManager::LogDebug("UnwrapWorker",
                QStringLiteral("MCF coherence calculation started after dimension preflight: phase=%1x%2")
                    .arg(phase.rows).arg(phase.cols),
                "unwrap.mcf.preflight");
            ret = util.phase_axial_concentration(phase, coherence, unwrapProgressCallback);
            if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) {
                failImage(i, QStringLiteral("calculating coherence"), ret);
                return;
            }
            ret = util.residue(phase, residue);
            if (ret < 0) {
                failImage(i, QStringLiteral("calculating residue"), ret);
                return;
            }
            qint64 normalizedResidues = 0;
            qint64 nonZeroResidues = 0;
            QString mcfInputError;
            if (!auditAndNormalizeMcfInputs(phase, coherence, residue,
                                             normalizedResidues, nonZeroResidues, mcfInputError)) {
                emit errorProcess(QStringLiteral("MCF 输入预检失败：%1").arg(mcfInputError));
                return;
            }
            InSARLogManager::LogDebug("UnwrapWorker",
                QStringLiteral("MCF input preflight passed: coherence=%1x%2, residue=%3x%4, nonZeroResidues=%5, normalizedResidues=%6, networkNodes=%7, networkArcs=%8")
                    .arg(coherence.rows).arg(coherence.cols)
                    .arg(residue.rows).arg(residue.cols)
                    .arg(nonZeroResidues).arg(normalizedResidues)
                    .arg(dimensionPreflight.nodes).arg(dimensionPreflight.arcs),
                "unwrap.mcf.preflight");
            QString app_path = QCoreApplication::applicationDirPath();
            UnwrapDiagnostic diagnostic = {};
            diagnostic.structSize = sizeof(diagnostic);
            ret = unwrap.MCFEx(phase, phase_unwrap, coherence, residue,
                               (absolute_path + "/MCF.net").toStdString().c_str(),
                               app_path.toStdString().c_str(), unwrapProgressCallback, &diagnostic);
            QFile::remove(absolute_path + "/MCF.net");
            if (ret != 0) {
                failDiagnostic(QStringLiteral("MCF"), diagnostic, ret);
                return;
            }
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }

            if (!copyH5Metadata(i)) {
				finishMetadataFailure(i);
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                failImage(i, QStringLiteral("writing output"), -1);
                return;
            }
            process_success[i] = true;
        }
    }
    else if (method == 3)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            t_unwrapLastReportedProgress = -1;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase) ? 0 : -1;
            if (ret < 0) {
                failImage(i, QStringLiteral("reading phase"), ret);
                return;
            }

            Mat validMask;
            const bool hasV2ValidMask = NodeUtils::readMatFromH5(phase_path.at(i), "phase_valid_mask",
                validMask, CV_8U);
            if (hasV2ValidMask && (validMask.size() != phase.size() || validMask.type() != CV_8UC1)) {
                emit errorProcess(QStringLiteral("SNAPHU 输入 phase_valid_mask 与相位网格不一致：%1")
                                      .arg(phase_path.at(i)));
                return;
            }
            const int validPixelCount = hasV2ValidMask ? countNonZero(validMask) : 0;
            const bool hasMaskedPixels = hasV2ValidMask &&
                static_cast<qint64>(validPixelCount) != static_cast<qint64>(validMask.rows) * validMask.cols;
            if (hasMaskedPixels) {
                InSARLogManager::LogInfo("UnwrapWorker", QStringLiteral(
                    "SNAPHU will apply the phase-validity mask: %1 invalid pixels.")
                    .arg(static_cast<qint64>(validMask.rows) * validMask.cols - validPixelCount));
            }

            Mat phase_unwrap;
            QString app_path = QCoreApplication::applicationDirPath();
            QTemporaryDir snaphuWorkDir;
            if (!snaphuWorkDir.isValid()) {
                emit errorProcess(QStringLiteral("无法创建 SNAPHU 临时工作目录"));
                return;
            }
            UnwrapDiagnostic diagnostic = {};
            diagnostic.structSize = sizeof(diagnostic);
            const SnaphuRunOptionsV1 options = makeSnaphuRunOptions(snaphuOptions);
            // 记录生效的 SNAPHU 统计代价模式，便于与外部参考（SNAP 导出默认 DEFO）对标
            {
                const QString costModeName = snaphuOptions.statisticalCostMode == 1 ? QStringLiteral("DEFO")
                    : (snaphuOptions.statisticalCostMode == 2 ? QStringLiteral("SMOOTH") : QStringLiteral("TOPO"));
                InSARLogManager::LogInfo("UnwrapWorker", QStringLiteral(
                    "SNAPHU 参数：统计代价模式=%1，分块=%2x%3，重叠=%4/%5")
                    .arg(costModeName)
                    .arg(snaphuOptions.tileRows).arg(snaphuOptions.tileCols)
                    .arg(snaphuOptions.rowOverlap).arg(snaphuOptions.colOverlap));
            }
            if (hasMaskedPixels) {
                ret = unwrap.SnaphuFileMaskedEx2(phase_path.at(i).toStdString().c_str(), validMask, phase_unwrap,
                                                  save_path.toStdString().c_str(), snaphuWorkDir.path().toStdString().c_str(),
                                                  app_path.toStdString().c_str(), &options, snaphuRunEventCallback, this,
                                                  &diagnostic);
            } else {
                ret = unwrap.SnaphuFileEx2(phase_path.at(i).toStdString().c_str(), phase_unwrap,
                                           save_path.toStdString().c_str(), snaphuWorkDir.path().toStdString().c_str(),
                                           app_path.toStdString().c_str(), &options, snaphuRunEventCallback, this,
                                           &diagnostic);
            }
            if (ret != 0 || snaphuOptions.keepArtifactsOnSuccess) {
                snaphuWorkDir.setAutoRemove(false);
                InSARLogManager::LogInfo("UnwrapWorker", QStringLiteral("SNAPHU artifacts retained at: %1")
                    .arg(snaphuWorkDir.path()));
            }
            if (ret != 0) {
                failDiagnostic(QStringLiteral("SNAPHU"), diagnostic, ret);
                return;
            }
            if (hasMaskedPixels) {
                if (phase_unwrap.size() != validMask.size()) {
                    emit errorProcess(QStringLiteral("SNAPHU 输出网格与 phase_valid_mask 不一致：%1")
                                          .arg(phase_path.at(i)));
                    return;
                }
                if (phase_unwrap.type() != CV_64F) {
                    phase_unwrap.convertTo(phase_unwrap, CV_64F);
                }
                for (int row = 0; row < phase_unwrap.rows; ++row) {
                    const uchar* maskRow = validMask.ptr<uchar>(row);
                    double* outputRow = phase_unwrap.ptr<double>(row);
                    for (int column = 0; column < phase_unwrap.cols; ++column) {
                        if (maskRow[column] == 0) {
                            outputRow[column] = std::numeric_limits<double>::quiet_NaN();
                        } else if (!std::isfinite(outputRow[column])) {
                            emit errorProcess(QStringLiteral("SNAPHU 输出的有效相位包含非有限像元：%1")
                                                  .arg(phase_path.at(i)));
                            return;
                        }
                    }
                }
            }
            const QString diagnosticSummary = diagnosticString(diagnostic.summary, sizeof(diagnostic.summary));
            if (!diagnosticSummary.isEmpty()) {
                amplitudeStatuses.at(static_cast<size_t>(i)) = parseSnaphuAmplitudeStatus(diagnosticSummary);
                const QString diagnosticMessage = QStringLiteral("SNAPHU completed (stage=%1): %2")
                    .arg(diagnosticStageName(diagnostic.stage), diagnosticSummary);
                if (diagnosticSummary.contains(QStringLiteral("corr=disabled")) ||
                    diagnosticSummary.contains(QStringLiteral("phase_derived")) ||
                    diagnosticSummary.contains(QStringLiteral("amp=omitted"))) {
                    InSARLogManager::LogWarning("UnwrapWorker", diagnosticMessage);
                } else {
                    InSARLogManager::LogInfo("UnwrapWorker", diagnosticMessage);
                }
            } else {
                amplitudeStatuses.at(static_cast<size_t>(i)).status = QStringLiteral("unknown");
                amplitudeStatuses.at(static_cast<size_t>(i)).reason = QStringLiteral("missing_amplitude_diagnostic");
                amplitudeStatuses.at(static_cast<size_t>(i)).degraded = true;
            }
            const int solverCompleteProgress = 10 + (i + 1) * 80 / image_number;
            emit updateProcess(solverCompleteProgress,
                               QStringLiteral("第%1幅图像的外部 SNAPHU 求解完成，正在验证并写入输出……")
                                   .arg(i + 1));
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }

            if (!copyH5Metadata(i)) {
				finishMetadataFailure(i);
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                failImage(i, QStringLiteral("writing output"), -1);
                return;
            }
            process_success[i] = true;
        }
    }
    else if (method == 4)
    {
        double distance_threshold = 5.0;
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            t_unwrapLastReportedProgress = -1;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase, CV_64F) ? 0 : -1;
            if (ret < 0) {
                failImage(i, QStringLiteral("reading phase"), ret);
                return;
            }

            Mat phase_unwrap;
            QString app_path = QCoreApplication::applicationDirPath();
            QTemporaryDir qualityGuidedWorkDir;
            if (!qualityGuidedWorkDir.isValid()) {
                emit errorProcess(QStringLiteral("无法创建质量引导 MCF 临时工作目录"));
                return;
            }
            UnwrapDiagnostic diagnostic = {};
            diagnostic.structSize = sizeof(diagnostic);
            ret = unwrap.QualityGuidedMCFEx(phase, phase_unwrap, coherence_threshold, distance_threshold,
                                             qualityGuidedWorkDir.path().toStdString().c_str(),
                                             app_path.toStdString().c_str(), unwrapProgressCallback, &diagnostic);
            if (ret != 0) {
                failDiagnostic(QStringLiteral("Quality Guided MCF"), diagnostic, ret);
                return;
            }
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }

            if (!copyH5Metadata(i)) {
				finishMetadataFailure(i);
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                failImage(i, QStringLiteral("writing output"), -1);
                return;
            }
            process_success[i] = true;
        }
    }

    QString methodName = "SPD_Guided";
    if (method == 2) methodName = "MCF";
    else if (method == 3) methodName = "Snaphu";
    else if (method == 4) methodName = "QualityGuided_MCF";

    for (int i = 0; i < image_number; ++i) {
        if (!process_success[i]) {
            continue;
        }
        UnwrapFileResult result;
        result.unwrapName = unwrap_name.at(i);
        result.absolutePath = absolute_unwrap_path.at(i);
        result.relativePath = relative_unwrap_path.at(i);
        result.offsetRow = offset_rows[i];
        result.offsetCol = offset_cols[i];
        result.method = methodName;
        const UnwrapAmplitudeStatus& amplitude = amplitudeStatuses.at(static_cast<size_t>(i));
        result.amplitudeStatus = amplitude.status;
        result.amplitudeReason = amplitude.reason;
        result.amplitudeMasterRows = amplitude.masterRows;
        result.amplitudeMasterCols = amplitude.masterCols;
        result.amplitudeSlaveRows = amplitude.slaveRows;
        result.amplitudeSlaveCols = amplitude.slaveCols;
        result.amplitudeExpectedRows = amplitude.expectedRows;
        result.amplitudeExpectedCols = amplitude.expectedCols;
        result.amplitudeDegraded = amplitude.degraded;
        Q_EMIT unwrapFileGenerated(result);
    }

    emit updateProcess(100, QStringLiteral("解缠输出已验证并写入。"));
    InSARLogManager::LogInfo("UnwrapWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

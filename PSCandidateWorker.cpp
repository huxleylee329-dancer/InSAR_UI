#include "PSCandidateWorker.h"
#include "FormatConversion.h"
#include "ComplexMat.h"
#include "PSI.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include <QThread>
#include <QFileInfo>
#include <QDir>

namespace {
bool __stdcall isCancellationRequested(void* context)
{
    return static_cast<std::atomic_bool*>(context)->load(std::memory_order_relaxed);
}
}

#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "PSI_d.lib")
#pragma comment(lib, "Utils_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "PSI.lib")
#pragma comment(lib, "Utils.lib")
#endif

PSCandidateWorker::PSCandidateWorker(QObject* parent)
    : BaseWorker(parent)
{
}

PSCandidateWorker::~PSCandidateWorker()
{
}

void PSCandidateWorker::StopProcess()
{
    BaseWorker::StopProcess();
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool PSCandidateWorker::cancellationRequested() const noexcept
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

void PSCandidateWorker::select_candidates(
    double da_threshold,
    int min_ps_count,
    int multilook_rg,
    int multilook_az,
    QString projectPath,
    QString projectName,
    QString dstNode,
    QStringList filePaths
)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("PSCandidateWorker", QString("select_candidates started. Output Node: %1").arg(dstNode));

    if (filePaths.isEmpty()) {
        emit errorProcess(QStringLiteral("输入影像列表为空"));
        return;
    }

    const QString outputDir = (projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(projectPath).absolutePath() : projectPath) + "/" + dstNode;
    const QString outputH5 = outputDir + "/PS_candidates.h5";
    const auto cancellationRequested = [this]() { return this->cancellationRequested(); };
    const auto finishCancelled = [this, &outputDir]() {
        QDir dir(outputDir);
        if (dir.exists() && !dir.removeRecursively()) {
            InSARLogManager::LogWarning("PSCandidateWorker", "Cancellation cleanup left output directory: " + outputDir);
        }
        emit cancelled();
    };

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    int num_images = filePaths.size();
    FormatConversion FC;
    cv::Mat sum_amplitude, sum_amplitude_sq;

    int rows = 0;
    int cols = 0;

    for (int i = 0; i < num_images; ++i) {
        if (cancellationRequested()) {
            finishCancelled();
            return;
        }

        QString filePath = filePaths.at(i);
        ComplexMat slc;
        emit updateProcess(int(double(i) / num_images * 90), QStringLiteral("正在读取并累计振幅: 景 %1/%2").arg(i + 1).arg(num_images));

        int ret = FC.read_slc_from_h5(filePath.toStdString().c_str(), slc);
        if (ret != 0) {
            emit errorProcess(QStringLiteral("读取 SLC H5 文件失败: ") + filePath);
            return;
        }

        if (i == 0) {
            rows = slc.GetRows();
            cols = slc.GetCols();
            sum_amplitude = cv::Mat::zeros(rows, cols, CV_32FC1);
            sum_amplitude_sq = cv::Mat::zeros(rows, cols, CV_32FC1);
        }

        cv::Mat amp;
        cv::magnitude(slc.re, slc.im, amp);
        if (amp.type() != CV_32FC1) {
            amp.convertTo(amp, CV_32FC1);
        }

        sum_amplitude += amp;
        sum_amplitude_sq += amp.mul(amp);

        InSARLogManager::LogInfo("PSCandidateWorker", QString("Loaded image %1/%2: %3").arg(i + 1).arg(num_images).arg(filePath));
    }

    emit updateProcess(90, QStringLiteral("正在调用 DLL 计算 PS 候选点..."));

    PSI psi;
    cv::Mat ps_mask, amplitude_dispersion;
    int ret = psi.compute_ps_candidates(
        sum_amplitude, sum_amplitude_sq, num_images, da_threshold, ps_mask, amplitude_dispersion,
        &isCancellationRequested, &m_cancelRequested, nullptr, nullptr);
    if (ret == -2 || cancellationRequested()) {
        finishCancelled();
        return;
    }
    if (ret != 0) {
        emit errorProcess(QStringLiteral("DLL 计算 PS 候选点失败"));
        return;
    }

    // 统计 PS 候选点数量
    int ps_count = cv::countNonZero(ps_mask);
    InSARLogManager::LogInfo("PSCandidateWorker", QString("Found %1 PS candidates.").arg(ps_count));

    if (ps_count < min_ps_count) {
        emit errorProcess(QStringLiteral("PS 候选点数量 (%1) 少于设定的最小数量限制 (%2)").arg(ps_count).arg(min_ps_count));
        return;
    }

    // 保存输出到 H5 文件
    QDir().mkpath(outputDir);

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    ret = FC.write_array_to_h5(outputH5.toStdString().c_str(), "amplitude_dispersion", amplitude_dispersion);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "ps_mask", ps_mask);
    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "ps_count", ps_count);
    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "multilook_rg", multilook_rg);
    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "multilook_az", multilook_az);

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    if (ret != 0) {
        emit errorProcess(QStringLiteral("写入 PS_candidates.h5 失败"));
        return;
    }

    emit updateProcess(100, QStringLiteral("计算完成"));
    emit endProcess();
}

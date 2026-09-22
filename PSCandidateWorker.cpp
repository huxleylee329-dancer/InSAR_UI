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
    QStringList filePaths,
    bool outputDirectoryIsStaging
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
    const auto finishCancelled = [this]() {
        emit cancelled();
    };

    if (outputDirectoryIsStaging && !QDir(outputDir).exists()) {
        emit errorProcess(QStringLiteral("staging输出目录不存在: ") + outputDir);
        return;
    }
    if (!outputDirectoryIsStaging && !QDir().mkpath(outputDir)) {
        emit errorProcess(QStringLiteral("无法创建输出目录: ") + outputDir);
        return;
    }

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    int num_images = filePaths.size();
    FormatConversion FC;
    cv::Mat sum_amplitude, sum_amplitude_sq;

    int rows = 0;
    int cols = 0;

    // 预检：跨景尺寸一致性。sum_amplitude/sum_amplitude_sq 按第 1 景的尺寸分配，之后每景
    // 都用 `sum_amplitude += amp` 累加 —— 尺寸不同时 OpenCV 会抛 cv::Exception，而本函数体
    // 没有 try/catch，异常会穿过 QThread::started 上的 lambda 逸出线程事件循环，
    // 用户看到的是崩溃而不是一条能读的错误。尺寸只需各景的 header，读一次即可判定。
    // 探测不到尺寸就整体跳过（不因元数据口径差异误拒），此时交由循环内的兜底判断拦住。
    for (int i = 0; i < num_images; ++i) {
        int probeRows = 0, probeCols = 0;
        QString probeError;
        if (!NodeUtils::probeH5DatasetMetadata(filePaths.at(i), QStringLiteral("s_re"),
                                               &probeRows, &probeCols, &probeError)) {
            break;
        }
        if (i == 0) {
            rows = probeRows;
            cols = probeCols;
        } else if (probeRows != rows || probeCols != cols) {
            emit errorProcess(QStringLiteral(
                "第 %1 景的 SLC 尺寸 (%2 x %3) 与第 1 景 (%4 x %5) 不一致，无法累计振幅。"
                "请确认所有输入属于同一景、同一子带与同一多视设置。")
                .arg(i + 1).arg(probeRows).arg(probeCols).arg(rows).arg(cols));
            return;
        }
    }

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
        } else if (slc.GetRows() != rows || slc.GetCols() != cols) {
            // 兜底：预检可能因元数据口径被跳过，这里用真实矩阵尺寸再挡一次，
            // 把 cv::Exception 换成可读错误（越界/尺寸不符不进入 += 的异常路径）。
            emit errorProcess(QStringLiteral(
                "第 %1 景的 SLC 尺寸 (%2 x %3) 与第 1 景 (%4 x %5) 不一致，无法累计振幅。")
                .arg(i + 1).arg(slc.GetRows()).arg(slc.GetCols()).arg(rows).arg(cols));
            return;
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
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    if (FC.write_array_to_h5(outputH5.toStdString().c_str(), "amplitude_dispersion", amplitude_dispersion) != 0 ||
        FC.write_array_to_h5(outputH5.toStdString().c_str(), "ps_mask", ps_mask) != 0 ||
        FC.write_int_to_h5(outputH5.toStdString().c_str(), "ps_count", ps_count) != 0 ||
        FC.write_int_to_h5(outputH5.toStdString().c_str(), "multilook_rg", multilook_rg) != 0 ||
        FC.write_int_to_h5(outputH5.toStdString().c_str(), "multilook_az", multilook_az) != 0) {
        emit errorProcess(QStringLiteral("写入 PS_candidates.h5 失败"));
        return;
    }

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(100, QStringLiteral("计算完成"));
    emit outputsGenerated(QStringList() << outputH5);
    emit endProcess();
}

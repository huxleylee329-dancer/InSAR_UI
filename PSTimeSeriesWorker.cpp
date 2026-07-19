#include "PSTimeSeriesWorker.h"
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
#include <cmath>
#include <algorithm>

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

PSTimeSeriesWorker::PSTimeSeriesWorker(QObject* parent)
    : BaseWorker(parent)
{
}

PSTimeSeriesWorker::~PSTimeSeriesWorker()
{
}

void PSTimeSeriesWorker::StopProcess()
{
    BaseWorker::StopProcess();
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool PSTimeSeriesWorker::cancellationRequested() const noexcept
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

void PSTimeSeriesWorker::ps_time_series(
    double coherence_threshold,
    double max_deformation_rate,
    int atmospheric_window,
    QString projectPath,
    QString projectName,
    QString dstNode,
    QStringList filePaths
)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("PSTimeSeriesWorker", QString("ps_time_series started. Output Node: %1").arg(dstNode));

    if (filePaths.isEmpty()) {
        emit errorProcess(QStringLiteral("输入文件路径为空"));
        return;
    }

    const QString outputDir = (projectPath.endsWith(".insar", Qt::CaseInsensitive)
        ? QFileInfo(projectPath).absolutePath() : projectPath) + "/" + dstNode;
    const QString outputH5 = outputDir + "/PS_time_series.h5";
    const auto cancellationRequested = [this]() { return this->cancellationRequested(); };
    const auto finishCancelled = [this, &outputDir]() {
        QDir dir(outputDir);
        if (dir.exists() && !dir.removeRecursively()) {
            InSARLogManager::LogWarning("PSTimeSeriesWorker", "Cancellation cleanup left output directory: " + outputDir);
        }
        emit cancelled();
    };

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    QString networkH5 = filePaths.first();
    FormatConversion FC;

    emit updateProcess(10, QStringLiteral("正在读取 PS 网络与雷达元数据..."));

    // 1. 读取维度和标量参数
    int ps_count = 0, edge_count = 0, ref_index = 0;
    int rows = 0, cols = 0;
    double carrier_frequency = 0.0, inc_center = 0.0, slant_range_first_pixel = 0.0, range_spacing = 0.0;
    int offset_col = 0;

    int ret = 0;
    if (!NodeUtils::readScalarFromH5(networkH5, "ps_count", ps_count) ||
        !NodeUtils::readScalarFromH5(networkH5, "edge_count", edge_count) ||
        !NodeUtils::readScalarFromH5(networkH5, "ref_index", ref_index) ||
        !NodeUtils::readScalarFromH5(networkH5, "rows", rows) ||
        !NodeUtils::readScalarFromH5(networkH5, "cols", cols) ||
        !NodeUtils::readScalarFromH5(networkH5, "offset_col", offset_col) ||
        !NodeUtils::readScalarFromH5(networkH5, "carrier_frequency", carrier_frequency) ||
        !NodeUtils::readScalarFromH5(networkH5, "inc_center", inc_center) ||
        !NodeUtils::readScalarFromH5(networkH5, "slant_range_first_pixel", slant_range_first_pixel) ||
        !NodeUtils::readScalarFromH5(networkH5, "range_spacing", range_spacing))
    {
        ret = -1;
    }

    if (ret != 0 || ps_count <= 0 || edge_count <= 0) {
        emit errorProcess(QStringLiteral("读取 PS 网络元数据失败"));
        return;
    }
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    // 2. 读取数组和矩阵
    cv::Mat ps_coords, edge_nodes, edge_phase_diff, temporal_baseline, spatial_baseline, formation_matrix;
    ret = (NodeUtils::readMatFromH5(networkH5, "ps_coordinates", ps_coords) &&
           NodeUtils::readMatFromH5(networkH5, "edges", edge_nodes) &&
           NodeUtils::readMatFromH5(networkH5, "edge_phase_diff", edge_phase_diff) &&
           NodeUtils::readMatFromH5(networkH5, "temporal_baseline", temporal_baseline) &&
           NodeUtils::readMatFromH5(networkH5, "spatial_baseline", spatial_baseline) &&
           NodeUtils::readMatFromH5(networkH5, "formation_matrix", formation_matrix)) ? 0 : -1;

    if (ret != 0 || ps_coords.empty() || edge_nodes.empty() || edge_phase_diff.empty()) {
        emit errorProcess(QStringLiteral("读取 PS 网络大矩阵数据失败"));
        return;
    }
    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(30, QStringLiteral("正在重建网格拓扑..."));

    // 3. 重建点集和边集对象
    std::vector<PS_Point> ps_points(ps_count);
    for (int i = 0; i < ps_count; ++i) {
        if ((i & 1023) == 0 && cancellationRequested()) {
            finishCancelled();
            return;
        }
        ps_points[i].row = ps_coords.at<int>(i, 0);
        ps_points[i].col = ps_coords.at<int>(i, 1);
        ps_points[i].index = i;
    }

    std::vector<PS_Edge> edges(edge_count);
    for (int e = 0; e < edge_count; ++e) {
        if ((e & 1023) == 0 && cancellationRequested()) {
            finishCancelled();
            return;
        }
        edges[e].num = e;
        edges[e].end1 = edge_nodes.at<int>(e, 0);
        edges[e].end2 = edge_nodes.at<int>(e, 1);
        
        // 建立双向点-边关联
        ps_points[edges[e].end1].neigh_edges.push_back(e);
        ps_points[edges[e].end2].neigh_edges.push_back(e);
    }

    emit updateProcess(40, QStringLiteral("正在进行时序反演计算..."));

    // 4. 计算雷达标量参数并调用 DLL 反演
    const double VEL_C = 299792458.0;
    const double PI = 3.14159265358979323846;
    double wavelength = VEL_C / carrier_frequency;
    double theta = inc_center / 180.0 * PI;
    double slant_range = slant_range_first_pixel + double(offset_col) * range_spacing;

    int num_images = temporal_baseline.rows;
    cv::Mat deformation_time_series, deformation_velocity, temporal_coherence, topographic_residual;

    PSI psi;
    ret = psi.ps_time_series_inversion(
        edges, ps_points, edge_phase_diff, formation_matrix,
        temporal_baseline, spatial_baseline, num_images,
        ref_index, wavelength, slant_range, theta,
        deformation_time_series, deformation_velocity, temporal_coherence, topographic_residual,
        &isCancellationRequested, &m_cancelRequested, nullptr, nullptr
    );

    if (ret == -2 || cancellationRequested()) {
        finishCancelled();
        return;
    }

    if (ret != 0) {
        emit errorProcess(QStringLiteral("时序反演计算失败"));
        return;
    }

    emit updateProcess(80, QStringLiteral("正在根据相干性筛选 PS 点..."));

    // 5. 调用 DLL 进行相干性过滤，输出稀疏的成果矩阵
    cv::Mat final_ps_mask, mask_count_map;
    cv::Mat filtered_velocity, filtered_coherence, filtered_topographic_residual, filtered_time_series;

    ret = psi.filter_ps_results(
        ps_points, temporal_coherence, deformation_velocity, deformation_time_series, topographic_residual,
        coherence_threshold, rows, cols,
        final_ps_mask, mask_count_map,
        filtered_velocity, filtered_coherence,
        filtered_topographic_residual, filtered_time_series,
        &isCancellationRequested, &m_cancelRequested, nullptr, nullptr
    );

    if (ret == -2 || cancellationRequested()) {
        finishCancelled();
        return;
    }

    if (ret != 0) {
        emit errorProcess(QStringLiteral("过滤 PS 结果数据失败"));
        return;
    }

    int filtered_ps_count = filtered_velocity.rows;
    InSARLogManager::LogInfo("PSTimeSeriesWorker", QString("Filtered PS count: %1 (threshold: %2)").arg(filtered_ps_count).arg(coherence_threshold));

    if (filtered_ps_count == 0) {
        emit errorProcess(QStringLiteral("未找到符合相干性阈值的 PS 点，请尝试降低阈值"));
        return;
    }

    // 计算极值
    double min_def = 0.0, max_def = 0.0;
    cv::minMaxLoc(filtered_time_series, &min_def, &max_def);

    emit updateProcess(90, QStringLiteral("正在写入 PS_time_series.h5 成果..."));

    // 6. 保存过滤后时序结果到 H5
    QDir().mkpath(outputDir);

    if (!temporal_coherence.empty() && temporal_coherence.type() != CV_64F) {
        temporal_coherence.convertTo(temporal_coherence, CV_64F);
    }

    // 准备过滤后的 PS 坐标矩阵
    cv::Mat filtered_coords(filtered_ps_count, 2, CV_32SC1);
    int new_idx = 0;
    for (int i = 0; i < ps_count; ++i) {
        if (temporal_coherence.at<double>(i) >= coherence_threshold) {
            filtered_coords.at<int>(new_idx, 0) = ps_points[i].row;
            filtered_coords.at<int>(new_idx, 1) = ps_points[i].col;
            new_idx++;
        }
    }

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }
    ret = FC.write_array_to_h5(outputH5.toStdString().c_str(), "ps_coordinates", filtered_coords);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "deformation_velocity", filtered_velocity);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "temporal_coherence", filtered_coherence);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "topographic_residual", filtered_topographic_residual);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "deformation_time_series", filtered_time_series);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "mask", final_ps_mask);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "mask_count_map", mask_count_map);
    ret += FC.write_array_to_h5(outputH5.toStdString().c_str(), "temporal_baseline", temporal_baseline);

    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "ps_count", filtered_ps_count);
    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "num_images", num_images);
    int ref_row_val = ps_points[ref_index].row;
    int ref_col_val = ps_points[ref_index].col;
    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "ref_row", ref_row_val);
    ret += FC.write_int_to_h5(outputH5.toStdString().c_str(), "ref_col", ref_col_val);

    ret += FC.write_double_to_h5(outputH5.toStdString().c_str(), "max_deformation", max_def);
    ret += FC.write_double_to_h5(outputH5.toStdString().c_str(), "min_deformation", min_def);

    if (cancellationRequested()) {
        finishCancelled();
        return;
    }

    if (ret != 0) {
        emit errorProcess(QStringLiteral("写入 PS_time_series.h5 失败"));
        return;
    }

    emit updateProcess(100, QStringLiteral("时序反演完成"));
    emit endProcess();
}

// GCPManagerWorker.cpp
#include "GCPManagerWorker.h"
#include "FormatConversion.h"
#include "NodeUtils.h"
#include "GCPManager.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <cmath>
#include <algorithm>

GCPManagerWorker::GCPManagerWorker(QObject* parent)
    : BaseWorker(parent)
{
}

GCPManagerWorker::~GCPManagerWorker()
{
}

void GCPManagerWorker::evaluate_gcps(
    const QString& projectPath,
    const QString& projectName,
    const QString& inputH5Path,
    const QString& outputH5Path,
    const std::vector<GCPPoint>& gcps,
    double thresholdSigma,
    int minQuality)
{
    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(10, QStringLiteral("开始读取影像多项式系数与分辨率参数..."));

    // 1. 获取 HDF5 文件锁保护，防止多线程死锁与重入冲突
    NodeUtils::Hdf5Locker locker;

    std::string inputH5 = inputH5Path.toStdString();
    std::string outputH5 = outputH5Path.toStdString();

    int sceneWidth = 0, sceneHeight = 0;
    int offsetRow = 0, offsetCol = 0;
    double rangeSpacing = 0.0, azimuthSpacing = 0.0;
    cv::Mat rowCoef, colCoef;

    // 从输入 H5 中加载必须的参数
    {
        NodeUtils::Hdf5Locker locker;
        if (!NodeUtils::readScalarFromH5(inputH5Path, "range_len", sceneWidth) ||
            !NodeUtils::readScalarFromH5(inputH5Path, "azimuth_len", sceneHeight)) {
            emit errorProcess(QStringLiteral("读取影像宽高数据失败。"));
            return;
        }

        NodeUtils::readScalarFromH5(inputH5Path, "offset_row", offsetRow);
        NodeUtils::readScalarFromH5(inputH5Path, "offset_col", offsetCol);
        NodeUtils::readMatFromH5(inputH5Path, "row_coefficient", rowCoef, CV_64F);
        NodeUtils::readMatFromH5(inputH5Path, "col_coefficient", colCoef, CV_64F);

        if (rowCoef.empty() || colCoef.empty()) {
            emit errorProcess(QStringLiteral("影像逆向行列映射多项式系数 row_coefficient 或 col_coefficient 为空。"));
            return;
        }

        if (!NodeUtils::readScalarFromH5(inputH5Path, "range_spacing", rangeSpacing)) {
            emit errorProcess(QStringLiteral("读取影像距离向采样间隔 range_spacing 失败。"));
            return;
        }

        // 尝试读取方位向分辨率，若读取失败，则设为合理的 Sentinel-1 Fallback 默认值
        if (!NodeUtils::readScalarFromH5(inputH5Path, "azimuth_spacing", azimuthSpacing)) {
            azimuthSpacing = rangeSpacing * 2.0; 
        }
    }

    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(40, QStringLiteral("参数加载完成，执行控制点清洗与残差解算..."));

    // 筛选出可以参与评估的已标注点 ( row 和 col 不是 NaN )
    std::vector<GCPPoint> activeGcps;
    for (const auto& gcp : gcps) {
        if (gcp.isAnnotated() && gcp.quality >= minQuality) {
            activeGcps.push_back(gcp);
        }
    }

    if (activeGcps.empty()) {
        emit errorProcess(QStringLiteral("没有足够已标注的有效控制点参与精度评估。"));
        return;
    }

    GCPManager manager;
    
    // A. 评估顺序优化：先标记并清洗离群点，将粗差标记为 quality = 0 (离群)
    manager.detect_outliers(activeGcps, thresholdSigma);

    // B. 对未被剔除的有效控制点进行配准精度残差计算
    GCPEvaluationResult evalResult;
    manager.evaluate_coregistration_accuracy(
        activeGcps,
        rowCoef, colCoef,
        sceneHeight, sceneWidth,
        offsetRow, offsetCol,
        rangeSpacing, azimuthSpacing,
        evalResult
    );

    // C. 生成格式化的分析报告文本
    std::string reportText;
    manager.generate_evaluation_report(evalResult, activeGcps, reportText);

    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(70, QStringLiteral("计算完成，开始写入临时输出 H5 结果数据集..."));

    // 确保输出 H5 文件夹存在
    QFileInfo outFi(outputH5Path);
    QDir().mkpath(outFi.absolutePath());

    // 创建新的结果 H5 快照
    FormatConversion conversion;
    if (conversion.creat_new_h5(outputH5.c_str()) != 0) {
        emit errorProcess(QStringLiteral("无法创建输出的 GCPResults.h5 临时快照文件。"));
        return;
    }

    int nPoints = static_cast<int>(activeGcps.size());
    cv::Mat lonMat(nPoints, 1, CV_64F);
    cv::Mat latMat(nPoints, 1, CV_64F);
    cv::Mat hgtMat(nPoints, 1, CV_64F);
    cv::Mat rowMat(nPoints, 1, CV_64F);
    cv::Mat colMat(nPoints, 1, CV_64F);
    cv::Mat resRMat(nPoints, 1, CV_64F);
    cv::Mat resAMat(nPoints, 1, CV_64F);
    cv::Mat resHMat(nPoints, 1, CV_64F);
    cv::Mat qualMat(nPoints, 1, CV_32S);

    for (int i = 0; i < nPoints; ++i) {
        const auto& p = activeGcps[i];
        lonMat.at<double>(i, 0) = p.lon;
        latMat.at<double>(i, 0) = p.lat;
        hgtMat.at<double>(i, 0) = p.height;
        rowMat.at<double>(i, 0) = p.row;
        colMat.at<double>(i, 0) = p.col;
        resRMat.at<double>(i, 0) = std::isnan(p.residual_range) ? -9999.0 : p.residual_range;
        resAMat.at<double>(i, 0) = std::isnan(p.residual_azimuth) ? -9999.0 : p.residual_azimuth;
        resHMat.at<double>(i, 0) = std::isnan(p.residual_height) ? -9999.0 : p.residual_height;
        qualMat.at<int>(i, 0) = p.quality;
    }

    // 写入矢量数组数据集
    {
        NodeUtils::Hdf5Locker locker;
        NodeUtils::writeMatToH5(outputH5Path, "gcp_lon", lonMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_lat", latMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_height", hgtMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_row", rowMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_col", colMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_residual_range", resRMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_residual_azimuth", resAMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_residual_height", resHMat);
        NodeUtils::writeMatToH5(outputH5Path, "gcp_quality", qualMat);

        NodeUtils::writeScalarToH5(outputH5Path, "num_gcp_total", static_cast<int>(gcps.size()));
        NodeUtils::writeScalarToH5(outputH5Path, "num_gcp_used", evalResult.num_gcp_used);
        NodeUtils::writeScalarToH5(outputH5Path, "num_gcp_rejected", evalResult.num_gcp_rejected);
        NodeUtils::writeScalarToH5(outputH5Path, "mean_residual_range", evalResult.mean_residual_range);
        NodeUtils::writeScalarToH5(outputH5Path, "mean_residual_azimuth", evalResult.mean_residual_azimuth);
        NodeUtils::writeScalarToH5(outputH5Path, "rms_residual_2d", evalResult.rms_residual_2d);
        NodeUtils::writeScalarToH5(outputH5Path, "rms_residual_3d", evalResult.rms_residual_3d);
    }

    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    emit updateProcess(100, QStringLiteral("GCP 精度评估和输出已成功完成！"));

    // 将最新的已计算点位传回主线程 (更新 SQLite 数据库)
    emit evaluationFinished(activeGcps, evalResult, QString::fromStdString(reportText));
    emit endProcess();
}

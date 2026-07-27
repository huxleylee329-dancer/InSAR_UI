#include "PhaseElevationRegressionWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <Utils.h>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <cmath>
#include <AtmosphericCorrection.h>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif

using namespace cv;
using namespace std;

static thread_local PhaseElevationRegressionWorker* currentWorker = nullptr;

static bool __stdcall regressionProgressCallback(int progress, const char* message)
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

    if (currentWorker) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }
        emit currentWorker->updateProcess(progress, QString::fromLocal8Bit(message));
    }
    return true;
}

PhaseElevationRegressionWorker::PhaseElevationRegressionWorker(QObject* parent)
    : BaseWorker(parent)
{
}

PhaseElevationRegressionWorker::~PhaseElevationRegressionWorker()
{
}

void PhaseElevationRegressionWorker::doRegression(
    int polyOrder, int windowSize, double coherenceThresh,
    QString save_path, QString project_name,
    QString node_name, QString file_name,
    QStringList phase_names, QStringList phase_paths)
{
    Q_UNUSED(project_name);
    Q_UNUSED(node_name);
    InSARLogManager::LogInfo("PhaseElevationRegressionWorker",
        QString("回归校正开始. 输出: %1, 阶数: %2, 窗口: %3, 相干阈值: %4")
        .arg(file_name).arg(polyOrder).arg(windowSize).arg(coherenceThresh));

    if (save_path.isEmpty() || project_name.isEmpty() ||
        node_name.isEmpty() || file_name.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    // 创建输出目录
    const QString outputDirectoryPath = save_path + "/" + file_name;
    QDir outputDirectory(outputDirectoryPath);
    if (outputDirectory.exists() && !outputDirectory.removeRecursively()) {
        emit errorProcess(QStringLiteral("无法清理已有输出目录: ") + outputDirectoryPath);
        return;
    }
    if (!QDir().mkpath(outputDirectoryPath)) {
        emit errorProcess(QStringLiteral("无法创建输出目录: ") + outputDirectoryPath);
        return;
    }

    // 从项目树中获取输入相位文件列表
    QList<QString> output_names;
    QList<QString> absolute_output_paths;

    emit updateProcess(5, QStringLiteral("准备数据……"));

    if (phase_names.size() != phase_paths.size()) {
        emit errorProcess(QStringLiteral("输入干涉图快照无效"));
        return;
    }
    for (int i = 0; i < phase_paths.size(); ++i) {
        const QString outputName = phase_names[i] + "_atmos";
        output_names.append(outputName);
        absolute_output_paths.append(outputDirectoryPath + "/" + outputName + ".h5");
    }

    int image_count = phase_paths.size();
    if (image_count == 0) {
        emit errorProcess(QStringLiteral("没有可校正的干涉图"));
        return;
    }

    FormatConversion FC;
    Utils util;
    int ret = 0;

    std::vector<int> offset_rows(image_count, 0);
    std::vector<int> offset_cols(image_count, 0);
    std::vector<bool> process_ok(image_count, false);

    currentWorker = this;

    // 处理每幅干涉图
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) {
            emit cancelled();
            return;
        }

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("校正第%1/%2幅干涉图……").arg(idx + 1).arg(image_count));

        // 读取相位
        Mat phase;
        Mat coherence;
        Mat dem;
        Mat lat_mat, lon_mat;
        bool has_coherence = false;
        bool has_dem = false;
        bool has_latlon = false;
        {
            NodeUtils::Hdf5Locker locker;
            QString phaseH5 = phase_paths[idx];
            
            ret = NodeUtils::readMatFromH5(phaseH5, "phase", phase, CV_32F) ? 0 : -1;
            if (ret < 0) { continue; }

            int rows = phase.rows;
            int cols = phase.cols;

            // 读取相干性
            if (NodeUtils::readMatFromH5(phaseH5, "coherence", coherence, CV_32F) && 
                coherence.rows == rows && coherence.cols == cols) {
                has_coherence = true;
            }

            // 读取高程数据（mapped_dem 或从 lat/lon 推断）
            if (NodeUtils::readMatFromH5(phaseH5, "mapped_dem", dem, CV_32F) && 
                dem.rows == rows && dem.cols == cols) {
                has_dem = true;
            }

            // 读取地理坐标
            if (NodeUtils::readMatFromH5(phaseH5, "mapped_lat", lat_mat, CV_32F) &&
                NodeUtils::readMatFromH5(phaseH5, "mapped_lon", lon_mat, CV_32F) &&
                lat_mat.rows == rows && lat_mat.cols == cols) {
                has_latlon = true;
            }
        }
        int rows = phase.rows;
        int cols = phase.cols;

        // 预分配输出矩阵 (双重保险)
        Mat corrected_phase = phase.clone();

        // 封装回归参数
        RegressionParams params;
        params.polyOrder = polyOrder;
        params.windowSize = windowSize;
        params.coherenceThresh = coherenceThresh;

        char errBuf[512] = {0};
        // 调用独立算法 DLL 进行相位-高程多项式回归与去轨道趋势拟合
        bool ok = computePhaseElevationRegression(
            phase, coherence, has_coherence,
            dem, has_dem,
            lat_mat, lon_mat, has_latlon,
            params,
            corrected_phase,
            errBuf, 512,
            regressionProgressCallback
        );

        if (!ok) {
            InSARLogManager::LogError("PhaseElevationRegressionWorker", 
                QString("回归算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            continue;
        }

        // 写入校正后的 H5 文件
        {
            NodeUtils::Hdf5Locker locker;
            ret = FC.creat_new_h5(absolute_output_paths[idx].toStdString().c_str());
        }
        if (ret < 0) continue;

        // 复制元数据
        {
            NodeUtils::Hdf5Locker locker;
            QString phaseH5 = phase_paths[idx];
            QString outH5 = absolute_output_paths[idx];
            string tmp_str;
            Mat tmp;
            
            NodeUtils::readStringFromH5(phaseH5, "source_1", tmp_str);
            FC.write_str_to_h5(outH5.toStdString().c_str(), "source_1", tmp_str.c_str());
            NodeUtils::readStringFromH5(phaseH5, "source_2", tmp_str);
            FC.write_str_to_h5(outH5.toStdString().c_str(), "source_2", tmp_str.c_str());

            NodeUtils::readMatFromH5(phaseH5, "flat_phase_coefficient", tmp);
            NodeUtils::writeMatToH5(outH5, "flat_phase_coefficient", tmp);
            NodeUtils::readMatFromH5(phaseH5, "range_len", tmp);
            NodeUtils::writeMatToH5(outH5, "range_len", tmp);
            NodeUtils::readMatFromH5(phaseH5, "azimuth_len", tmp);
            NodeUtils::writeMatToH5(outH5, "azimuth_len", tmp);
            NodeUtils::readMatFromH5(phaseH5, "multilook_rg", tmp);
            NodeUtils::writeMatToH5(outH5, "multilook_rg", tmp);
            NodeUtils::readMatFromH5(phaseH5, "multilook_az", tmp);
            NodeUtils::writeMatToH5(outH5, "multilook_az", tmp);

            if (NodeUtils::readMatFromH5(phaseH5, "mapped_lon", tmp))
                NodeUtils::writeMatToH5(outH5, "mapped_lon", tmp);
            if (NodeUtils::readMatFromH5(phaseH5, "mapped_lat", tmp))
                NodeUtils::writeMatToH5(outH5, "mapped_lat", tmp);
            if (has_coherence) {
                NodeUtils::writeMatToH5(outH5, "coherence", coherence);
            }

            // 写入校正后的相位
            NodeUtils::writeMatToH5(outH5, "phase", corrected_phase);

            // 读取偏移量
            Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
            NodeUtils::readMatFromH5(phaseH5, "offset_row", tmp_int);
            offset_rows[idx] = tmp_int.at<int>(0, 0);
            NodeUtils::readMatFromH5(phaseH5, "offset_col", tmp_int);
            offset_cols[idx] = tmp_int.at<int>(0, 0);
        }

        process_ok[idx] = true;
    }

    QStringList generatedNames;
    QStringList generatedPaths;
    QList<int> generatedOffsetRows;
    QList<int> generatedOffsetCols;
    for (int i = 0; i < image_count; ++i) {
        if (process_ok[i]) {
            generatedNames.append(output_names[i]);
            generatedPaths.append(absolute_output_paths[i]);
            generatedOffsetRows.append(offset_rows[i]);
            generatedOffsetCols.append(offset_cols[i]);
        }
    }
    if (generatedPaths.isEmpty()) {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("回归校正未生成任何输出文件"));
        return;
    }

    currentWorker = nullptr;
    InSARLogManager::LogInfo("PhaseElevationRegressionWorker", "回归校正完成");
    emit outputsGenerated(generatedNames, generatedPaths, generatedOffsetRows, generatedOffsetCols);
    emit endProcess();
}

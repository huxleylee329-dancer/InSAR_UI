#include "OrbitRefinementWorker.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <FormatConversion.h>
#include <OrbitRefinement.h>
#include <GCPDatabase.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <vector>
#include <string>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "GCPManager_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "GCPManager.lib")
#endif

OrbitRefinementWorker::OrbitRefinementWorker(QObject* parent)
    : BaseWorker(parent)
{
}

OrbitRefinementWorker::~OrbitRefinementWorker()
{
}

void OrbitRefinementWorker::refine_orbit(
    QString projectPath,
    QString projectName,
    QString dstNode,
    QStringList filePaths,
    QString dbPath,
    int masterIndex,
    int polyDegree
)
{
    // 1. 获取 HDF5 文件读写锁，保证多线程安全
    NodeUtils::Hdf5Locker locker;

    // 2. 任务启动日志埋点
    InSARLogManager::LogInfo("OrbitRefinementWorker", QString("开始轨道精炼任务。工程文件：%1，工程名：%2，目标节点：%3，参考影像索引：%4，多项式阶数：%5")
        .arg(projectPath).arg(projectName).arg(dstNode).arg(masterIndex).arg(polyDegree));

    if (filePaths.isEmpty() || masterIndex < 1 || masterIndex > filePaths.size() || dstNode.isEmpty()) {
        emit errorProcess(QStringLiteral("无效的输入参数！"));
        return;
    }

    // 3. 路径安全解析：提取工程所在的物理根目录
    QString projectDir = QFileInfo(projectPath).absolutePath();
    QDir dir(projectDir);
    if (!dir.exists(dstNode)) {
        if (!dir.mkdir(dstNode)) {
            InSARLogManager::LogError("OrbitRefinementWorker", QString("创建输出目录失败：%1/%2").arg(projectDir).arg(dstNode));
            emit errorProcess(QStringLiteral("无法创建输出目录！"));
            return;
        }
    }

    // 4. 加载 GCP 数据库并筛选已标注点
    GCPDatabase db;
    if (!db.open(dbPath)) {
        InSARLogManager::LogError("OrbitRefinementWorker", QString("无法打开 GCP 数据库：%1").arg(dbPath));
        emit errorProcess(QStringLiteral("打不开项目控制点数据库！"));
        return;
    }

    std::vector<GCPPoint> gcps = db.getAnnotatedGCPs();
    int totalGcps = gcps.size();
    InSARLogManager::LogInfo("OrbitRefinementWorker", QString("从数据库读取到已标注控制点数量：%1").arg(totalGcps));

    if (totalGcps < 1) {
        InSARLogManager::LogError("OrbitRefinementWorker", QString("没有已标注的有效控制点，中止计算。"));
        emit errorProcess(QStringLiteral("未找到已标注的控制点，请先在界面上进行控制点标注！"));
        return;
    }

    emit updateProcess(10, QStringLiteral("开始读取主影像 H5 参数……"));

    FormatConversion conversion;
    OrbitRefinement refinement;

    // 获取主影像 H5 文件路径
    QString masterH5Path = filePaths.at(masterIndex - 1);
    std::string masterPathStd = masterH5Path.toStdString();

    cv::Mat state_vec;
    cv::Mat lon_coefficient;
    cv::Mat lat_coefficient;
    double prf = 0.0;
    double range_spacing = 0.0;
    double carrier_frequency = 0.0;
    double slant_range_first_pixel = 0.0;
    int offset_row = 0;
    int offset_col = 0;
    int sceneHeight = 0;
    int sceneWidth = 0;
    std::string start_time_str;

    // 从主影像读取几何及轨道参数
    try {
        if (conversion.read_array_from_h5(masterPathStd.c_str(), "state_vec", state_vec) != 0 ||
            conversion.read_array_from_h5(masterPathStd.c_str(), "lon_coefficient", lon_coefficient) != 0 ||
            conversion.read_array_from_h5(masterPathStd.c_str(), "lat_coefficient", lat_coefficient) != 0 ||
            conversion.read_double_from_h5(masterPathStd.c_str(), "prf", &prf) != 0 ||
            conversion.read_double_from_h5(masterPathStd.c_str(), "range_spacing", &range_spacing) != 0 ||
            conversion.read_double_from_h5(masterPathStd.c_str(), "carrier_frequency", &carrier_frequency) != 0 ||
            conversion.read_double_from_h5(masterPathStd.c_str(), "slant_range_first_pixel", &slant_range_first_pixel) != 0 ||
            conversion.read_int_from_h5(masterPathStd.c_str(), "offset_row", &offset_row) != 0 ||
            conversion.read_int_from_h5(masterPathStd.c_str(), "offset_col", &offset_col) != 0 ||
            conversion.read_int_from_h5(masterPathStd.c_str(), "azimuth_len", &sceneHeight) != 0 ||
            conversion.read_int_from_h5(masterPathStd.c_str(), "range_len", &sceneWidth) != 0 ||
            conversion.read_str_from_h5(masterPathStd.c_str(), "acquisition_start_time", start_time_str) != 0)
        {
            emit errorProcess(QStringLiteral("读取主影像 H5 文件几何参数失败！"));
            return;
        }
    }
    catch (const std::exception& e) {
        emit errorProcess(QStringLiteral("读取 H5 参数时发生异常: ") + QString::fromStdString(e.what()));
        return;
    }

    if (!state_vec.empty() && state_vec.type() != CV_64F) {
        state_vec.convertTo(state_vec, CV_64F);
    }
    if (!lon_coefficient.empty() && lon_coefficient.type() != CV_64F) {
        lon_coefficient.convertTo(lon_coefficient, CV_64F);
    }
    if (!lat_coefficient.empty() && lat_coefficient.type() != CV_64F) {
        lat_coefficient.convertTo(lat_coefficient, CV_64F);
    }

    // 转换影像首行 GPS 开始时间
    double start_gps_time = 0.0;
    if (conversion.utc2gps(start_time_str.c_str(), &start_gps_time) != 0) {
        emit errorProcess(QStringLiteral("UTC时间转换为GPS时间失败！"));
        return;
    }

    emit updateProcess(30, QStringLiteral("开始调用 DLL 算法执行轨道精炼……"));

    // 调用外部 DLL 一键轨道精炼核心算法
    OrbitCorrection correction;
    int runRet = -1;
    try {
        runRet = refinement.refine_orbit(
            state_vec,
            lon_coefficient,
            lat_coefficient,
            gcps,
            start_gps_time,
            polyDegree,
            prf,
            offset_row,
            offset_col,
            sceneHeight,
            sceneWidth,
            slant_range_first_pixel,
            range_spacing,
            carrier_frequency,
            correction
        );
    }
    catch (const std::exception& e) {
        InSARLogManager::LogError("OrbitRefinementWorker", QString("算法执行异常：%1").arg(e.what()));
        emit errorProcess(QStringLiteral("算法类运行期间发生未知内存异常！"));
        return;
    }

    if (runRet != 0) {
        InSARLogManager::LogError("OrbitRefinementWorker", QString("轨道精炼算法失败，错误标志：%1").arg(runRet));
        emit errorProcess(QStringLiteral("轨道精炼算法拟合失败，请检查控制点分布及质量！"));
        return;
    }

    // 拟合成功日志埋点
    InSARLogManager::LogInfo("OrbitRefinementWorker", QString("轨道精炼拟合成功。参与控制点数：%1，距离向 RMS 残差：%2 米，方位向 RMS 残差：%3 米")
        .arg(correction.num_gcp_used).arg(correction.rms_residual_range).arg(correction.rms_residual_azimuth));

    emit updateProcess(60, QStringLiteral("正在回写控制点残差至数据库……"));

    // 回写最新的拟合残差到项目 SQLite 数据库
    if (!db.updateGCPs(gcps)) {
        InSARLogManager::LogError("OrbitRefinementWorker", "回写控制点残差至数据库失败！");
    } else {
        InSARLogManager::LogInfo("OrbitRefinementWorker", "控制点残差成功批量同步至数据库。");
    }

    emit updateProcess(70, QStringLiteral("正在生成输出影像并回写轨道参数……"));

    // 复制所有 H5 图像至输出路径并覆写主影像的轨道参数
    QStringList outputFilePaths;
    for (int i = 0; i < filePaths.size(); i++) {
        QString srcPath = filePaths.at(i);
        QFileInfo fileInfo(srcPath);
        QString dstPath = QString("%1/%2/%3.h5").arg(projectDir).arg(dstNode).arg(fileInfo.baseName());
        
        // 删除已存在的旧文件
        if (QFile::exists(dstPath)) {
            QFile::remove(dstPath);
        }

        if (!QFile::copy(srcPath, dstPath)) {
            InSARLogManager::LogError("OrbitRefinementWorker", QString("复制文件失败：%1 -> %2").arg(srcPath).arg(dstPath));
            emit errorProcess(QStringLiteral("复制输出影像文件失败！"));
            return;
        }

        outputFilePaths.append(dstPath);

        // 如果是主影像，则需要将精炼后的 state_vec, lon_coefficient, lat_coefficient 写回该 H5
        if (i == masterIndex - 1) {
            std::string outPathStd = dstPath.toStdString();
            try {
                if (conversion.write_array_to_h5(outPathStd.c_str(), "state_vec", state_vec) != 0 ||
                    conversion.write_array_to_h5(outPathStd.c_str(), "lon_coefficient", lon_coefficient) != 0 ||
                    conversion.write_array_to_h5(outPathStd.c_str(), "lat_coefficient", lat_coefficient) != 0 ||
                    conversion.write_int_to_h5(outPathStd.c_str(), "orbit_refined", 1) != 0 ||
                    conversion.write_double_to_h5(outPathStd.c_str(), "orbit_refinement_rms_range", correction.rms_residual_range) != 0 ||
                    conversion.write_double_to_h5(outPathStd.c_str(), "orbit_refinement_rms_azimuth", correction.rms_residual_azimuth) != 0 ||
                    conversion.write_int_to_h5(outPathStd.c_str(), "orbit_refinement_num_gcp", correction.num_gcp_used) != 0 ||
                    conversion.write_int_to_h5(outPathStd.c_str(), "orbit_refinement_poly_degree", polyDegree) != 0)
                {
                    emit errorProcess(QStringLiteral("将精炼轨道参数回写主影像失败！"));
                    return;
                }
            }
            catch (const std::exception& e) {
                emit errorProcess(QStringLiteral("回写 H5 数据时发生异常: ") + QString::fromStdString(e.what()));
                return;
            }
        }
    }
    emit updateProcess(90, QStringLiteral("正在广播输出结果信号……"));

    QStringList originNames;
    for (const QString& srcPath : filePaths) {
        originNames.append(QFileInfo(srcPath).baseName());
    }

    emit sendResults(dstNode, outputFilePaths, originNames);

    emit updateProcess(100, QStringLiteral("轨道精炼已完成！"));
    InSARLogManager::LogInfo("OrbitRefinementWorker", "轨道精炼 Worker 计算流程成功结束。");
    emit endProcess();
}

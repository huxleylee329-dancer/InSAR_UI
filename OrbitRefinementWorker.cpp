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
#include <QStorageInfo>
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
    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

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
        QString masterPath = QString::fromStdString(masterPathStd);
        if (!NodeUtils::readMatFromH5(masterPath, "state_vec", state_vec) ||
            !NodeUtils::readMatFromH5(masterPath, "lon_coefficient", lon_coefficient) ||
            !NodeUtils::readMatFromH5(masterPath, "lat_coefficient", lat_coefficient) ||
            !NodeUtils::readScalarFromH5(masterPath, "prf", prf) ||
            !NodeUtils::readScalarFromH5(masterPath, "range_spacing", range_spacing) ||
            !NodeUtils::readScalarFromH5(masterPath, "carrier_frequency", carrier_frequency) ||
            !NodeUtils::readScalarFromH5(masterPath, "slant_range_first_pixel", slant_range_first_pixel) ||
            !NodeUtils::readScalarFromH5(masterPath, "offset_row", offset_row) ||
            !NodeUtils::readScalarFromH5(masterPath, "offset_col", offset_col) ||
            !NodeUtils::readScalarFromH5(masterPath, "azimuth_len", sceneHeight) ||
            !NodeUtils::readScalarFromH5(masterPath, "range_len", sceneWidth) ||
            !NodeUtils::readStringFromH5(masterPath, "acquisition_start_time", start_time_str))
        {
            emit errorProcess(QStringLiteral("读取主影像 H5 文件几何参数失败！"));
            return;
        }
    }
    catch (const std::exception& e) {
        emit errorProcess(QStringLiteral("读取 H5 参数时发生异常: ") + QString::fromStdString(e.what()));
        return;
    }

    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
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
    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    if (!db.updateGCPs(gcps)) {
        InSARLogManager::LogError("OrbitRefinementWorker", "回写控制点残差至数据库失败！");
    } else {
        InSARLogManager::LogInfo("OrbitRefinementWorker", "控制点残差成功批量同步至数据库。");
    }

    emit updateProcess(70, QStringLiteral("正在生成输出影像并回写轨道参数……"));

    // 预检：本段的成本几乎全在 QFile::copy 上（每景 GB 级、纯字节复制、无 reflink），
    // 失败的现实原因就是目标盘写满。Σ 源文件大小 vs 可用空间是一次 O(1) 元数据读取
    //（只读 fileSize，不读文件内容，符合项目的大文件禁哈希规范），
    // 可以在做任何复制之前判死，而不是拷到第 k 景才 ENOSPC 后整批回滚。
    {
        qint64 requiredBytes = 0;
        for (const QString& srcPath : filePaths) {
            const QFileInfo info(srcPath);
            if (info.isFile()) requiredBytes += info.size();
        }
        const QStorageInfo storage(projectDir);
        if (storage.isValid() && requiredBytes > 0 && storage.bytesAvailable() < requiredBytes) {
            emit errorProcess(QStringLiteral("目标磁盘可用空间不足：复制 %1 幅影像需要约 %2 GB，"
                                             "当前可用 %3 GB（目录：%4）。")
                .arg(filePaths.size())
                .arg(double(requiredBytes) / 1073741824.0, 0, 'f', 2)
                .arg(double(storage.bytesAvailable()) / 1073741824.0, 0, 'f', 2)
                .arg(projectDir));
            return;
        }
    }

    // 复制所有 H5 图像至输出路径并覆写主影像的轨道参数。
    // 处理顺序上主影像优先：它的复制 + 轨道回写是全流程唯一会写 payload 且可能硬失败的一步，
    // 而复制本身是主要成本（每景 GB 级 QFile::copy，纯字节复制，无 reflink）。原先回写夹在
    // 循环内、只有 i == masterIndex-1 时才触发 —— 主影像若排在靠后，前面若干份复制会一起作废
    //（失败即走事务回滚删掉整个 staging）。
    // 注意 outputFilePaths 按下标落位（不再是 append），所以处理顺序变了、输出列表顺序仍与
    // 输入完全一致 —— 这一点必须保证：下面的 originNames 是按 filePaths 顺序建的，两者按下标配对。
    QStringList outputFilePaths;
    outputFilePaths.reserve(filePaths.size());
    // Qt5 的 QList 没有 resize()，按输入顺序先占位，后面再按下标落位
    for (int i = 0; i < filePaths.size(); i++) outputFilePaths.append(QString());

    QList<int> copyOrder;
    copyOrder.reserve(filePaths.size());
    if (masterIndex >= 1 && masterIndex <= filePaths.size()) copyOrder.append(masterIndex - 1);
    for (int i = 0; i < filePaths.size(); i++) {
        if (i != masterIndex - 1) copyOrder.append(i);
    }

    for (int orderIndex = 0; orderIndex < copyOrder.size(); orderIndex++) {
        const int i = copyOrder.at(orderIndex);
        if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
            emit cancelled();
            return;
        }

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

        outputFilePaths[i] = dstPath;

        // 如果是主影像，则需要将精炼后的 state_vec, lon_coefficient, lat_coefficient 写回该 H5
        if (i == masterIndex - 1) {
            std::string outPathStd = dstPath.toStdString();
            try {
                if (!NodeUtils::writeMatToH5(dstPath, "state_vec", state_vec) ||
                    !NodeUtils::writeMatToH5(dstPath, "lon_coefficient", lon_coefficient) ||
                    !NodeUtils::writeMatToH5(dstPath, "lat_coefficient", lat_coefficient) ||
                    !NodeUtils::writeScalarToH5(dstPath, "orbit_refined", 1) ||
                    !NodeUtils::writeScalarToH5(dstPath, "orbit_refinement_rms_range", correction.rms_residual_range) ||
                    !NodeUtils::writeScalarToH5(dstPath, "orbit_refinement_rms_azimuth", correction.rms_residual_azimuth) ||
                    !NodeUtils::writeScalarToH5(dstPath, "orbit_refinement_num_gcp", correction.num_gcp_used) ||
                    !NodeUtils::writeScalarToH5(dstPath, "orbit_refinement_poly_degree", polyDegree))
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

    if (isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }

    QStringList originNames;
    for (const QString& srcPath : filePaths) {
        originNames.append(QFileInfo(srcPath).baseName());
    }

    emit sendResults(dstNode, outputFilePaths, originNames);

    emit updateProcess(100, QStringLiteral("轨道精炼已完成！"));
    InSARLogManager::LogInfo("OrbitRefinementWorker", "轨道精炼 Worker 计算流程成功结束。");
    emit endProcess();
}

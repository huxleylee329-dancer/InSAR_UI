#include "TroposphericCorrectionWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
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

#include <QRegularExpression>

static QString extractDateString(const QString& str) {
    QRegularExpression re1("(\\d{4}-\\d{2}-\\d{2})");
    QRegularExpressionMatch match1 = re1.match(str);
    if (match1.hasMatch()) {
        return match1.captured(1).remove('-'); // 返回 YYYYMMDD
    }
    QRegularExpression re2("(?<!\\d)(20\\d{6})(?!\\d)");
    QRegularExpressionMatch match2 = re2.match(str);
    if (match2.hasMatch()) {
        return match2.captured(1);
    }
    return "";
}

static thread_local TroposphericCorrectionWorker* currentWorker = nullptr;

static bool __stdcall troposphericProgressCallback(int progress, const char* message)
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

TroposphericCorrectionWorker::TroposphericCorrectionWorker(QObject* parent)
    : BaseWorker(parent)
{
}

TroposphericCorrectionWorker::~TroposphericCorrectionWorker()
{
}

void TroposphericCorrectionWorker::doCorrection(
    QString era5Dir, QString save_path, QString project_name,
    QString node_name, QString file_name,
    QStringList phase_names, QStringList phase_paths)
{
    Q_UNUSED(project_name);
    Q_UNUSED(node_name);
    InSARLogManager::LogInfo("TroposphericCorrectionWorker",
        QString("ERA5对流层校正开始. ERA5目录: %1, 输出: %2").arg(era5Dir).arg(file_name));

    if (save_path.isEmpty() || project_name.isEmpty() ||
        node_name.isEmpty() || file_name.isEmpty() || era5Dir.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }
    // 输出目录由节点事务预先创建；worker 只允许写入 staging 目录。
    const QString outputDirectoryPath = save_path + "/" + file_name;
    QDir outputDirectory(outputDirectoryPath);
    if (!outputDirectory.exists()) {
        emit errorProcess(QStringLiteral("staging输出目录不存在: ") + outputDirectoryPath);
        return;
    }

    // 获取输入文件列表
    QList<QString> output_names, abs_paths;

    emit updateProcess(5, QStringLiteral("准备数据……"));

    if (phase_names.size() != phase_paths.size()) {
        emit errorProcess(QStringLiteral("输入干涉图快照无效"));
        return;
    }
    for (int i = 0; i < phase_paths.size(); ++i) {
        const QString outName = phase_names[i] + "_tropo";
        output_names.append(outName);
        abs_paths.append(outputDirectoryPath + "/" + outName + ".h5");
    }

    int image_count = phase_paths.size();
    if (image_count == 0) { emit errorProcess(QStringLiteral("没有可校正的干涉图")); return; }

    // 扫描 ERA5 目录下的 .nc 文件
    QDir era5_dir(era5Dir);
    QStringList ncFilters;
    ncFilters << "*.nc";
    QStringList ncFiles = era5_dir.entryList(ncFilters, QDir::Files);
    if (ncFiles.isEmpty()) {
        emit errorProcess(QStringLiteral("ERA5目录中未找到NetCDF (.nc) 文件: ") + era5Dir);
        return;
    }

    FormatConversion FC;
    int ret = 0;

    auto findNcFileForDate = [&](const QString& dateStr) -> QString {
        if (dateStr.isEmpty()) return QString();
        QString dashDate = QString("%1-%2-%3")
            .arg(dateStr.left(4))
            .arg(dateStr.mid(4, 2))
            .arg(dateStr.mid(6, 2));

        for (const QString& file : ncFiles) {
            if (file.contains(dateStr) || file.contains(dashDate)) {
                return era5_dir.absoluteFilePath(file);
            }
        }
        return QString();
    };

    currentWorker = this;
    const auto fail = [this](const QString& message) {
        currentWorker = nullptr;
        emit errorProcess(message);
    };
    const auto writeMat = [&fail](const QString& filePath, const QString& dataset, const cv::Mat& value) {
        if (NodeUtils::writeMatToH5(filePath, dataset, value)) {
            return true;
        }
        fail(QStringLiteral("无法写入H5数据集: %1").arg(dataset));
        return false;
    };

    std::vector<bool> process_ok(image_count, false);

    // 处理每幅干涉图
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) {
            currentWorker = nullptr;
            emit cancelled();
            return;
        }

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("校正第%1/%2幅干涉图……").arg(idx + 1).arg(image_count));

        // 读取相位和坐标
        Mat phase;
        bool has_latlon = false, has_dem = false;
        Mat lat_mat, lon_mat, dem;
        string src1_str, src2_str;
        {
            NodeUtils::Hdf5Locker locker;
            QString phaseH5 = phase_paths[idx];
            
            ret = NodeUtils::readMatFromH5(phaseH5, "phase", phase, CV_32F) ? 0 : -1;
            if (ret < 0) {
                fail(QStringLiteral("无法读取第%1幅干涉图相位数据").arg(idx + 1));
                return;
            }

            if (NodeUtils::readMatFromH5(phaseH5, "mapped_lat", lat_mat, CV_32F) &&
                NodeUtils::readMatFromH5(phaseH5, "mapped_lon", lon_mat, CV_32F)) {
                has_latlon = true;
            }
            if (NodeUtils::readMatFromH5(phaseH5, "mapped_dem", dem, CV_32F)) {
                has_dem = true;
            }

            NodeUtils::readStringFromH5(phaseH5, "source_1", src1_str);
            NodeUtils::readStringFromH5(phaseH5, "source_2", src2_str);
        }
        int rows = phase.rows, cols = phase.cols;

        if (!has_latlon) {
            fail(QStringLiteral("第%1幅干涉图缺少坐标数据").arg(idx + 1));
            return;
        }

        // 提取日期字符串
        QString masterDate = extractDateString(QString::fromStdString(src1_str));
        QString slaveDate = extractDateString(QString::fromStdString(src2_str));

        QString master_nc = findNcFileForDate(masterDate);
        QString slave_nc = findNcFileForDate(slaveDate);

        if (master_nc.isEmpty() && !ncFiles.isEmpty()) {
            master_nc = era5_dir.absoluteFilePath(ncFiles.first());
            InSARLogManager::LogWarning("TroposphericCorrectionWorker", 
                QString("未找到主影像日期 %1 的 ERA5 文件，使用第一个文件：%2").arg(masterDate).arg(ncFiles.first()));
        }
        if (slave_nc.isEmpty() && !ncFiles.isEmpty()) {
            slave_nc = era5_dir.absoluteFilePath(ncFiles.first());
            InSARLogManager::LogWarning("TroposphericCorrectionWorker", 
                QString("未找到从影像日期 %1 的 ERA5 文件，使用第一个文件：%2").arg(slaveDate).arg(ncFiles.first()));
        }

        // 获取雷达波长 (米)
        double wavelength = 0.055465763; // 默认值 (Sentinel-1 C波段)
        double carrier_frequency = 0;
        if (NodeUtils::readScalarFromH5(phase_paths[idx], "carrier_frequency", carrier_frequency)) {
            if (carrier_frequency > 0) {
                wavelength = 299792458.0 / carrier_frequency;
            }
        }

        // 预分配输出矩阵 (双重保险)
        Mat corrected_phase = phase.clone();

        char errBuf[512] = {0};
        // 调用独立算法 DLL 进行 ERA5 对流层校正
        bool ok = computeTroposphericCorrection(
            phase, lat_mat, lon_mat, dem, has_dem,
            master_nc.toLocal8Bit().constData(),
            slave_nc.toLocal8Bit().constData(),
            wavelength,
            corrected_phase,
            errBuf, 512,
            troposphericProgressCallback
        );

        if (!ok) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                currentWorker = nullptr;
                emit cancelled();
                return;
            }
            InSARLogManager::LogError("TroposphericCorrectionWorker", 
                QString("对流层算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            fail(QStringLiteral("对流层算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            return;
        }

        // 写入输出 H5
        {
            NodeUtils::Hdf5Locker locker;
            ret = FC.creat_new_h5(abs_paths[idx].toStdString().c_str());
        }
        if (ret < 0) {
            fail(QStringLiteral("无法创建第%1幅对流层输出文件").arg(idx + 1));
            return;
        }

        {
            NodeUtils::Hdf5Locker locker;
            QString phaseH5 = phase_paths[idx];
            QString absH5 = abs_paths[idx];
            string tmp_str;
            Mat tmp;
            
            NodeUtils::readStringFromH5(phaseH5, "source_1", tmp_str);
            FC.write_str_to_h5(absH5.toStdString().c_str(), "source_1", tmp_str.c_str());
            NodeUtils::readStringFromH5(phaseH5, "source_2", tmp_str);
            FC.write_str_to_h5(absH5.toStdString().c_str(), "source_2", tmp_str.c_str());

            if (!NodeUtils::readMatFromH5(phaseH5, "flat_phase_coefficient", tmp)) {
                fail(QStringLiteral("无法读取第%1幅干涉图平坦相位系数").arg(idx + 1));
                return;
            }
            if (!writeMat(absH5, QStringLiteral("flat_phase_coefficient"), tmp)) return;
            if (!NodeUtils::readMatFromH5(phaseH5, "range_len", tmp)) {
                fail(QStringLiteral("无法读取第%1幅干涉图距离尺寸").arg(idx + 1));
                return;
            }
            if (!writeMat(absH5, QStringLiteral("range_len"), tmp)) return;
            if (!NodeUtils::readMatFromH5(phaseH5, "azimuth_len", tmp)) {
                fail(QStringLiteral("无法读取第%1幅干涉图方位尺寸").arg(idx + 1));
                return;
            }
            if (!writeMat(absH5, QStringLiteral("azimuth_len"), tmp)) return;
            if (!NodeUtils::readMatFromH5(phaseH5, "multilook_rg", tmp)) {
                fail(QStringLiteral("无法读取第%1幅干涉图距离多视参数").arg(idx + 1));
                return;
            }
            if (!writeMat(absH5, QStringLiteral("multilook_rg"), tmp)) return;
            if (!NodeUtils::readMatFromH5(phaseH5, "multilook_az", tmp)) {
                fail(QStringLiteral("无法读取第%1幅干涉图方位多视参数").arg(idx + 1));
                return;
            }
            if (!writeMat(absH5, QStringLiteral("multilook_az"), tmp)) return;
            if (has_latlon) {
                if (!writeMat(absH5, QStringLiteral("mapped_lat"), lat_mat)) return;
                if (!writeMat(absH5, QStringLiteral("mapped_lon"), lon_mat)) return;
            }

            if (!writeMat(absH5, QStringLiteral("phase"), corrected_phase)) return;
        }

        process_ok[idx] = true;
    }

    QStringList generatedNames;
    QStringList generatedPaths;
    for (int i = 0; i < image_count; ++i) {
        if (process_ok[i]) {
            generatedNames.append(output_names[i]);
            generatedPaths.append(abs_paths[i]);
        }
    }
    if (generatedPaths.size() != image_count) {
        currentWorker = nullptr;
        emit errorProcess(QStringLiteral("对流层校正未完成全部输出文件"));
        return;
    }

    currentWorker = nullptr;
    InSARLogManager::LogInfo("TroposphericCorrectionWorker", "对流层校正完成");
    emit outputsGenerated(generatedNames, generatedPaths);
    emit endProcess();
}

#include "TroposphericCorrectionWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QStandardItem>
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
    QStandardItemModel* model)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("TroposphericCorrectionWorker",
        QString("ERA5对流层校正开始. ERA5目录: %1, 输出: %2").arg(era5Dir).arg(file_name));

    if (save_path.isEmpty() || project_name.isEmpty() ||
        node_name.isEmpty() || file_name.isEmpty() || era5Dir.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }
    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }

    // 创建输出目录
    QDir dir(save_path);
    if (dir.exists(file_name)) dir.remove(file_name);
    dir.mkdir(file_name);

    // 获取输入文件列表
    QList<QString> phase_names, phase_paths, output_names, rel_paths, abs_paths;
    bool found_project = false, found_node = false;

    emit updateProcess(5, QStringLiteral("准备数据……"));

    QMetaObject::invokeMethod(model, [&]() {
        QList<QStandardItem*> foundProjects = model->findItems(project_name);
        if (foundProjects.isEmpty()) return;
        found_project = true;
        QStandardItem* project = foundProjects.first();

        for (int i = 0; i < project->rowCount(); i++) {
            if (project->child(i, 0)->text() == node_name) {
                found_node = true;
                QStandardItem* node = project->child(i, 0);
                for (int j = 0; j < node->rowCount(); j++) {
                    if (node->child(j, 0)->toolTip() == "phase") {
                        QString origin = node->child(j, 0)->text();
                        phase_names.append(origin);
                        phase_paths.append(node->child(j, 1)->text());
                        QString out_name = origin + "_tropo";
                        output_names.append(out_name);
                        rel_paths.append("/" + file_name + "/" + out_name + ".h5");
                        abs_paths.append(save_path + "/" + file_name + "/" + out_name + ".h5");
                    }
                }
                break;
            }
        }
    }, Qt::BlockingQueuedConnection);

    if (!found_project) { emit errorProcess(QStringLiteral("未找到工程: ") + project_name); return; }
    if (!found_node) { emit errorProcess(QStringLiteral("未找到数据节点: ") + node_name); return; }

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

    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) xml_path += ".Insar";

    XMLFile temp_xml;
    if (temp_xml.XMLFile_load(xml_path.toStdString().c_str()) < 0) {
        emit errorProcess(QStringLiteral("加载项目XML失败: ") + xml_path);
        return;
    }

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

    std::vector<bool> process_ok(image_count, false);

    // 处理每幅干涉图
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) break;

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("校正第%1/%2幅干涉图……").arg(idx + 1).arg(image_count));

        // 读取相位和坐标
        Mat phase;
        ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "phase", phase);
        if (ret < 0) continue;
        if (phase.type() != CV_32F) {
            phase.convertTo(phase, CV_32F);
        }

        int rows = phase.rows, cols = phase.cols;

        Mat lat_mat, lon_mat, dem;
        bool has_latlon = false, has_dem = false;
        if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lat", lat_mat)) {
            if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lon", lon_mat)) {
                has_latlon = true;
                if (lat_mat.type() != CV_32F) lat_mat.convertTo(lat_mat, CV_32F);
                if (lon_mat.type() != CV_32F) lon_mat.convertTo(lon_mat, CV_32F);
            }
        }
        if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_dem", dem)) {
            has_dem = true;
            if (dem.type() != CV_32F) {
                dem.convertTo(dem, CV_32F);
            }
        }

        if (!has_latlon) {
            emit updateProcess(progress, QStringLiteral("第%1幅缺少坐标数据，跳过").arg(idx + 1));
            continue;
        }

        // 读取主从影像时间戳（用于求差）
        string src1_str, src2_str;
        FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_1", src1_str);
        FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_2", src2_str);

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
        if (0 == FC.read_double_from_h5(phase_paths[idx].toStdString().c_str(), "carrier_frequency", &carrier_frequency)) {
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
            InSARLogManager::LogError("TroposphericCorrectionWorker", 
                QString("对流层算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            continue;
        }

        // 写入输出 H5
        ret = FC.creat_new_h5(abs_paths[idx].toStdString().c_str());
        if (ret < 0) continue;

        string tmp_str;
        Mat tmp;
        FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_1", tmp_str);
        FC.write_str_to_h5(abs_paths[idx].toStdString().c_str(), "source_1", tmp_str.c_str());
        FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_2", tmp_str);
        FC.write_str_to_h5(abs_paths[idx].toStdString().c_str(), "source_2", tmp_str.c_str());

        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "flat_phase_coefficient", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "flat_phase_coefficient", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "range_len", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "range_len", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "azimuth_len", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "azimuth_len", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "multilook_rg", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "multilook_rg", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "multilook_az", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "multilook_az", tmp);
        if (has_latlon) {
            FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "mapped_lat", lat_mat);
            FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "mapped_lon", lon_mat);
        }

        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "phase", corrected_phase);

        process_ok[idx] = true;
    }

    currentWorker = nullptr;

    // 更新项目树
    QMetaObject::invokeMethod(model, [&]() {
        QList<QStandardItem*> foundProjects = model->findItems(project_name);
        if (foundProjects.isEmpty()) return;
        QStandardItem* project = foundProjects.first();

        QStandardItem* tropo_node = NodeUtils::findOrCreateProjectNode(
            project, file_name, "phase-2.5", FOLDER_ICON);

        XMLFile local_xml;
        local_xml.XMLFile_load(xml_path.toStdString().c_str());

        for (int i = 0; i < image_count; i++) {
            if (!process_ok[i]) continue;
            local_xml.XMLFile_add_unwrap(file_name.toStdString().c_str(),
                output_names[i].toStdString().c_str(), rel_paths[i].toStdString().c_str(),
                0, 0, "ERA5_Tropospheric", 0);
            NodeUtils::findOrCreateChildItem(tropo_node, output_names[i], "phase",
                abs_paths[i], IMAGEDATA_ICON);
        }
        local_xml.XMLFile_save(xml_path.toStdString().c_str());
    }, Qt::BlockingQueuedConnection);

    emit sendModel(model);
    InSARLogManager::LogInfo("TroposphericCorrectionWorker", "对流层校正完成");
    emit endProcess();
}

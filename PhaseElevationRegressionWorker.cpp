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
    QStandardItemModel* model)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("PhaseElevationRegressionWorker",
        QString("回归校正开始. 输出: %1, 阶数: %2, 窗口: %3, 相干阈值: %4")
        .arg(file_name).arg(polyOrder).arg(windowSize).arg(coherenceThresh));

    if (save_path.isEmpty() || project_name.isEmpty() ||
        node_name.isEmpty() || file_name.isEmpty())
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
    QString absolute_path = save_path + "/" + file_name;
    if (dir.exists(file_name)) {
        dir.remove(file_name);
    }
    dir.mkdir(file_name);

    // 从项目树中获取输入相位文件列表
    QList<QString> phase_names;
    QList<QString> phase_paths;
    QList<QString> output_names;
    QList<QString> relative_output_paths;
    QList<QString> absolute_output_paths;
    bool found_project = false;
    bool found_node = false;

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
                        QString origin_name = node->child(j, 0)->text();
                        phase_names.append(origin_name);
                        phase_paths.append(node->child(j, 1)->text());
                        QString change_name = origin_name + "_atmos";
                        output_names.append(change_name);
                        relative_output_paths.append("/" + file_name + "/" + change_name + ".h5");
                        absolute_output_paths.append(save_path + "/" + file_name + "/" + change_name + ".h5");
                    }
                }
                break;
            }
        }
    }, Qt::BlockingQueuedConnection);

    if (!found_project) {
        emit errorProcess(QStringLiteral("未找到工程: ") + project_name);
        return;
    }
    if (!found_node) {
        emit errorProcess(QStringLiteral("未找到数据节点: ") + node_name);
        return;
    }

    int image_count = phase_paths.size();
    if (image_count == 0) {
        emit errorProcess(QStringLiteral("没有可校正的干涉图"));
        return;
    }

    FormatConversion FC;
    Utils util;
    int ret = 0;

    // 加载项目 XML
    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) {
        xml_path += ".Insar";
    }

    XMLFile temp_xml;
    if (temp_xml.XMLFile_load(xml_path.toStdString().c_str()) < 0) {
        emit errorProcess(QStringLiteral("加载项目XML失败: ") + xml_path);
        return;
    }

    std::vector<int> offset_rows(image_count, 0);
    std::vector<int> offset_cols(image_count, 0);
    std::vector<bool> process_ok(image_count, false);

    currentWorker = this;

    // 处理每幅干涉图
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) break;

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("校正第%1/%2幅干涉图……").arg(idx + 1).arg(image_count));

        // 读取相位
        Mat phase;
        ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "phase", phase);
        if (ret < 0) { continue; }
        if (phase.type() != CV_32F) {
            phase.convertTo(phase, CV_32F);
        }

        int rows = phase.rows;
        int cols = phase.cols;

        // 读取相干性
        Mat coherence;
        ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "coherence", coherence);
        bool has_coherence = (ret == 0 && coherence.rows == rows && coherence.cols == cols);
        if (has_coherence && coherence.type() != CV_32F) {
            coherence.convertTo(coherence, CV_32F);
        }

        // 读取高程数据（mapped_dem 或从 lat/lon 推断）
        Mat dem;
        bool has_dem = false;
        ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_dem", dem);
        if (ret == 0 && dem.rows == rows && dem.cols == cols) {
            has_dem = true;
            if (dem.type() != CV_32F) {
                dem.convertTo(dem, CV_32F);
            }
        }

        // 读取地理坐标
        Mat lat_mat, lon_mat;
        bool has_latlon = false;
        ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lat", lat_mat);
        if (ret == 0) {
            ret = FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lon", lon_mat);
            if (ret == 0 && lat_mat.rows == rows && lat_mat.cols == cols) {
                has_latlon = true;
                if (lat_mat.type() != CV_32F) lat_mat.convertTo(lat_mat, CV_32F);
                if (lon_mat.type() != CV_32F) lon_mat.convertTo(lon_mat, CV_32F);
            }
        }

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
        ret = FC.creat_new_h5(absolute_output_paths[idx].toStdString().c_str());
        if (ret < 0) continue;

        // 复制元数据
        string tmp_str;
        Mat tmp;
        FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_1", tmp_str);
        FC.write_str_to_h5(absolute_output_paths[idx].toStdString().c_str(), "source_1", tmp_str.c_str());
        FC.read_str_from_h5(phase_paths[idx].toStdString().c_str(), "source_2", tmp_str);
        FC.write_str_to_h5(absolute_output_paths[idx].toStdString().c_str(), "source_2", tmp_str.c_str());

        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "flat_phase_coefficient", tmp);
        FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "flat_phase_coefficient", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "range_len", tmp);
        FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "range_len", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "azimuth_len", tmp);
        FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "azimuth_len", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "multilook_rg", tmp);
        FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "multilook_rg", tmp);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "multilook_az", tmp);
        FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "multilook_az", tmp);

        if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lon", tmp))
            FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "mapped_lon", tmp);
        if (0 == FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "mapped_lat", tmp))
            FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "mapped_lat", tmp);
        if (has_coherence) {
            FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "coherence", coherence);
        }

        // 写入校正后的相位
        FC.write_array_to_h5(absolute_output_paths[idx].toStdString().c_str(), "phase", corrected_phase);

        // 读取偏移量
        Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "offset_row", tmp_int);
        offset_rows[idx] = tmp_int.at<int>(0, 0);
        FC.read_array_from_h5(phase_paths[idx].toStdString().c_str(), "offset_col", tmp_int);
        offset_cols[idx] = tmp_int.at<int>(0, 0);

        process_ok[idx] = true;
    }

    currentWorker = nullptr;

    // 更新项目树和 XML
    QMetaObject::invokeMethod(model, [&]() {
        QList<QStandardItem*> foundProjects = model->findItems(project_name);
        if (foundProjects.isEmpty()) return;
        QStandardItem* project = foundProjects.first();

        QStandardItem* atmos_node = NodeUtils::findOrCreateProjectNode(
            project, file_name, "phase-2.5", FOLDER_ICON);

        XMLFile local_xml;
        local_xml.XMLFile_load(xml_path.toStdString().c_str());

        for (int i = 0; i < image_count; i++) {
            if (!process_ok[i]) continue;

            local_xml.XMLFile_add_unwrap(
                file_name.toStdString().c_str(),
                output_names[i].toStdString().c_str(),
                relative_output_paths[i].toStdString().c_str(),
                offset_rows[i], offset_cols[i],
                "PhaseElevationRegression", 0);

            NodeUtils::findOrCreateChildItem(
                atmos_node, output_names[i], "phase",
                absolute_output_paths[i], IMAGEDATA_ICON);
        }
        local_xml.XMLFile_save(xml_path.toStdString().c_str());
    }, Qt::BlockingQueuedConnection);

    emit sendModel(model);
    InSARLogManager::LogInfo("PhaseElevationRegressionWorker", "回归校正完成");
    emit endProcess();
}

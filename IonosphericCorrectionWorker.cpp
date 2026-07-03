#include "IonosphericCorrectionWorker.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QThread>
#include <QStandardItem>
#include <cmath>
#include <complex>
#include <vector>
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

static thread_local IonosphericCorrectionWorker* currentWorker = nullptr;

static bool __stdcall ionosphericProgressCallback(int progress, const char* message)
{
    if (currentWorker) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }
        emit currentWorker->updateProcess(progress, QString::fromLocal8Bit(message));
    }
    return true;
}

IonosphericCorrectionWorker::IonosphericCorrectionWorker(QObject* parent)
    : BaseWorker(parent)
{
}

IonosphericCorrectionWorker::~IonosphericCorrectionWorker()
{
}

void IonosphericCorrectionWorker::doCorrection(
    double subbandRatio, double filterStrength, bool outputTEC,
    QString save_path, QString project_name,
    QString node_name, QString file_name,
    QStandardItemModel* model)
{
    currentWorker = this;
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("IonosphericCorrectionWorker",
        QString("电离层校正开始. 输出: %1, 子频带比例: %2, 滤波强度: %3")
        .arg(file_name).arg(subbandRatio).arg(filterStrength));

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
    if (dir.exists(file_name)) dir.remove(file_name);
    dir.mkdir(file_name);

    // 获取输入文件列表（已配准的主从 SLC 影像对）
    QList<QString> slc_names, slc_paths, output_names, rel_paths, abs_paths;
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
                    if (node->child(j, 0)->toolTip() == "complex") {
                        QString origin = node->child(j, 0)->text();
                        slc_names.append(origin);
                        slc_paths.append(node->child(j, 1)->text());
                        QString out_name = origin + "_iono";
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

    int image_count = slc_paths.size();
    if (image_count == 0) { emit errorProcess(QStringLiteral("没有可处理的SLC影像")); return; }

    FormatConversion FC;
    int ret = 0;

    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) xml_path += ".Insar";

    XMLFile temp_xml;
    if (temp_xml.XMLFile_load(xml_path.toStdString().c_str()) < 0) {
        emit errorProcess(QStringLiteral("加载项目XML失败: ") + xml_path);
        return;
    }

    std::vector<bool> process_ok(image_count, false);

    // 处理每对 SLC 影像
    for (int idx = 0; idx < image_count; idx++)
    {
        if (QThread::currentThread()->isInterruptionRequested()) return;

        int progress = 10 + idx * 80 / image_count;
        emit updateProcess(progress, QStringLiteral("电离层校正第%1/%2幅……").arg(idx + 1).arg(image_count));

        if (idx == 0) {
            // Master 图像作为参考，不进行电离层相位校正，直接拷贝
            ret = FC.creat_new_h5(abs_paths[idx].toStdString().c_str());
            if (ret < 0) continue;

            Mat slc_complex;
            ret = FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "complex", slc_complex);
            if (ret >= 0) {
                FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "complex", slc_complex);
            }

            // 复制元数据
            string tmp_str;
            Mat tmp;
            if (0 == FC.read_str_from_h5(slc_paths[idx].toStdString().c_str(), "source_1", tmp_str))
                FC.write_str_to_h5(abs_paths[idx].toStdString().c_str(), "source_1", tmp_str.c_str());
            if (0 == FC.read_str_from_h5(slc_paths[idx].toStdString().c_str(), "source_2", tmp_str))
                FC.write_str_to_h5(abs_paths[idx].toStdString().c_str(), "source_2", tmp_str.c_str());
            if (0 == FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "range_len", tmp))
                FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "range_len", tmp);
            if (0 == FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "azimuth_len", tmp))
                FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "azimuth_len", tmp);
            if (0 == FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "mapped_lat", tmp))
                FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "mapped_lat", tmp);
            if (0 == FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "mapped_lon", tmp))
                FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "mapped_lon", tmp);

            process_ok[idx] = true;
            continue;
        }

        // 读取 Master 复数 SLC 数据
        Mat master_complex;
        ret = FC.read_array_from_h5(slc_paths[0].toStdString().c_str(), "complex", master_complex);
        if (ret < 0) continue;

        // 读取 Slave 复数 SLC 数据
        Mat slave_complex;
        ret = FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "complex", slave_complex);
        if (ret < 0) continue;

        int rows = slave_complex.rows;
        int cols = slave_complex.cols;
        if (master_complex.rows != rows || master_complex.cols != cols) {
            InSARLogManager::LogError("IonosphericCorrectionWorker", "主从图像尺寸不匹配");
            continue;
        }

        // 转换为双通道复数格式 (CV_32FC2)
        auto toComplex2Ch = [](const Mat& src, Mat& dst, int r, int c) -> bool {
            if (src.channels() == 1) {
                vector<Mat> channels = {src, Mat::zeros(r, c, CV_32FC1)};
                merge(channels, dst);
            } else if (src.channels() == 2) {
                dst = src.clone();
            } else {
                return false;
            }
            return true;
        };

        Mat master_2ch, slave_2ch;
        if (!toComplex2Ch(master_complex, master_2ch, rows, cols)) continue;
        if (!toComplex2Ch(slave_complex, slave_2ch, rows, cols)) continue;

        // 预分配输出矩阵 (双重保险)
        Mat corrected(rows, cols, CV_32FC2);

        // 设置 DLL 参数
        IonosphericParams params;
        params.subbandRatio = subbandRatio;
        params.filterStrength = filterStrength;

        char errBuf[512] = {0};
        // 调用独立算法 DLL 进行电离层校正
        bool ok = computeIonosphericCorrection(
            master_2ch, slave_2ch, params,
            corrected,
            errBuf, 512,
            ionosphericProgressCallback
        );

        if (!ok) {
            InSARLogManager::LogError("IonosphericCorrectionWorker", 
                QString("电离层算法计算失败 (图%1): %2").arg(idx + 1).arg(QString::fromLocal8Bit(errBuf)));
            continue;
        }

        // 写入输出 H5
        ret = FC.creat_new_h5(abs_paths[idx].toStdString().c_str());
        if (ret < 0) continue;

        // 复制元数据
        string tmp_str;
        Mat tmp;
        FC.read_str_from_h5(slc_paths[idx].toStdString().c_str(), "source_1", tmp_str);
        FC.write_str_to_h5(abs_paths[idx].toStdString().c_str(), "source_1", tmp_str.c_str());
        FC.read_str_from_h5(slc_paths[idx].toStdString().c_str(), "source_2", tmp_str);
        FC.write_str_to_h5(abs_paths[idx].toStdString().c_str(), "source_2", tmp_str.c_str());

        FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "range_len", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "range_len", tmp);
        FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "azimuth_len", tmp);
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "azimuth_len", tmp);

        // 写入校正后的复数 SLC
        FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "complex", corrected);

        // 可选输出 TEC 估计图
        if (outputTEC) {
            // 由于算法封装至 DLL 内部，此处输出空的 TEC 占位矩阵
            cv::Mat tec_placeholder = cv::Mat::zeros(corrected.rows, corrected.cols, CV_32FC1);
            FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "tec_estimate", tec_placeholder);
        }

        if (0 == FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "mapped_lat", tmp))
            FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "mapped_lat", tmp);
        if (0 == FC.read_array_from_h5(slc_paths[idx].toStdString().c_str(), "mapped_lon", tmp))
            FC.write_array_to_h5(abs_paths[idx].toStdString().c_str(), "mapped_lon", tmp);

        process_ok[idx] = true;
    }

    // 更新项目树
    QMetaObject::invokeMethod(model, [&]() {
        QList<QStandardItem*> foundProjects = model->findItems(project_name);
        if (foundProjects.isEmpty()) return;
        QStandardItem* project = foundProjects.first();

        QStandardItem* iono_node = NodeUtils::findOrCreateProjectNode(
            project, file_name, "complex-1.5", FOLDER_ICON);

        XMLFile local_xml;
        local_xml.XMLFile_load(xml_path.toStdString().c_str());

        for (int i = 0; i < image_count; i++) {
            if (!process_ok[i]) continue;
            local_xml.XMLFile_add_unwrap(file_name.toStdString().c_str(),
                output_names[i].toStdString().c_str(), rel_paths[i].toStdString().c_str(),
                0, 0, "Ionospheric_Correction", 0);
            NodeUtils::findOrCreateChildItem(iono_node, output_names[i], "complex",
                abs_paths[i], IMAGEDATA_ICON);
        }
        local_xml.XMLFile_save(xml_path.toStdString().c_str());
    }, Qt::BlockingQueuedConnection);

    emit sendModel(model);
    currentWorker = nullptr;
    InSARLogManager::LogInfo("IonosphericCorrectionWorker", "电离层校正完成");
    emit endProcess();
}

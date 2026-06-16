#include "LidarImportWorker.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <opencv2/core.hpp>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include "InSARLogManager.h"
#include "NodeUtils.h"

LidarImportWorker::LidarImportWorker(QObject* parent)
    : QObject(parent)
    , stop_flag(true)
{
}

LidarImportWorker::~LidarImportWorker()
{
}

void LidarImportWorker::StopProcess()
{
    QMutexLocker locker(&lock);
    this->stop_flag = false;
}

bool LidarImportWorker::isStopRequested()
{
    QMutexLocker locker(&lock);
    return !stop_flag;
}

void LidarImportWorker::import_Lidar_patch(
    QString savepath,
    std::vector<QString> original_file_list,
    std::vector<QString> import_namelist,
    QString product_type,
    int rh_percentile,
    QString dst_node,
    QString dst_project,
    QStandardItemModel* model
)
{
    if (savepath.isEmpty() ||
        dst_node.isEmpty() ||
        dst_project.isEmpty() ||
        original_file_list.empty() ||
        import_namelist.empty() ||
        model == NULL
        )
    {
        InSARLogManager::LogInfo("LidarImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
        emit endProcess();
        return;
    }

    NodeUtils::Hdf5Locker locker;
    int ret = 0;
    QDir dir(savepath);
    if (!dir.exists(dst_node))
        ret = dir.mkdir(dst_node);

    int n_images = original_file_list.size();
    int process = 2;
    FormatConversion conversion;
    emit updateProcess(process, QStringLiteral("正在导入..."));

    for (int i = 0; i < n_images; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) break;
        QString filename = import_namelist[i];
        QString input_file = original_file_list[i];
        QString temp_folder = QString("/") + dst_node + QString("/");
        QString relative_path = temp_folder + filename + ".h5";
        QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);

        std::string file_std = input_file.toStdString();
        std::string h5_path_std = h5_path.toStdString();
        if (product_type == "GEDI L2A")
        {
            cv::Mat rh, lon, lat, dem, quality;
            ret = conversion.read_height_metric_from_GEDI_L2A(file_std.c_str(), rh, lon, lat, dem, quality, rh_percentile);
            if (ret >= 0)
            {
                ret = conversion.creat_new_h5(h5_path_std.c_str());
                if (ret >= 0)
                {
                    conversion.write_array_to_h5(h5_path_std.c_str(), "rh", rh);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "lon", lon);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "lat", lat);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "dem", dem);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "quality", quality);
                }
            }
        }
        else if (product_type == "GEDI L2B")
        {
            cv::Mat rh100, lowestmode, highestreturn, lon, lat, dem, quality;
            ret = conversion.read_height_metric_from_GEDI_L2B(file_std.c_str(), rh100, lowestmode, highestreturn, lon, lat, dem, quality);
            if (ret >= 0)
            {
                ret = conversion.creat_new_h5(h5_path_std.c_str());
                if (ret >= 0)
                {
                    conversion.write_array_to_h5(h5_path_std.c_str(), "rh100", rh100);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "elev_lowestmode", lowestmode);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "elev_highestreturn", highestreturn);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "lon", lon);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "lat", lat);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "dem", dem);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "quality", quality);
                }
            }
        }
        else if (product_type == "ICESat-2 L3A")
        {
            cv::Mat rh, lon, lat, dem, quality;
            ret = conversion.read_height_metric_from_ICESat_2_L3A(file_std.c_str(), rh, lon, lat, dem, quality, rh_percentile);
            if (ret >= 0)
            {
                ret = conversion.creat_new_h5(h5_path_std.c_str());
                if (ret >= 0)
                {
                    conversion.write_array_to_h5(h5_path_std.c_str(), "rh", rh);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "lon", lon);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "lat", lat);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "dem", dem);
                    conversion.write_array_to_h5(h5_path_std.c_str(), "quality", quality);
                }
            }
        }

        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("LidarImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            emit errorProcess(QStringLiteral("导入文件失败，数据格式错误或读取失败！"));
            return;
        }

        int localRet = 0;
        if (model) {
            QMetaObject::invokeMethod(model, [=, &localRet]() {
                QStandardItem* project = model->findItems(dst_project)[0];
                if (!project) {
                    localRet = -1;
                    return;
                }
                QModelIndex pro_index = model->indexFromItem(project);
                QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
                QStandardItem* origin = NULL;
                for (int j = 0; j < project->rowCount(); j++)
                {
                    if (dst_node == project->child(j)->text() && project->child(j, 1)->text() == "complex-0.0")
                    {
                        origin = project->child(j); break;
                    }
                }
                if (!origin)
                {
                    origin = new QStandardItem(dst_node);
                    origin->setIcon(QIcon(FOLDER_ICON));
                    project->appendRow(origin);
                    QStandardItem* Rank = new QStandardItem("complex-0.0");
                    project->setChild(project->rowCount() - 1, 1, Rank);
                }
                QStandardItem* img = new QStandardItem(filename);
                img->setToolTip("complex");
                QStandardItem* img_path = new QStandardItem(h5_path);
                img->setIcon(QIcon(IMAGEDATA_ICON));
                origin->appendRow(img);
                origin->setChild(origin->rowCount() - 1, 1, img_path);

                XMLFile DOC;
                int ret = DOC.XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
                if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    localRet = -2;
                    return;
                }
                ret = DOC.XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "LiDAR");
                if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    localRet = -2;
                    return;
                }
                ret = DOC.XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
                if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    localRet = -2;
                    return;
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            localRet = -1;
        }

        if (localRet < 0)
        {
            InSARLogManager::LogError("LidarImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            if (localRet == -1) {
                emit errorProcess(QStringLiteral("未找到项目节点。"));
            } else {
                emit errorProcess(QStringLiteral("保存配置文件失败。"));
            }
            return;
        }

        process = 2 + (i + 1) * 98 / n_images;
        emit updateProcess(process, QStringLiteral("正在导入..."));
    }

    emit sendModel(model);
    InSARLogManager::LogInfo("LidarImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

#include "HTHTImportWorker.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include "InSARLogManager.h"
#include "NodeUtils.h"

HTHTImportWorker::HTHTImportWorker(QObject* parent)
    : QObject(parent)
    , stop_flag(true)
{
}

HTHTImportWorker::~HTHTImportWorker()
{
}

void HTHTImportWorker::StopProcess()
{
    QMutexLocker locker(&lock);
    this->stop_flag = false;
}

bool HTHTImportWorker::isStopRequested()
{
    QMutexLocker locker(&lock);
    return !stop_flag;
}

void HTHTImportWorker::import_HTHT_patch(
    QString savepath,
    std::vector<QString> data_files,
    std::vector<QString> xml_files,
    std::vector<int> modes,
    std::vector<QString> import_names,
    QString dst_node,
    QString dst_project,
    QStandardItemModel* model
)
{
    if (savepath.isEmpty() ||
        dst_node.isEmpty() ||
        dst_project.isEmpty() ||
        data_files.empty() ||
        xml_files.empty() ||
        modes.empty() ||
        import_names.empty() ||
        model == NULL
        )
    {
        InSARLogManager::LogInfo("HTHTImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
        emit endProcess();
        return;
    }

    NodeUtils::Hdf5Locker locker;
    int ret = 0;
    QDir dir(savepath);
    if (!dir.exists(dst_node))
        ret = dir.mkdir(dst_node);

    int n_images = data_files.size();
    int process = 2;
    emit updateProcess(process, QStringLiteral("正在导入..."));

    for (int i = 0; i < n_images; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) break;

        QString filename = import_names[i];
        QString data_file = data_files[i];
        QString xml_file = xml_files[i];
        int mode = modes[i];

        QString temp_folder = QString("/") + dst_node + QString("/");
        QString relative_path = temp_folder + filename + ".h5";
        QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);

        HTHT_reader reader(data_file.toStdString().c_str(), xml_file.toStdString().c_str(), mode);
        ret = reader.init();
        if (ret >= 0)
        {
            ret = reader.write_to_h5(h5_path.toStdString().c_str());
        }

        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("HTHTImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            InSARLogManager::LogError("HTHTImportWorker", "unknown format or conversion error!");
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            emit errorProcess("unknown format or conversion error!");
            return;
        }

        // 生成 JPG 预览缩略图
        QFileInfo fi(h5_path);
        QString jpg_path = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        NodeUtils::generateJpgPreviewFromH5(h5_path, jpg_path, "complex");

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
                QStandardItem* img = NULL;
                for (int j = 0; j < origin->rowCount(); j++)
                {
                    if (origin->child(j)->text() == filename)
                    {
                        img = origin->child(j);
                        break;
                    }
                }
                if (!img)
                {
                    img = new QStandardItem(filename);
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
                    ret = DOC.XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "Hongtu-1");
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
                }
                else
                {
                    origin->setChild(img->row(), 1, new QStandardItem(h5_path));
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            localRet = -1;
        }

        if (localRet < 0)
        {
            InSARLogManager::LogError("HTHTImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            if (localRet == -1) {
                emit errorProcess(QStringLiteral("未找到项目节点。"));
            } else {
                emit errorProcess(QStringLiteral("保存项目配置文件失败。"));
            }
            return;
        }
        process = double(i + 1) / double(n_images) * 100.0;
        emit updateProcess(process, QStringLiteral("正在导入..."));
    }

    emit sendModel(model);
    InSARLogManager::LogInfo("HTHTImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

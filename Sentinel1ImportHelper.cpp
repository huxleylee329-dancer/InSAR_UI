#include "InSARLogManager.h"
#include "Sentinel1ImportHelper.h"
#include "Sentinel1ImportWorker.h"
#include "NodeUtils.h"
#include "FormatConversion.h"
#include "icon_source.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QStandardItem>

namespace Sentinel1ImportHelper {

void importSentinel(
    Sentinel1ImportWorker* worker,
    QString PODFile,
    QString manifest_file,
    QString subswath,
    QString polarization,
    QString project_path,
    QString folder,
    QString filename,
    QString project_name,
    QStandardItemModel* model
)
{
    InSARLogManager::LogInfo("Sentinel1ImportHelper", QString("Task started: ") + QString(__FUNCTION__));
    if (manifest_file.isEmpty() ||
        subswath.isEmpty() ||
        polarization.isEmpty() ||
        folder.isEmpty() ||
        project_path.isEmpty() ||
        filename.isEmpty() ||
        project_name.isEmpty() ||
        model == nullptr
        )
    {
        return;
    }

    NodeUtils::Hdf5Locker locker;
    int ret;
    QDir dir(project_path);
    if (!dir.exists(folder))
        ret = dir.mkdir(folder);

    QString temp_folder = QString("/") + folder + QString("/");
    QString relative_path = temp_folder + filename + ".h5";
    QString h5_path = QString("%1%2%3.h5").arg(project_path).arg(temp_folder).arg(filename);

    Q_EMIT worker->updateProcess(20, QStringLiteral("正在导入数据，请耐心等待……"));

    FormatConversion conversion;
    ret = conversion.import_sentinel(manifest_file.toStdString().c_str(),
        subswath.toStdString().c_str(),
        polarization.toStdString().c_str(),
        h5_path.toStdString().c_str(),
        PODFile.toStdString().c_str()
    );

    if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
    {
        InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
        QFile::remove(h5_path);
        QDir tmp_dir(project_path + QString("/") + folder);
        tmp_dir.removeRecursively();
        return;
    }

    Q_EMIT worker->updateProcess(90, QStringLiteral("即将完成……"));

    QStandardItem* project = model->findItems(project_name)[0];
    if (!project) {
        QFile::remove(h5_path);
        QDir tmp_dir(project_path + QString("/") + folder);
        tmp_dir.removeRecursively();
        return;
    }

    QModelIndex pro_index = model->indexFromItem(project);
    QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
    QStandardItem* origin = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (folder == project->child(i)->text() && project->child(i, 1)->text() == "complex-0.0")
        {
            origin = project->child(i);
            break;
        }
    }

    if (!origin)
    {
        origin = new QStandardItem(folder);
        origin->setIcon(QIcon(FOLDER_ICON));
        project->appendRow(origin);
        QStandardItem* Rank = new QStandardItem("complex-0.0");
        project->setChild(project->rowCount() - 1, 1, Rank);
    }

    QStandardItem* img = nullptr;
    for (int i = 0; i < origin->rowCount(); i++)
    {
        if (origin->child(i)->text() == filename)
        {
            img = origin->child(i);
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

        XMLFile xml;
        ret = xml.XMLFile_load(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
        {
            InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(project_path + QString("/") + folder);
            tmp_dir.removeRecursively();
            return;
        }

        ret = xml.XMLFile_add_origin(folder.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "sentinel");
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
        {
            InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(project_path + QString("/") + folder);
            tmp_dir.removeRecursively();
            return;
        }

        ret = xml.XMLFile_save(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
        {
            InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(project_path + QString("/") + folder);
            tmp_dir.removeRecursively();
            return;
        }
    }
    else
    {
        origin->setChild(img->row(), 1, new QStandardItem(h5_path));
    }

    // 静默生成预览图
    QString bmp_path = QString("%1%2%3.jpg").arg(project_path).arg(temp_folder).arg(filename);
    NodeUtils::generateJpgPreviewFromH5(h5_path, bmp_path, "complex");

    Q_EMIT worker->sendModel(model);
    InSARLogManager::LogInfo("Sentinel1ImportHelper", QString("Task completed: ") + QString(__FUNCTION__));
    Q_EMIT worker->endProcess();
}

void importSentinelPatch(
    Sentinel1ImportWorker* worker,
    std::vector<QString> original_filelist,
    std::vector<QString> import_namelist,
    QString subswath,
    QString polarization,
    QString savepath,
    QString dst_node,
    QString dst_project,
    QStandardItemModel* model
)
{
    InSARLogManager::LogInfo("Sentinel1ImportHelper", QString("Task started: ") + QString(__FUNCTION__));
    if (original_filelist.size() != import_namelist.size() ||
        import_namelist.size() < 1 ||
        subswath.isEmpty() ||
        polarization.isEmpty() ||
        dst_node.isEmpty() ||
        dst_project.isEmpty() ||
        savepath.isEmpty() ||
        model == nullptr
        )
    {
        InSARLogManager::LogInfo("Sentinel1ImportHelper", QString("Task completed (empty/invalid input): ") + QString(__FUNCTION__));
        Q_EMIT worker->endProcess();
        return;
    }

    NodeUtils::Hdf5Locker locker;
    int ret;
    QDir dir(savepath);
    if (!dir.exists(dst_node))
        ret = dir.mkdir(dst_node);

    QString temp_folder = QString("/") + dst_node + QString("/");
    int n_images = original_filelist.size();
    int process = 2;
    FormatConversion conversion;

    Q_EMIT worker->updateProcess(process, QStringLiteral("正在导入..."));

    for (int i = 0; i < n_images; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
            break;

        QString filename = import_namelist[i];
        QString manifest_file = original_filelist[i];
        QString relative_path = temp_folder + filename + ".h5";
        QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);

        ret = conversion.import_sentinel(manifest_file.toStdString().c_str(),
            subswath.toStdString().c_str(),
            polarization.toStdString().c_str(),
            h5_path.toStdString().c_str()
        );

        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
        {
            InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            return;
        }

        // 静默生成预览图
        QString bmp_path = QString("%1%2%3.jpg").arg(savepath).arg(temp_folder).arg(filename);
        NodeUtils::generateJpgPreviewFromH5(h5_path, bmp_path, "complex");

        QStandardItem* project = model->findItems(dst_project)[0];
        if (!project) {
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            return;
        }

        QModelIndex pro_index = model->indexFromItem(project);
        QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
        QStandardItem* origin = nullptr;
        for (int j = 0; j < project->rowCount(); j++)
        {
            if (dst_node == project->child(j)->text() && project->child(j, 1)->text() == "complex-0.0")
            {
                origin = project->child(j);
                break;
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

        QStandardItem* img = nullptr;
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

            XMLFile xml;
            ret = xml.XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
            {
                InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
                QFile::remove(h5_path);
                QDir tmp_dir(savepath + QString("/") + dst_node);
                tmp_dir.removeRecursively();
                return;
            }

            ret = xml.XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "sentinel");
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
            {
                InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
                QFile::remove(h5_path);
                QDir tmp_dir(savepath + QString("/") + dst_node);
                tmp_dir.removeRecursively();
                return;
            }

            ret = xml.XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || worker->isStopRequested())
            {
                InSARLogManager::LogError("Sentinel1ImportHelper", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
                QFile::remove(h5_path);
                QDir tmp_dir(savepath + QString("/") + dst_node);
                tmp_dir.removeRecursively();
                return;
            }
        }
        else
        {
            origin->setChild(img->row(), 1, new QStandardItem(h5_path));
        }

        process = 2 + 98 * (i + 1) / n_images;
        Q_EMIT worker->updateProcess(process, QStringLiteral("正在导入..."));
    }

    Q_EMIT worker->sendModel(model);
    InSARLogManager::LogInfo("Sentinel1ImportHelper", QString("Task completed: ") + QString(__FUNCTION__));
    Q_EMIT worker->endProcess();
}

} // namespace Sentinel1ImportHelper

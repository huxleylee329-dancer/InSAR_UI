#include "TSXImportWorker.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include <QDebug>
#include "InSARLogManager.h"

TSXImportWorker::TSXImportWorker(QObject* parent)
    : QObject(parent)
    , stop_flag(true)
{
}

TSXImportWorker::~TSXImportWorker()
{
}

void TSXImportWorker::StopProcess()
{
    QMutexLocker locker(&lock);
    this->stop_flag = false;
}

bool TSXImportWorker::isStopRequested()
{
    QMutexLocker locker(&lock);
    return !stop_flag;
}

void TSXImportWorker::import_TSX(
    QString polarization,
    QString xml_filename,
    QString project_path,
    QString folder,
    QString filename,
    QString project_name,
    QStandardItemModel* model
)
{
    if (xml_filename.isEmpty() ||
        folder.isEmpty() ||
        project_path.isEmpty() ||
        filename.isEmpty() ||
        project_name.isEmpty() ||
        model == NULL
        )
    {
        return;
    }

    int ret;
    QDir dir(project_path);
    if (!dir.exists(folder))
        ret = dir.mkdir(folder);
    QString temp_folder = QString("/") + folder + QString("/");
    QString relative_path = temp_folder + filename + ".h5";
    QString h5_path = QString("%1%2%3.h5").arg(project_path).arg(temp_folder).arg(filename);
    emit updateProcess(20, QStringLiteral("正在导入数据，请耐心等待……"));
    FormatConversion conversion;
    ret = conversion.TSX2h5(xml_filename.toStdString().c_str(), 
        h5_path.toStdString().c_str(),
        polarization.toStdString().c_str());
    if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
    {
        InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
        QFile::remove(h5_path);
        QDir tmp_dir(project_path + QString("/") + folder);
        tmp_dir.removeRecursively();
        emit errorProcess(QStringLiteral("导入失败或被中止。"));
        return;
    }
    emit updateProcess(90, QStringLiteral("即将完成……"));

    QStandardItem* project = model->findItems(project_name)[0];
    if (!project) {
        QFile::remove(h5_path);
        QDir tmp_dir(project_path + QString("/") + folder);
        tmp_dir.removeRecursively();
        emit errorProcess(QStringLiteral("未找到项目节点。"));
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
    QStandardItem* origin = NULL;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (folder == project->child(i)->text() && project->child(i, 1)->text() == "complex-0.0")
        {
            origin = project->child(i); break;
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
    QStandardItem* img = NULL;
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

        XMLFile* DOC = new XMLFile();
        ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(project_path + QString("/") + folder);
            tmp_dir.removeRecursively();
            delete DOC;
            emit errorProcess(QStringLiteral("保存项目配置文件失败。"));
            return;
        }
        ret = DOC->XMLFile_add_origin(folder.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "TSX");
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(project_path + QString("/") + folder);
            tmp_dir.removeRecursively();
            delete DOC;
            emit errorProcess(QStringLiteral("保存项目配置文件失败。"));
            return;
        }
        ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
        delete DOC;
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(project_path + QString("/") + folder);
            tmp_dir.removeRecursively();
            emit errorProcess(QStringLiteral("保存项目配置文件失败。"));
            return;
        }
    }
    else
    {
        origin->setChild(img->row(), 1, new QStandardItem(h5_path));
    }
    emit sendModel(model);
    InSARLogManager::LogInfo("TSXImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

void TSXImportWorker::import_TSX_patch(
    QString polarization,
    QString savepath,
    std::vector<QString> original_file_list,
    std::vector<QString> import_namelist,
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
        InSARLogManager::LogInfo("TSXImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
        emit endProcess();
        return;
    }

    int ret;
    QDir dir(savepath);
    if (!dir.exists(dst_node))
        ret = dir.mkdir(dst_node);
    int n_images = original_file_list.size();
    int process = 2;
    FormatConversion conversion;
    XMLFile* DOC = new XMLFile();
    emit updateProcess(process, QStringLiteral("正在导入..."));
    for (int i = 0; i < n_images; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) break;
        QString filename = import_namelist[i];
        QString xml_filename = original_file_list[i];
        QString temp_folder = QString("/") + dst_node + QString("/");
        QString relative_path = temp_folder + filename + ".h5";
        QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);
        
        ret = conversion.TSX2h5(xml_filename.toStdString().c_str(), h5_path.toStdString().c_str(), 
            polarization.toStdString().c_str());
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            delete DOC;
            emit errorProcess(QStringLiteral("导入失败或被中断。"));
            return;
        }

        QStandardItem* project = model->findItems(dst_project)[0];
        if (!project) {
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            delete DOC;
            emit errorProcess(QStringLiteral("未找到项目节点。"));
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

            ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
                QFile::remove(h5_path);
                QDir tmp_dir(savepath + QString("/") + dst_node);
                tmp_dir.removeRecursively();
                delete DOC;
                emit errorProcess(QStringLiteral("保存配置文件失败。"));
                return;
            }
            ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "TSX");
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
                QFile::remove(h5_path);
                QDir tmp_dir(savepath + QString("/") + dst_node);
                tmp_dir.removeRecursively();
                delete DOC;
                emit errorProcess(QStringLiteral("保存配置文件失败。"));
                return;
            }
            ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                InSARLogManager::LogError("TSXImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
                QFile::remove(h5_path);
                QDir tmp_dir(savepath + QString("/") + dst_node);
                tmp_dir.removeRecursively();
                delete DOC;
                emit errorProcess(QStringLiteral("保存配置文件失败。"));
                return;
            }
        }
        else
        {
            origin->setChild(img->row(), 1, new QStandardItem(h5_path));
        }
        process = double(i + 1) / double(n_images) * 100.0;
        emit updateProcess(process, QStringLiteral("正在导入..."));
    }
    delete DOC;

    emit sendModel(model);
    InSARLogManager::LogInfo("TSXImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

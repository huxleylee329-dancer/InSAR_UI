#include "BaseImportWorker.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include <FormatConversion.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

BaseImportWorker::BaseImportWorker(const QString& satelliteName, QObject* parent)
    : BaseWorker(parent)
    , m_satelliteName(satelliteName)
{
}

BaseImportWorker::~BaseImportWorker()
{
}

void BaseImportWorker::updateImportProgress(int percent, const QString& message)
{
    emit updateProcess(percent, message.isEmpty() ? QStringLiteral("正在导入...") : message);
}

void BaseImportWorker::handleError(const QString& error_msg, const QString& h5_path, const QString& dir_path)
{
    InSARLogManager::LogError(m_satelliteName + "ImportWorker", QString("Task failed: ") + error_msg);
    QFile::remove(h5_path);
    QDir tmp_dir(dir_path);
    tmp_dir.removeRecursively();
    emit errorProcess(error_msg);
}

void BaseImportWorker::import_patch(
    const QString& savepath,
    const std::vector<ImportTask>& tasks,
    const QString& dst_node,
    const QString& dst_project,
    QStandardItemModel* model
)
{
    if (savepath.isEmpty() || dst_node.isEmpty() || dst_project.isEmpty() || tasks.empty() || model == nullptr)
    {
        InSARLogManager::LogError(m_satelliteName + "ImportWorker", "Invalid parameter arguments.");
        emit endProcess();
        return;
    }

    NodeUtils::Hdf5Locker hdf5Locker; // H5数据安全锁
    QDir dir(savepath);
    if (!dir.exists(dst_node)) {
        if (!dir.mkdir(dst_node)) {
            emit errorProcess(QStringLiteral("创建目标节点目录失败。"));
            return;
        }
    }

    int n_images = tasks.size();
    InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", QString("Task started: import_patch. Target Node: %1, Project: %2, Total Images: %3").arg(dst_node).arg(dst_project).arg(tasks.size()));
    emit updateProcess(2, QStringLiteral("正在开始导入..."));

    for (int i = 0; i < n_images; ++i)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) break;

        const auto& task = tasks[i];
        InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", QString("Importing image %1/%2: %3").arg(i + 1).arg(n_images).arg(task.filename));

        QString temp_folder = QString("/") + dst_node + QString("/");
        QString relative_path = temp_folder + task.filename + ".h5";
        QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(task.filename);

        // A. 执行子类特有的转换（传入当前任务的进度区间，最大限制为 90%）
        int progressMin = (double(i) / n_images) * 90;
        int progressMax = (double(i + 1) / n_images) * 90;
        bool success = convertToH5(task.arguments, h5_path, progressMin, progressMax);

        if (!success || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            handleError("Conversion to H5 failed or interrupted.", h5_path, savepath + "/" + dst_node);
            return;
        }

        InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", QString("Successfully converted image to H5: %1").arg(task.filename));

        // B. 静默生成 JPG 预览缩略图
        QFileInfo fi(h5_path);
        QString jpg_path = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        NodeUtils::generateJpgPreviewFromH5(h5_path, jpg_path, "complex");

        // C. 跨线程更新项目树与 XML
        int localRet = 0;
        QMetaObject::invokeMethod(model, [=, &localRet]() {
            if (model->findItems(dst_project).isEmpty()) {
                localRet = -1;
                return;
            }
            QStandardItem* project = model->findItems(dst_project)[0];
            if (!project) {
                localRet = -1;
                return;
            }
            QModelIndex pro_index = model->indexFromItem(project);
            QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();

            // 查找或创建 Origin 节点
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

            // 查找或创建映像叶子项
            QStandardItem* img = nullptr;
            for (int j = 0; j < origin->rowCount(); j++)
            {
                if (origin->child(j)->text() == task.filename)
                {
                    img = origin->child(j);
                    break;
                }
            }

            if (!img)
            {
                img = new QStandardItem(task.filename);
                img->setToolTip("complex");
                QStandardItem* img_path = new QStandardItem(h5_path);
                img->setIcon(QIcon(IMAGEDATA_ICON));
                origin->appendRow(img);
                origin->setChild(origin->rowCount() - 1, 1, img_path);

                // 更新 XML 项目配置文件
                XMLFile DOC;
                QString xml_path = QString("%1/%2").arg(pro_path).arg(dst_project);
                int ret = DOC.XMLFile_load(xml_path.toStdString().c_str());
                if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    localRet = -2;
                    return;
                }

                // 此处动态调用 satelliteFormatTag() 虚函数写入具体的卫星格式名称
                ret = DOC.XMLFile_add_origin(dst_node.toStdString().c_str(), task.filename.toStdString().c_str(), relative_path.toStdString().c_str(), satelliteFormatTag().toStdString().c_str());
                if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    localRet = -2;
                    return;
                }
                ret = DOC.XMLFile_save(xml_path.toStdString().c_str());
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

        if (localRet < 0)
        {
            handleError(localRet == -1 ? QStringLiteral("未找到项目节点。") : QStringLiteral("保存项目配置文件失败。"), h5_path, savepath + "/" + dst_node);
            return;
        }

        // 更新总体进度
        int progress = double(i + 1) / double(n_images) * 100.0;
        emit updateProcess(progress, QStringLiteral("正在导入..."));
    }

    emit sendModel(model);
    InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", "Task completed: import_patch");
    emit endProcess();
}

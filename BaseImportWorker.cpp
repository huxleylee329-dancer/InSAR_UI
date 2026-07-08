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

        // A. 执行子类特有的转换（分配 80% 进度给 H5 转换，余下 20% 分配给预览及元数据处理）
        int startRange = (double(i) / n_images) * 90;
        int endRange = (double(i + 1) / n_images) * 90;
        int progressMin = startRange;
        int progressMax = startRange + (endRange - startRange) * 0.8;
        bool success = false;
        {
            NodeUtils::Hdf5Locker hdf5Locker;
            success = convertToH5(task.arguments, h5_path, progressMin, progressMax);
        }

        if (!success || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            handleError("Conversion to H5 failed or interrupted.", h5_path, savepath + "/" + dst_node);
            return;
        }

        InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", QString("Successfully converted image to H5: %1").arg(task.filename));

        // B. 生成 JPG 预览缩略图（带分块进度更新）
        QFileInfo fi(h5_path);
        QString jpg_path = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        NodeUtils::generateJpgPreviewFromH5WithProgress(h5_path, jpg_path, previewDataType(), [&](int cur, int total) {
            int subProgress = progressMax + (endRange - progressMax) * double(cur) / total;
            emit updateProcess(subProgress, QStringLiteral("正在生成图像预览..."));
        });

        // C. 在子线程执行 XML 的 Load/Save 操作以避免阻塞 UI 主线程，仅在 UI 线程插入/更新树节点
        QString pro_path;
        bool isNewChild = true;
        int checkRet = 0;
        QMetaObject::invokeMethod(model, [&]() {
            if (model->findItems(dst_project).isEmpty()) {
                checkRet = -1;
                return;
            }
            QStandardItem* project = model->findItems(dst_project)[0];
            if (!project) {
                checkRet = -1;
                return;
            }
            QModelIndex pro_index = model->indexFromItem(project);
            pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();

            QStandardItem* origin = NodeUtils::findOrCreateProjectNode(project, dst_node, "complex-0.0");
            for (int r = 0; r < origin->rowCount(); ++r) {
                if (origin->child(r, 0)->text() == task.filename) {
                    isNewChild = false;
                    break;
                }
            }
        }, Qt::BlockingQueuedConnection);

        if (checkRet < 0 || pro_path.isEmpty())
        {
            handleError(QStringLiteral("未找到项目节点。"), h5_path, savepath + "/" + dst_node);
            return;
        }

        if (isNewChild)
        {
            XMLFile DOC;
            QString xml_path = QString("%1/%2").arg(pro_path).arg(dst_project);
            int ret = DOC.XMLFile_load(xml_path.toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                handleError(QStringLiteral("保存项目配置文件失败。"), h5_path, savepath + "/" + dst_node);
                return;
            }

            ret = DOC.XMLFile_add_origin(dst_node.toStdString().c_str(), task.filename.toStdString().c_str(), relative_path.toStdString().c_str(), satelliteFormatTag().toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                handleError(QStringLiteral("保存项目配置文件失败。"), h5_path, savepath + "/" + dst_node);
                return;
            }
            ret = DOC.XMLFile_save(xml_path.toStdString().c_str());
            if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                handleError(QStringLiteral("保存项目配置文件失败。"), h5_path, savepath + "/" + dst_node);
                return;
            }
        }

        // 更新树模型节点（仅 UI 数据呈现操作在 UI 主线程进行）
        QMetaObject::invokeMethod(model, [=]() {
            if (model->findItems(dst_project).isEmpty()) return;
            QStandardItem* project = model->findItems(dst_project)[0];
            if (!project) return;
            QStandardItem* origin = NodeUtils::findOrCreateProjectNode(project, dst_node, "complex-0.0");
            
            bool created = false;
            QStandardItem* img = NodeUtils::findOrCreateChildItem(origin, task.filename, previewDataType(), h5_path, "", &created);
            if (!created)
            {
                origin->setChild(img->row(), 1, new QStandardItem(h5_path));
            }
        }, Qt::BlockingQueuedConnection);

        // 更新总体进度（将进度推进至此图对应的 endRange 处）
        int progress = endRange;
        QString progressMsg = QStringLiteral("正在导入...");
        emit updateProcess(progress, progressMsg);
    }

    // 整个批量任务结束，将进度更新为100%并完成收尾
    emit updateProcess(100, QStringLiteral("导入完成"));
    emit sendModel(model);
    InSARLogManager::LogInfo(m_satelliteName + "ImportWorker", "Task completed: import_patch");
    emit endProcess();
}

#include "ALOS2ImportWorker.h"
#include "icon_source.h"
#include <FormatConversion.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThread>
#include "InSARLogManager.h"
#include "NodeUtils.h"

// 批量进度回调上下文
struct Alos2BatchProgressContext
{
    ALOS2ImportWorker* worker;
    int imageIndex;
    int totalImages;
};

// 批量DLL进度回调
static void onAlos2BatchProgress(int percent, const char* message, void* userData)
{
    Alos2BatchProgressContext* ctx = static_cast<Alos2BatchProgressContext*>(userData);
    if (!ctx || !ctx->worker)
        return;
    int mapped = 2 + ((ctx->imageIndex * 100 + percent) * 98) / (ctx->totalImages * 100);
    QString msg = (message && message[0]) ? QString::fromUtf8(message) : QStringLiteral("正在导入...");
    emit ctx->worker->updateProcess(mapped, msg);
}

ALOS2ImportWorker::ALOS2ImportWorker(QObject* parent)
    : QObject(parent)
    , stop_flag(true)
{
}

ALOS2ImportWorker::~ALOS2ImportWorker()
{
}

void ALOS2ImportWorker::StopProcess()
{
    QMutexLocker locker(&lock);
    this->stop_flag = false;
}

bool ALOS2ImportWorker::isStopRequested()
{
    QMutexLocker locker(&lock);
    return !stop_flag;
}

void ALOS2ImportWorker::import_ALOS2_patch(
    QString savepath,
    std::vector<QString> IMG_file_list,
    std::vector<QString> LED_file_list,
    std::vector<QString> import_namelist,
    QString dst_node,
    QString dst_project,
    QStandardItemModel* model
)
{
    if (savepath.isEmpty() ||
        dst_node.isEmpty() ||
        dst_project.isEmpty() ||
        IMG_file_list.empty() ||
        LED_file_list.empty() ||
        import_namelist.empty() ||
        model == NULL
        )
    {
        InSARLogManager::LogInfo("ALOS2ImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
        emit endProcess();
        return;
    }

    NodeUtils::Hdf5Locker locker;
    int ret;
    QDir dir(savepath);
    if (!dir.exists(dst_node))
        ret = dir.mkdir(dst_node);
    int n_images = IMG_file_list.size();
    int process = 2;
    FormatConversion conversion;
    XMLFile DOC;
    Alos2BatchProgressContext batchCtx;
    batchCtx.worker = this;
    batchCtx.totalImages = n_images;
    emit updateProcess(process, QStringLiteral("正在导入..."));
    for (int i = 0; i < n_images; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) break;
        QString filename = import_namelist[i];
        QString ALOS_IMG_filename = IMG_file_list[i];
        QString ALOS_LED_filename = LED_file_list[i];
        QString temp_folder = QString("/") + dst_node + QString("/");
        QString relative_path = temp_folder + filename + ".h5";
        QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);
        batchCtx.imageIndex = i;
        ret = conversion.ALOS2h5(ALOS_IMG_filename.toStdString().c_str(), ALOS_LED_filename.toStdString().c_str(),
            h5_path.toStdString().c_str(),
            onAlos2BatchProgress,
            &batchCtx);
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("ALOS2ImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            InSARLogManager::LogError("ALOS2ImportWorker", "unknown format!");
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            emit errorProcess("unknown format!");
            return;
        }

        QStandardItem* project = model->findItems(dst_project)[0];
        if (!project) {
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
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
        QStandardItem* img = new QStandardItem(filename);
        img->setToolTip("complex");
        QStandardItem* img_path = new QStandardItem(h5_path);
        img->setIcon(QIcon(IMAGEDATA_ICON));
        origin->appendRow(img);
        origin->setChild(origin->rowCount() - 1, 1, img_path);

        ret = DOC.XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("ALOS2ImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            emit errorProcess(QStringLiteral("保存配置文件失败。"));
            return;
        }
        ret = DOC.XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "ALOS2");
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("ALOS2ImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            emit errorProcess(QStringLiteral("保存配置文件失败。"));
            return;
        }
        ret = DOC.XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
        if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            InSARLogManager::LogError("ALOS2ImportWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
            QFile::remove(h5_path);
            QDir tmp_dir(savepath + QString("/") + dst_node);
            tmp_dir.removeRecursively();
            emit errorProcess(QStringLiteral("保存配置文件失败。"));
            return;
        }
    }

    emit sendModel(model);
    InSARLogManager::LogInfo("ALOS2ImportWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

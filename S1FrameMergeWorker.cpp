#include "S1FrameMergeWorker.h"
#include <Utils.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "tinyxml.h"
#include "FormatConversion.h"
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QIcon>
#include <atomic>

namespace {
std::atomic<S1FrameMergeWorker*> g_frameMergeWorker{ nullptr };

bool __stdcall frameMergeProgressCallback(int progress, const char* message)
{
    S1FrameMergeWorker* worker = g_frameMergeWorker.load(std::memory_order_acquire);
    if (!worker || worker->isStopRequested()) {
        return false;
    }

    const QString text = message ? QString::fromLocal8Bit(message) : QString();
    emit worker->updateProcess(30 + progress * 60 / 100,
        QStringLiteral("正在拼接帧数据：%1% %2").arg(progress).arg(text));
    return true;
}
}

S1FrameMergeWorker::S1FrameMergeWorker(QObject* parent)
    : BaseWorker(parent)
{
}

S1FrameMergeWorker::~S1FrameMergeWorker()
{
}

void S1FrameMergeWorker::S1_frame_merge(
    int index1, 
    int index2, 
    QString project_name, 
    QString srcNode1, 
    QString srcNode2, 
    QString dstNode, 
    QStandardItemModel* model
)
{
    NodeUtils::Hdf5Locker locker;
    if (QThread::currentThread()->isInterruptionRequested()) {
        emit cancelled();
        return;
    }
    InSARLogManager::LogInfo("S1FrameMergeWorker", QString("S1_frame_merge started. Project: %1").arg(project_name));
    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }

    QList<QStandardItem*> projects = model->findItems(project_name);
    if (projects.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到项目"));
        return;
    }
    QStandardItem* project = projects[0];
    QString save_path = model->item(project->row(), 1)->text();
    QDir dir(save_path);
    if (!dir.exists(dstNode)) {
        dir.mkdir(dstNode);
    }
    InSARLogManager::LogInfo("S1FrameMergeWorker", QString("Starting frame merge. Source Nodes: %1 & %2, Destination Node: %3").arg(srcNode1).arg(srcNode2).arg(dstNode));

    // 确定相邻单视复图像文件
    QString IW1_h5, IW2_h5;
    bool b_dstNode_existed = false;
    QStandardItem* frame_merge = nullptr;
    if (model) {
        QMetaObject::invokeMethod(model, [=, &b_dstNode_existed, &frame_merge, &IW1_h5, &IW2_h5]() {
            for (int i = 0; i < project->rowCount(); i++)
            {
                QStandardItem* node = project->child(i, 0);
                if (node->text() == dstNode && project->child(i, 1)->text() == QString("complex-0.0"))
                {
                    b_dstNode_existed = true;
                    frame_merge = project->child(i, 0);
                }
                if (node->text() == srcNode1)
                {
                    if (node->rowCount() >= index1) IW1_h5 = node->child(index1 - 1, 1)->text();
                }
                if (node->text() == srcNode2)
                {
                    if (node->rowCount() >= index2) IW2_h5 = node->child(index2 - 1, 1)->text();
                }
            }
        }, Qt::BlockingQueuedConnection);
    }

    if (IW1_h5.isEmpty() || IW2_h5.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到输入图像文件"));
        return;
    }

    InSARLogManager::LogInfo("S1FrameMergeWorker", QString("Selected images for merge: IW1=%1, IW2=%2").arg(IW1_h5).arg(IW2_h5));
    QFileInfo fileinfo1(IW1_h5);
    QFileInfo fileinfo2(IW2_h5);
    QString filename = fileinfo1.baseName() + "_" + fileinfo2.baseName();
    emit updateProcess(30, QStringLiteral("正在拼接……"));
    InSARLogManager::LogInfo("S1FrameMergeWorker", QString("Merging frames into: %1").arg(filename + ".h5"));
    
    int ret;
    Utils util;
    QString merged_h5 = save_path + "/" + dstNode + "/" + filename + ".h5";
    const QString bmp_path = save_path + "/" + dstNode + "/" + filename + ".jpg";
    const auto finishCancelled = [this, &merged_h5, &bmp_path]() {
        QFile::remove(merged_h5);
        QFile::remove(bmp_path);
        emit cancelled();
    };

    g_frameMergeWorker.store(this, std::memory_order_release);
    ret = util.S1_frame_merge(IW1_h5.toStdString().c_str(), IW2_h5.toStdString().c_str(),
        merged_h5.toStdString().c_str(), frameMergeProgressCallback);
    g_frameMergeWorker.store(nullptr, std::memory_order_release);
    if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        finishCancelled();
        return;
    }
    if (ret < 0)
    {
        InSARLogManager::LogError("S1FrameMergeWorker", QStringLiteral("输入不符合要求，请重试！"));
        emit errorProcess(QStringLiteral("输入不符合要求，请重试！"));
        return;
    }

    InSARLogManager::LogInfo("S1FrameMergeWorker", "Frames merged successfully. Generating preview image...");
    // 拼接成功后，生成 JPG 预览图（在后台线程中执行）
    NodeUtils::generateJpgPreviewFromH5(merged_h5, bmp_path, "complex");
    if (QThread::currentThread()->isInterruptionRequested()) {
        finishCancelled();
        return;
    }

    emit updateProcess(90, QStringLiteral("正在拼接……"));

    /* 建立子带拼接根节点 */
    if (model) {
        QMetaObject::invokeMethod(model, [=, &frame_merge]() {
            QStandardItem* local_frame_merge = frame_merge;
            if (!b_dstNode_existed)
            {
                local_frame_merge = new QStandardItem(dstNode);
                local_frame_merge->setToolTip(project_name);
                local_frame_merge->setIcon(QIcon(FOLDER_ICON));
                project->appendRow(local_frame_merge);
                QStandardItem* frame_merge_Rank = new QStandardItem("complex-0.0");
                project->setChild(project->rowCount() - 1, 1, frame_merge_Rank);
                frame_merge = local_frame_merge;
            }

            QStandardItem* item_img = nullptr;
            for (int j = 0; j < local_frame_merge->rowCount(); j++)
            {
                if (local_frame_merge->child(j, 0)->text() == filename)
                {
                    item_img = local_frame_merge->child(j, 0);
                    break;
                }
            }

            if (!item_img)
            {
                QStandardItem* frame_merge_images_name = new QStandardItem(filename);
                frame_merge_images_name->setToolTip("complex");
                QStandardItem* frame_merge_images_path = new QStandardItem(merged_h5);
                frame_merge_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                local_frame_merge->appendRow(frame_merge_images_name);
                local_frame_merge->setChild(local_frame_merge->rowCount() - 1, 1, frame_merge_images_path);
            }
            else
            {
                local_frame_merge->setChild(item_img->row(), 1, new QStandardItem(merged_h5));
            }
        }, Qt::BlockingQueuedConnection);
    }

    InSARLogManager::LogInfo("S1FrameMergeWorker", "Updating project tree model with merged frame output...");

    // XML 落盘由调用方完成，绕过外部 DLL 接口（SOP 避坑经验 #9）
    emit sendResult(dstNode, filename, save_path, project_name);

    emit updateProcess(100, QStringLiteral("完成……"));
    InSARLogManager::LogInfo("S1FrameMergeWorker", "Model and merged result successfully emitted to Node UI.");
    InSARLogManager::LogInfo("S1FrameMergeWorker", "S1_frame_merge completed successfully.");
    emit sendModel(model);
    emit endProcess();
}

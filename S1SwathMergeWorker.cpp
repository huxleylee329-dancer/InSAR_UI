#include "S1SwathMergeWorker.h"
#include <Utils.h>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "FormatConversion.h"
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QIcon>

S1SwathMergeWorker::S1SwathMergeWorker(QObject* parent)
    : QObject(parent)
{
}

S1SwathMergeWorker::~S1SwathMergeWorker()
{
}

void S1SwathMergeWorker::S1_swath_merge(
    int index1, 
    int index2, 
    int index3, 
    QString project_name, 
    QString srcNode1, 
    QString srcNode2, 
    QString srcNode3, 
    QString dstNode, 
    QStandardItemModel* model
)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("S1SwathMergeWorker", QString("S1_swath_merge started. Project: %1").arg(project_name));
    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }

    QString IW1_h5, IW2_h5, IW3_h5;
    QString save_path;
    bool b_dstNode_existed = false;

    QMetaObject::invokeMethod(model, [=, &IW1_h5, &IW2_h5, &IW3_h5, &save_path, &b_dstNode_existed]() {
        QList<QStandardItem*> projects = model->findItems(project_name);
        if (projects.isEmpty()) return;
        QStandardItem* project = projects[0];
        save_path = model->item(project->row(), 1)->text();

        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* node = project->child(i, 0);
            if (node->text() == dstNode)
            {
                b_dstNode_existed = true;
            }
            if (node->text() == srcNode1)
            {
                if (node->rowCount() >= index1) IW1_h5 = node->child(index1 - 1, 1)->text();
            }
            if (node->text() == srcNode2)
            {
                if (node->rowCount() >= index2) IW2_h5 = node->child(index2 - 1, 1)->text();
            }
            if (node->text() == srcNode3)
            {
                if (node->rowCount() >= index3) IW3_h5 = node->child(index3 - 1, 1)->text();
            }
        }
    }, Qt::BlockingQueuedConnection);

    if (save_path.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到项目"));
        return;
    }
    QDir dir(save_path);
    if (!dir.exists(dstNode)) {
        dir.mkdir(dstNode);
    }
    InSARLogManager::LogInfo("S1SwathMergeWorker", QString("Starting swath merge. Source Nodes: %1, %2, & %3, Destination Node: %4")
        .arg(srcNode1).arg(srcNode2).arg(srcNode3).arg(dstNode));

    if (IW1_h5.isEmpty() || IW2_h5.isEmpty() || IW3_h5.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到输入图像文件"));
        return;
    }

    InSARLogManager::LogInfo("S1SwathMergeWorker", QString("Selected images for merge: IW1=%1, IW2=%2, IW3=%3").arg(IW1_h5).arg(IW2_h5).arg(IW3_h5));
    emit updateProcess(30, QStringLiteral("正在拼接……"));
    
    QString merged_h5 = save_path + "/" + dstNode + "/merged_phase.h5";
    InSARLogManager::LogInfo("S1SwathMergeWorker", QString("Merging swaths into: %1").arg(merged_h5));
    
    Utils util;
    int ret = util.S1_subswath_merge(IW1_h5.toStdString().c_str(), IW2_h5.toStdString().c_str(), IW3_h5.toStdString().c_str(), 
        merged_h5.toStdString().c_str());
    if (ret < 0)
    {
        InSARLogManager::LogError("S1SwathMergeWorker", QStringLiteral("输入不符合要求，请重试！"));
        emit errorProcess(QStringLiteral("输入不符合要求，请重试！"));
        return;
    }

    InSARLogManager::LogInfo("S1SwathMergeWorker", "Swaths merged successfully. Generating preview image...");
    // 生成 JPG 预览图 (类型为 phase)
    QString bmp_path = save_path + "/" + dstNode + "/merged_phase.jpg";
    NodeUtils::generateJpgPreviewFromH5(merged_h5, bmp_path, "phase");

    emit updateProcess(90, QStringLiteral("正在拼接……"));

    /* 建立子带拼接根节点 */
    QMetaObject::invokeMethod(model, [=]() {
        QList<QStandardItem*> projects = model->findItems(project_name);
        if (projects.isEmpty()) return;
        QStandardItem* project = projects[0];

        QStandardItem* swath_merge = nullptr;
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* node = project->child(i, 0);
            if (node->text() == dstNode)
            {
                swath_merge = node;
                break;
            }
        }

        if (!swath_merge)
        {
            swath_merge = new QStandardItem(dstNode);
            swath_merge->setToolTip(project_name);
            swath_merge->setIcon(QIcon(FOLDER_ICON));
            project->appendRow(swath_merge);
            QStandardItem* swath_merge_Rank = new QStandardItem("phase-1.0");
            project->setChild(project->rowCount() - 1, 1, swath_merge_Rank);
        }

        QStandardItem* item_img = nullptr;
        for (int j = 0; j < swath_merge->rowCount(); j++)
        {
            if (swath_merge->child(j, 0)->text() == "merged_phase")
            {
                item_img = swath_merge->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* swath_merge_images_name = new QStandardItem("merged_phase");
            swath_merge_images_name->setToolTip("phase");
            QStandardItem* swath_merge_images_path = new QStandardItem(merged_h5);
            swath_merge_images_name->setIcon(QIcon(IMAGEDATA_ICON));
            swath_merge->appendRow(swath_merge_images_name);
            swath_merge->setChild(swath_merge->rowCount() - 1, 1, swath_merge_images_path);
        }
        else
        {
            swath_merge->setChild(item_img->row(), 1, new QStandardItem(merged_h5));
        }
    }, Qt::BlockingQueuedConnection);

    InSARLogManager::LogInfo("S1SwathMergeWorker", "Updating project tree model with merged swath output...");

    // XML 落盘交由调用方，这里只通知计算完成
    emit sendResult(dstNode, "merged_phase", save_path, project_name);

    emit updateProcess(100, QStringLiteral("完成……"));
    InSARLogManager::LogInfo("S1SwathMergeWorker", "Model and merged result successfully emitted to Node UI.");
    InSARLogManager::LogInfo("S1SwathMergeWorker", "S1_swath_merge completed successfully.");
    emit sendModel(model);
    emit endProcess();
}

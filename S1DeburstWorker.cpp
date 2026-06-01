#include "S1DeburstWorker.h"
#include "FormatConversion.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QList>
#include <QIcon>
#include <vector>
#include <string>

S1DeburstWorker::S1DeburstWorker(QObject* parent)
    : QObject(parent)
{
}

S1DeburstWorker::~S1DeburstWorker()
{
}

void S1DeburstWorker::S1_Deburst(
    QString savePath, 
    QString dstProject, 
    QString srcNode, 
    QString dstNode, 
    QStandardItemModel* model
)
{
    InSARLogManager::LogInfo("S1DeburstWorker", "S1_Deburst started.");
    if (savePath.isEmpty() || dstProject.isEmpty() || dstNode.isEmpty() || srcNode.isEmpty() || !model)
    {
        emit errorProcess(QStringLiteral("参数错误"));
        return;
    }
    int ret;
    QDir dir(savePath);
    if (!dir.exists(dstNode)) {
        dir.mkdir(dstNode);
    }
    std::vector<std::string> SAR_images;
    std::vector<std::string> SAR_images_deburst;
    QList<QString> origin;
    
    QList<QStandardItem*> foundProjects = model->findItems(dstProject);
    if (foundProjects.isEmpty())
    {
        emit errorProcess(QStringLiteral("未找到目标工程"));
        return;
    }
    QStandardItem* project = foundProjects[0];
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* images = project->child(i, 0);
        if (images && images->text() == srcNode)
        {
            for (int j = 0; j < images->rowCount(); j++)
            {
                QStandardItem* childPathItem = images->child(j, 1);
                if (childPathItem)
                {
                    QFileInfo fileinfo(childPathItem->text());
                    QString origin_name = fileinfo.baseName();
                    origin.append(origin_name);
                    SAR_images.push_back(childPathItem->text().toStdString());
                    SAR_images_deburst.push_back(QString("%1/%2/%3_deburst.h5").arg(savePath).arg(dstNode)
                        .arg(origin_name).toStdString());
                }
            }
        }
    }
    
    if (SAR_images.empty())
    {
        emit errorProcess(QStringLiteral("无可处理数据"));
        return;
    }

    emit updateProcess(10, QStringLiteral("开始burst拼接……"));
    // burst拼接
    for (size_t i = 1; i <= SAR_images.size(); i++)
    {
        if (QThread::currentThread()->isInterruptionRequested())
        {
            emit errorProcess(QStringLiteral("用户取消操作"));
            return;
        }
        Sentinel1Utils su(SAR_images[i - 1].c_str());
        ret = su.init();
        if (ret < 0) {
            emit errorProcess(QStringLiteral("Sentinel1Utils 初始化失败"));
            return;
        }
        ret = su.deburst(SAR_images_deburst[i - 1].c_str());
        if (ret < 0) {
            emit errorProcess(QStringLiteral("deburst 拼接失败"));
            return;
        }
        
        // 拼接成功后，立刻生成 JPG 预览图（在后台线程中执行）
        QString h5Path = QString::fromStdString(SAR_images_deburst[i - 1]);
        QFileInfo fi(h5Path);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        NodeUtils::generateJpgPreviewFromH5(h5Path, jpgPath, "complex");

        emit updateProcess(int(10.0 + 80.0 / SAR_images.size() * i), QStringLiteral("burst拼接进度%1%").arg(int(10.0 + 80.0 / SAR_images.size() * i)));
    }
    
    if (QThread::currentThread()->isInterruptionRequested())
    {
        emit errorProcess(QStringLiteral("用户取消操作"));
        return;
    }

    /*建立deburst根节点*/
    QStandardItem* deburst = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == dstNode)
        {
            deburst = project->child(i, 0);
            break;
        }
    }

    if (!deburst)
    {
        deburst = new QStandardItem(dstNode);
        deburst->setToolTip(dstProject);
        int insert = 0;
        for (; insert < project->rowCount(); insert++)
        {
            if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-1.0") == 0)
                continue;
            else
                break;
        }
        deburst->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, deburst);
        QStandardItem* deburst_Rank = new QStandardItem("complex-1.0");
        project->setChild(insert, 1, deburst_Rank);
    }
    
    /*写入XML*/
    XMLFile* xmlfile = new XMLFile();
    emit updateProcess(95, QStringLiteral("写入工程文件……"));
    xmlfile->XMLFile_load((savePath + "/" + dstProject).toStdString().c_str());
    for (size_t i = 0; i < SAR_images_deburst.size(); i++)
    {
        QFileInfo fileinfo = QFileInfo(QString(SAR_images_deburst.at(i).c_str()));
        QString deburst_name = fileinfo.baseName();
        QStandardItem* item_img = nullptr;
        for (int j = 0; j < deburst->rowCount(); j++)
        {
            if (deburst->child(j, 0)->text() == deburst_name)
            {
				item_img = deburst->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* deburst_images_name = new QStandardItem(deburst_name);
            deburst_images_name->setToolTip("complex");
            QStandardItem* deburst_images_path = new QStandardItem(fileinfo.absoluteFilePath());
            deburst_images_name->setIcon(QIcon(IMAGEDATA_ICON));
            deburst->appendRow(deburst_images_name);
            deburst->setChild(deburst->rowCount() - 1, 1, deburst_images_path);
        }
        else
        {
            deburst->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
        }

        QString relativePath = QString("/%1/%2").arg(dstNode).arg(origin.at(i) + "_deburst.h5");
        ret = xmlfile->XMLFile_add_S1_Deburst(dstNode.toStdString().c_str(), (origin.at(i) + "_deburst").toStdString().c_str(),
            relativePath.toStdString().c_str());
    }
    xmlfile->XMLFile_save((savePath + "/" + dstProject).toStdString().c_str());
    delete xmlfile;

    emit sendModel(model);
    InSARLogManager::LogInfo("S1DeburstWorker", "S1_Deburst finished successfully.");
    emit endProcess();
}

#include "DemWorker.h"
#include "NodeUtils.h"
#include <Dem.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QStandardItem>
#include <QDebug>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Dem_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Dem.lib")
#endif

using namespace cv;
using namespace std;

thread_local DemWorker* t_activeDemWorker = nullptr;
thread_local int t_activeDemImageIndex = 0;
thread_local int t_totalDemImagesCount = 1;
thread_local int t_lastLoggedDemProgress = -10;

static bool __stdcall demProgressCallback(int progress, const char* message)
{
    thread_local QElapsedTimer s_cbTimer;
    thread_local bool s_timerStarted = false;
    if (!s_timerStarted) {
        s_cbTimer.start();
        s_timerStarted = true;
    }
    if (progress != 0 && progress != 100 && s_cbTimer.elapsed() < 100) {
        return true;
    }
    s_cbTimer.restart();

    if (t_activeDemWorker)
    {
        if (t_activeDemWorker->thread()->isInterruptionRequested() || t_activeDemWorker->isStopRequested())
        {
            return false;
        }

        int start_prog = 10 + t_activeDemImageIndex * 80 / t_totalDemImagesCount;
        int end_prog = 10 + (t_activeDemImageIndex + 1) * 80 / t_totalDemImagesCount;
        int mapped_prog = start_prog + progress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_activeDemWorker->updateProcess(mapped_prog, QStringLiteral("第%1幅图像高程反演中：%2% (%3)")
            .arg(t_activeDemImageIndex + 1).arg(progress).arg(msgStr));

        if (progress == 0 || progress == 100 || (progress - t_lastLoggedDemProgress) >= 10)
        {
            InSARLogManager::LogInfo("DemWorker", QString("Dem progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
            t_lastLoggedDemProgress = progress;
        }
    }
    return true;
}

struct DemThreadLocalGuard {
    DemThreadLocalGuard(DemWorker* worker, int total) {
        t_activeDemWorker = worker;
        t_activeDemImageIndex = 0;
        t_totalDemImagesCount = total;
        t_lastLoggedDemProgress = -10;
    }
    ~DemThreadLocalGuard() {
        t_activeDemWorker = nullptr;
        t_activeDemImageIndex = 0;
        t_totalDemImagesCount = 1;
        t_lastLoggedDemProgress = -10;
    }
};

DemWorker::DemWorker(QObject* parent)
    : BaseWorker(parent)
{
}

DemWorker::~DemWorker()
{
}

void DemWorker::Dem(int method, int times, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
    const auto finishCancelled = [this]() { Q_EMIT cancelled(); };
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("DemWorker", QString("DEM Generation task started. Output folder: %1, Method: %2, Iterations: %3").arg(file_name).arg(method).arg(times));

    if (save_path.isEmpty() ||
        project_name.isEmpty() ||
        node_name.isEmpty() ||
        file_name.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }

    QString absolute_path = save_path + "/" + file_name;
    QDir target_dir(absolute_path);
    if (target_dir.exists())
    {
        target_dir.removeRecursively();
    }
    QDir(save_path).mkdir(file_name);

    QList<QStandardItem*> foundProjects = model->findItems(project_name);
    if (foundProjects.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到对应的工程: ") + project_name);
        return;
    }

    QStandardItem* project = foundProjects.first();
    QStandardItem* node = NULL;
    QList<QString> phase_name;
    QList<QString> phase_path;
    QList<QString> dem_name;
    QList<QString> relative_dem_path;
    QList<QString> absolute_dem_path;

    emit updateProcess(10, QStringLiteral("准备数据……"));
    for (int i = 0; i < project->rowCount(); i++)
    {
        QString childName = project->child(i, 0)->text();
        QString childType = project->child(i, 1) ? project->child(i, 1)->text() : "";
        if (childName == node_name)
        {
            node = project->child(i, 0);
            for (int j = 0; j < node->rowCount(); j++)
            {
                QString imgName = node->child(j, 0)->text();
                QString imgTooltip = node->child(j, 0)->toolTip();
                QString imgPath = node->child(j, 1) ? node->child(j, 1)->text() : "";
                if (imgTooltip == "phase")
                {
                    QString change_name;
                    change_name = imgName + "_dem";
                    phase_name.append(imgName);
                    phase_path.append(imgPath);
                    dem_name.append(change_name);
                    relative_dem_path.append("/" + file_name + "/" + change_name + ".h5");
                    absolute_dem_path.append(save_path + "/" + file_name + "/" + change_name + ".h5");
                }
            }
            break;
        }
    }

    if (!node) {
        emit errorProcess(QStringLiteral("未找到指定的数据节点: ") + node_name);
        return;
    }

    /*建立根节点*/
    QStandardItem* Dem_node = NULL;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == file_name)
        {
            Dem_node = project->child(i, 0);
            break;
        }
    }

    if (!Dem_node)
    {
        Dem_node = new QStandardItem(file_name);
        Dem_node->setToolTip(project_name);
        int insert = 0;
        for (; insert < project->rowCount(); insert++)
        {
            if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-3.0") == 0 ||
                project->child(insert, 1)->text().compare("dem-1.0") == 0
                )
                continue;
            else
                break;
        }
        Dem_node->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, Dem_node);
        QStandardItem* Dem_node_Rank = new QStandardItem("dem-1.0");
        project->setChild(insert, 1, Dem_node_Rank);
    }

    int image_number = phase_name.size();
    DemThreadLocalGuard tlGuard(this, image_number);
    if (image_number == 0) {
        emit errorProcess(QStringLiteral("没有可解析的解缠相位图像"));
        return;
    }

    ::Dem dem;
    FormatConversion FC;
    Utils util;
    XMLFile xml;

    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) {
        xml_path += ".Insar";
    }

    if (xml.XMLFile_load(xml_path.toStdString().c_str()) < 0) {
        emit errorProcess(QStringLiteral("加载项目XML文件失败: ") + xml_path);
        return;
    }

    if (method == 1)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_activeDemImageIndex = i;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解析高程中……").arg(i + 1));
            
            QString inputH5 = phase_path.at(i);
            // If the path is relative, convert to absolute using save_path
            if (QDir::isRelativePath(inputH5)) {
                inputH5 = save_path + "/" + inputH5;
            }

            Mat phase;
            int ret = NodeUtils::readMatFromH5(inputH5, "phase", phase) ? 0 : -1;
            if (ret < 0) {
                emit errorProcess(QStringLiteral("读取相位数据失败，路径: ") + QFileInfo(inputH5).fileName());
                return;
            }

            // 预检测 flat_phase_coefficient，防止进入 DLL 的数值计算陷入死循环
            Mat tmp_flat_check;
            if (!NodeUtils::readMatFromH5(inputH5, "flat_phase_coefficient", tmp_flat_check)) {
                emit errorProcess(QStringLiteral("高程反演失败：输入相位文件缺少\"平地相位消除系数(flat_phase_coefficient)\"。请确保上游干涉形成阶段开启了\"平地消除(IsDeflat)\"。"));
                return;
            }

            Mat phase_dem;
            ret = dem.dem_newton_iter(inputH5.toStdString().c_str(), phase_dem, save_path.toStdString().c_str(), times, 1, demProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) {
                emit errorProcess(QStringLiteral("高程迭代反演算法失败，请确保上游\"干涉形成\"节点开启了\"平地消除(IsDeflat)\"。"));
                return;
            }

            /*写入h5*/
            QString outputH5 = absolute_dem_path.at(i);
            ret = FC.creat_new_h5(outputH5.toStdString().c_str());
            if (ret < 0) {
                emit errorProcess(QStringLiteral("创建输出文件失败，路径: ") + QFileInfo(outputH5).fileName());
                return;
            }

            NodeUtils::writeMatToH5(outputH5, "dem", phase_dem);
            NodeUtils::writeScalarToH5(outputH5, "dem_generation_method", method);
            NodeUtils::writeScalarToH5(outputH5, "dem_generation_iterations", times);
            
            string tmp_str;
            Mat tmp;
            NodeUtils::readStringFromH5(inputH5, "source_1", tmp_str);
            FC.write_str_to_h5(outputH5.toStdString().c_str(), "source_1", tmp_str.c_str());
            QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
            
            NodeUtils::readStringFromH5(inputH5, "source_2", tmp_str);
            FC.write_str_to_h5(outputH5.toStdString().c_str(), "source_2", tmp_str.c_str());
            QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
            
            QString phasePath = phase_path.at(i);
            QString demPath = absolute_dem_path.at(i);

            if (NodeUtils::readMatFromH5(phasePath, "flat_phase_coefficient", tmp) ||
                NodeUtils::readMatFromH5(phasePath, "flat_phase_coefficientficient", tmp)) 
            {
                NodeUtils::writeMatToH5(demPath, "flat_phase_coefficient", tmp);
            }

            NodeUtils::readMatFromH5(phasePath, "range_len", tmp);
            NodeUtils::writeMatToH5(demPath, "range_len", tmp);
            
            NodeUtils::readMatFromH5(phasePath, "azimuth_len", tmp);
            NodeUtils::writeMatToH5(demPath, "azimuth_len", tmp);
            
            NodeUtils::readMatFromH5(phasePath, "multilook_rg", tmp);
            NodeUtils::writeMatToH5(demPath, "multilook_rg", tmp);
            
            NodeUtils::readMatFromH5(phasePath, "multilook_az", tmp);
            NodeUtils::writeMatToH5(demPath, "multilook_az", tmp);
            
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            
            /*行列偏移量*/
            Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
            NodeUtils::readMatFromH5(master_path, "offset_row", tmp_int);
            int offset_row = tmp_int.at<int>(0, 0);
            NodeUtils::readMatFromH5(master_path, "offset_col", tmp_int);
            int offset_col = tmp_int.at<int>(0, 0);
            
            xml.XMLFile_add_dem(file_name.toStdString().c_str(), dem_name.at(i).toStdString().c_str(),
                relative_dem_path.at(i).toStdString().c_str(), offset_row, offset_col, "Iteration", times);

            if (Dem_node && Dem_node->model()) {
                QMetaObject::invokeMethod(Dem_node->model(), [=]() {
                    QStandardItem* item_img = NULL;
                    for (int j = 0; j < Dem_node->rowCount(); j++)
                    {
                        if (Dem_node->child(j, 0)->text() == dem_name.at(i))
                        {
                            item_img = Dem_node->child(j, 0);
                            break;
                        }
                    }

                    if (!item_img)
                    {
                        QStandardItem* image = new QStandardItem(dem_name.at(i));
                        image->setToolTip("dem");
                        image->setIcon(QIcon(IMAGEDATA_ICON));
                        Dem_node->appendRow(image);
                        QStandardItem* image_path = new QStandardItem(absolute_dem_path.at(i));
                        Dem_node->setChild(Dem_node->rowCount() - 1, 1, image_path);
                    }
                    else
                    {
                        Dem_node->setChild(item_img->row(), 1, new QStandardItem(absolute_dem_path.at(i)));
                    }
                }, Qt::BlockingQueuedConnection);
            }
        }
    }
    else
    {
        // Placeholder for other methods
    }

    xml.XMLFile_save(xml_path.toStdString().c_str());

    emit sendModel(model);
    InSARLogManager::LogInfo("DemWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

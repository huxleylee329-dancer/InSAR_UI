#include "DemWorker.h"
#include "NodeUtils.h"
#include <Dem.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QDir>
#include <QThread>
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

DemWorker::DemWorker(QObject* parent)
    : QObject(parent)
{
}

DemWorker::~DemWorker()
{
}

void DemWorker::Dem(int method, int times, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
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

    QDir dir(save_path);
    QString absolute_path;
    if (!dir.exists(file_name))
    {
        dir.mkdir(file_name);
        absolute_path = save_path + "/" + file_name;
    }
    else
    {
        dir.remove(file_name);
        dir.mkdir(file_name);
        absolute_path = save_path + "/" + file_name;
    }

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
            if (QThread::currentThread()->isInterruptionRequested())
            {
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解析高程中……").arg(i + 1));
            
            QString inputH5 = phase_path.at(i);
            // If the path is relative, convert to absolute using save_path
            if (QDir::isRelativePath(inputH5)) {
                inputH5 = save_path + "/" + inputH5;
            }

            Mat phase;
            int ret = FC.read_array_from_h5(inputH5.toStdString().c_str(), "phase", phase);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("读取相位数据失败，路径: ") + QFileInfo(inputH5).fileName());
                return;
            }

            // 预检测 flat_phase_coefficient，防止进入 DLL 的数值计算陷入死循环
            Mat tmp_flat_check;
            if (FC.read_array_from_h5(inputH5.toStdString().c_str(), "flat_phase_coefficient", tmp_flat_check) != 0) {
                emit errorProcess(QStringLiteral("高程反演失败：输入相位文件缺少\"平地相位消除系数(flat_phase_coefficient)\"。请确保上游干涉形成阶段开启了\"平地消除(IsDeflat)\"。"));
                return;
            }

            Mat phase_dem;
            ret = dem.dem_newton_iter(inputH5.toStdString().c_str(), phase_dem, save_path.toStdString().c_str(), times, 1);
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

            ret = FC.write_array_to_h5(outputH5.toStdString().c_str(), "dem", phase_dem);
            
            string tmp_str;
            Mat tmp;
            ret = FC.read_str_from_h5(inputH5.toStdString().c_str(), "source_1", tmp_str);
            ret = FC.write_str_to_h5(outputH5.toStdString().c_str(), "source_1", tmp_str.c_str());
            QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
            
            ret = FC.read_str_from_h5(inputH5.toStdString().c_str(), "source_2", tmp_str);
            ret = FC.write_str_to_h5(outputH5.toStdString().c_str(), "source_2", tmp_str.c_str());
            QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
            
            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
            if (ret < 0) {
                // Fallback check: in some files it might be flat_phase_coefficientficient
                ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficientficient", tmp);
            }
            if (ret == 0) {
                ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
            }

            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
            ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "range_len", tmp);
            
            ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
            ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
            
            FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
            FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
            
            FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
            FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "multilook_az", tmp);
            
            if (QThread::currentThread()->isInterruptionRequested())
            {
                return;
            }
            
            /*行列偏移量*/
            Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
            ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
            int offset_row = tmp_int.at<int>(0, 0);
            ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
            int offset_col = tmp_int.at<int>(0, 0);
            
            xml.XMLFile_add_dem(file_name.toStdString().c_str(), dem_name.at(i).toStdString().c_str(),
                relative_dem_path.at(i).toStdString().c_str(), offset_row, offset_col, "Iteration", times);

            /*工程树*/
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

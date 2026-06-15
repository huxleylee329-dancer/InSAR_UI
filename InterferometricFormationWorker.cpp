#include "InterferometricFormationWorker.h"
#include <Utils.h>
#include <Deflat.h>
#include <FormatConversion.h>
#include "Package.h"
#include "icon_source.h"
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include "InSARLogManager.h"
#include "NodeUtils.h"

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "Deflat_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "Deflat.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#endif

using namespace cv;
using namespace std;

InterferometricFormationWorker::InterferometricFormationWorker(QObject* parent)
    : QObject(parent)
{
}

InterferometricFormationWorker::~InterferometricFormationWorker()
{
}

void InterferometricFormationWorker::Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence,
                                                     int master_index, int win_width, int win_height,
                                                     int multilook_rg, int multilook_az, QString save_path,
                                                     QString project_name, QString node_name, QString file_name,
                                                     QStandardItemModel* model)
{

    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("InterferometricFormationWorker", QString("Interferometric task started. Output folder: %1").arg(file_name));
    
    FormatConversion FC;
    Deflat flat; 
    Utils util;
    
    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }
    
    QList<QStandardItem*> foundProjects = model->findItems(project_name);
    if (foundProjects.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到对应的工程: ") + project_name);
        return;
    }
    
    QStandardItem* project = foundProjects.first();
    save_path = model->item(project->row(), 1)->text();
    
    QStandardItem* origin_node = nullptr;
    QDir dir(save_path);
    if (!dir.exists(file_name))
    {
        dir.mkdir(file_name);
    }
    QString absolute_path = save_path + "/" + file_name;

    // 外部DEM文件夹
    QString appPath = QCoreApplication::applicationDirPath();
    QString demPath = appPath + "/dem";
    QDir appDir(appPath);
    if (!appDir.exists("dem")) appDir.mkdir("dem");

    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == node_name)
        {
            origin_node = project->child(i, 0);
            break;
        }
    }
    
    if (!origin_node) {
        emit errorProcess(QStringLiteral("未找到输入节点: ") + node_name);
        return;
    }
    
    if (master_index < 0 || master_index >= origin_node->rowCount()) {
        emit errorProcess(QStringLiteral("无效的主图像索引: %1").arg(master_index));
        return;
    }

    QString master_regis_name = origin_node->child(master_index, 0)->text();
    QString master_path = origin_node->child(master_index, 1)->text();
    QFileInfo fileinfo(master_path);
    QString master_name = fileinfo.baseName();
    
    /*建立根节点*/
    QStandardItem* interferometric_phase = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == file_name)
        {
            interferometric_phase = project->child(i, 0);
            break;
        }
    }

    if (!interferometric_phase)
    {
        interferometric_phase = new QStandardItem(file_name);
        interferometric_phase->setToolTip(project_name);
        int insert = 0;
        for (; insert < project->rowCount(); insert++)
        {
            if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
                project->child(insert, 1)->text().compare("phase-1.0") == 0)
                continue;
            else
                break;
        }
        interferometric_phase->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, interferometric_phase);
        QStandardItem* interferometric_phase_Rank = new QStandardItem("phase-1.0");
        project->setChild(insert, 1, interferometric_phase_Rank);
    }
    
    emit updateProcess(2, QStringLiteral("开始处理……"));

    ComplexMat Master;
    int ret = FC.read_slc_from_h5(master_path.toStdString().c_str(), Master);
    if (ret < 0) {
        emit errorProcess(QStringLiteral("读取主图像数据失败: ") + master_path);
        return;
    }
    
    Mat statevec, lon_coef, lat_coef, inc_coef, statevec2;
    double prf, prf2, rangeSpacing, wavelength, nearRangeTime, acquisitionStartTime, acquisitionStopTime;
    string start, end;
    int offset_row, offset_col, sceneHeight, sceneWidth;
    
    FC.read_array_from_h5(master_path.toStdString().c_str(), "state_vec", statevec);
    FC.read_array_from_h5(master_path.toStdString().c_str(), "lon_coefficient", lon_coef);
    FC.read_array_from_h5(master_path.toStdString().c_str(), "lat_coefficient", lat_coef);
    FC.read_array_from_h5(master_path.toStdString().c_str(), "inc_coefficient", inc_coef);
    FC.read_double_from_h5(master_path.toStdString().c_str(), "prf", &prf);
    FC.read_double_from_h5(master_path.toStdString().c_str(), "range_spacing", &rangeSpacing);
    FC.read_double_from_h5(master_path.toStdString().c_str(), "carrier_frequency", &wavelength);
    
    wavelength = VEL_C / wavelength;
    FC.read_int_from_h5(master_path.toStdString().c_str(), "offset_row", &offset_row);
    FC.read_int_from_h5(master_path.toStdString().c_str(), "offset_col", &offset_col);
    FC.read_int_from_h5(master_path.toStdString().c_str(), "range_len", &sceneWidth);
    FC.read_int_from_h5(master_path.toStdString().c_str(), "azimuth_len", &sceneHeight);
    FC.read_double_from_h5(master_path.toStdString().c_str(), "slant_range_first_pixel", &nearRangeTime);
    
    nearRangeTime = nearRangeTime / VEL_C * 2.0;
    FC.read_str_from_h5(master_path.toStdString().c_str(), "acquisition_start_time", start);
    FC.read_str_from_h5(master_path.toStdString().c_str(), "acquisition_stop_time", end);
    FC.utc2gps(start.c_str(), &acquisitionStartTime);
    FC.utc2gps(end.c_str(), &acquisitionStopTime);
    

    // 地理编码信息
    Mat mapped_lon, mapped_lat;
    bool b_mapped = false;
    if (multilook_az > 1 || multilook_rg > 1)
    {
        int rows_mapped = sceneHeight / multilook_az;
        int cols_mapped = sceneWidth / multilook_rg;
        
        if (0 == FC.read_array_from_h5(master_path.toStdString().c_str(), "mapped_lon", mapped_lon))
        {
            Mat lon_new(rows_mapped, cols_mapped, CV_32F);
            for (int i = 0; i < rows_mapped; i++)
            {
                for (int j = 0; j < cols_mapped; j++)
                {
                    lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
                        cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                }
            }
            lon_new.copyTo(mapped_lon);
            
            if (0 == FC.read_array_from_h5(master_path.toStdString().c_str(), "mapped_lat", mapped_lat))
            {
                for (int i = 0; i < rows_mapped; i++)
                {
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
                            cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
                    }
                }
                lon_new.copyTo(mapped_lat);
                b_mapped = true;
            }
        }
    }
    
    XMLFile xml;
    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) {
        xml_path += ".Insar";
    }
    int xml_ret = xml.XMLFile_load(xml_path.toStdString().c_str());
    if (xml_ret != 0) {
        InSARLogManager::LogError("InterferometricFormationWorker", "Failed to load project XML file: " + xml_path);
        emit errorProcess(QStringLiteral("加载项目XML文件失败: ") + xml_path);
        return;
    }
    
    int count = origin_node->rowCount();
    int pair = 1;
    for (int i = 0; i < count; i++)
    {
        if (i == master_index)
        {
            continue;
        }
        else
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                InSARLogManager::LogInfo("InterferometricFormationWorker", "Task cancelled by interruption request.");
                return;
            }
            QString slave_regis_name = origin_node->child(i, 0)->text();
            QString slave_path = origin_node->child(i, 1)->text();
            fileinfo = slave_path;
            QString slave_name = fileinfo.baseName();

            QString h5_path = QString("%1/%2/%3_%4.h5").arg(save_path).arg(file_name).arg(master_name).arg(slave_name);
            QString h5_name = QString("%1_%2").arg(master_name).arg(slave_name);
            QString phase_name = QString("%1_%2_phase").arg(master_name).arg(slave_name);
            QString coh_name = QString("%1_%2_coh").arg(master_name).arg(slave_name);
            
            ComplexMat Slave;
            Mat phase;
            ret = FC.read_slc_from_h5(slave_path.toStdString().c_str(), Slave);
            if (ret < 0) {
                InSARLogManager::LogError("InterferometricFormationWorker", "Failed to read slave image: " + slave_path);
                continue;
            }
            
            if (Master.type() != CV_32F) Master.convertTo(Master, CV_32F);
            if (Slave.type() != CV_32F) Slave.convertTo(Slave, CV_32F);
            ret = util.Multilook(Master, Slave, 1, 1, phase);
            
            /*写入h5*/
            ret = FC.creat_new_h5(h5_path.toStdString().c_str());
            QString master_relative_path = "/" + node_name + "/" + master_regis_name + ".h5";
            QString slave_relative_path = "/" + node_name + "/" + slave_regis_name + ".h5";
            ret = FC.write_str_to_h5(h5_path.toStdString().c_str(), "source_1", master_relative_path.toStdString().c_str());
            ret = FC.write_str_to_h5(h5_path.toStdString().c_str(), "source_2", slave_relative_path.toStdString().c_str());
            ret = FC.read_array_from_h5(slave_path.toStdString().c_str(), "state_vec", statevec2);
            ret = FC.read_double_from_h5(slave_path.toStdString().c_str(), "prf", &prf2);
            Mat phase_deflatted, flat_phase_coefficient;
            
            if (isdeflat)
            {

                int ret_deflat = flat.deflat(statevec, statevec2, lon_coef, lat_coef, phase, offset_row, offset_col, 0,
                    1 / prf, 1 / prf2, 1, wavelength, phase_deflatted, flat_phase_coefficient);
                phase_deflatted.copyTo(phase);
                if (ret_deflat < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Deflat process failed.");
                    emit errorProcess(QStringLiteral("平地相位消除失败"));
                    return;
                }
                FC.write_array_to_h5(h5_path.toStdString().c_str(), "flat_phase_coefficient", flat_phase_coefficient);
            }
            if (istopo_removal)
            {
                int ret_topo = flat.topography_simulation(phase_deflatted, statevec, statevec2, lon_coef, lat_coef, inc_coef, prf, prf2,
                    sceneHeight, sceneWidth, offset_row, offset_col, nearRangeTime, rangeSpacing, wavelength,
                    acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str());
                if (ret_topo < 0) {
                    InSARLogManager::LogWarning("InterferometricFormationWorker", "Topography simulation failed.");
                }
                else {
                    phase_deflatted = phase - phase_deflatted;
                    util.wrap(phase_deflatted, phase);
                }
            }
            if (multilook_rg > 1 || multilook_az > 1)
            {
                util.multilook(phase, phase_deflatted, multilook_rg, multilook_az);
                phase_deflatted.copyTo(phase);
            }
            if (b_mapped)
            {
                FC.write_array_to_h5(h5_path.toStdString().c_str(), "mapped_lon", mapped_lon);
                FC.write_array_to_h5(h5_path.toStdString().c_str(), "mapped_lat", mapped_lat);
            }
            FC.write_int_to_h5(h5_path.toStdString().c_str(), "azimuth_len", phase.rows);
            FC.write_int_to_h5(h5_path.toStdString().c_str(), "range_len", phase.cols);
            FC.write_int_to_h5(h5_path.toStdString().c_str(), "multilook_rg", multilook_rg);
            FC.write_int_to_h5(h5_path.toStdString().c_str(), "multilook_az", multilook_az);
            FC.write_array_to_h5(h5_path.toStdString().c_str(), "phase", phase);
            FC.write_double_to_h5(h5_path.toStdString().c_str(), "range_spacing", rangeSpacing);
            FC.write_double_to_h5(h5_path.toStdString().c_str(), "slant_range_first_pixel", nearRangeTime / 2.0 * VEL_C);
            FC.write_double_to_h5(h5_path.toStdString().c_str(), "prf", prf);
            FC.write_str_to_h5(h5_path.toStdString().c_str(), "acquisition_start_time", start.c_str());
            FC.write_str_to_h5(h5_path.toStdString().c_str(), "acquisition_stop_time", end.c_str());
            
            if (interferometric_phase && interferometric_phase->model()) {
                QMetaObject::invokeMethod(interferometric_phase->model(), [=, &xml]() {
                    QStandardItem* item_img = nullptr;
                    for (int j = 0; j < interferometric_phase->rowCount(); j++)
                    {
                        if (interferometric_phase->child(j, 0)->text() == phase_name)
                        {
                            item_img = interferometric_phase->child(j, 0);
                            break;
                        }
                    }

                    if (!item_img)
                    {
                        QStandardItem* interferometric_phase_name = new QStandardItem(phase_name);
                        interferometric_phase_name->setToolTip("phase");
                        QStandardItem* interferometric_phase_path = new QStandardItem(h5_path);
                        interferometric_phase_path->setToolTip(h5_name);
                        interferometric_phase_name->setIcon(QIcon(IMAGEDATA_ICON));
                        interferometric_phase->appendRow(interferometric_phase_name);
                        interferometric_phase->setChild(interferometric_phase->rowCount() - 1, 1, interferometric_phase_path);

                        xml.XMLFile_add_interferometric_phase(file_name.toStdString().c_str(), phase_name.toStdString().c_str(),
                            ("/" + file_name + "/" + h5_name + ".h5").toStdString().c_str(), master_name.toStdString().c_str(), "phase-1.0", offset_row, offset_col,
                            isdeflat, istopo_removal, iscoherence, win_width, win_height, multilook_rg, multilook_az);
                    }
                    else
                    {
                        interferometric_phase->setChild(item_img->row(), 1, new QStandardItem(h5_path));
                    }
                }, Qt::BlockingQueuedConnection);
            }

            if (iscoherence)
            {
                if (QThread::currentThread()->isInterruptionRequested())
                {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task cancelled by interruption request inside coherence block.");
                    return;
                }
                Mat coherence;
                util.phase_coherence(phase, win_width, win_height, coherence);

                if (interferometric_phase && interferometric_phase->model()) {
                    QMetaObject::invokeMethod(interferometric_phase->model(), [=, &xml]() {
                        QStandardItem* item_coh = nullptr;
                        for (int j = 0; j < interferometric_phase->rowCount(); j++)
                        {
                            if (interferometric_phase->child(j, 0)->text() == coh_name)
                            {
                                item_coh = interferometric_phase->child(j, 0);
                                break;
                            }
                        }

                        if (!item_coh)
                        {
                            QStandardItem* coherence_name = new QStandardItem(coh_name);
                            coherence_name->setToolTip("coherence");
                            QStandardItem* coherence_path = new QStandardItem(h5_path);
                            coherence_path->setToolTip(h5_name);
                            coherence_name->setIcon(QIcon(IMAGEDATA_ICON));
                            interferometric_phase->appendRow(coherence_name);
                            interferometric_phase->setChild(interferometric_phase->rowCount() - 1, 1, coherence_path);

                            xml.XMLFile_add_interferometric_phase(file_name.toStdString().c_str(), coh_name.toStdString().c_str(),
                                ("/" + file_name + "/" + h5_name + ".h5").toStdString().c_str(), master_name.toStdString().c_str(), "coherence-1.0", offset_row, offset_col,
                                isdeflat, istopo_removal, iscoherence, win_width, win_height, multilook_rg, multilook_az);
                        }
                        else
                        {
                            interferometric_phase->setChild(item_coh->row(), 1, new QStandardItem(h5_path));
                        }
                    }, Qt::BlockingQueuedConnection);
                }
                FC.write_array_to_h5(h5_path.toStdString().c_str(), "coherence", coherence);
            }
            
            int prog = 10 + pair * 80 / (count - 1);
            emit updateProcess(prog, QStringLiteral("生成第%1幅干涉图……").arg(pair));
            pair++;
        }
    }
    
    xml.XMLFile_save(xml_path.toStdString().c_str());

    emit sendModel(model);
    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task completed: Interferometric");
    emit endProcess();
}

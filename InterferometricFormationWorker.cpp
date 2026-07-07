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
#include <QElapsedTimer>
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

// 用于 DLL 进度回调与取消的全局线程局部变量及桥接实现
static thread_local InterferometricFormationWorker* current_worker = nullptr;
static thread_local int g_substep_prog_start = 0;
static thread_local int g_substep_prog_end = 0;
static thread_local QString g_current_pair_info;

static bool __stdcall DeflatProgressCallbackImpl(int progress, const char* message) {
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

    if (current_worker) {
        if (current_worker->isStopRequested() || QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }
        int mapped_prog = g_substep_prog_start + (progress * (g_substep_prog_end - g_substep_prog_start)) / 100;
        QString info = g_current_pair_info;
        if (message && message[0] != '\0') {
            info += QString(" (%1)").arg(QString::fromUtf8(message));
        }
        emit current_worker->updateProcess(mapped_prog, info);
    }
    return true;
}

struct WorkerResetGuard {
    ~WorkerResetGuard() {
        current_worker = nullptr;
    }
};

InterferometricFormationWorker::InterferometricFormationWorker(QObject* parent)
    : BaseWorker(parent)
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
    InterferometricWithDem(isdeflat, istopo_removal, iscoherence, master_index, win_width, win_height,
                           multilook_rg, multilook_az, save_path, project_name, node_name, file_name,
                           model, QString());
}

void InterferometricFormationWorker::InterferometricWithDem(bool isdeflat, bool istopo_removal, bool iscoherence,
                                                            int master_index, int win_width, int win_height,
                                                            int multilook_rg, int multilook_az, QString save_path,
                                                            QString project_name, QString node_name, QString file_name,
                                                            QStandardItemModel* model, QString dem_path)
{
    current_worker = this;
    WorkerResetGuard reset_guard;

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

    // 外部DEM文件夹或文件路径
    QString demPath = dem_path;
    if (demPath.isEmpty()) {
        QString appPath = QCoreApplication::applicationDirPath();
        demPath = appPath + "/dem";
        QDir appDir(appPath);
        if (!appDir.exists("dem")) appDir.mkdir("dem");
    }

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
    QStandardItem* interferometric_phase = NodeUtils::findOrCreateProjectNode(project, file_name, "phase-1.0");
    if (interferometric_phase)
    {
        interferometric_phase->setToolTip(project_name);
    }
    
    emit updateProcess(2, QStringLiteral("正在读取主影像SLC数据……"));

    ComplexMat Master;
    int ret = FC.read_slc_from_h5(master_path.toStdString().c_str(), Master);
    if (ret < 0) {
        emit errorProcess(QStringLiteral("读取主图像数据失败: ") + master_path);
        return;
    }
    
    emit updateProcess(5, QStringLiteral("正在解析主影像元数据……"));
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
        emit updateProcess(7, QStringLiteral("正在多视降采样地理坐标矩阵……"));
        int rows_mapped = sceneHeight / multilook_az;
        int cols_mapped = sceneWidth / multilook_rg;
        
        if (0 == FC.read_array_from_h5(master_path.toStdString().c_str(), "mapped_lon", mapped_lon))
        {
            // ================= 算法块 1: 使用 OpenCV 高性能内置区域插值 (当前启用) =================
            cv::resize(mapped_lon, mapped_lon, cv::Size(cols_mapped, rows_mapped), 0, 0, cv::INTER_AREA);
            
            /*
            // ================= 算法块 2: 100% 数值等价的高性能指针迭代版 (已注释，可切换测试) =================
            // 说明：如需切换至 100% 字节等价版本，请注释掉上面的 cv::resize，并取消本段注释
            {
                Mat lon_new(rows_mapped, cols_mapped, CV_32F);
                float pixel_count = static_cast<float>(multilook_az * multilook_rg);
                for (int i = 0; i < rows_mapped; i++)
                {
                    float* dst_row = lon_new.ptr<float>(i);
                    for (int j = 0; j < cols_mapped; j++)
                    {
                        float sum = 0.0f;
                        for (int r = 0; r < multilook_az; r++)
                        {
                            const float* src_row = mapped_lon.ptr<float>(i * multilook_az + r);
                            for (int c = 0; c < multilook_rg; c++)
                            {
                                sum += src_row[j * multilook_rg + c];
                            }
                        }
                        dst_row[j] = sum / pixel_count;
                    }
                }
                lon_new.copyTo(mapped_lon);
            }
            */
            
            if (0 == FC.read_array_from_h5(master_path.toStdString().c_str(), "mapped_lat", mapped_lat))
            {
                // ================= 算法块 1: 使用 OpenCV 高性能内置区域插值 (当前启用) =================
                cv::resize(mapped_lat, mapped_lat, cv::Size(cols_mapped, rows_mapped), 0, 0, cv::INTER_AREA);
                
                /*
                // ================= 算法块 2: 100% 数值等价的高性能指针迭代版 (已注释，可切换测试) =================
                // 说明：如需切换至 100% 字节等价版本，请注释掉上面的 cv::resize，并取消本段注释
                {
                    Mat lat_new(rows_mapped, cols_mapped, CV_32F);
                    float pixel_count = static_cast<float>(multilook_az * multilook_rg);
                    for (int i = 0; i < rows_mapped; i++)
                    {
                        float* dst_row = lat_new.ptr<float>(i);
                        for (int j = 0; j < cols_mapped; j++)
                        {
                            float sum = 0.0f;
                            for (int r = 0; r < multilook_az; r++)
                            {
                                const float* src_row = mapped_lat.ptr<float>(i * multilook_az + r);
                                for (int c = 0; c < multilook_rg; c++)
                                {
                                    sum += src_row[j * multilook_rg + c];
                                }
                            }
                            dst_row[j] = sum / pixel_count;
                        }
                    }
                    lat_new.copyTo(mapped_lat);
                }
                */
                b_mapped = true;
            }
        }
    }
    
    emit updateProcess(9, QStringLiteral("正在加载项目配置文件……"));
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
    int total_pairs = count - 1;
    if (total_pairs <= 0) total_pairs = 1;
    
    int pair = 1;
    for (int i = 0; i < count; i++)
    {
        if (i == master_index)
        {
            continue;
        }
        else
        {
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
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
            
            int pair_span = 90 / total_pairs;
            int pair_prog_start = 10 + (pair - 1) * pair_span;
            int pair_prog_end = 10 + pair * pair_span;
            
            emit updateProcess(pair_prog_start, QStringLiteral("生成第%1/%2幅干涉图：正在读取辅影像数据……").arg(pair).arg(total_pairs));
            ComplexMat Slave;
            Mat phase;
            ret = FC.read_slc_from_h5(slave_path.toStdString().c_str(), Slave);
            if (ret < 0) {
                InSARLogManager::LogError("InterferometricFormationWorker", "Failed to read slave image: " + slave_path);
                continue;
            }
            
            emit updateProcess(pair_prog_start + pair_span * 0.1, QStringLiteral("生成第%1/%2幅干涉图：正在计算多视相干乘积……").arg(pair).arg(total_pairs));
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
                // 配置回调映射范围并绑定全局变量
                g_substep_prog_start = pair_prog_start + pair_span * 0.2;
                g_substep_prog_end = pair_prog_start + pair_span * 0.4;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在消除平地相位").arg(pair).arg(total_pairs);

                int ret_deflat = flat.deflat(statevec, statevec2, lon_coef, lat_coef, phase, offset_row, offset_col, 0,
                    1 / prf, 1 / prf2, 1, wavelength, phase_deflatted, flat_phase_coefficient, DeflatProgressCallbackImpl);
                phase_deflatted.copyTo(phase);
                
                if (ret_deflat == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Deflat process cancelled by user.");
                    return;
                }
                else if (ret_deflat < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Deflat process failed.");
                    emit errorProcess(QStringLiteral("平地相位消除失败"));
                    return;
                }
                FC.write_array_to_h5(h5_path.toStdString().c_str(), "flat_phase_coefficient", flat_phase_coefficient);
            }
            if (istopo_removal)
            {
                // 配置回调映射范围并绑定全局变量
                g_substep_prog_start = pair_prog_start + pair_span * 0.4;
                g_substep_prog_end = pair_prog_start + pair_span * 0.8;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在进行地形相位模拟").arg(pair).arg(total_pairs);

                int ret_topo = flat.topography_simulation(phase_deflatted, statevec, statevec2, lon_coef, lat_coef, inc_coef, prf, prf2,
                    sceneHeight, sceneWidth, offset_row, offset_col, nearRangeTime, rangeSpacing, wavelength,
                    acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str(), 20, DeflatProgressCallbackImpl);

                if (ret_topo == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Topography simulation cancelled by user.");
                    return;
                }
                else if (ret_topo < 0) {
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
                // 配置回调参数映射范围并绑定全局变量
                g_substep_prog_start = pair_prog_start + pair_span * 0.8;
                g_substep_prog_end = pair_prog_start + pair_span * 0.98;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在计算干涉相干系数").arg(pair).arg(total_pairs);

                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task cancelled by interruption request inside coherence block.");
                    return;
                }
                Mat coherence;
                int ret_coh = util.phase_coherence(phase, win_width, win_height, coherence, DeflatProgressCallbackImpl);
                if (ret_coh == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Coherence calculation cancelled by user.");
                    return;
                }
                else if (ret_coh < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Coherence calculation failed.");
                    emit errorProcess(QStringLiteral("相干系数计算失败"));
                    return;
                }

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
            
            emit updateProcess(pair_prog_end, QStringLiteral("生成第%1/%2幅干涉图已完成").arg(pair).arg(total_pairs));
            pair++;
        }
    }
    
    xml.XMLFile_save(xml_path.toStdString().c_str());

    emit sendModel(model);
    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task completed: Interferometric");
    emit endProcess();
}

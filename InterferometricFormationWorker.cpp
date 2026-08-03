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
#include <cmath>
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
    qRegisterMetaType<InterferogramFileResult>("InterferogramFileResult");
}

InterferometricFormationWorker::~InterferometricFormationWorker()
{
}

void InterferometricFormationWorker::Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence,
                                                     int master_index, int win_width, int win_height,
                                                     int multilook_rg, int multilook_az, QString save_path,
                                                     QString file_name, QStringList input_paths,
                                                     bool outputDirectoryIsStaging)
{
    InterferometricWithDem(isdeflat, istopo_removal, iscoherence, master_index, win_width, win_height,
                           multilook_rg, multilook_az, save_path, file_name, input_paths,
                           QString(), outputDirectoryIsStaging);
}

void InterferometricFormationWorker::InterferometricWithDem(bool isdeflat, bool istopo_removal, bool iscoherence,
                                                            int master_index, int win_width, int win_height,
                                                            int multilook_rg, int multilook_az, QString save_path,
                                                            QString file_name, QStringList input_paths,
                                                            QString dem_path,
                                                            bool outputDirectoryIsStaging)
{
    ScopedTaskLogContext taskLogContextGuard(m_taskLogContext);
    current_worker = this;
    WorkerResetGuard reset_guard;

    InSARLogManager::LogTaskEvent(m_taskLogContext, InSARLogManager::LevelDebug,
                                  "InterferometricFormationWorker", QStringLiteral("干涉形成 Worker 已启动。"),
                                  LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile,
                                  QStringLiteral("worker_started"), QStringLiteral("running"));
    InSARLogManager::LogInfo("InterferometricFormationWorker", QString("Interferometric task started. Output folder: %1").arg(file_name));

    FormatConversion FC;
    Deflat flat; 
    Utils util;
    const auto writeArray = [this, &FC](const QString& h5Path, const char* dataset, const Mat& value) {
        if (FC.write_array_to_h5(h5Path.toStdString().c_str(), dataset, value) >= 0) {
            return true;
        }
        emit errorProcess(QStringLiteral("写入干涉H5数据集失败: %1 (%2)")
                          .arg(QString::fromLatin1(dataset), h5Path));
        return false;
    };
    if (save_path.isEmpty() || file_name.isEmpty() || input_paths.isEmpty()) {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
        emit cancelled();
        return;
    }

    QDir dir(save_path);
    if (outputDirectoryIsStaging) {
        if (!dir.exists(file_name)) {
            emit errorProcess(QStringLiteral("staging输出目录不存在: %1").arg(dir.absoluteFilePath(file_name)));
            return;
        }
    } else if (!dir.exists(file_name) && !dir.mkpath(file_name)) {
        emit errorProcess(QStringLiteral("无法创建输出目录: %1").arg(dir.absoluteFilePath(file_name)));
        return;
    }
    QString absolute_path = save_path + "/" + file_name;

    QString demPath = dem_path;
    if (demPath.isEmpty()) {
        demPath = QDir::toNativeSeparators(save_path + "/.dem_cache");
    }

    if (master_index < 0 || master_index >= input_paths.size()) {
        emit errorProcess(QStringLiteral("无效的主图像索引: %1").arg(master_index));
        return;
    }

    QString master_path = input_paths.at(master_index);

    QFileInfo fileinfo(master_path);
    QString master_name = fileinfo.baseName();

    emit updateProcess(2, QStringLiteral("正在读取主影像SLC数据……"));

    ComplexMat Master;
    int ret = 0;
    {
        NodeUtils::Hdf5Locker locker;
        ret = FC.read_slc_from_h5(master_path.toStdString().c_str(), Master);
    }
    if (ret < 0) {
        emit errorProcess(QStringLiteral("读取主图像数据失败: ") + master_path);
        return;
    }
    
    emit updateProcess(5, QStringLiteral("正在解析主影像元数据……"));
    Mat statevec, lon_coef, lat_coef, inc_coef, statevec2;
    double prf = 0.0, prf2 = 0.0, rangeSpacing = 0.0, wavelength = 0.0;
    double nearRangeTime = 0.0, acquisitionStartTime = 0.0, acquisitionStopTime = 0.0;
    string start, end;
    int offset_row = 0, offset_col = 0, sceneHeight = 0, sceneWidth = 0;
    bool masterMetadataRead = false;
    
    {
        NodeUtils::Hdf5Locker locker;
        masterMetadataRead =
            NodeUtils::readMatFromH5(master_path, "state_vec", statevec) &&
            NodeUtils::readMatFromH5(master_path, "lon_coefficient", lon_coef) &&
            NodeUtils::readMatFromH5(master_path, "lat_coefficient", lat_coef) &&
            NodeUtils::readMatFromH5(master_path, "inc_coefficient", inc_coef) &&
            NodeUtils::readScalarFromH5(master_path, "prf", prf) &&
            NodeUtils::readScalarFromH5(master_path, "range_spacing", rangeSpacing) &&
            NodeUtils::readScalarFromH5(master_path, "carrier_frequency", wavelength) &&
            NodeUtils::readScalarFromH5(master_path, "offset_row", offset_row) &&
            NodeUtils::readScalarFromH5(master_path, "offset_col", offset_col) &&
            NodeUtils::readScalarFromH5(master_path, "range_len", sceneWidth) &&
            NodeUtils::readScalarFromH5(master_path, "azimuth_len", sceneHeight) &&
            NodeUtils::readScalarFromH5(master_path, "slant_range_first_pixel", nearRangeTime) &&
            NodeUtils::readStringFromH5(master_path, "acquisition_start_time", start) &&
            NodeUtils::readStringFromH5(master_path, "acquisition_stop_time", end);
    }

    if (!masterMetadataRead || statevec.empty() || lon_coef.empty() || lat_coef.empty() || inc_coef.empty() ||
        !std::isfinite(prf) || !std::isfinite(rangeSpacing) || !std::isfinite(wavelength) || !std::isfinite(nearRangeTime) ||
        prf <= 0.0 || rangeSpacing <= 0.0 || wavelength <= 0.0 || nearRangeTime <= 0.0 ||
        sceneHeight <= 0 || sceneWidth <= 0) {
        const QString error = QStringLiteral("Master image metadata is incomplete or invalid: %1").arg(master_path);
        InSARLogManager::LogError("InterferometricFormationWorker", error);
        emit errorProcess(error);
        return;
    }

    wavelength = VEL_C / wavelength;
    nearRangeTime = nearRangeTime / VEL_C * 2.0;
    if (FC.utc2gps(start.c_str(), &acquisitionStartTime) != 0 ||
        FC.utc2gps(end.c_str(), &acquisitionStopTime) != 0 ||
        !std::isfinite(acquisitionStartTime) || !std::isfinite(acquisitionStopTime) ||
        (istopo_removal && acquisitionStopTime <= acquisitionStartTime)) {
        const QString error = QStringLiteral("Invalid master acquisition time range: start=%1, stop=%2, file=%3")
            .arg(QString::fromStdString(start), QString::fromStdString(end), master_path);
        InSARLogManager::LogError("InterferometricFormationWorker", error);
        emit errorProcess(error);
        return;
    }
    
    int total_pairs = input_paths.size() - 1;
    if (total_pairs <= 0) total_pairs = 1;
    int pair = 1;

    for (int i = 0; i < input_paths.size(); i++)
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
                emit cancelled();
                return;
            }
            QString slave_path = input_paths.at(i);

            QFileInfo slave_fileinfo(slave_path);
            QString slave_name = slave_fileinfo.baseName();

            QString h5_name = QString("%1_%2").arg(master_name).arg(slave_name);
            QString h5_path = absolute_path + "/" + h5_name + ".h5";
            QString phase_name = QString("%1_%2_phase").arg(master_name).arg(slave_name);
            QString coh_name = QString("%1_%2_coh").arg(master_name).arg(slave_name);
            
            int pair_span = 90 / total_pairs;
            int pair_prog_start = 10 + (pair - 1) * pair_span;
            int pair_prog_end = 10 + pair * pair_span;
            
            emit updateProcess(pair_prog_start, QStringLiteral("生成第%1/%2幅干涉图：正在读取辅影像数据……").arg(pair).arg(total_pairs));
            ComplexMat Slave;
            Mat phase;
            {
                NodeUtils::Hdf5Locker locker;
                ret = FC.read_slc_from_h5(slave_path.toStdString().c_str(), Slave);
            }
            if (ret < 0) {
                const QString error = QStringLiteral("读取辅影像数据失败: %1").arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            
            emit updateProcess(pair_prog_start + pair_span * 0.1, QStringLiteral("生成第%1/%2幅干涉图：正在计算多视相干乘积……").arg(pair).arg(total_pairs));
            if (Master.type() != CV_32F) Master.convertTo(Master, CV_32F);
            if (Slave.type() != CV_32F) Slave.convertTo(Slave, CV_32F);
            ret = util.Multilook(Master, Slave, 1, 1, phase);
            if (ret < 0 || phase.empty()) {
                const QString error = QStringLiteral("干涉相位计算失败: %1").arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            
            {
                NodeUtils::Hdf5Locker locker;
                ret = NodeUtils::readMatFromH5(slave_path, "state_vec", statevec2) ? 0 : -1;
                if (ret == 0) {
                    ret = NodeUtils::readScalarFromH5(slave_path, "prf", prf2) ? 0 : -1;
                }
            }
            if (ret < 0 || statevec2.empty() || !std::isfinite(prf2) || prf2 <= 0.0) {
                const QString error = QStringLiteral("Slave image metadata is incomplete or invalid: %1").arg(slave_path);
                InSARLogManager::LogError("InterferometricFormationWorker", error);
                emit errorProcess(error);
                return;
            }
            Mat phase_deflatted, flat_phase_coefficient;
            
            if (isdeflat)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.2;
                g_substep_prog_end = pair_prog_start + pair_span * 0.4;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在消除平地相位").arg(pair).arg(total_pairs);

                {
                    NodeUtils::Hdf5Locker locker;
                    NodeUtils::readMatFromH5(slave_path, "state_vec", statevec2);
                    NodeUtils::readScalarFromH5(slave_path, "prf", prf2);
                }

                int ret_deflat = flat.deflat(statevec, statevec2, lon_coef, lat_coef, phase, offset_row, offset_col, 0,
                    1 / prf, 1 / prf2, 1, wavelength, phase_deflatted, flat_phase_coefficient, DeflatProgressCallbackImpl);
                phase_deflatted.copyTo(phase);

                if (ret_deflat == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Deflat process cancelled by user.");
                    emit cancelled();
                    return;
                }
                else if (ret_deflat < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Deflat process failed.");
                    emit errorProcess(QStringLiteral("平地相位消除失败"));
                    return;
                }
            }

            if (istopo_removal)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.6;
                g_substep_prog_end = pair_prog_start + pair_span * 0.8;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在进行地形相位模拟").arg(pair).arg(total_pairs);

                int ret_topo = flat.topography_simulation(phase_deflatted, statevec, statevec2, lon_coef, lat_coef, inc_coef, prf, prf2,
                    sceneHeight, sceneWidth, offset_row, offset_col, nearRangeTime, rangeSpacing, wavelength,
                    acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str(), 20, DeflatProgressCallbackImpl);

                if (ret_topo == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Topography simulation cancelled by user.");
                    emit cancelled();
                    return;
                }
                else if (ret_topo < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Topography simulation failed.");
                    emit errorProcess(QStringLiteral("地形相位模拟失败"));
                    return;
                }
                else {
                    phase_deflatted = phase - phase_deflatted;
                    if (util.wrap(phase_deflatted, phase) < 0) {
                        emit errorProcess(QStringLiteral("地形相位包裹失败"));
                        return;
                    }
                }
            }

            if (multilook_rg > 1 || multilook_az > 1)
            {
                if (util.multilook(phase, phase_deflatted, multilook_rg, multilook_az) < 0 ||
                    phase_deflatted.empty()) {
                    emit errorProcess(QStringLiteral("干涉相位多视处理失败"));
                    return;
                }
                phase_deflatted.copyTo(phase);
            }

            if (isdeflat && flat_phase_coefficient.empty())
            {
                emit errorProcess(QStringLiteral("平地相位消除未生成有效系数"));
                return;
            }

            {
                NodeUtils::Hdf5Locker locker;
                ret = FC.creat_new_h5(h5_path.toStdString().c_str());
                if (ret >= 0) {
                    if ((!flat_phase_coefficient.empty() &&
                         !writeArray(h5_path, "flat_phase_coefficient", flat_phase_coefficient)) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_processing_schema_version", 1) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_flat_earth_removed", isdeflat ? 1 : 0) ||
                        !NodeUtils::writeScalarToH5(h5_path, "phase_topography_removed", istopo_removal ? 1 : 0) ||
                        !writeArray(h5_path, "range_len", Mat(1, 1, CV_32S, &sceneWidth)) ||
                        !writeArray(h5_path, "azimuth_len", Mat(1, 1, CV_32S, &sceneHeight)) ||
                        !writeArray(h5_path, "multilook_rg", Mat(1, 1, CV_32S, &multilook_rg)) ||
                        !writeArray(h5_path, "multilook_az", Mat(1, 1, CV_32S, &multilook_az)) ||
                        !writeArray(h5_path, "phase", phase)) {
                        return;
                    }
                    QString sourcePathMetadataError;
                    if (!NodeUtils::writeSourcePathMetadata(h5_path, master_path.toStdString(),
                                                            slave_path.toStdString(), &sourcePathMetadataError)) {
                        emit errorProcess(QStringLiteral("写入源路径元数据失败: %1").arg(sourcePathMetadataError));
                        return;
                    }
                }
            }
            if (ret < 0) {
                emit errorProcess(QStringLiteral("创建干涉H5文件失败: ") + h5_path);
                return;
            }

            if (iscoherence)
            {
                g_substep_prog_start = pair_prog_start + pair_span * 0.8;
                g_substep_prog_end = pair_prog_start + pair_span * 0.98;
                g_current_pair_info = QStringLiteral("生成第%1/%2幅干涉图：正在计算干涉相干系数").arg(pair).arg(total_pairs);

                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
                {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task cancelled by interruption request inside coherence block.");
                    emit cancelled();
                    return;
                }
                Mat coherence;
                int ret_coh = util.phase_coherence(phase, win_width, win_height, coherence, DeflatProgressCallbackImpl);
                if (ret_coh == -2) {
                    InSARLogManager::LogInfo("InterferometricFormationWorker", "Coherence calculation cancelled by user.");
                    emit cancelled();
                    return;
                }
                else if (ret_coh < 0) {
                    InSARLogManager::LogError("InterferometricFormationWorker", "Coherence calculation failed.");
                    emit errorProcess(QStringLiteral("相干系数计算失败"));
                    return;
                }

                {
                    NodeUtils::Hdf5Locker locker;
                    if (!writeArray(h5_path, "coherence", coherence)) {
                        return;
                    }
                }
            }

            InterferogramFileResult fileRes;
            fileRes.phaseName = phase_name;
            fileRes.cohName = coh_name;
            fileRes.h5Path = h5_path;
            fileRes.relativePath = "/" + file_name + "/" + h5_name + ".h5";
            fileRes.masterName = master_name;
            fileRes.offsetRow = offset_row;
            fileRes.offsetCol = offset_col;
            fileRes.isDeflat = isdeflat;
            fileRes.isTopoRemoval = istopo_removal;
            fileRes.isCoherence = iscoherence;
            fileRes.winWidth = win_width;
            fileRes.winHeight = win_height;
            fileRes.multilookRg = multilook_rg;
            fileRes.multilookAz = multilook_az;
            Q_EMIT interferogramGenerated(fileRes);
            
            emit updateProcess(pair_prog_end, QStringLiteral("生成第%1/%2幅干涉图已完成").arg(pair).arg(total_pairs));
            pair++;
        }
    }
    
    InSARLogManager::LogInfo("InterferometricFormationWorker", "Task completed: Interferometric");
    emit endProcess();
}

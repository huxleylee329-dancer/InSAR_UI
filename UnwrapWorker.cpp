#include "UnwrapWorker.h"
#include "NodeUtils.h"
#include <Unwrap.h>
#include <FormatConversion.h>
#include <Utils.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QDir>
#include <QFile>
#include <QThread>
#include <QElapsedTimer>
#include <QCoreApplication>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Unwrap_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Unwrap.lib")
#endif

thread_local UnwrapWorker* t_currentUnwrapWorker = nullptr;
thread_local int t_unwrapCurrentImageIndex = 0;
thread_local int t_unwrapTotalImagesCount = 1;
thread_local int t_unwrapLastLoggedProgress = -10;

static bool __stdcall unwrapProgressCallback(int progress, const char* message)
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

    if (t_currentUnwrapWorker)
    {
        if (t_currentUnwrapWorker->thread()->isInterruptionRequested() || t_currentUnwrapWorker->isStopRequested())
        {
            return false;
        }

        int start_prog = 10 + t_unwrapCurrentImageIndex * 80 / t_unwrapTotalImagesCount;
        int end_prog = 10 + (t_unwrapCurrentImageIndex + 1) * 80 / t_unwrapTotalImagesCount;
        int mapped_prog = start_prog + progress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_currentUnwrapWorker->updateProcess(mapped_prog, QStringLiteral("第%1幅图像解缠中：%2% (%3)")
            .arg(t_unwrapCurrentImageIndex + 1).arg(progress).arg(msgStr));

        if (progress == 0 || progress == 100 || (progress - t_unwrapLastLoggedProgress) >= 10)
        {
            InSARLogManager::LogInfo("UnwrapWorker", QString("Unwrap progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
            t_unwrapLastLoggedProgress = progress;
        }
    }
    return true;
}

struct UnwrapThreadLocalGuard {
    UnwrapThreadLocalGuard(UnwrapWorker* worker, int total) {
        t_currentUnwrapWorker = worker;
        t_unwrapCurrentImageIndex = 0;
        t_unwrapTotalImagesCount = total;
        t_unwrapLastLoggedProgress = -10;
    }
    ~UnwrapThreadLocalGuard() {
        t_currentUnwrapWorker = nullptr;
        t_unwrapCurrentImageIndex = 0;
        t_unwrapTotalImagesCount = 1;
        t_unwrapLastLoggedProgress = -10;
    }
};

UnwrapWorker::UnwrapWorker(QObject* parent)
    : BaseWorker(parent)
{
    qRegisterMetaType<UnwrapFileResult>("UnwrapFileResult");
}

UnwrapWorker::~UnwrapWorker()
{
}

void UnwrapWorker::Unwrap(int method, double coherence_threshold, QString save_path, QString file_name, QStringList phasePaths)
{
    const auto finishCancelled = [this]() {
        InSARLogManager::LogInfo("UnwrapWorker", "Unwrap cancelled by user.");
        Q_EMIT cancelled();
    };
    UnwrapThreadLocalGuard guard(this, qMax(1, phasePaths.size()));
    InSARLogManager::LogInfo("UnwrapWorker", QString("Unwrap task started. Output folder: %1, Method: %2").arg(file_name).arg(method));

    if (save_path.isEmpty() || file_name.isEmpty() || phasePaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }


    QString absolute_path = save_path + "/" + file_name;
    QDir target_dir(absolute_path);
    if (target_dir.exists())
    {
        target_dir.removeRecursively();
    }
    QDir(save_path).mkdir(file_name);

    QList<QString> phase_name;
    QList<QString> phase_path;
    QList<QString> unwrap_name;
    QList<QString> relative_unwrap_path;
    QList<QString> absolute_unwrap_path;
    emit updateProcess(10, QStringLiteral("准备数据……"));

    for (const QString& path : phasePaths) {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists() || fileInfo.baseName().isEmpty()) {
            emit errorProcess(QStringLiteral("Invalid phase input path: %1").arg(path));
            return;
        }
        const QString originName = fileInfo.baseName();
        const QString changeName = originName + "_unwrapped";
        phase_name.append(originName);
        phase_path.append(fileInfo.absoluteFilePath());
        unwrap_name.append(changeName);
        relative_unwrap_path.append("/" + file_name + "/" + changeName + ".h5");
        absolute_unwrap_path.append(QDir(save_path).filePath(file_name + "/" + changeName + ".h5"));
    }
    int image_number = phase_name.size();
    if (image_number == 0) {
        emit errorProcess(QStringLiteral("没有可解缠的干涉图像"));
        return;
    }
    t_unwrapTotalImagesCount = image_number;

    ::Unwrap unwrap;
    FormatConversion FC;
    Utils util;
    
    int ret = 0;

    std::vector<int> offset_rows(image_number, 0);
    std::vector<int> offset_cols(image_number, 0);
    std::vector<bool> process_success(image_number, false);

    auto copyH5Metadata = [&](int idx) -> bool {
        NodeUtils::Hdf5Locker locker;
        /*写入h5*/
        ret = FC.creat_new_h5(absolute_unwrap_path.at(idx).toStdString().c_str());
        if (ret < 0) return false;

        string tmp_str;
        Mat tmp;
        QString phaseH5 = phase_path.at(idx);
        QString unwrapH5 = absolute_unwrap_path.at(idx);
        
        {
            NodeUtils::Hdf5Locker locker;
            FormatConversion FC;
            NodeUtils::readStringFromH5(phaseH5, "source_1", tmp_str);
            FC.write_str_to_h5(unwrapH5.toStdString().c_str(), "source_1", tmp_str.c_str());
        }
        QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());

        {
            NodeUtils::Hdf5Locker locker;
            FormatConversion FC;
            NodeUtils::readStringFromH5(phaseH5, "source_2", tmp_str);
            FC.write_str_to_h5(unwrapH5.toStdString().c_str(), "source_2", tmp_str.c_str());
        }
        QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());

        {
            NodeUtils::Hdf5Locker locker;
            NodeUtils::readMatFromH5(phaseH5, "flat_phase_coefficient", tmp);
            NodeUtils::writeMatToH5(unwrapH5, "flat_phase_coefficient", tmp);

            NodeUtils::readMatFromH5(phaseH5, "range_len", tmp);
            NodeUtils::writeMatToH5(unwrapH5, "range_len", tmp);

            NodeUtils::readMatFromH5(phaseH5, "azimuth_len", tmp);
            NodeUtils::writeMatToH5(unwrapH5, "azimuth_len", tmp);

            NodeUtils::readMatFromH5(phaseH5, "multilook_rg", tmp);
            NodeUtils::writeMatToH5(unwrapH5, "multilook_rg", tmp);

            NodeUtils::readMatFromH5(phaseH5, "multilook_az", tmp);
            NodeUtils::writeMatToH5(unwrapH5, "multilook_az", tmp);

            if (NodeUtils::readMatFromH5(phaseH5, "mapped_lon", tmp))
                NodeUtils::writeMatToH5(unwrapH5, "mapped_lon", tmp);
            if (NodeUtils::readMatFromH5(phaseH5, "mapped_lat", tmp))
                NodeUtils::writeMatToH5(unwrapH5, "mapped_lat", tmp);
        }

        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            return false;
        }

        /*行列偏移量*/
        Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
        {
            NodeUtils::Hdf5Locker locker;
            NodeUtils::readMatFromH5(master_path, "offset_row", tmp_int);
            offset_rows[idx] = tmp_int.at<int>(0, 0);
            NodeUtils::readMatFromH5(master_path, "offset_col", tmp_int);
            offset_cols[idx] = tmp_int.at<int>(0, 0);
        }

        return true;
    };

    auto writeOutputPhase = [&](int idx, Mat& phase_unwrap) -> bool {
        if (phase_unwrap.type() != CV_32F) {
            phase_unwrap.convertTo(phase_unwrap, CV_32F);
        }

        const QString& outputPath = absolute_unwrap_path.at(idx);
        if (!NodeUtils::writeMatToH5(outputPath, "phase", phase_unwrap)) {
            return false;
        }

        return NodeUtils::writeScalarToH5(outputPath, "unwrap_method", method)
            && NodeUtils::writeScalarToH5(outputPath, "unwrap_coherence_threshold", coherence_threshold);
    };

    if (method == 1)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase, CV_64F) ? 0 : -1;
            if (ret < 0) continue;

            Mat phase_unwrap;
            ret = unwrap.SPD_Guided_Unwrap(phase, phase_unwrap, unwrapProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) continue;

            if (!copyH5Metadata(i)) {
                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                    finishCancelled();
                }
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                continue;
            }
            process_success[i] = true;
        }
    }
    else if (method == 2)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase, CV_64F) ? 0 : -1;
            if (ret < 0) continue;

            Mat phase_unwrap;
            Mat coherence, residue;
            ret = util.phase_coherence(phase, coherence);
            ret = util.residue(phase, residue);
            QString app_path = QCoreApplication::applicationDirPath();
            ret = unwrap.MCF(phase, phase_unwrap, coherence, residue, (absolute_path + "/MCF.net").toStdString().c_str(), app_path.toStdString().c_str(), unwrapProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) continue;

            if (!copyH5Metadata(i)) {
                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                    finishCancelled();
                }
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                continue;
            }
            process_success[i] = true;
        }
    }
    else if (method == 3)
    {
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase) ? 0 : -1;
            if (ret < 0) continue;

            Mat phase_unwrap;
            QString app_path = QCoreApplication::applicationDirPath();
            ret = unwrap.snaphu(phase_path.at(i).toStdString().c_str(), phase_unwrap, save_path.toStdString().c_str(), absolute_path.toStdString().c_str(), app_path.toStdString().c_str(), unwrapProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) continue;

            if (!copyH5Metadata(i)) {
                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                    finishCancelled();
                }
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                continue;
            }
            process_success[i] = true;
        }
    }
    else if (method == 4)
    {
        double distance_threshold = 5.0;
        for (int i = 0; i < image_number; i++)
        {
            t_unwrapCurrentImageIndex = i;
            t_unwrapLastLoggedProgress = -10;
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
            {
                finishCancelled();
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像解缠中……").arg(i + 1));
            Mat phase;
            ret = NodeUtils::readMatFromH5(phase_path.at(i), "phase", phase, CV_64F) ? 0 : -1;
            if (ret < 0) continue;

            Mat phase_unwrap;
            QString app_path = QCoreApplication::applicationDirPath();
            ret = unwrap.QualityGuided_MCF(phase, phase_unwrap, coherence_threshold, distance_threshold, absolute_path.toStdString().c_str(), app_path.toStdString().c_str(), unwrapProgressCallback);
            if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                finishCancelled();
                return;
            }
            if (ret < 0) continue;

            if (!copyH5Metadata(i)) {
                if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
                    finishCancelled();
                }
                return;
            }
            if (!writeOutputPhase(i, phase_unwrap)) {
                QFile::remove(absolute_unwrap_path.at(i));
                continue;
            }
            process_success[i] = true;
        }
    }

    QString methodName = "SPD_Guided";
    if (method == 2) methodName = "MCF";
    else if (method == 3) methodName = "Snaphu";
    else if (method == 4) methodName = "QualityGuided_MCF";

    for (int i = 0; i < image_number; ++i) {
        if (!process_success[i]) {
            continue;
        }
        UnwrapFileResult result;
        result.unwrapName = unwrap_name.at(i);
        result.absolutePath = absolute_unwrap_path.at(i);
        result.relativePath = relative_unwrap_path.at(i);
        result.offsetRow = offset_rows[i];
        result.offsetCol = offset_cols[i];
        result.method = methodName;
        Q_EMIT unwrapFileGenerated(result);
    }

    InSARLogManager::LogInfo("UnwrapWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

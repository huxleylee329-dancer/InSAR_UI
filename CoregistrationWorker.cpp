#include "CoregistrationWorker.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include <Utils.h>
#include <Registration.h>
#include <RobustCoregistration.h>
#include <QDir>
#include <QThread>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QRegularExpression>
#include "InSARLogManager.h"
#include <omp.h>
#include <QElapsedTimer>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Registration_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Registration.lib")
#endif

using namespace cv;
using namespace std;

thread_local CoregistrationWorker* t_currentCoregisWorker = nullptr;
static std::atomic<int> s_coregisLastLoggedProgress(-10);
static std::atomic<int> s_coregisLastEmittedProgress(-10);

namespace
{
struct RobustFitSettings
{
    double minimumBlockMatchSnr;
    int minimumInlierCount;
    double minimumInlierRatio;
    double maximumDesignConditionNumber;
    int minimumOccupiedGridCells;
    double residualFloorPixels;
    double huberCutoffSigma;
    double inlierSigma;
};

double readPositiveSetting(QSettings& settings, const char* key, double fallback)
{
    bool ok = false;
    const double value = settings.value(key, fallback).toDouble(&ok);
    return ok && std::isfinite(value) && value > 0.0 ? value : fallback;
}

double readRangeSetting(QSettings& settings, const char* key, double fallback, double minimum, double maximum)
{
    bool ok = false;
    const double value = settings.value(key, fallback).toDouble(&ok);
    return ok && std::isfinite(value) && value >= minimum && value <= maximum ? value : fallback;
}

int readPositiveIntSetting(QSettings& settings, const char* key, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int value = settings.value(key, fallback).toInt(&ok);
    return ok && value >= minimum && value <= maximum ? value : fallback;
}

RobustFitSettings loadRobustFitSettings()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    RobustFitSettings values;
    values.minimumBlockMatchSnr = readPositiveSetting(settings, "Coregistration/MinimumBlockMatchSnr", 3.0);
    values.minimumInlierCount = readPositiveIntSetting(settings, "Coregistration/MinimumInlierCount", 10, 3, 1000000);
    values.minimumInlierRatio = readRangeSetting(settings, "Coregistration/MinimumInlierRatio", 0.5, 0.0, 1.0);
    values.maximumDesignConditionNumber = readPositiveSetting(settings, "Coregistration/MaximumDesignConditionNumber", 10000.0);
    values.minimumOccupiedGridCells = readPositiveIntSetting(settings, "Coregistration/MinimumOccupiedGridCells", 5, 1, 9);
    values.residualFloorPixels = readPositiveSetting(settings, "Coregistration/RobustResidualFloorPixels", 1e-3);
    values.huberCutoffSigma = readPositiveSetting(settings, "Coregistration/RobustHuberCutoffSigma", 2.4477);
    values.inlierSigma = readPositiveSetting(settings, "Coregistration/RobustResidualInlierSigma", 3.0);
    return values;
}

}

static int __stdcall robustCoregistrationCancelCallback(void* userData)
{
	CoregistrationWorker* worker = static_cast<CoregistrationWorker*>(userData);
	return worker && (worker->isStopRequested() || worker->thread()->isInterruptionRequested()) ? 1 : 0;
}

static bool __stdcall coregisProgressCallback(int progress, const char* message, void* userData)
{
    // 回调防洪节流：100ms 内非边界进度直接秒退，降低 CPU 调度暴风
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

    CoregistrationWorker* worker = static_cast<CoregistrationWorker*>(userData);
    if (!worker)
    {
        worker = t_currentCoregisWorker;
    }
    if (worker)
    {
        if (worker->thread()->isInterruptionRequested() || worker->isStopRequested())
        {
            return false;
        }

        double start_prog = worker->getStageStart();
        double stage_width = worker->getStageWidth();
        int mapped_prog = qBound(0, qRound(start_prog + progress * stage_width / 100.0), 100);

        int lastEmitted = s_coregisLastEmittedProgress.load();
        if (progress == 0 || progress == 100 || progress != lastEmitted)
        {
            s_coregisLastEmittedProgress.store(progress);
            QString msgStr = QString::fromLocal8Bit(message);
            emit worker->updateProcess(mapped_prog, QStringLiteral("配准中 - 重采样进度：%1% (%2)")
                .arg(progress).arg(msgStr));
        }

        int lastLogged = s_coregisLastLoggedProgress.load();
        if (progress == 0 || progress == 100 || (progress - lastLogged) >= 10 || progress < lastLogged)
        {
            s_coregisLastLoggedProgress.store(progress);
            QString msgStr = QString::fromLocal8Bit(message);
            InSARLogManager::LogInfo("CoregistrationWorker", QString("Bilinear resampling progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
        }
    }
    return true;
}

struct CoregisThreadLocalGuard {
    CoregisThreadLocalGuard(CoregistrationWorker* worker) {
        t_currentCoregisWorker = worker;
        s_coregisLastLoggedProgress.store(-10);
        s_coregisLastEmittedProgress.store(-10);
    }
    ~CoregisThreadLocalGuard() {
        t_currentCoregisWorker = nullptr;
        s_coregisLastLoggedProgress.store(-10);
        s_coregisLastEmittedProgress.store(-10);
    }
};

CoregistrationWorker::CoregistrationWorker(QObject* parent)
    : BaseWorker(parent)
    , m_demPath("")
    , m_filePattern("{InputName}_regis")
{
}

CoregistrationWorker::~CoregistrationWorker()
{
}

void CoregistrationWorker::ResampleSlaveInverseWithAffineOffset(const ComplexMat& slave, ComplexMat& out,
    int outputRows, int outputCols, const Mat& coefRows, const Mat& coefCols,
    double offsetX, double offsetY, double scaleX, double scaleY, CoregistrationWorker* worker,
    int progressStart, int progressEnd, int imageIndex, int imageCount)
{
    const int rowsSlave = slave.GetRows();
    const int colsSlave = slave.GetCols();
    const int type = slave.type();
    out.re = Mat::zeros(outputRows, outputCols, type);
    out.im = Mat::zeros(outputRows, outputCols, type);
    std::atomic<int> completedRows(0);
    int lastReportedProgress = progressStart;

#pragma omp parallel for schedule(guided)
    for (int i = 0; i < outputRows; i++)
    {
        if (worker && worker->isStopRequested()) {
            continue;
        }

        double x, y, sampleRow, sampleCol;
        Mat tmp(1, 3, CV_64F);
        Mat result;
        int row0, col0, row1, col1;
        double offsetRows, offsetCols, upper, lower;
        for (int j = 0; j < outputCols; j++)
        {
            sampleCol = static_cast<double>(j);
            sampleRow = static_cast<double>(i);
            x = (sampleCol - offsetX) / scaleX;
            y = (sampleRow - offsetY) / scaleY;
            tmp.at<double>(0, 0) = 1.0;
            tmp.at<double>(0, 1) = x;
            tmp.at<double>(0, 2) = y;
            result = tmp * coefRows;
            offsetRows = result.at<double>(0, 0);
            result = tmp * coefCols;
            offsetCols = result.at<double>(0, 0);

            sampleRow += offsetRows;
            sampleCol += offsetCols;

            row0 = static_cast<int>(floor(sampleRow));
            col0 = static_cast<int>(floor(sampleCol));
            if (row0 < 0 || col0 < 0 || row0 > rowsSlave - 1 || col0 > colsSlave - 1)
            {
                continue;
            }

            row1 = row0 + 1;
            col1 = col0 + 1;
            row1 = row1 >= rowsSlave - 1 ? rowsSlave - 1 : row1;
            col1 = col1 >= colsSlave - 1 ? colsSlave - 1 : col1;
            if (type == CV_16S)
            {
                upper = static_cast<double>(slave.re.at<short>(row0, col0)) + static_cast<double>(slave.re.at<short>(row0, col1) - slave.re.at<short>(row0, col0)) * (sampleCol - static_cast<double>(col0));
                lower = static_cast<double>(slave.re.at<short>(row1, col0)) + static_cast<double>(slave.re.at<short>(row1, col1) - slave.re.at<short>(row1, col0)) * (sampleCol - static_cast<double>(col0));
                out.re.at<short>(i, j) = upper + static_cast<double>(lower - upper) * (sampleRow - static_cast<double>(row0));
                upper = static_cast<double>(slave.im.at<short>(row0, col0)) + static_cast<double>(slave.im.at<short>(row0, col1) - slave.im.at<short>(row0, col0)) * (sampleCol - static_cast<double>(col0));
                lower = static_cast<double>(slave.im.at<short>(row1, col0)) + static_cast<double>(slave.im.at<short>(row1, col1) - slave.im.at<short>(row1, col0)) * (sampleCol - static_cast<double>(col0));
                out.im.at<short>(i, j) = upper + static_cast<double>(lower - upper) * (sampleRow - static_cast<double>(row0));
            }
            else if (type == CV_32F)
            {
                upper = slave.re.at<float>(row0, col0) + (slave.re.at<float>(row0, col1) - slave.re.at<float>(row0, col0)) * (sampleCol - static_cast<double>(col0));
                lower = slave.re.at<float>(row1, col0) + (slave.re.at<float>(row1, col1) - slave.re.at<float>(row1, col0)) * (sampleCol - static_cast<double>(col0));
                out.re.at<float>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
                upper = slave.im.at<float>(row0, col0) + (slave.im.at<float>(row0, col1) - slave.im.at<float>(row0, col0)) * (sampleCol - static_cast<double>(col0));
                lower = slave.im.at<float>(row1, col0) + (slave.im.at<float>(row1, col1) - slave.im.at<float>(row1, col0)) * (sampleCol - static_cast<double>(col0));
                out.im.at<float>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
            }
            else
            {
                upper = slave.re.at<double>(row0, col0) + (slave.re.at<double>(row0, col1) - slave.re.at<double>(row0, col0)) * (sampleCol - static_cast<double>(col0));
                lower = slave.re.at<double>(row1, col0) + (slave.re.at<double>(row1, col1) - slave.re.at<double>(row1, col0)) * (sampleCol - static_cast<double>(col0));
                out.re.at<double>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
                upper = slave.im.at<double>(row0, col0) + (slave.im.at<double>(row0, col1) - slave.im.at<double>(row0, col0)) * (sampleCol - static_cast<double>(col0));
                lower = slave.im.at<double>(row1, col0) + (slave.im.at<double>(row1, col1) - slave.im.at<double>(row1, col0)) * (sampleCol - static_cast<double>(col0));
                out.im.at<double>(i, j) = upper + (lower - upper) * (sampleRow - static_cast<double>(row0));
            }
        }

        const int completed = ++completedRows;
        const int progress = progressStart + static_cast<int>(
            std::floor(static_cast<double>(completed) * (progressEnd - progressStart) / outputRows));
#pragma omp critical(coreg_resample_progress)
        {
            if (worker && progress > lastReportedProgress)
            {
                lastReportedProgress = progress;
                emit worker->updateProcess(progress, QStringLiteral("第%1/%2对图像重采样中：%3%")
                    .arg(imageIndex).arg(imageCount).arg(progress));
            }
        }
    }
}

QString CoregistrationWorker::resolveOutputFileName(const QString& originalName) const
{
    QString pattern = m_filePattern.trimmed();
    if (pattern.isEmpty()) {
        pattern = "{InputName}_regis";
    }
    // Normalize brackets
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    pattern.replace(re, "{InputName}");
    if (pattern.contains("{InputName}")) {
        pattern.replace("{InputName}", originalName);
    } else {
        pattern = originalName + "_" + pattern;
    }
    return pattern;
}

void CoregistrationWorker::Regis(QList<int> para, QString save_path, QString project_name, QString file_name, QStringList inputPaths)
{
	if (para.size() != 4 ||
		save_path.isEmpty() ||
		project_name.isEmpty() ||
		file_name.isEmpty() || inputPaths.size() < 2)
	{
		Q_EMIT errorProcess(QStringLiteral("Coregistration input is invalid."));
		return;
	}
    QDir dir(save_path);
    if (!dir.exists(file_name))
        int ret = dir.mkdir(file_name);
    int index = para.at(0);
    int interp_times = para.at(1);
    int block_size = para.at(2);
    int image_number = inputPaths.size();
    if (index < 1 || index > image_number) {
        Q_EMIT errorProcess(QStringLiteral("Coregistration master index is invalid."));
        return;
    }
    Utils util;
    vector<cv::String> SAR_images;
    vector<cv::String> SAR_images_regis;
    QList<QString> origin;
    for (const QString& inputPath : inputPaths)
    {
				QFileInfo fileinfo(inputPath);
                QString origin_name = fileinfo.baseName();
                origin.append(origin_name);
                SAR_images.push_back(inputPath.toStdString());
                QString outName = resolveOutputFileName(origin_name);
                if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
                    outName += ".h5";
                }
                SAR_images_regis.push_back(QString("%1/%2/%3").arg(save_path).arg(file_name)
                    .arg(outName).toStdString());
    }
	if (SAR_images.empty()) { emit errorProcess(QStringLiteral("没有可配准的输入影像")); return; }
	emit updateProcess(10, QStringLiteral("开始进行配准……"));
	int maxThreads = omp_get_num_procs();
	omp_set_num_threads(maxThreads);

	Mat offset_row_out, offset_col_out;
    int ret = Registration_copy(SAR_images, SAR_images_regis, offset_row_out, offset_col_out, index, interp_times, block_size);
    if (ret == -2 || QThread::currentThread()->isInterruptionRequested() || isStopRequested())
    {
		Q_EMIT cancelled();
		return;
    }
    if (ret < 0)
    {
		InSARLogManager::LogError("CoregistrationWorker", QString("Task failed in: ") + QString(__FUNCTION__));
		Q_EMIT errorProcess(QStringLiteral("配准计算失败。"));
		return;
	}
    FormatConversion FC;
    /*获取主星参数*/
    Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
    Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
    double interp_interval;
    double offset_row = 0.0, offset_col = 0.0;
    int Rows, Cols;
    double time_Master = 0;
    string time_master_str;
    {
        NodeUtils::Hdf5Locker locker;
        QString masterImgPath = QString::fromStdString(SAR_images.at(index - 1));
        NodeUtils::readMatFromH5(masterImgPath, "state_vec", State_Vec_Master, CV_64F);
        NodeUtils::readMatFromH5(masterImgPath, "lon_coefficient", Lon_Coeff_Master, CV_64F);
        NodeUtils::readMatFromH5(masterImgPath, "lat_coefficient", Lat_Coeff_Master, CV_64F);
        
        double prf = 0.0;
        NodeUtils::readScalarFromH5(masterImgPath, "prf", prf);
        interp_interval = 1.0 / prf;

        NodeUtils::readScalarFromH5(masterImgPath, "offset_row", offset_row);
        NodeUtils::readScalarFromH5(masterImgPath, "offset_col", offset_col);
        NodeUtils::readStringFromH5(masterImgPath, "acquisition_start_time", time_master_str);
        FC.utc2gps(time_master_str.c_str(), &time_Master);
        ComplexMat SLC;
        FC.read_slc_from_h5(SAR_images_regis.at(index - 1).c_str(), SLC);
        Rows = SLC.GetRows();
        Cols = SLC.GetCols();
    }
    QString temporal_baseline, B_parallel, B_effect;
    /*添加图像到model中并复制h5参数*/
    vector<double> Row_offset;
    vector<double> Col_offset;
	emit updateProcess(90, QStringLiteral("写入辅助参数……"));
    for (int i = 0; i < image_number; i++)
    {
		if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
		{
			Q_EMIT cancelled();
			return;
		}
        /*写入辅助参数到h5*/
		offset_row = offset_col = 0.0;
        {
            NodeUtils::Hdf5Locker locker;
            FC.Copy_para_from_h5_2_h5(SAR_images.at(i).c_str(), SAR_images_regis.at(i).c_str());
            FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "process_state", "coregistration");
            FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "comment", "complex-2.0");
            
            QString slaveImgPath = QString::fromStdString(SAR_images.at(i));
            NodeUtils::readScalarFromH5(slaveImgPath, "offset_row", offset_row);
		    offset_row += offset_row_out.at<double>(i, 0);
            FC.write_double_to_h5(SAR_images_regis.at(i).c_str(), "offset_row", offset_row);
            Row_offset.push_back(offset_row);
            NodeUtils::readScalarFromH5(slaveImgPath, "offset_col", offset_col);
		    offset_col += offset_col_out.at<double>(i, 0);
            FC.write_double_to_h5(SAR_images_regis.at(i).c_str(), "offset_col", offset_col);
            Col_offset.push_back(offset_col);
            FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "azimuth_len", Rows);
            FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "range_len", Cols);
        }
        /*估计时空基线*/
        if (i == index - 1)  //主图像
        {
            temporal_baseline += "0 ";
            B_parallel += "0 ";
            B_effect += "0 ";
        }
        else
        {
            Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
            double interp_interval_slave;
            double V_baseline = 0, H_baseline = 0;
            double sigma_V = 0, sigma_H = 0;
            double time_Slave = 0;
            string time_slave_str;
            {
                NodeUtils::Hdf5Locker locker;
                QString slaveImgPath = QString::fromStdString(SAR_images.at(i));
                NodeUtils::readMatFromH5(slaveImgPath, "state_vec", State_Vec_Slave, CV_64F);
                NodeUtils::readMatFromH5(slaveImgPath, "lon_coefficient", Lon_Coeff_Slave, CV_64F);
                NodeUtils::readMatFromH5(slaveImgPath, "lat_coefficient", Lat_Coeff_Slave, CV_64F);
                
                double prf_slave = 0.0;
                NodeUtils::readScalarFromH5(slaveImgPath, "prf", prf_slave);
                interp_interval_slave = 1 / prf_slave;
                
                NodeUtils::readStringFromH5(slaveImgPath, "acquisition_start_time", time_slave_str);
                FC.utc2gps(time_slave_str.c_str(), &time_Slave);
            }
            double delta = (time_Slave - time_Master) / 60 / 60 / 24;
            char tmp_d2s[512];
            sprintf_s(tmp_d2s, "%.4f", delta);
            temporal_baseline += QString("%1 ").arg(QString(tmp_d2s));
            util.baseline_estimation(State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
                offset_row, offset_col, Rows, Cols, interp_interval, interp_interval_slave, &V_baseline, &H_baseline, &sigma_V, &sigma_H);

            sprintf_s(tmp_d2s, "%.2f", V_baseline);
            B_parallel += QString("%1 ").arg(QString(tmp_d2s));
            sprintf_s(tmp_d2s, "%.2f", H_baseline);
            B_effect += QString("%1 ").arg(QString(tmp_d2s));
			
        }
    }
	QStringList outputNames;
    QStringList outputPaths;
    QList<int> offsetRows;
    QList<int> offsetCols;
    for (int i = 0; i < image_number; ++i) {
        outputNames.append(QFileInfo(QString::fromStdString(SAR_images_regis.at(i))).baseName());
        outputPaths.append(QString::fromStdString(SAR_images_regis.at(i)));
        offsetRows.append(Row_offset.at(i));
        offsetCols.append(Col_offset.at(i));
    }
    emit outputsGenerated(outputNames, outputPaths, offsetRows, offsetCols,
        temporal_baseline, B_effect, B_parallel);
	InSARLogManager::LogInfo("CoregistrationWorker", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

void CoregistrationWorker::DEMAssistCoregistration(
	int masterIndex,
	QString savepath,
	QString project_name,
	QString dstNode,
	QStringList inputPaths
)
{
	if (savepath.isEmpty() || project_name.isEmpty() || dstNode.isEmpty() || inputPaths.size() < 2)
	{
		Q_EMIT errorProcess(QStringLiteral("DEM-assisted coregistration input is invalid."));
		return;
	}
	QDir dir(savepath);
	if (!dir.exists(dstNode))
		int ret = dir.mkdir(dstNode);

    const QString demPath = m_demPath;
    if (demPath.isEmpty() || !QFileInfo(demPath).isFile()) {
        Q_EMIT errorProcess(QStringLiteral("Coregistration requires a resolved Auxiliary DEM file."));
        return;
    }
	string dempath = demPath.toStdString();

	Utils util;
	FormatConversion conversion; Registration coregis;
	vector<string> SAR_images;
	vector<string> SAR_images_regis;
	QList<QString> origin;
	const int images_number = inputPaths.size();
	for (const QString& inputPath : inputPaths)
	{
		QFileInfo fileinfo(inputPath);
		const QString origin_name = fileinfo.baseName();
		origin.append(origin_name);
		SAR_images.push_back(inputPath.toStdString());
        QString outName = resolveOutputFileName(origin_name);
        if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
            outName += ".h5";
        }
		SAR_images_regis.push_back(QString("%1/%2/%3").arg(savepath).arg(dstNode)
			.arg(outName).toStdString());
	}

	emit updateProcess(10, QStringLiteral("开始进行配准……"));
	int maxThreads = omp_get_num_procs();
	omp_set_num_threads(maxThreads);

	CoregisThreadLocalGuard tlGuard(this);
	masterIndex = masterIndex < 1 ? 1 : masterIndex;
	masterIndex = masterIndex > images_number ? images_number : masterIndex;

	double lonMax, lonMin, latMax, latMin, lon_upperleft, lat_upperleft, rangeSpacing, rangeSpacing2,
		nearRangeTime, nearRangeTime2, wavelength, prf, prf2,
		start, end, start2, end2, a0, a1, a2, b0, b1, b2;
	int sceneHeight, sceneWidth, sceneHeight2, sceneWidth2, offset_row, offset_col, offset_row2, offset_col2;
	Mat lon_coef, lat_coef, dem, statevec, rangePos, azimuthPos,
		lon_coef2, lat_coef2, statevec2, rangePos2, azimuthPos2, slaveRangeOffset, slaveAzimuthOffset;
	string start_time, end_time;
	ComplexMat slave;
	offset_row = offset_col = 0;
	const char* master_file = SAR_images[masterIndex - 1].c_str();
	const char* slave_file = NULL;
	{
		NodeUtils::Hdf5Locker locker;
		QString masterPath = QString::fromStdString(master_file);
		NodeUtils::readScalarFromH5(masterPath, "range_len", sceneWidth);
		NodeUtils::readScalarFromH5(masterPath, "azimuth_len", sceneHeight);
		NodeUtils::readScalarFromH5(masterPath, "offset_row", offset_row);
		NodeUtils::readScalarFromH5(masterPath, "offset_col", offset_col);
		NodeUtils::readMatFromH5(masterPath, "lon_coefficient", lon_coef);
		NodeUtils::readMatFromH5(masterPath, "lat_coefficient", lat_coef);
		NodeUtils::readScalarFromH5(masterPath, "prf", prf);
		NodeUtils::readScalarFromH5(masterPath, "carrier_frequency", wavelength);
		wavelength = VEL_C / wavelength;
		NodeUtils::readScalarFromH5(masterPath, "range_spacing", rangeSpacing);
		NodeUtils::readScalarFromH5(masterPath, "slant_range_first_pixel", nearRangeTime);
		nearRangeTime = 2.0 * nearRangeTime / VEL_C;
		NodeUtils::readStringFromH5(masterPath, "acquisition_start_time", start_time);
		conversion.utc2gps(start_time.c_str(), &start);
		NodeUtils::readStringFromH5(masterPath, "acquisition_stop_time", end_time);
		conversion.utc2gps(end_time.c_str(), &end);
		NodeUtils::readMatFromH5(masterPath, "state_vec", statevec);
		conversion.read_slc_from_h5(master_file, slave);
		InSARLogManager::LogInfo("CoregistrationWorker", QString("Master image resolved. Size: %1 x %2 (Width x Height)").arg(sceneWidth).arg(sceneHeight));
		
		conversion.creat_new_h5(SAR_images_regis[masterIndex - 1].c_str());
		conversion.write_slc_to_h5(SAR_images_regis[masterIndex - 1].c_str(), slave);
		conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "range_len", slave.GetCols());
		conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "azimuth_len", slave.GetRows());
		conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "offset_row", offset_row);
		conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "offset_col", offset_col);
		conversion.Copy_para_from_h5_2_h5(SAR_images[masterIndex - 1].c_str(), SAR_images_regis[masterIndex - 1].c_str());
		conversion.write_str_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "process_state", "coregistration");
		conversion.write_str_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "comment", "complex-2.0");
	}
	Mat Row_offset(images_number, 1, CV_32S); Row_offset.at<int>(masterIndex - 1, 0) = 0;
	Mat Col_offset(images_number, 1, CV_32S); Col_offset.at<int>(masterIndex - 1, 0) = 0;
	
	Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
		&lonMax, &latMax, &lonMin, &latMin);
	Utils::getSRTMDEM(dempath.c_str(), dem, &lon_upperleft, &lat_upperleft, lonMin, lonMax, latMin, latMax);
	this->setStage(10.0, 20.0);
	coregis.getDEMRgAzPos(dem, statevec, rangePos, azimuthPos, lon_upperleft, lat_upperleft, offset_row, offset_col,
		sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime, start, end, 5.0 / 6000.0, 5.0 / 6000.0, coregisProgressCallback, this);
	int count = 0;
	for (int i = 0; i < images_number; i++)
	{
		if (i == masterIndex - 1) continue;
		if (QThread::currentThread()->isInterruptionRequested() || isStopRequested()) {
			Q_EMIT cancelled();
			return;
		}
		int offset_r, offset_c;
		offset_row2 = offset_col2 = 0;
		slave_file = SAR_images[i].c_str();
		{
			NodeUtils::Hdf5Locker locker;
			QString slavePath = QString::fromStdString(slave_file);
			NodeUtils::readScalarFromH5(slavePath, "range_len", sceneWidth2);
			NodeUtils::readScalarFromH5(slavePath, "azimuth_len", sceneHeight2);
			NodeUtils::readScalarFromH5(slavePath, "offset_row", offset_row2);
			NodeUtils::readScalarFromH5(slavePath, "offset_col", offset_col2);
			NodeUtils::readMatFromH5(slavePath, "lon_coefficient", lon_coef2);
			NodeUtils::readMatFromH5(slavePath, "lat_coefficient", lat_coef2);
			NodeUtils::readScalarFromH5(slavePath, "prf", prf2);
			NodeUtils::readScalarFromH5(slavePath, "range_spacing", rangeSpacing2);
			NodeUtils::readScalarFromH5(slavePath, "slant_range_first_pixel", nearRangeTime2);
			nearRangeTime2 = 2.0 * nearRangeTime2 / VEL_C;
			NodeUtils::readStringFromH5(slavePath, "acquisition_start_time", start_time);
			conversion.utc2gps(start_time.c_str(), &start2);
			NodeUtils::readStringFromH5(slavePath, "acquisition_stop_time", end_time);
			conversion.utc2gps(end_time.c_str(), &end2);
			NodeUtils::readMatFromH5(slavePath, "state_vec", statevec2);
			conversion.read_slc_from_h5(slave_file, slave);
			InSARLogManager::LogInfo("CoregistrationWorker", QString("Slave image resolved. Index: %1, Size: %2 x %3 (Width x Height)").arg(i + 1).arg(sceneWidth2).arg(sceneHeight2));
		}
 
		double totalWidth = 60.0 / double(images_number - 1);
		double currentSlaveStart = 30.0 + double(count) * totalWidth;
		this->setStage(currentSlaveStart, totalWidth * 0.40);
		coregis.getDEMRgAzPos(dem, statevec2, rangePos2, azimuthPos2, lon_upperleft, lat_upperleft, offset_row2, offset_col2,
			sceneHeight2, sceneWidth2, prf2, rangeSpacing2, wavelength, nearRangeTime2, start2, end2, 5.0 / 6000.0, 5.0 / 6000.0, coregisProgressCallback, this);
 
		coregis.computeSlaveOffset(rangePos, azimuthPos, rangePos2, azimuthPos2, slaveAzimuthOffset, slaveRangeOffset);
		coregis.fitSlaveOffset(slaveAzimuthOffset, rangePos, azimuthPos, &a0, &a1, &a2);
		coregis.fitSlaveOffset(slaveRangeOffset, rangePos, azimuthPos, &b0, &b1, &b2);
		this->setStage(currentSlaveStart + totalWidth * 0.40, totalWidth * 0.60);
		coregis.performBilinearResampling(slave, sceneHeight, sceneWidth, b0, b1, b2, a0, a1, a2, &offset_r, &offset_c, coregisProgressCallback, this);
		{
			NodeUtils::Hdf5Locker locker;
			conversion.creat_new_h5(SAR_images_regis[i].c_str());
			conversion.write_slc_to_h5(SAR_images_regis[i].c_str(), slave);
			conversion.write_int_to_h5(SAR_images_regis[i].c_str(), "range_len", slave.GetCols());
			conversion.write_int_to_h5(SAR_images_regis[i].c_str(), "azimuth_len", slave.GetRows());
			conversion.write_int_to_h5(SAR_images_regis[i].c_str(), "offset_row", offset_row2 + offset_r);
			Row_offset.at<int>(i, 0) = offset_row2 + offset_r;
			conversion.write_int_to_h5(SAR_images_regis[i].c_str(), "offset_col", offset_col2 + offset_c);
			Col_offset.at<int>(i, 0) = offset_col2 + offset_c;
			conversion.Copy_para_from_h5_2_h5(SAR_images[i].c_str(), SAR_images_regis.at(i).c_str());
			conversion.write_str_to_h5(SAR_images_regis.at(i).c_str(), "process_state", "coregistration");
			conversion.write_str_to_h5(SAR_images_regis.at(i).c_str(), "comment", "complex-2.0");
		}
		count++;
		emit updateProcess(10 + double(count) / double(images_number - 1) * 80, QStringLiteral("正在处理..."));
	}

	QString temporal_baseline, B_parallel, B_effect;
	for (int i = 0; i < images_number; i++)
	{
		if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
		{
			Q_EMIT cancelled();
			return;
		}
		temporal_baseline += "0 ";
		B_parallel += "0 ";
		B_effect += "0 ";
	}

    QStringList outputNames, outputPaths;
    QList<int> offsetRows, offsetCols;
    for (int i = 0; i < images_number; ++i) {
        const QString outName = QFileInfo(QString::fromStdString(SAR_images_regis[i])).baseName();
        outputNames.append(outName);
        outputPaths.append(QString::fromStdString(SAR_images_regis[i]));
        offsetRows.append(Row_offset.at<int>(i, 0));
        offsetCols.append(Col_offset.at<int>(i, 0));
    }
    emit outputsGenerated(outputNames, outputPaths, offsetRows, offsetCols,
        temporal_baseline, B_effect, B_parallel);
	InSARLogManager::LogInfo("CoregistrationWorker", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

int CoregistrationWorker::Registration_copy(
	vector<string>& SAR_images,
	vector<string>& SAR_images_out,
	Mat& offset_row_out,
	Mat& offset_col_out,
	int Master_index,
	int interp_times, int blocksize
)
{
	if (SAR_images.size() < 2 ||
		Master_index < 1 ||
		Master_index > SAR_images.size() ||
		SAR_images_out.size() != SAR_images.size() ||
		interp_times < 1 ||
		blocksize < 16
		)
	{
		fprintf(stderr, "stack_coregistration(): input check failed!\n\n");
		return -1;
	}
	
	//获取各图像的尺寸，并创建输出h5文件
	FormatConversion conversion;
	int ret, type;
	const auto cancellationRequested = [this]() {
		return isStopRequested() || QThread::currentThread()->isInterruptionRequested();
	};
	const RobustFitSettings robustFitSettings = loadRobustFitSettings();
	int n_images = SAR_images.size();
	int num_slaves = n_images - 1;
	int slave_idx = 0;
	const double pairProgressWidth = 75.0;
	const double matchingProgressWidth = 45.0;
	const double resamplingProgressWidth = 24.0;
	offset_col_out.create(n_images, 1, CV_64F);
	offset_row_out.create(n_images, 1, CV_64F);
	Mat images_rows, images_cols, tmp;
	images_rows = Mat::zeros(n_images, 1, CV_32S); images_cols = Mat::zeros(n_images, 1, CV_32S);
	for (int i = 0; i < n_images; i++)
	{
		bool step_ok = false;
		{
			NodeUtils::Hdf5Locker locker;
			ret = conversion.creat_new_h5(SAR_images_out[i].c_str());
			if (ret >= 0)
			{
				ret = conversion.read_array_from_h5(SAR_images[i].c_str(), "range_len", tmp);
				if (ret >= 0)
				{
					images_cols.at<int>(i, 0) = tmp.at<int>(0, 0);
					ret = conversion.read_array_from_h5(SAR_images[i].c_str(), "azimuth_len", tmp);
					if (ret >= 0)
					{
						images_rows.at<int>(i, 0) = tmp.at<int>(0, 0);
						step_ok = true;
					}
				}
			}
		}
		if (cancellationRequested()) return -2;
		if (!step_ok) return -1;
	}
	//分块读取数据并求取偏移量
	Utils util; Registration regis;
	int rows = images_rows.at<int>(Master_index - 1, 0); int cols = images_cols.at<int>(Master_index - 1, 0);
	int m = rows / blocksize;
	int n = cols / blocksize;
	if (m * n < 10)
	{
		fprintf(stderr, "stack_coregistration(): try smaller blocksize!\n");
		return -1;
	}
	Mat offset_r = Mat::zeros(m, n, CV_64F); Mat offset_c = Mat::zeros(m, n, CV_64F); Mat eligibleMask = Mat::zeros(m, n, CV_8U);
	Mat snr_values = Mat::zeros(m, n, CV_64F);
	Mat offset_coord_row = Mat::zeros(m, n, CV_64F);
	Mat offset_coord_col = Mat::zeros(m, n, CV_64F);
	//子块中心坐标
	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < n; j++)
		{
			offset_coord_row.at<double>(i, j) = ((double)blocksize) / 2 * (double)(2 * i + 1);
			offset_coord_col.at<double>(i, j) = ((double)blocksize) / 2 * (double)(2 * j + 1);
		}
	}
	//根据输入图像尺寸大小判断是否分块读取（超过20000×20000则分块读取，否则一次性读取）
	ComplexMat master_w, slave_w;
	bool b_block = true; bool master_read = false;
	if (rows * cols < 20000 * 20000) b_block = false;
	for (int ii = 0; ii < n_images; ii++)
	{
		if (cancellationRequested()) return -2;
		if (ii == Master_index - 1)
		{
			offset_row_out.at<double>(ii, 0) = 0.0;
			offset_col_out.at<double>(ii, 0) = 0.0;
			continue;
		}
		if (!b_block)//不分块读取
		{
			if (!master_read)
			{
				ret = conversion.read_slc_from_h5(SAR_images[Master_index - 1].c_str(), master_w);
				if (cancellationRequested()) return -2;
				if (ret < 0) return -1;
				master_read = true;
				type = master_w.type();
				ret = conversion.write_slc_to_h5(SAR_images_out[Master_index - 1].c_str(), master_w);//写主图像
			}

			ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave_w);
			if (cancellationRequested()) return -2;
			if (ret < 0) return -1;
			if (type != slave_w.type())
			{
				fprintf(stderr, "stack_coregistration(): images type mismatch!\n");
				return -1;
			}
			if (type != CV_16S && type != CV_64F && type != CV_32F)
			{
				fprintf(stderr, "stack_coregistration(): data type not supported!\n");
				return -1;
			}
		}
		else
		{
			if (!master_read)
			{
				ret = conversion.read_slc_from_h5(SAR_images[Master_index - 1].c_str(), master_w);
				if (cancellationRequested()) return -2;
				if (ret < 0) return -1;
				master_read = true;
				type = master_w.type();
				ret = conversion.write_slc_to_h5(SAR_images_out[Master_index - 1].c_str(), master_w);//写主图像
			}
		}
		
		//分块读取并计算偏移量
		double pairStart = 10.0 + (pairProgressWidth / num_slaves) * slave_idx;
		double matchingEnd = pairStart + matchingProgressWidth / num_slaves;
		double resamplingEnd = matchingEnd + resamplingProgressWidth / num_slaves;
		double pairEnd = pairStart + pairProgressWidth / num_slaves;
		emit updateProcess(qRound(pairStart), QStringLiteral("第%1对图像匹配中……").arg(slave_idx + 1));
		std::atomic<int> completed_blocks(0);
		int init_pct = qRound(pairStart);
		std::atomic<int> max_reported_pct(init_pct);
		m = images_rows.at<int>(ii, 0) / blocksize;
		n = images_cols.at<int>(ii, 0) / blocksize;
		int total_blocks = m * n;
		offset_r = Mat::zeros(m, n, CV_64F);
		offset_c = Mat::zeros(m, n, CV_64F);
		eligibleMask = Mat::zeros(m, n, CV_8U);
		offset_coord_row = Mat::zeros(m, n, CV_64F);
		snr_values = Mat::zeros(m, n, CV_64F);
		offset_coord_col = Mat::zeros(m, n, CV_64F);
		for (int i = 0; i < m; i++)
		{
			for (int j = 0; j < n; j++)
			{
				offset_coord_row.at<double>(i, j) = ((double)blocksize) / 2 * (double)(2 * i + 1);
				offset_coord_col.at<double>(i, j) = ((double)blocksize) / 2 * (double)(2 * j + 1);
			}
		}
		if (!b_block)
		{
# pragma omp parallel for schedule(guided)

			for (int j = 0; j < m; j++)
			{
				if (isStopRequested()) {
					continue;
				}
				int offset_row, offset_col;
				double move_r, move_c, snr_val;
				ComplexMat master, slave;
				for (int k = 0; k < n; k++)
				{
					offset_row = j * blocksize; offset_col = k * blocksize;
					if ((j + 1) * blocksize <= images_rows.at<int>(ii, 0) && (k + 1) * blocksize <= images_cols.at<int>(ii, 0))
					{
						master = master_w(cv::Range(offset_row, offset_row + blocksize), cv::Range(offset_col, offset_col + blocksize));
						slave = slave_w(cv::Range(offset_row, offset_row + blocksize), cv::Range(offset_col, offset_col + blocksize));

						//计算偏移量
						if (master.type() != CV_64F) master.convertTo(master, CV_64F);
						if (slave.type() != CV_64F) slave.convertTo(slave, CV_64F);
						move_r = 0.0; move_c = 0.0; snr_val = 0.0;
						int matchRet = regis.real_coherent(master, slave, &move_r, &move_c, &snr_val);
						if (matchRet >= 0 && snr_val >= robustFitSettings.minimumBlockMatchSnr) {
							offset_r.at<double>(j, k) = move_r;
							offset_c.at<double>(j, k) = move_c;
							eligibleMask.at<uchar>(j, k) = 1;
							snr_values.at<double>(j, k) = snr_val;
						} else {
							eligibleMask.at<uchar>(j, k) = 0;
						}
					}
					int current_done = ++completed_blocks;
					int step = std::max(1, total_blocks / 50);
					if (current_done % step == 0 || current_done == total_blocks) {
						double block_ratio = double(current_done) / double(total_blocks);
						double current_prog = pairStart + block_ratio * (matchingEnd - pairStart);
						int progress_pct = int(current_prog);
						int prev = max_reported_pct.load();
						while (progress_pct > prev && !max_reported_pct.compare_exchange_weak(prev, progress_pct)) {
							// Keep trying
						}
						if (progress_pct > prev) {
							#pragma omp critical(coreg_progress_1)
							{
								emit updateProcess(progress_pct, QStringLiteral("第%1对图像配准中：%2%")
									.arg(ii + 1).arg(int(block_ratio * 100)));
							}
						}
					}
				}
			}
			if (cancellationRequested()) return -2;

		}
		else
		{
			int offset_row, offset_col;
			double move_r, move_c, snr_val;
			ComplexMat master, slave;
			for (int j = 0; j < m; j++)
			{
				if (isStopRequested()) {
					break;
				}
				for (int k = 0; k < n; k++)
				{
					offset_row = j * blocksize; offset_col = k * blocksize;
					if ((j + 1) * blocksize <= images_rows.at<int>(ii, 0) && (k + 1) * blocksize <= images_cols.at<int>(ii, 0))
					{
						ret = conversion.read_subarray_from_h5(SAR_images[Master_index - 1].c_str(), "s_im", offset_row, offset_col, blocksize, blocksize, master.im);
						if (cancellationRequested()) return -2;
						if (ret < 0) return -1;
						ret = conversion.read_subarray_from_h5(SAR_images[Master_index - 1].c_str(), "s_re", offset_row, offset_col, blocksize, blocksize, master.re);
						if (cancellationRequested()) return -2;
						if (ret < 0) return -1;
						ret = conversion.read_subarray_from_h5(SAR_images[ii].c_str(), "s_im", offset_row, offset_col, blocksize, blocksize, slave.im);
						if (cancellationRequested()) return -2;
						if (ret < 0) return -1;
						ret = conversion.read_subarray_from_h5(SAR_images[ii].c_str(), "s_re", offset_row, offset_col, blocksize, blocksize, slave.re);
						if (cancellationRequested()) return -2;
						if (ret < 0) return -1;

						//计算偏移量
						if (master.type() != CV_64F) master.convertTo(master, CV_64F);
						if (slave.type() != CV_64F) slave.convertTo(slave, CV_64F);

						move_r = 0.0; move_c = 0.0; snr_val = 0.0;
						int matchRet = regis.real_coherent(master, slave, &move_r, &move_c, &snr_val);
						if (cancellationRequested()) return -2;
						if (matchRet >= 0 && snr_val >= robustFitSettings.minimumBlockMatchSnr) {
							offset_r.at<double>(j, k) = move_r;
							offset_c.at<double>(j, k) = move_c;
							snr_values.at<double>(j, k) = snr_val;
							eligibleMask.at<uchar>(j, k) = 1;
						} else {
							eligibleMask.at<uchar>(j, k) = 0;
						}
					}
					int current_done = ++completed_blocks;
					int step = std::max(1, total_blocks / 50);
					if (current_done % step == 0 || current_done == total_blocks) {
						double block_ratio = double(current_done) / double(total_blocks);
						double current_prog = pairStart + block_ratio * (matchingEnd - pairStart);
						int progress_pct = int(current_prog);
						int prev = max_reported_pct.load();
						while (progress_pct > prev && !max_reported_pct.compare_exchange_weak(prev, progress_pct)) {
							// Keep trying
						}
						if (progress_pct > prev) {
							#pragma omp critical(coreg_progress_2)
							{
								emit updateProcess(progress_pct, QStringLiteral("第%1对图像配准中：%2%")
									.arg(ii + 1).arg(int(block_ratio * 100)));
							}
						}
					}
				}
			}
			if (cancellationRequested()) return -2;
		}


		//剔除outliers
		Mat sentinel = Mat::zeros(m, n, CV_64F);
		std::vector<double> rawRows;
		std::vector<double> rawCols;
		std::vector<double> rawSnr;
		rawRows.reserve(total_blocks);
		rawCols.reserve(total_blocks);
		rawSnr.reserve(total_blocks);
		for (int blockRow = 0; blockRow < m; ++blockRow)
		{
			for (int blockCol = 0; blockCol < n; ++blockCol)
			{
				if (eligibleMask.at<uchar>(blockRow, blockCol) == 1) {
					rawRows.push_back(offset_r.at<double>(blockRow, blockCol));
					rawCols.push_back(offset_c.at<double>(blockRow, blockCol));
					rawSnr.push_back(snr_values.at<double>(blockRow, blockCol));
				}
			}
		}
		const int rawEligibleCount = static_cast<int>(rawRows.size());
		if (rawEligibleCount > 0)
		{
			auto medianOf = [](std::vector<double> values) {
				std::sort(values.begin(), values.end());
				const size_t middle = values.size() / 2;
				return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) * 0.5 : values[middle];
			};
			double minRow, maxRow, minCol, maxCol, minSnr, maxSnr;
			cv::minMaxLoc(offset_r, &minRow, &maxRow, nullptr, nullptr, eligibleMask);
			cv::minMaxLoc(offset_c, &minCol, &maxCol, nullptr, nullptr, eligibleMask);
			cv::minMaxLoc(snr_values, &minSnr, &maxSnr, nullptr, nullptr, eligibleMask);
			const QString slaveName = QFileInfo(QString::fromStdString(SAR_images[ii])).fileName();
			InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: grid=%2x%3, accepted=%4/%5; dy(mean=%6, median=%7, min=%8, max=%9); dx(mean=%10, median=%11, min=%12, max=%13); snr(mean=%14, median=%15, min=%16, max=%17), minimumSNR=%18")
				.arg(slaveName).arg(m).arg(n).arg(rawEligibleCount).arg(total_blocks)
				.arg(cv::mean(offset_r, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawRows), 0, 'f', 4).arg(minRow, 0, 'f', 4).arg(maxRow, 0, 'f', 4)
				.arg(cv::mean(offset_c, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawCols), 0, 'f', 4).arg(minCol, 0, 'f', 4).arg(maxCol, 0, 'f', 4)
				.arg(cv::mean(snr_values, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawSnr), 0, 'f', 4).arg(minSnr, 0, 'f', 4).arg(maxSnr, 0, 'f', 4)
				.arg(robustFitSettings.minimumBlockMatchSnr, 0, 'f', 3));
		}
		else
		{
			InSARLogManager::LogWarning("CoregistrationWorker", QString("Registration diagnostics [%1]: no blocks passed real_coherent/SNR filtering (minimumSNR=%2).").arg(QFileInfo(QString::fromStdString(SAR_images[ii])).fileName()).arg(robustFitSettings.minimumBlockMatchSnr, 0, 'f', 3));
		}

		const QString diagnosticSlaveName = QFileInfo(QString::fromStdString(SAR_images[ii])).fileName();
		std::vector<RobustCoregistrationBlockMatch> blockMatches(static_cast<size_t>(total_blocks));
		for (int blockRow = 0; blockRow < m; ++blockRow)
		{
			for (int blockCol = 0; blockCol < n; ++blockCol)
			{
				RobustCoregistrationBlockMatch& match = blockMatches[static_cast<size_t>(blockRow * n + blockCol)];
				match.structSize = sizeof(RobustCoregistrationBlockMatch);
				match.version = 1;
				match.blockRow = blockRow;
				match.blockColumn = blockCol;
				match.centerRow = offset_coord_row.at<double>(blockRow, blockCol);
				match.centerColumn = offset_coord_col.at<double>(blockRow, blockCol);
				match.dy = offset_r.at<double>(blockRow, blockCol);
				match.dx = offset_c.at<double>(blockRow, blockCol);
				match.snr = snr_values.at<double>(blockRow, blockCol);
				match.valid = eligibleMask.at<uchar>(blockRow, blockCol) == 1 ? 1 : 0;
			}
		}

		RobustCoregistrationConfig baseConfig = {};
		baseConfig.structSize = sizeof(RobustCoregistrationConfig);
		int configStatus = GetDefaultRobustCoregistrationConfig(&baseConfig);
		if (configStatus != ROBUST_COREGISTRATION_SUCCESS)
		{
			InSARLogManager::LogWarning("CoregistrationWorker", QString("Registration diagnostics [%1]: unable to initialize robust coregistration configuration (status=%2).").arg(diagnosticSlaveName).arg(configStatus));
			return -1;
		}
		baseConfig.minimumBlockMatchSnr = robustFitSettings.minimumBlockMatchSnr;
		baseConfig.minimumInlierCount = robustFitSettings.minimumInlierCount;
		baseConfig.minimumInlierRatio = robustFitSettings.minimumInlierRatio;
		baseConfig.maximumDesignConditionNumber = robustFitSettings.maximumDesignConditionNumber;
		baseConfig.minimumOccupiedGridCells = robustFitSettings.minimumOccupiedGridCells;
		baseConfig.residualFloorPixels = robustFitSettings.residualFloorPixels;
		baseConfig.huberCutoffSigma = robustFitSettings.huberCutoffSigma;
		baseConfig.inlierSigma = robustFitSettings.inlierSigma;

		RobustCoregistrationImageSize imageSize = {};
		imageSize.structSize = sizeof(RobustCoregistrationImageSize);
		imageSize.version = 1;
		imageSize.rows = rows;
		imageSize.columns = cols;
		const auto runRobustFit = [&](const RobustCoregistrationConfig& config,
			RobustCoregistrationResult& result, std::vector<unsigned char>& inlierFlags,
			std::vector<char>& diagnosticBuffer) {
			result = {};
			result.structSize = sizeof(RobustCoregistrationResult);
			result.version = 1;
			inlierFlags.assign(static_cast<size_t>(total_blocks), 0);
			diagnosticBuffer.assign(1024, '\0');
			const int callStatus = FitAffineRobustCoregistration(
				blockMatches.data(), total_blocks, &imageSize, &config,
				robustCoregistrationCancelCallback, this,
				inlierFlags.data(), static_cast<int>(inlierFlags.size()),
				&result, diagnosticBuffer.data(), static_cast<int>(diagnosticBuffer.size()));
			return callStatus != ROBUST_COREGISTRATION_SUCCESS ? callStatus : result.statusCode;
		};

		// Shadow comparison uses the same block matches. The 1B RANSAC result is
		// the production fit; 1A remains a diagnostics-only reference.
		RobustCoregistrationConfig baselineConfig = baseConfig;
		baselineConfig.enableRansac = 0;
		RobustCoregistrationResult robustResult = {};
		std::vector<unsigned char> inlierFlags;
		std::vector<char> diagnosticBuffer;
		const int baselineStatus = runRobustFit(baselineConfig, robustResult, inlierFlags, diagnosticBuffer);
		if (baselineStatus == ROBUST_COREGISTRATION_CANCELLED) {
			return -2;
		}

		RobustCoregistrationConfig ransacConfig = baseConfig;
		ransacConfig.enableRansac = 1;
		RobustCoregistrationResult ransacResult = {};
		std::vector<unsigned char> ransacInlierFlags;
		std::vector<char> ransacDiagnosticBuffer;
		const int ransacStatus = runRobustFit(ransacConfig, ransacResult, ransacInlierFlags, ransacDiagnosticBuffer);
		if (ransacStatus == ROBUST_COREGISTRATION_CANCELLED) {
			return -2;
		}

		const bool baselineSucceeded = baselineStatus == ROBUST_COREGISTRATION_SUCCESS;
		const bool ransacSucceeded = ransacStatus == ROBUST_COREGISTRATION_SUCCESS;
		if (baselineSucceeded && ransacSucceeded)
		{
			int sharedInliers = 0;
			int unionInliers = 0;
			for (int matchIndex = 0; matchIndex < total_blocks; ++matchIndex)
			{
				const bool baselineInlier = inlierFlags[static_cast<size_t>(matchIndex)] != 0;
				const bool ransacInlier = ransacInlierFlags[static_cast<size_t>(matchIndex)] != 0;
				sharedInliers += baselineInlier && ransacInlier ? 1 : 0;
				unionInliers += baselineInlier || ransacInlier ? 1 : 0;
			}
			double maximumCoefficientDelta = 0.0;
			for (int coefficient = 0; coefficient < 3; ++coefficient)
			{
				maximumCoefficientDelta = std::max(maximumCoefficientDelta,
					std::abs(ransacResult.rowCoefficients[coefficient] - robustResult.rowCoefficients[coefficient]));
				maximumCoefficientDelta = std::max(maximumCoefficientDelta,
					std::abs(ransacResult.columnCoefficients[coefficient] - robustResult.columnCoefficients[coefficient]));
			}
			InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration shadow comparison [%1]: active=1B RANSAC, reference=1A; baselineInliers=%2, ransacInliers=%3, shared/union=%4/%5, maxCoefficientDelta=%6, topLeftDelta=(dy=%7, dx=%8), residualDelta=%9; RANSAC candidates=%10, validHypotheses=%11, degenerateHypotheses=%12, iterations=%13, bestConsensus=%14/%15. %16")
				.arg(diagnosticSlaveName).arg(robustResult.finalInlierCount).arg(ransacResult.finalInlierCount)
				.arg(sharedInliers).arg(unionInliers).arg(maximumCoefficientDelta, 0, 'g', 8)
				.arg(ransacResult.topLeftDy - robustResult.topLeftDy, 0, 'g', 8)
				.arg(ransacResult.topLeftDx - robustResult.topLeftDx, 0, 'g', 8)
				.arg(ransacResult.residualRms - robustResult.residualRms, 0, 'g', 8)
				.arg(ransacResult.ransacCandidateCount).arg(ransacResult.ransacValidHypothesisCount)
				.arg(ransacResult.ransacDegenerateHypothesisCount).arg(ransacResult.ransacIterations)
				.arg(ransacResult.ransacBestConsensusCount).arg(ransacResult.ransacBestConsensusRatio, 0, 'f', 3)
				.arg(QString::fromLocal8Bit(ransacDiagnosticBuffer.data())));
			InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1] 1B RANSAC: prefiltered=%2, inliers=%3/%4 (%5), irlsIterations=%6, finalRefits=%7, sigma=%8, coverage=%9/9, bands=%10x%11, rank=%12, condition=%13, residual(rms=%14, p95=%15, rowRms=%16, colRms=%17), row=[%18, %19, %20], col=[%21, %22, %23], center dy=%24, dx=%25, topLeft dy=%26, dx=%27.")
				.arg(diagnosticSlaveName).arg(ransacResult.prefilteredCount).arg(ransacResult.finalInlierCount).arg(ransacResult.prefilteredCount).arg(ransacResult.finalInlierRatio, 0, 'f', 3).arg(ransacResult.irlsIterations).arg(ransacResult.finalRefitPasses)
				.arg(ransacResult.robustSigma, 0, 'f', 6).arg(ransacResult.occupiedGridCellCount).arg(ransacResult.occupiedRowBandCount).arg(ransacResult.occupiedColumnBandCount)
				.arg(ransacResult.designRank).arg(ransacResult.conditionNumber, 0, 'g', 6)
				.arg(ransacResult.residualRms, 0, 'f', 6).arg(ransacResult.residualP95, 0, 'f', 6).arg(ransacResult.rowResidualRms, 0, 'f', 6).arg(ransacResult.columnResidualRms, 0, 'f', 6)
				.arg(ransacResult.rowCoefficients[0], 0, 'f', 8).arg(ransacResult.rowCoefficients[1], 0, 'f', 8).arg(ransacResult.rowCoefficients[2], 0, 'f', 8)
				.arg(ransacResult.columnCoefficients[0], 0, 'f', 8).arg(ransacResult.columnCoefficients[1], 0, 'f', 8).arg(ransacResult.columnCoefficients[2], 0, 'f', 8)
				.arg(ransacResult.centerDy, 0, 'f', 6).arg(ransacResult.centerDx, 0, 'f', 6)
				.arg(ransacResult.topLeftDy, 0, 'f', 6).arg(ransacResult.topLeftDx, 0, 'f', 6));
		}
		else
		{
			InSARLogManager::LogWarning("CoregistrationWorker", QString("Registration shadow comparison [%1]: reference 1A status=%2 (%3); active 1B RANSAC status=%4 (%5).")
				.arg(diagnosticSlaveName).arg(baselineStatus).arg(QString::fromLocal8Bit(diagnosticBuffer.data()))
				.arg(ransacStatus).arg(QString::fromLocal8Bit(ransacDiagnosticBuffer.data())));
		}
		if (!ransacSucceeded)
		{
			return -1;
		}

		Mat coef_r(3, 1, CV_64F);
		Mat coef_c(3, 1, CV_64F);
		for (int coefficient = 0; coefficient < 3; ++coefficient)
		{
			coef_r.at<double>(coefficient, 0) = ransacResult.rowCoefficients[coefficient];
			coef_c.at<double>(coefficient, 0) = ransacResult.columnCoefficients[coefficient];
		}
		const double offset_x = static_cast<double>(cols) / 2.0;
		const double offset_y = static_cast<double>(rows) / 2.0;
		const double scale_x = static_cast<double>(cols);
		const double scale_y = static_cast<double>(rows);
		const double centerRow = static_cast<double>(rows) / 2.0;
		const double centerCol = static_cast<double>(cols) / 2.0;
		offset_row_out.at<double>(ii, 0) = ransacResult.topLeftDy;
		offset_col_out.at<double>(ii, 0) = ransacResult.topLeftDx;

		if (baselineSucceeded)
		{
			InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1] 1A baseline: prefiltered=%2, inliers=%3/%4 (%5), irlsIterations=%6, finalRefits=%7, sigma=%8, coverage=%9/9, bands=%10x%11, rank=%12, condition=%13, residual(rms=%14, p95=%15, rowRms=%16, colRms=%17), row=[%18, %19, %20], col=[%21, %22, %23], center dy=%24, dx=%25. %26")
				.arg(diagnosticSlaveName).arg(robustResult.prefilteredCount).arg(robustResult.finalInlierCount).arg(robustResult.prefilteredCount).arg(robustResult.finalInlierRatio, 0, 'f', 3).arg(robustResult.irlsIterations).arg(robustResult.finalRefitPasses)
				.arg(robustResult.robustSigma, 0, 'f', 6).arg(robustResult.occupiedGridCellCount).arg(robustResult.occupiedRowBandCount).arg(robustResult.occupiedColumnBandCount)
				.arg(robustResult.designRank).arg(robustResult.conditionNumber, 0, 'g', 6)
				.arg(robustResult.residualRms, 0, 'f', 6).arg(robustResult.residualP95, 0, 'f', 6).arg(robustResult.rowResidualRms, 0, 'f', 6).arg(robustResult.columnResidualRms, 0, 'f', 6)
				.arg(robustResult.rowCoefficients[0], 0, 'f', 8).arg(robustResult.rowCoefficients[1], 0, 'f', 8).arg(robustResult.rowCoefficients[2], 0, 'f', 8)
				.arg(robustResult.columnCoefficients[0], 0, 'f', 8).arg(robustResult.columnCoefficients[1], 0, 'f', 8).arg(robustResult.columnCoefficients[2], 0, 'f', 8)
				.arg(robustResult.centerDy, 0, 'f', 6).arg(robustResult.centerDx, 0, 'f', 6).arg(QString::fromLocal8Bit(diagnosticBuffer.data())));
		}

		/*---------------------------------------*/
		/*    双线性插值获取重采样后的辅图像     */
		/*---------------------------------------*/

		//获取辅图像左上角相对于主图像的偏移量
		Mat tt(1, 3, CV_64F);
		tt.at<double>(0, 0) = 1.0;
		tt.at<double>(0, 1) = (0.0 - offset_x) / scale_x;
		tt.at<double>(0, 2) = (0.0 - offset_y) / scale_y;
		offset_row_out.at<double>(ii, 0) = sum(tt * coef_r)[0];
		offset_col_out.at<double>(ii, 0) = sum(tt * coef_c)[0];

		emit updateProcess(qRound(matchingEnd), QStringLiteral("正在读取第%1/%2对辅图像……")
			.arg(slave_idx + 1).arg(num_slaves));
		ComplexMat slave1;
		ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave1);
		if (cancellationRequested()) return -2;
		if (ret < 0) return -1;
		type = slave1.type();
		ComplexMat slave_tmp;
		InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1] 1B RANSAC: manual-bilinear resample input=%2x%3 type=%4, output=%5x%6, center source=(r=%7, c=%8), topLeft dy=%9, dx=%10.")
			.arg(diagnosticSlaveName).arg(slave1.GetCols()).arg(slave1.GetRows()).arg(type).arg(cols).arg(rows)
			.arg(centerRow + ransacResult.centerDy, 0, 'f', 6).arg(centerCol + ransacResult.centerDx, 0, 'f', 6)
			.arg(offset_row_out.at<double>(ii, 0), 0, 'f', 6).arg(offset_col_out.at<double>(ii, 0), 0, 'f', 6));
		ResampleSlaveInverseWithAffineOffset(slave1, slave_tmp, rows, cols,
			coef_r, coef_c, offset_x, offset_y, scale_x, scale_y, this,
			qRound(matchingEnd), qRound(resamplingEnd), slave_idx + 1, num_slaves);
		if (cancellationRequested()) return -2;

		emit updateProcess(qRound(resamplingEnd), QStringLiteral("正在写入第%1/%2对配准结果……")
			.arg(slave_idx + 1).arg(num_slaves));
		ret = conversion.write_slc_to_h5(SAR_images_out[ii].c_str(), slave_tmp);
		if (cancellationRequested()) return -2;
		if (ret < 0) return -1;
		emit updateProcess(qRound(pairEnd), QStringLiteral("第%1/%2对图像配准完成……")
			.arg(slave_idx + 1).arg(num_slaves));
		slave_idx++;
	}
	return 0;
}

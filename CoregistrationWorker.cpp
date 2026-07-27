#include "CoregistrationWorker.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include <Utils.h>
#include <Registration.h>
#include <QDir>
#include <QThread>
#include <QMessageBox>
#include <QCoreApplication>
#include <QFile>
#include <atomic>
#include <algorithm>
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
    double offsetX, double offsetY, double scaleX, double scaleY, CoregistrationWorker* worker)
{
    const int rowsSlave = slave.GetRows();
    const int colsSlave = slave.GetCols();
    const int type = slave.type();
    out.re = Mat::zeros(outputRows, outputCols, type);
    out.im = Mat::zeros(outputRows, outputCols, type);

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

void CoregistrationWorker::Regis(QList<int> para, QString save_path, QString project_name, QString Cut_name, QString file_name, QStandardItemModel* model)
{
	if (para.size() != 4 ||
		save_path.isEmpty() ||
		project_name.isEmpty() ||
		Cut_name.isEmpty() ||
		file_name.isEmpty())
	{
		return;
	}
	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) return;
	save_path = model->item(project->row(), 1)->text();
    QDir dir(save_path);
    if (!dir.exists(file_name))
        int ret = dir.mkdir(file_name);
    int index = para.at(0);
    int interp_times = para.at(1);
    int block_size = para.at(2);
    int image_number = para.at(3);
    Utils util;
    vector<cv::String> SAR_images;
    vector<cv::String> SAR_images_regis;
    QList<QString> origin;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* images = project->child(i,0);
        if (images->text() == Cut_name)
        {
            for (int j = 0; j < images->rowCount(); j++)
            {
				QFileInfo fileinfo(images->child(j, 1)->text());
                QString origin_name = fileinfo.baseName();
                origin.append(origin_name);
                SAR_images.push_back(images->child(j, 1)->text().toStdString());
                QString outName = resolveOutputFileName(origin_name);
                if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
                    outName += ".h5";
                }
                SAR_images_regis.push_back(QString("%1/%2/%3").arg(save_path).arg(file_name)
                    .arg(outName).toStdString());
            }
			break;
        }
    }
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
    /*建立配准根节点*/
    QStandardItem* regis = NodeUtils::findOrCreateProjectNode(project, file_name, "complex-2.0");
    if (regis)
    {
        regis->setToolTip(project_name);
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
        if (regis && regis->model()) {
            QMetaObject::invokeMethod(regis->model(), [=]() {
                QFileInfo fileinfo = QFileInfo(QString(SAR_images_regis.at(i).c_str()));
                QString regis_name = fileinfo.baseName();
                QStandardItem* item_img = NULL;
                for (int j = 0; j < regis->rowCount(); j++)
                {
                    if (regis->child(j, 0)->text() == regis_name)
                    {
                        item_img = regis->child(j, 0);
                        break;
                    }
                }

                if (!item_img)
                {
                    QStandardItem* regis_images_name = new QStandardItem(regis_name);
                    regis_images_name->setToolTip("complex");
                    QStandardItem* regis_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                    regis_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                    regis->appendRow(regis_images_name);
                    regis->setChild(regis->rowCount() - 1, 1, regis_images_path);
                }
                else
                {
                    regis->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                }
            }, Qt::QueuedConnection);
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
    /*写入XML*/
    XMLFile xmlfile;
	emit updateProcess(95, QStringLiteral("写入工程文件……"));
	xmlfile.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
    for (int i = 0; i < image_number; i++)
    {
		if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
		{
			Q_EMIT cancelled();
			return;
		}
        QString outName = resolveOutputFileName(origin.at(i));
        if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
            outName += ".h5";
        }
        QString regis_name = QFileInfo(outName).baseName();
        QString relativePath = QString("/%1/%2").arg(file_name).arg(outName);
        xmlfile.XMLFile_add_regis(file_name.toStdString().c_str(), regis_name.toStdString().c_str(), relativePath.toStdString().c_str(),
            Row_offset.at(i), Col_offset.at(i), index, interp_times, block_size,
            temporal_baseline.toStdString().c_str(), B_effect.toStdString().c_str(), B_parallel.toStdString().c_str());
    }
	xmlfile.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
    emit sendModel(model);
	InSARLogManager::LogInfo("CoregistrationWorker", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

void CoregistrationWorker::DEMAssistCoregistration(
	int masterIndex,
	QString savepath, 
	QString project_name,
	QString srcNode,
	QString dstNode,
	QStandardItemModel* model
)
{
	if (savepath.isEmpty() || project_name.isEmpty() || srcNode.isEmpty() || dstNode.isEmpty() || !model)
	{
		return;
	}
	QDir dir(savepath);
	if (!dir.exists(dstNode))
		int ret = dir.mkdir(dstNode);

    QString demPath = m_demPath;
    if (demPath.isEmpty()) {
        demPath = QDir::toNativeSeparators(savepath + "/.dem_cache");
    }
	string dempath = demPath.toStdString();

	Utils util;
	FormatConversion conversion; Registration coregis;
	vector<string> SAR_images;
	vector<string> SAR_images_regis;
	QList<QString> origin;
	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) return;
	savepath = model->item(project->row(), 1)->text();
	int images_number;
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* images = project->child(i, 0);
		if (images->text() == srcNode)
		{
			images_number = images->rowCount();
			for (int j = 0; j < images_number; j++)
			{
				QFileInfo fileinfo(images->child(j, 1)->text());
				QString origin_name = fileinfo.baseName();
				origin.append(origin_name);
				SAR_images.push_back(images->child(j, 1)->text().toStdString());
                QString outName = resolveOutputFileName(origin_name);
                if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
                    outName += ".h5";
                }
				SAR_images_regis.push_back(QString("%1/%2/%3").arg(savepath).arg(dstNode)
					.arg(outName).toStdString());
			}
			break;
		}
	}
	if (SAR_images.size() < 2) return;

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

	/*建立配准根节点*/
	QStandardItem* regis = NodeUtils::findOrCreateProjectNode(project, dstNode, "complex-2.0");
	if (regis)
	{
		regis->setToolTip(project_name);
	}
	
	QString temporal_baseline, B_parallel, B_effect;
	for (int i = 0; i < images_number; i++)
	{
		if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
		{
			Q_EMIT cancelled();
			return;
		}
		if (regis && regis->model()) {
			QMetaObject::invokeMethod(regis->model(), [=]() {
				QFileInfo fileinfo = QFileInfo(QString(SAR_images_regis.at(i).c_str()));
				QString regis_name = fileinfo.baseName();
				QStandardItem* item_img = NULL;
				for (int j = 0; j < regis->rowCount(); j++)
				{
					if (regis->child(j, 0)->text() == regis_name)
					{
						item_img = regis->child(j, 0);
						break;
					}
				}

				if (!item_img)
				{
					QStandardItem* regis_images_name = new QStandardItem(regis_name);
					regis_images_name->setToolTip("complex");
					QStandardItem* regis_images_path = new QStandardItem(fileinfo.absoluteFilePath());
					regis_images_name->setIcon(QIcon(IMAGEDATA_ICON));
					regis->appendRow(regis_images_name);
					regis->setChild(regis->rowCount() - 1, 1, regis_images_path);
				}
				else
				{
					regis->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
				}
			}, Qt::QueuedConnection);
		}
		
		temporal_baseline += "0 ";
		B_parallel += "0 ";
		B_effect += "0 ";
	}

	/*写入XML*/
	XMLFile xmlfile;
	emit updateProcess(95, QStringLiteral("写入工程文件……"));
	xmlfile.XMLFile_load((QString(savepath) + "/" + project_name).toStdString().c_str());

	for (int i = 0; i < images_number; i++)
	{
		if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
		{
			Q_EMIT cancelled();
			return;
		}
        QString outName = resolveOutputFileName(origin.at(i));
        if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
            outName += ".h5";
        }
        QString regis_name = QFileInfo(outName).baseName();
		QString relativePath = QString("/%1/%2").arg(dstNode).arg(outName);
		xmlfile.XMLFile_add_regis(dstNode.toStdString().c_str(), regis_name.toStdString().c_str(),
			relativePath.toStdString().c_str(),
			Row_offset.at<int>(i, 0), Col_offset.at<int>(i, 0), masterIndex, -1, -1,
			temporal_baseline.toStdString().c_str(), B_effect.toStdString().c_str(), B_parallel.toStdString().c_str());

	}
	xmlfile.XMLFile_save((QString(savepath) + "/" + project_name).toStdString().c_str());
	emit sendModel(model);
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
	int n_images = SAR_images.size();
	int num_slaves = n_images - 1;
	int slave_idx = 0;
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
		double start_p = 10.0 + (70.0 / num_slaves) * slave_idx;
		emit updateProcess(int(start_p), QStringLiteral("第%1对图像处理中……").arg(ii + 1));
		std::atomic<int> completed_blocks(0);
		int init_pct = static_cast<int>(start_p);
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
						if (matchRet >= 0 && snr_val >= 3.0) {
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
						double start_p = 10.0 + (70.0 / num_slaves) * slave_idx;
						double end_p = 10.0 + (70.0 / num_slaves) * (slave_idx + 1);
						double current_prog = start_p + block_ratio * (end_p - start_p);
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
						if (matchRet >= 0 && snr_val >= 3.0) {
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
						double start_p = 10.0 + (70.0 / num_slaves) * slave_idx;
						double end_p = 10.0 + (70.0 / num_slaves) * (slave_idx + 1);
						double current_prog = start_p + block_ratio * (end_p - start_p);
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
			InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: grid=%2x%3, accepted=%4/%5; dy(mean=%6, median=%7, min=%8, max=%9); dx(mean=%10, median=%11, min=%12, max=%13); snr(mean=%14, median=%15, min=%16, max=%17)")
				.arg(slaveName).arg(m).arg(n).arg(rawEligibleCount).arg(total_blocks)
				.arg(cv::mean(offset_r, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawRows), 0, 'f', 4).arg(minRow, 0, 'f', 4).arg(maxRow, 0, 'f', 4)
				.arg(cv::mean(offset_c, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawCols), 0, 'f', 4).arg(minCol, 0, 'f', 4).arg(maxCol, 0, 'f', 4)
				.arg(cv::mean(snr_values, eligibleMask)[0], 0, 'f', 4).arg(medianOf(rawSnr), 0, 'f', 4).arg(minSnr, 0, 'f', 4).arg(maxSnr, 0, 'f', 4));
		}
		else
		{
			InSARLogManager::LogWarning("CoregistrationWorker", QString("Registration diagnostics [%1]: no blocks passed real_coherent/SNR filtering.").arg(QFileInfo(QString::fromStdString(SAR_images[ii])).fileName()));
		}

		int count = 0, c = 0; double delta, thresh = 2.0;
		for (int i = 0; i < m; i++)
		{
			for (int j = 0; j < n; j++)
			{
				if (eligibleMask.at<uchar>(i, j) == 0) {
					sentinel.at<double>(i, j) = 1.0;
					c++;
					continue;
				}

				int valid_neighbors = 0;
				int anomaly_count = 0;
				int ix, iy;

				// 上
				ix = j; iy = i - 1;
				if (iy >= 0 && eligibleMask.at<uchar>(iy, ix) == 1) {
					valid_neighbors++;
					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
					if (delta >= thresh) anomaly_count++;
				}
				// 下
				ix = j; iy = i + 1;
				if (iy < m && eligibleMask.at<uchar>(iy, ix) == 1) {
					valid_neighbors++;
					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
					if (delta >= thresh) anomaly_count++;
				}
				// 左
				ix = j - 1; iy = i;
				if (ix >= 0 && eligibleMask.at<uchar>(iy, ix) == 1) {
					valid_neighbors++;
					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
					if (delta >= thresh) anomaly_count++;
				}
				// 右
				ix = j + 1; iy = i;
				if (ix < n && eligibleMask.at<uchar>(iy, ix) == 1) {
					valid_neighbors++;
					delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix)) + fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
					if (delta >= thresh) anomaly_count++;
				}

				if (valid_neighbors >= 2 && anomaly_count >= 2) {
					sentinel.at<double>(i, j) = 1.0;
					c++;
				}
			}
		}
		Mat offset_c_0, offset_r_0, offset_coord_row_0, offset_coord_col_0;
		offset_c_0 = Mat::zeros(m * n - c, 1, CV_64F);
		offset_r_0 = Mat::zeros(m * n - c, 1, CV_64F);
		offset_coord_row_0 = Mat::zeros(m * n - c, 1, CV_64F);
		offset_coord_col_0 = Mat::zeros(m * n - c, 1, CV_64F);
		count = 0;
		for (int i = 0; i < m; i++)
		{
			for (int j = 0; j < n; j++)
			{
				if (sentinel.at<double>(i, j) < 0.5)
				{
					offset_r_0.at<double>(count, 0) = offset_r.at<double>(i, j);
					offset_c_0.at<double>(count, 0) = offset_c.at<double>(i, j);
					offset_coord_row_0.at<double>(count, 0) = offset_coord_row.at<double>(i, j);
					offset_coord_col_0.at<double>(count, 0) = offset_coord_col.at<double>(i, j);
					count++;
				}
			}
		}

		InSARLogManager::LogInfo("CoregistrationWorker",
			QString("Registration diagnostics [%1]: filtering retained=%2/%3 blocks; qualityRejected=%4, outlierRejected=%5.")
				.arg(QFileInfo(QString::fromStdString(SAR_images[ii])).fileName())
				.arg(count)
				.arg(total_blocks)
				.arg(total_blocks - rawEligibleCount)
				.arg(rawEligibleCount - count));


		m = 1; n = count;
		if (count < 10)
		{
			fprintf(stderr, "stack_coregistration(): insufficient valide sub blocks!\n");
			return -1;
		}
		//偏移量拟合（坐标做归一化处理）
		//拟合公式为 offser_row / offser_col = a0 + a1 * x + a2 * y;
		double offset_x = (double)cols / 2;
		double offset_y = (double)rows / 2;
		double scale_x = (double)cols;
		double scale_y = (double)rows;
		offset_coord_row_0 -= offset_y;
		offset_coord_col_0 -= offset_x;
		offset_coord_row_0 /= scale_y;
		offset_coord_col_0 /= scale_x;
		Mat A = Mat::ones(m * n, 3, CV_64F);
		Mat temp, A_t;
		offset_coord_col_0.copyTo(A(Range(0, m * n), Range(1, 2)));

		offset_coord_row_0.copyTo(A(Range(0, m * n), Range(2, 3)));


		cv::transpose(A, A_t);

		Mat b_r, b_c, coef_r, coef_c, error_r, error_c, b_t, a, a_t;

		A.copyTo(a);
		cv::transpose(a, a_t);
		offset_r_0.copyTo(b_r);
		b_r = A_t * b_r;

		offset_c_0.copyTo(b_c);
		b_c = A_t * b_c;

		A = A_t * A;

		double rms1 = -1.0; double rms2 = -1.0;
		Mat eye = Mat::zeros(m * n, m * n, CV_64F);
		for (int i = 0; i < m * n; i++)
		{
			eye.at<double>(i, i) = 1.0;
		}
		if (cv::invert(A, error_r, cv::DECOMP_LU) > 0)
		{
			cv::transpose(offset_r_0, b_t);
			error_r = b_t * (eye - a * error_r * a_t) * offset_r_0;
			rms1 = sqrt(error_r.at<double>(0, 0) / double(m * n));
		}
		if (cv::invert(A, error_c, cv::DECOMP_LU) > 0)
		{
			cv::transpose(offset_c_0, b_t);
			error_c = b_t * (eye - a * error_c * a_t) * offset_c_0;
			rms2 = sqrt(error_c.at<double>(0, 0) / double(m * n));
		}
		if (!cv::solve(A, b_r, coef_r, cv::DECOMP_NORMAL))
		{
			fprintf(stderr, "stack_coregistration(): matrix defficiency!\n");

			return -1;
		}
		if (!cv::solve(A, b_c, coef_c, cv::DECOMP_NORMAL))
		{
			fprintf(stderr, "stack_coregistration(): matrix defficiency!\n");
			return -1;
		}

		const double centerRow = static_cast<double>(rows / 2);
		const double centerCol = static_cast<double>(cols / 2);
		const double centerX = (centerCol - offset_x) / scale_x;
		const double centerY = (centerRow - offset_y) / scale_y;
		const double centerDy = coef_r.at<double>(0, 0) + coef_r.at<double>(1, 0) * centerX + coef_r.at<double>(2, 0) * centerY;
		const double centerDx = coef_c.at<double>(0, 0) + coef_c.at<double>(1, 0) * centerX + coef_c.at<double>(2, 0) * centerY;
		const QString diagnosticSlaveName = QFileInfo(QString::fromStdString(SAR_images[ii])).fileName();
		InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: fit row=[%2, %3, %4], col=[%5, %6, %7], rms(row=%8, col=%9), center=(r=%10, c=%11) predicts dy=%12, dx=%13.")
			.arg(diagnosticSlaveName)
			.arg(coef_r.at<double>(0, 0), 0, 'f', 8).arg(coef_r.at<double>(1, 0), 0, 'f', 8).arg(coef_r.at<double>(2, 0), 0, 'f', 8)
			.arg(coef_c.at<double>(0, 0), 0, 'f', 8).arg(coef_c.at<double>(1, 0), 0, 'f', 8).arg(coef_c.at<double>(2, 0), 0, 'f', 8)
			.arg(rms1, 0, 'f', 6).arg(rms2, 0, 'f', 6)
			.arg(centerRow, 0, 'f', 1).arg(centerCol, 0, 'f', 1).arg(centerDy, 0, 'f', 6).arg(centerDx, 0, 'f', 6));

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

		ComplexMat slave1;
		ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave1);
		if (cancellationRequested()) return -2;
		if (ret < 0) return -1;
		type = slave1.type();
		ComplexMat slave_tmp;
		InSARLogManager::LogInfo("CoregistrationWorker", QString("Registration diagnostics [%1]: manual-bilinear resample input=%2x%3 type=%4, output=%5x%6, center source=(r=%7, c=%8), topLeft dy=%9, dx=%10.")
			.arg(diagnosticSlaveName).arg(slave1.GetCols()).arg(slave1.GetRows()).arg(type).arg(cols).arg(rows)
			.arg(centerRow + centerDy, 0, 'f', 6).arg(centerCol + centerDx, 0, 'f', 6)
			.arg(offset_row_out.at<double>(ii, 0), 0, 'f', 6).arg(offset_col_out.at<double>(ii, 0), 0, 'f', 6));
		ResampleSlaveInverseWithAffineOffset(slave1, slave_tmp, rows, cols,
			coef_r, coef_c, offset_x, offset_y, scale_x, scale_y, this);
#if 0
		offset_col_out.at<double>(ii, 0) = sum(tt * coef_c)[0];

		ComplexMat slave1;
		ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave1);
		if (cancellationRequested()) return -2;
		if (ret < 0) return -1;
		int rows_slave = slave1.GetRows(); int cols_slave = slave1.GetCols();
		type = slave1.type();
		ComplexMat slave_tmp; slave_tmp.re = Mat::zeros(rows, cols, type); slave_tmp.im = Mat::zeros(rows, cols, type);
#pragma omp parallel for schedule(guided)
		for (int i = 0; i < rows; i++)
		{
			if (isStopRequested()) {
				continue;
			}
			double x, y, iiii, jjjj; Mat tmp(1, 3, CV_64F); Mat result;
			int mm0, nn0, mm1, nn1;
			double offset_rows, offset_cols, upper, lower;
			for (int j = 0; j < cols; j++)
			{
				jjjj = (double)j;
				iiii = (double)i;
				x = (jjjj - offset_x) / scale_x;
				y = (iiii - offset_y) / scale_y;
				tmp.at<double>(0, 0) = 1.0;
				tmp.at<double>(0, 1) = x;
				tmp.at<double>(0, 2) = y;
				result = tmp * coef_r;
				offset_rows = result.at<double>(0, 0);
				result = tmp * coef_c;
				offset_cols = result.at<double>(0, 0);

				iiii += offset_rows;
				jjjj += offset_cols;

				mm0 = (int)floor(iiii); nn0 = (int)floor(jjjj);
				if (mm0 < 0 || nn0 < 0 || mm0 > rows_slave - 1 || nn0 > cols_slave - 1)
				{
					if (type == CV_64F)
					{
						slave_tmp.re.at<double>(i, j) = 0;
						slave_tmp.im.at<double>(i, j) = 0;
					}
					else if (type == CV_32F)
					{
						slave_tmp.re.at<float>(i, j) = 0;
						slave_tmp.im.at<float>(i, j) = 0;
					}
					else
					{
						slave_tmp.re.at<short>(i, j) = 0;
						slave_tmp.im.at<short>(i, j) = 0;
					}
				}
				else
				{
					mm1 = mm0 + 1; nn1 = nn0 + 1;
					mm1 = mm1 >= rows_slave - 1 ? rows_slave - 1 : mm1;
					nn1 = nn1 >= cols_slave - 1 ? cols_slave - 1 : nn1;
					if (type == CV_16S)
					{
						//实部插值
						upper = (double)slave1.re.at<short>(mm0, nn0) + double(slave1.re.at<short>(mm0, nn1) - slave1.re.at<short>(mm0, nn0)) * (jjjj - (double)nn0);
						lower = (double)slave1.re.at<short>(mm1, nn0) + double(slave1.re.at<short>(mm1, nn1) - slave1.re.at<short>(mm1, nn0)) * (jjjj - (double)nn0);
						slave_tmp.re.at<short>(i, j) = upper + double(lower - upper) * (iiii - (double)mm0);
						//虚部插值
						upper = (double)slave1.im.at<short>(mm0, nn0) + double(slave1.im.at<short>(mm0, nn1) - slave1.im.at<short>(mm0, nn0)) * (jjjj - (double)nn0);
						lower = (double)slave1.im.at<short>(mm1, nn0) + double(slave1.im.at<short>(mm1, nn1) - slave1.im.at<short>(mm1, nn0)) * (jjjj - (double)nn0);
						slave_tmp.im.at<short>(i, j) = upper + double(lower - upper) * (iiii - (double)mm0);
					}
					else if (type == CV_32F)
					{
						//实部插值
						upper = slave1.re.at<float>(mm0, nn0) + (slave1.re.at<float>(mm0, nn1) - slave1.re.at<float>(mm0, nn0)) * (jjjj - (double)nn0);
						lower = slave1.re.at<float>(mm1, nn0) + (slave1.re.at<float>(mm1, nn1) - slave1.re.at<float>(mm1, nn0)) * (jjjj - (double)nn0);
						slave_tmp.re.at<float>(i, j) = upper + (lower - upper) * (iiii - (double)mm0);
						//虚部插值
						upper = slave1.im.at<float>(mm0, nn0) + (slave1.im.at<float>(mm0, nn1) - slave1.im.at<float>(mm0, nn0)) * (jjjj - (double)nn0);
						lower = slave1.im.at<float>(mm1, nn0) + (slave1.im.at<float>(mm1, nn1) - slave1.im.at<float>(mm1, nn0)) * (jjjj - (double)nn0);
						slave_tmp.im.at<float>(i, j) = upper + (lower - upper) * (iiii - (double)mm0);
					}
					else
					{
						//实部插值
						upper = slave1.re.at<double>(mm0, nn0) + (slave1.re.at<double>(mm0, nn1) - slave1.re.at<double>(mm0, nn0)) * (jjjj - (double)nn0);
						lower = slave1.re.at<double>(mm1, nn0) + (slave1.re.at<double>(mm1, nn1) - slave1.re.at<double>(mm1, nn0)) * (jjjj - (double)nn0);
						slave_tmp.re.at<double>(i, j) = upper + (lower - upper) * (iiii - (double)mm0);
						//虚部插值
						upper = slave1.im.at<double>(mm0, nn0) + (slave1.im.at<double>(mm0, nn1) - slave1.im.at<double>(mm0, nn0)) * (jjjj - (double)nn0);
						lower = slave1.im.at<double>(mm1, nn0) + (slave1.im.at<double>(mm1, nn1) - slave1.im.at<double>(mm1, nn0)) * (jjjj - (double)nn0);
						slave_tmp.im.at<double>(i, j) = upper + (lower - upper) * (iiii - (double)mm0);
					}

				}

			}
		}
#endif
		if (cancellationRequested()) return -2;

		ret = conversion.write_slc_to_h5(SAR_images_out[ii].c_str(), slave_tmp);
		if (cancellationRequested()) return -2;
		if (ret < 0) return -1;
		double end_p = 10.0 + (70.0 / num_slaves) * (slave_idx + 1);
		emit updateProcess(int(end_p), QStringLiteral("第%1对图像处理中……").arg(ii + 1));
		slave_idx++;
	}
	return 0;
}

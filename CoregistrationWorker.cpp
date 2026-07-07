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
    if (ret<0 || QThread::currentThread()->isInterruptionRequested())
    {
		InSARLogManager::LogError("CoregistrationWorker", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
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
    int offset_row, offset_col;
    int Rows, Cols;
    double time_Master = 0;
    string time_master_str;
    {
        NodeUtils::Hdf5Locker locker;
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "state_vec", State_Vec_Master);
        if (!State_Vec_Master.empty() && State_Vec_Master.type() != CV_64F) State_Vec_Master.convertTo(State_Vec_Master, CV_64F);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lon_coefficient", Lon_Coeff_Master);
        if (!Lon_Coeff_Master.empty() && Lon_Coeff_Master.type() != CV_64F) Lon_Coeff_Master.convertTo(Lon_Coeff_Master, CV_64F);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lat_coefficient", Lat_Coeff_Master);
        if (!Lat_Coeff_Master.empty() && Lat_Coeff_Master.type() != CV_64F) Lat_Coeff_Master.convertTo(Lat_Coeff_Master, CV_64F);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "prf", tmp_double);
        if (!tmp_double.empty() && tmp_double.type() != CV_64F) {
            tmp_double.convertTo(tmp_double, CV_64F);
        }
        interp_interval = 1 / tmp_double.at<double>(0, 0);
        Mat tmp = Mat::zeros(1, 1, CV_32SC1);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "offset_row", tmp);
        offset_row = tmp.at<int>(0, 0);
        FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "offset_col", tmp);
        offset_col = tmp.at<int>(0, 0);
        FC.read_str_from_h5(SAR_images.at(index - 1).c_str(), "acquisition_start_time", time_master_str);
        FC.utc2gps(time_master_str.c_str(), &time_Master);
        ComplexMat SLC;
        FC.read_slc_from_h5(SAR_images_regis.at(index - 1).c_str(), SLC);
        Rows = SLC.GetRows();
        Cols = SLC.GetCols();
    }
    QString temporal_baseline, B_parallel, B_effect;
    /*添加图像到model中并复制h5参数*/
    vector<int> Row_offset;
    vector<int> Col_offset;
	emit updateProcess(90, QStringLiteral("写入辅助参数……"));
    for (int i = 0; i < image_number; i++)
    {
		if (QThread::currentThread()->isInterruptionRequested())
		{
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
		offset_row = offset_col = 0;
        {
            NodeUtils::Hdf5Locker locker;
            FC.Copy_para_from_h5_2_h5(SAR_images.at(i).c_str(), SAR_images_regis.at(i).c_str());
            FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "process_state", "coregistration");
            FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "comment", "complex-2.0");
            FC.read_int_from_h5(SAR_images.at(i).c_str(), "offset_row", &offset_row);
		    offset_row += offset_row_out.at<int>(i, 0);
            FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_row", offset_row);
            Row_offset.push_back(offset_row);
            FC.read_int_from_h5(SAR_images.at(i).c_str(), "offset_col", &offset_col);
		    offset_col += offset_col_out.at<int>(i, 0);
            FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_col", offset_col);
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
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "state_vec", State_Vec_Slave);
                if (!State_Vec_Slave.empty() && State_Vec_Slave.type() != CV_64F) State_Vec_Slave.convertTo(State_Vec_Slave, CV_64F);
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "lon_coefficient", Lon_Coeff_Slave);
                if (!Lon_Coeff_Slave.empty() && Lon_Coeff_Slave.type() != CV_64F) Lon_Coeff_Slave.convertTo(Lon_Coeff_Slave, CV_64F);
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "lat_coefficient", Lat_Coeff_Slave);
                if (!Lat_Coeff_Slave.empty() && Lat_Coeff_Slave.type() != CV_64F) Lat_Coeff_Slave.convertTo(Lat_Coeff_Slave, CV_64F);
                FC.read_array_from_h5(SAR_images.at(i).c_str(), "prf", tmp_double);
                if (!tmp_double.empty() && tmp_double.type() != CV_64F) {
                    tmp_double.convertTo(tmp_double, CV_64F);
                }
                interp_interval_slave = 1 / tmp_double.at<double>(0, 0);
                FC.read_str_from_h5(SAR_images.at(i).c_str(), "acquisition_start_time", time_slave_str);
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
		if (QThread::currentThread()->isInterruptionRequested())
		{
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

	//外部DEM文件夹
    QString demPath = m_demPath;
    if (demPath.isEmpty()) {
        QString appPath = QCoreApplication::applicationDirPath();
        demPath = appPath + "/dem";
        QDir appDir(appPath);
        if (!appDir.exists("dem")) appDir.mkdir("dem");
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
		conversion.read_int_from_h5(master_file, "range_len", &sceneWidth);
		conversion.read_int_from_h5(master_file, "azimuth_len", &sceneHeight);
		conversion.read_int_from_h5(master_file, "offset_row", &offset_row);
		conversion.read_int_from_h5(master_file, "offset_col", &offset_col);
		conversion.read_array_from_h5(master_file, "lon_coefficient", lon_coef);
		conversion.read_array_from_h5(master_file, "lat_coefficient", lat_coef);
		conversion.read_double_from_h5(master_file, "prf", &prf);
		conversion.read_double_from_h5(master_file, "carrier_frequency", &wavelength);
		wavelength = VEL_C / wavelength;
		conversion.read_double_from_h5(master_file, "range_spacing", &rangeSpacing);
		conversion.read_double_from_h5(master_file, "slant_range_first_pixel", &nearRangeTime);
		nearRangeTime = 2.0 * nearRangeTime / VEL_C;
		conversion.read_str_from_h5(master_file, "acquisition_start_time", start_time);
		conversion.utc2gps(start_time.c_str(), &start);
		conversion.read_str_from_h5(master_file, "acquisition_stop_time", end_time);
		conversion.utc2gps(end_time.c_str(), &end);
		conversion.read_array_from_h5(master_file, "state_vec", statevec);
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
			return;
		}
		int offset_r, offset_c;
		offset_row2 = offset_col2 = 0;
		slave_file = SAR_images[i].c_str();
		{
			NodeUtils::Hdf5Locker locker;
			conversion.read_int_from_h5(slave_file, "range_len", &sceneWidth2);
			conversion.read_int_from_h5(slave_file, "azimuth_len", &sceneHeight2);
			conversion.read_int_from_h5(slave_file, "offset_row", &offset_row2);
			conversion.read_int_from_h5(slave_file, "offset_col", &offset_col2);
			conversion.read_array_from_h5(slave_file, "lon_coefficient", lon_coef2);
			conversion.read_array_from_h5(slave_file, "lat_coefficient", lat_coef2);
			conversion.read_double_from_h5(slave_file, "prf", &prf2);
			conversion.read_double_from_h5(slave_file, "range_spacing", &rangeSpacing2);
			conversion.read_double_from_h5(slave_file, "slant_range_first_pixel", &nearRangeTime2);
			nearRangeTime2 = 2.0 * nearRangeTime2 / VEL_C;
			conversion.read_str_from_h5(slave_file, "acquisition_start_time", start_time);
			conversion.utc2gps(start_time.c_str(), &start2);
			conversion.read_str_from_h5(slave_file, "acquisition_stop_time", end_time);
			conversion.utc2gps(end_time.c_str(), &end2);
			conversion.read_array_from_h5(slave_file, "state_vec", statevec2);
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
		if (QThread::currentThread()->isInterruptionRequested())
		{
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
		if (QThread::currentThread()->isInterruptionRequested())
		{
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
	int n_images = SAR_images.size();
	int num_slaves = n_images - 1;
	int slave_idx = 0;
	offset_col_out.create(n_images, 1, CV_32S);
	offset_row_out.create(n_images, 1, CV_32S);
	Mat images_rows, images_cols, tmp;
	images_rows = Mat::zeros(n_images, 1, CV_32S); images_cols = Mat::zeros(n_images, 1, CV_32S);
	for (int i = 0; i < n_images; i++)
	{
		ret = conversion.creat_new_h5(SAR_images_out[i].c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
		ret = conversion.read_array_from_h5(SAR_images[i].c_str(), "range_len", tmp);
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
		images_cols.at<int>(i, 0) = tmp.at<int>(0, 0);

		ret = conversion.read_array_from_h5(SAR_images[i].c_str(), "azimuth_len", tmp);
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
		images_rows.at<int>(i, 0) = tmp.at<int>(0, 0);
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
	Mat offset_r = Mat::zeros(m, n, CV_64F); Mat offset_c = Mat::zeros(m, n, CV_64F);
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
		if (ii == Master_index - 1)
		{
			offset_row_out.at<int>(ii, 0) = 0;
			offset_col_out.at<int>(ii, 0) = 0;
			continue;
		}
		if (!b_block)//不分块读取
		{
			if (!master_read)
			{
				ret = conversion.read_slc_from_h5(SAR_images[Master_index - 1].c_str(), master_w);
				if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
				master_read = true;
				type = master_w.type();
				ret = conversion.write_slc_to_h5(SAR_images_out[Master_index - 1].c_str(), master_w);//写主图像
			}

			ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave_w);
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
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
				if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
				master_read = true;
				type = master_w.type();
				ret = conversion.write_slc_to_h5(SAR_images_out[Master_index - 1].c_str(), master_w);//写主图像
			}
		}
		
		//分块读取并计算偏移量
		double start_p = 10.0 + (70.0 / num_slaves) * slave_idx;
		emit updateProcess(int(start_p), QStringLiteral("第%1对图像处理中……").arg(ii + 1));
		std::atomic<int> completed_blocks(0);
		int total_blocks = m * n;
		int mm, nn;
		mm = images_rows.at<int>(ii, 0) / blocksize;
		nn = images_cols.at<int>(ii, 0) / blocksize;
		if (!b_block)
		{
# pragma omp parallel for schedule(guided)

			for (int j = 0; j < m; j++)
			{
				if (isStopRequested()) {
					continue;
				}
				int offset_row, offset_col, move_r, move_c;
				ComplexMat master, slave, master_interp, slave_interp;
				for (int k = 0; k < n; k++)
				{
					offset_row = j * blocksize; offset_col = k * blocksize;
					if ((j + 1) * blocksize < images_rows.at<int>(ii, 0) && (k + 1) * blocksize < images_cols.at<int>(ii, 0))
					{
						master = master_w(cv::Range(offset_row, offset_row + blocksize), cv::Range(offset_col, offset_col + blocksize));
						slave = slave_w(cv::Range(offset_row, offset_row + blocksize), cv::Range(offset_col, offset_col + blocksize));

						//计算偏移量
						if (master.type() != CV_64F) master.convertTo(master, CV_64F);
						if (slave.type() != CV_64F) slave.convertTo(slave, CV_64F);
						move_r = 0; move_c = 0;
						ret = regis.interp_paddingzero(master, master_interp, interp_times);
						ret = regis.interp_paddingzero(slave, slave_interp, interp_times);
						ret = regis.real_coherent(master_interp, slave_interp, &move_r, &move_c);
						offset_r.at<double>(j, k) = double(move_r) / double(interp_times);
						offset_c.at<double>(j, k) = double(move_c) / double(interp_times);
					}
					int current_done = ++completed_blocks;
					int step = std::max(1, total_blocks / 50);
					if (current_done % step == 0 || current_done == total_blocks) {
						double block_ratio = double(current_done) / double(total_blocks);
						double start_p = 10.0 + (70.0 / num_slaves) * slave_idx;
						double end_p = 10.0 + (70.0 / num_slaves) * (slave_idx + 1);
						double current_prog = start_p + block_ratio * (end_p - start_p);
						#pragma omp critical
						{
							emit updateProcess(int(current_prog), QStringLiteral("第%1对图像配准中：%2%")
								.arg(ii + 1).arg(int(block_ratio * 100)));
						}
					}
				}
			}

		}
		else
		{
			int offset_row, offset_col, move_r, move_c;
			ComplexMat master, slave, master_interp, slave_interp;
			for (int j = 0; j < m; j++)
			{
				if (isStopRequested()) {
					break;
				}
				for (int k = 0; k < n; k++)
				{
					offset_row = j * blocksize; offset_col = k * blocksize;
					if ((j + 1) * blocksize < images_rows.at<int>(ii, 0) && (k + 1) * blocksize < images_cols.at<int>(ii, 0))
					{
						ret = conversion.read_subarray_from_h5(SAR_images[Master_index - 1].c_str(), "s_im", offset_row, offset_col, blocksize, blocksize, master.im);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
						ret = conversion.read_subarray_from_h5(SAR_images[Master_index - 1].c_str(), "s_re", offset_row, offset_col, blocksize, blocksize, master.re);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
						ret = conversion.read_subarray_from_h5(SAR_images[ii].c_str(), "s_im", offset_row, offset_col, blocksize, blocksize, slave.im);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
						ret = conversion.read_subarray_from_h5(SAR_images[ii].c_str(), "s_re", offset_row, offset_col, blocksize, blocksize, slave.re);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;

						//计算偏移量
						if (master.type() != CV_64F) master.convertTo(master, CV_64F);
						if (slave.type() != CV_64F) slave.convertTo(slave, CV_64F);

						ret = regis.interp_paddingzero(master, master_interp, interp_times);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
						ret = regis.interp_paddingzero(slave, slave_interp, interp_times);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
						ret = regis.real_coherent(master_interp, slave_interp, &move_r, &move_c);
						if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
						offset_r.at<double>(j, k) = double(move_r) / double(interp_times);
						offset_c.at<double>(j, k) = double(move_c) / double(interp_times);
					}
					int current_done = ++completed_blocks;
					int step = std::max(1, total_blocks / 50);
					if (current_done % step == 0 || current_done == total_blocks) {
						double block_ratio = double(current_done) / double(total_blocks);
						double start_p = 10.0 + (70.0 / num_slaves) * slave_idx;
						double end_p = 10.0 + (70.0 / num_slaves) * (slave_idx + 1);
						double current_prog = start_p + block_ratio * (end_p - start_p);
						#pragma omp critical
						{
							emit updateProcess(int(current_prog), QStringLiteral("第%1对图像配准中：%2%")
								.arg(ii + 1).arg(int(block_ratio * 100)));
						}
					}
				}
			}
		}


		//剔除outliers
		m = mm; n = nn;//更新实际子块行列数
		Mat sentinel = Mat::zeros(m, n, CV_64F);
		int ix, iy, count = 0, c = 0; double delta, thresh = 2.0;
		for (int i = 0; i < m; i++)
		{
			for (int j = 0; j < n; j++)
			{
				count = 0;
				//上
				ix = j;
				iy = i - 1; iy = iy < 0 ? 0 : iy;
				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
				if (fabs(delta) >= thresh) count++;
				//下
				ix = j;
				iy = i + 1; iy = iy > m - 1 ? m - 1 : iy;
				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
				if (fabs(delta) >= thresh) count++;
				//左
				ix = j - 1; ix = ix < 0 ? 0 : ix;
				iy = i;
				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
				if (fabs(delta) >= thresh) count++;
				//右
				ix = j + 1; ix = ix > n - 1 ? n - 1 : ix;
				iy = i;
				delta = fabs(offset_c.at<double>(i, j) - offset_c.at<double>(iy, ix));
				delta += fabs(offset_r.at<double>(i, j) - offset_r.at<double>(iy, ix));
				if (fabs(delta) >= thresh) count++;

				if (count > 2) { sentinel.at<double>(i, j) = 1.0; c++; }
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

		/*---------------------------------------*/
		/*    双线性插值获取重采样后的辅图像     */
		/*---------------------------------------*/

		//获取辅图像左上角相对于主图像的偏移量
		Mat tt(1, 3, CV_64F);
		tt.at<double>(0, 0) = 1.0;
		tt.at<double>(0, 1) = (0.0 - offset_x) / scale_x;
		tt.at<double>(0, 2) = (0.0 - offset_y) / scale_y;
		offset_row_out.at<int>(ii, 0) = sum(tt * coef_r)[0];
		offset_col_out.at<int>(ii, 0) = sum(tt * coef_c)[0];

		ComplexMat slave1;
		ret = conversion.read_slc_from_h5(SAR_images[ii].c_str(), slave1);
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
		int rows_slave = slave1.GetRows(); int cols_slave = slave1.GetCols();
		type = slave1.type();
		ComplexMat slave_tmp; slave_tmp.re = Mat::zeros(rows, cols, type); slave_tmp.im = Mat::zeros(rows, cols, type);
#pragma omp parallel for schedule(guided)
		for (int i = 0; i < rows; i++)
		{
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

		ret = conversion.write_slc_to_h5(SAR_images_out[ii].c_str(), slave_tmp);
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested() || isStopRequested()) return -1;
		double end_p = 10.0 + (70.0 / num_slaves) * (slave_idx + 1);
		emit updateProcess(int(end_p), QStringLiteral("第%1对图像处理中……").arg(ii + 1));
		slave_idx++;
	}
	return 0;
}

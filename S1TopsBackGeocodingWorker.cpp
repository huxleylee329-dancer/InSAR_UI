#include "S1TopsBackGeocodingWorker.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <Utils.h>
#include <FormatConversion.h>
#include "tinyxml.h"
#include "NodeUtils.h"
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QFileInfo>
#include <vector>
#include <string>
#include <algorithm>

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#endif

thread_local S1TopsBackGeocodingWorker* t_currentBackGeocodingWorker = nullptr;

static bool __stdcall backGeocodingProgressCallback(int progress, const char* message)
{
    if (t_currentBackGeocodingWorker)
    {
        if (t_currentBackGeocodingWorker->thread()->isInterruptionRequested())
        {
            return false;
        }
    }
    return true;
}

struct BackGeocodingThreadLocalGuard {
    BackGeocodingThreadLocalGuard(S1TopsBackGeocodingWorker* worker) {
        t_currentBackGeocodingWorker = worker;
    }
    ~BackGeocodingThreadLocalGuard() {
        t_currentBackGeocodingWorker = nullptr;
    }
};

S1TopsBackGeocodingWorker::S1TopsBackGeocodingWorker(QObject* parent)
    : BaseWorker(parent)
{
}

S1TopsBackGeocodingWorker::~S1TopsBackGeocodingWorker()
{
}

void S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding(
	int images_number,
	int masterIndex,
	QString savePath,
	QString dstProject, 
	QString srcNode,
	QString dstNode,
	QStandardItemModel* model,
	bool b_ESD
)
{
	BackGeocodingThreadLocalGuard guard(this);
	if (images_number < 2 ||
		masterIndex < 1 ||
		masterIndex > images_number ||
		savePath.isEmpty() ||
		dstProject.isEmpty() ||
		dstNode.isEmpty() ||
		srcNode.isEmpty() ||
		!model
		)
	{
		emit errorProcess("Invalid parameters for BackGeocoding.");
		return;
	}
	int ret;
	QDir dir(savePath);
	if (!dir.exists(dstNode)) {
		if (!dir.mkdir(dstNode)) {
			emit errorProcess("Failed to create directory: " + dstNode);
			return;
		}
	}
	QDir outDir(savePath + "/" + dstNode);
	std::vector<std::string> SAR_images;
	std::vector<std::string> SAR_images_regis;
	QList<QString> origin;
	bool found_project = false;
	QString demPath = m_demPath;
	QMetaObject::invokeMethod(model, [=, &SAR_images, &SAR_images_regis, &origin, &found_project, &demPath]() {
		QList<QStandardItem*> foundProjects = model->findItems(dstProject);
		if (foundProjects.isEmpty()) return;
		found_project = true;
		QStandardItem* project = foundProjects.first();
		for (int i = 0; i < project->rowCount(); i++)
		{
			QStandardItem* images = project->child(i, 0);
			if (images && images->text() == srcNode)
			{
				for (int j = 0; j < images->rowCount(); j++)
				{
					QStandardItem* pathItem = images->child(j, 1);
					if (pathItem) {
						QFileInfo fileinfo(pathItem->text());
						QString origin_name = fileinfo.baseName();
						origin.append(origin_name);
						SAR_images.push_back(pathItem->text().toStdString());
						SAR_images_regis.push_back(QString("%1/%2/%3_regis.h5").arg(savePath).arg(dstNode)
							.arg(origin_name).toStdString());
					}
				}
			}
		}

		// 如果外部未传入DEM路径，在 GUI 线程安全地查询项目全局默认高程数据路径
		if (demPath.isEmpty()) {
			auto* iface = NodeUtils::getProjectContext(nullptr);
			if (iface) {
				demPath = NodeUtils::getGlobalDemPath(iface);
			}
		}
	}, Qt::BlockingQueuedConnection);

	if (!found_project)
	{
		emit errorProcess("Project node not found in project tree.");
		return;
	}
	if (SAR_images.empty())
	{
		emit errorProcess("No input images found in the project tree under: " + srcNode);
		return;
	}
	emit updateProcess(10, QStringLiteral("开始后向地理编码配准……"));
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Starting S1 TOPS Back-Geocoding. Total images: %1, Master Index: %2, ESD Enabled: %3")
		.arg(images_number).arg(masterIndex).arg(b_ESD ? "True" : "False"));

	// 优先使用项目全局高程路径，如为空则回退到运行程序下的 dem 文件夹
	if (demPath.isEmpty()) {
		QString appPath = QCoreApplication::applicationDirPath();
		demPath = appPath + "/dem";
	}
	// 只有当 demPath 不是文件路径且目录不存在时，才创建目录
	if (!demPath.isEmpty()) {
		bool isFile = demPath.endsWith(".tif", Qt::CaseInsensitive) || 
		              demPath.endsWith(".tiff", Qt::CaseInsensitive) || 
		              demPath.endsWith(".h5", Qt::CaseInsensitive);
		if (!isFile && !QDir(demPath).exists()) {
			QDir().mkpath(demPath);
		}
	}
	//后向地理编码配准
	Sentinel1BackGeocoding backgeocoding; FormatConversion conversion;
	ComplexMat slaveSLC, tmp;
	Utils util;
	std::string tmpDem = demPath.toStdString();
	std::replace(tmpDem.begin(), tmpDem.end(), '/', '\\');

	{
		NodeUtils::Hdf5Locker locker;
	ret = backgeocoding.loadData(SAR_images);
	ret = backgeocoding.setDEMPath(tmpDem.c_str());
	ret = backgeocoding.loadOutFiles(SAR_images_regis);
	ret = backgeocoding.setMasterIndex(masterIndex);
	if (backgeocoding.numOfImages < 2) {
		emit errorProcess("Number of loaded images is less than 2.");
		return;
	}
	ret = conversion.read_slc_from_h5(backgeocoding.su[masterIndex - 1]->h5File.c_str(), tmp);
	if (ret < 0) {
		emit errorProcess("Failed to read master SLC from H5.");
		return;
	}
	tmp.convertTo(tmp, CV_32F);
	ret = conversion.creat_new_h5(backgeocoding.outFiles[masterIndex - 1].c_str());
	if (ret < 0) {
		emit errorProcess("Failed to create master registration H5 file.");
		return;
	}
	ret = conversion.write_slc_to_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), tmp);
	if (ret < 0) {
		emit errorProcess("Failed to write master SLC to H5 file.");
		return;
	}
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Successfully loaded Master SLC data and set DEM path.");
	tmp.re = 0.0; tmp.im = 0.0;
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
		if (i == masterIndex - 1) continue;
		ret = conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
		if (ret < 0) {
			emit errorProcess("Failed to create slave registration H5 file: " + QString::fromStdString(backgeocoding.outFiles[i]));
			return;
		}
		ret = conversion.write_slc_to_h5(backgeocoding.outFiles[i].c_str(), tmp);
		if (ret < 0) {
			emit errorProcess("Failed to write empty SLC to H5 file: " + QString::fromStdString(backgeocoding.outFiles[i]));
			return;
		}
	}

	cv::Mat start(backgeocoding.su[masterIndex - 1]->burstCount, 1, CV_32S), end(backgeocoding.su[masterIndex - 1]->burstCount, 1, CV_32S);
	start.at<int>(0, 0) = 1;
	end.at<int>(0, 0) = backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(0, 0);
	double lastValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(0, 0) +
		(backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(0, 0) - 1) * backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;
	double firstValidTime;
	int overlap;
	cv::Mat overlapMat = cv::Mat::zeros(backgeocoding.su[masterIndex - 1]->burstCount - 1, 1, CV_32S);
	for (int i = 1; i < backgeocoding.su[masterIndex - 1]->burstCount; i++)
	{
		firstValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(i, 0) + (backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) - 1) *
			backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;

		overlap = round((lastValidTime - firstValidTime) / backgeocoding.su[masterIndex - 1]->azimuthTimeInterval + 1);
		overlapMat.at<int>(i - 1, 0) = overlap;
		end.at<int>(i - 1, 0) = end.at<int>(i - 1, 0) - int(overlap / 2);

		start.at<int>(i, 0) = backgeocoding.su[masterIndex - 1]->linesPerBurst * i + backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) + overlap - int(overlap / 2);

		end.at<int>(i, 0) = backgeocoding.su[masterIndex - 1]->linesPerBurst * i + backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(i, 0);

		lastValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(i, 0) +
			(backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(i, 0) - 1) * backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;
	}
	end.at<int>(backgeocoding.su[masterIndex - 1]->burstCount - 1, 0) = backgeocoding.su[masterIndex - 1]->linesPerBurst * backgeocoding.su[masterIndex - 1]->burstCount;
	start -= 1;
	start.copyTo(backgeocoding.start);
	end.copyTo(backgeocoding.end);
	backgeocoding.isdeBurstConfig = true;

	int linesPerBurst = backgeocoding.su[masterIndex - 1]->linesPerBurst;
	int samplesPerBurst = backgeocoding.su[masterIndex - 1]->samplesPerBurst;
	int offset_row = 0;
	double lonMin, lonMax, latMin, latMax;
	int burstCount = backgeocoding.su[masterIndex - 1]->burstCount;
	for (int i = 0; i < burstCount; i++)
	{
		InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Registration: processing burst %1/%2...").arg(i + 1).arg(burstCount));
		ret = backgeocoding.su[masterIndex - 1]->computeImageGeoBoundry(&lonMin, &lonMax, &latMin, &latMax, i + 1);
		if (ret < 0) {
			emit errorProcess("Failed to compute master image geo boundary.");
			return;
		}
		ret = backgeocoding.loadDEM(backgeocoding.DEMPath.c_str(), lonMin, lonMax, latMin, latMax);
		if (ret < 0) {
			emit errorProcess("Failed to load DEM.");
			return;
		}
		
		for (int j = 0; j < backgeocoding.numOfImages; j++)
		{
			if (j == masterIndex - 1) continue;
			if (!backgeocoding.burstOffsetComputed)
			{
				ret = backgeocoding.computeBurstOffset(); if (ret < 0) return;
			}
			int mBurstIndex = i + 1; int slaveImageIndex = j + 1;
			int sBurstIndex = mBurstIndex + backgeocoding.su[slaveImageIndex - 1]->burstOffset;
			if (sBurstIndex < 1 || sBurstIndex > backgeocoding.su[slaveImageIndex - 1]->burstCount) {
				continue;
			}
			double a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az;
			cv::Mat coef(1, 6, CV_64F);
			ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
			if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
			cv::Mat derampDemodPhase;
			ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
			ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
			ret = backgeocoding.computeSlavePosition(slaveImageIndex, mBurstIndex);
			cv::Mat slaveAzimuthOffset, slaveRangeOffset;
			ret = backgeocoding.computeSlaveOffset(slaveAzimuthOffset, slaveRangeOffset);
			ret = backgeocoding.fitSlaveOffset(slaveAzimuthOffset, &a0Az, &a1Az, &a2Az);
			ret = backgeocoding.fitSlaveOffset(slaveRangeOffset, &a0Rg, &a1Rg, &a2Rg);
			coef.at<double>(0) = a0Rg;
			coef.at<double>(1) = a1Rg;
			coef.at<double>(2) = a2Rg;
			coef.at<double>(3) = a0Az;
			coef.at<double>(4) = a1Az;
			coef.at<double>(5) = a2Az;
			ret = backgeocoding.performBilinearResampling(slaveSLC, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
				a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
			tmp.SetRe(derampDemodPhase); tmp.SetIm(derampDemodPhase);
			ret = backgeocoding.performBilinearResampling(tmp, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
				a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
			tmp.re.copyTo(derampDemodPhase);
			util.phase2cos(derampDemodPhase, tmp.re, tmp.im);
			slaveSLC.Mul(tmp, slaveSLC, true);//reramp
			slaveSLC.convertTo(slaveSLC, CV_32F);
			char str[256];
			sprintf(str, "burst_%d_coef", i + 1);
			conversion.write_array_to_h5(backgeocoding.outFiles[j].c_str(), str, coef);
			ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_re", slaveSLC.re,
				offset_row, 0, linesPerBurst, samplesPerBurst);
			if (ret < 0) {
				emit errorProcess("Failed to write slave SLC real part to H5.");
				return;
			}
			ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
				offset_row, 0, linesPerBurst, samplesPerBurst);
			if (ret < 0) {
				emit errorProcess("Failed to write slave SLC imag part to H5.");
				return;
			}
		}
		offset_row += linesPerBurst;
		backgeocoding.isMasterRgAzComputed = false;
		if (QThread::currentThread()->isInterruptionRequested())
		{
			outDir.removeRecursively();
			return;
		}
		emit updateProcess(10.0 + 50.0 / burstCount * (i + 1), QStringLiteral("后向地理编码配准……"));
	}

	if (b_ESD)
	{
		InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Starting Enhanced Spectral Diversity (ESD) correction...");
		cv::Mat overlap_phase(cv::sum(overlapMat)[0], samplesPerBurst, CV_64F);
		cv::Mat phase; int count_sum = 0;
		ComplexMat overlap_master_up, overlap_slave_up, overlap_master_down, overlap_slave_down;
		for (int j = 0; j < backgeocoding.numOfImages; j++)
		{
			if (j == masterIndex - 1) continue;
			for (int i = 1; i < burstCount; i++)
			{
				int offset_col = 0;
				offset_row = (i - 1) * linesPerBurst + backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(i - 1, 0) - overlapMat.at<int>(i - 1, 0);
				conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_master_up.re);
				conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_master_up.im);

				conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_slave_up.re);
				conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_slave_up.im);

				offset_row = linesPerBurst * i + backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) - 1;

				conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_master_down.re);
				conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_master_down.im);

				conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_slave_down.re);
				conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
					samplesPerBurst, overlap_slave_down.im);

				overlap_master_up.convertTo(overlap_master_up, CV_64F);
				overlap_master_down.convertTo(overlap_master_down, CV_64F);
				overlap_slave_up.convertTo(overlap_slave_up, CV_64F);
				overlap_slave_down.convertTo(overlap_slave_down, CV_64F);

				overlap_master_up.Mul(overlap_slave_up, overlap_master_up, true);
				overlap_master_down.Mul(overlap_slave_down, overlap_master_down, true);

				overlap_master_up.Mul(overlap_master_down, overlap_master_up, true);

				phase = overlap_master_up.GetPhase();
				overlap = overlapMat.at<int>(i - 1);
				phase.copyTo(overlap_phase(cv::Range(count_sum, count_sum + overlap), cv::Range(0, samplesPerBurst)));
				count_sum += overlap;
			}
			count_sum = 0;
			cv::Mat phase0, coh;
			util.multilook(overlap_phase, phase0, 16, 4);

			util.phase_coherence(phase0, coh);
			for (int mm = 0; mm < coh.rows; mm++)
			{
				for (int nn = 0; nn < coh.cols; nn++)
				{
					if (coh.at<double>(mm, nn) < 0.4) phase0.at<double>(mm, nn) = 0.0;
				}
			}
			
			cv::Point p;
			double t1, t2;
			cv::Mat output, out_x;
			phase0 = phase0.reshape(0, 1);
			util.hist(phase0, -PI, PI, 0.1, out_x, output);
			//拉格朗日插值
			double x0, x1, x2, x3, y0, y1, y2, y3, x; x = 31;
			x0 = 29; x1 = 30; x2 = 32; x3 = 33;
			y0 = output.at<double>(29); y1 = output.at<double>(30); y2 = output.at<double>(32); y3 = output.at<double>(33);
			output.at<double>(31) = (x - x1) * (x - x2) * (x - x3) / ((x0 - x1) * (x0 - x2) * (x0 - x3)) * y0 +
				(x - x0) * (x - x2) * (x - x3) / ((x1 - x0) * (x1 - x2) * (x1 - x3)) * y1 +
				(x - x0) * (x - x1) * (x - x3) / ((x2 - x0) * (x2 - x1) * (x2 - x3)) * y2 +
				(x - x0) * (x - x1) * (x - x2) / ((x3 - x0) * (x3 - x1) * (x3 - x2)) * y3;
			cv::minMaxLoc(output, &t1, &t2, NULL, &p);
			double offset = out_x.at<double>(p.x);
			double offset_a = offset / (2 * 3.1415926535 * 4500) * 486;
			conversion.write_double_to_h5(backgeocoding.outFiles[j].c_str(), "offset_a", offset_a);
			InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Slave image %1 (index %2) ESD azimuth offset calculated: %3")
				.arg(origin[j]).arg(j + 1).arg(offset_a));
		}

		offset_row = 0;
		for (int i = 0; i < burstCount; i++)
		{
			for (int j = 0; j < backgeocoding.numOfImages; j++)
			{
				if (j == masterIndex - 1) continue;
				double offset_a = 0.0;
				conversion.read_double_from_h5(backgeocoding.outFiles[j].c_str(), "offset_a", &offset_a);
				//偏移低于0.001像素则不予补偿
				if (fabs(offset_a) < 0.001) continue;
				if (!backgeocoding.burstOffsetComputed)
				{
					ret = backgeocoding.computeBurstOffset();
				}
				int mBurstIndex = i + 1; int slaveImageIndex = j + 1;
				int sBurstIndex = mBurstIndex + backgeocoding.su[slaveImageIndex - 1]->burstOffset;
				if (sBurstIndex < 1 || sBurstIndex > backgeocoding.su[slaveImageIndex - 1]->burstCount) {
					continue;
				}
				double a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az;
				cv::Mat coef(1, 6, CV_64F);
				ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
				if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
				cv::Mat derampDemodPhase;
				ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
				ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
				char str[256];
				sprintf(str, "burst_%d_coef", i + 1);
				
				conversion.read_array_from_h5(backgeocoding.outFiles[j].c_str(), str, coef);
				
				a0Rg = coef.at<double>(0);
				a1Rg = coef.at<double>(1);
				a2Rg = coef.at<double>(2);
				a0Az = coef.at<double>(3) + offset_a;
				a1Az = coef.at<double>(4);
				a2Az = coef.at<double>(5);
				ret = backgeocoding.performBilinearResampling(slaveSLC, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
					a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
				tmp.SetRe(derampDemodPhase); tmp.SetIm(derampDemodPhase);
				ret = backgeocoding.performBilinearResampling(tmp, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
					a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
				tmp.re.copyTo(derampDemodPhase);
				util.phase2cos(derampDemodPhase, tmp.re, tmp.im);
				slaveSLC.Mul(tmp, slaveSLC, true);//reramp
				slaveSLC.convertTo(slaveSLC, CV_32F);

				ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_re", slaveSLC.re,
					offset_row, 0, linesPerBurst, samplesPerBurst);
				if (ret < 0) {
					emit errorProcess("Failed to write ESD compensated SLC real part to H5.");
					return;
				}
				ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
					offset_row, 0, linesPerBurst, samplesPerBurst);
				if (ret < 0) {
					emit errorProcess("Failed to write ESD compensated SLC imag part to H5.");
					return;
				}
			}
			offset_row += linesPerBurst;
			backgeocoding.isMasterRgAzComputed = false;

			emit updateProcess(60 + 30 / burstCount * (i + 1), QStringLiteral("增强谱分集校正……"));
		}
	}

	//deburst
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Starting Deburst processing for %1 images...").arg(backgeocoding.numOfImages));
	ComplexMat slc;
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
		InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Debursting image %1/%2: %3")
			.arg(i + 1).arg(backgeocoding.numOfImages).arg(origin[i]));
		conversion.read_slc_from_h5(backgeocoding.outFiles[i].c_str(), slaveSLC);
		conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
		slc = slaveSLC(cv::Range(backgeocoding.start.at<int>(0, 0), backgeocoding.end.at<int>(0, 0)),
			cv::Range(0, backgeocoding.su[masterIndex - 1]->samplesPerBurst));
		for (int j = 1; j < backgeocoding.su[masterIndex - 1]->burstCount; j++)
		{
			tmp = slaveSLC(cv::Range(backgeocoding.start.at<int>(j, 0), backgeocoding.end.at<int>(j, 0)),
				cv::Range(0, backgeocoding.su[masterIndex - 1]->samplesPerBurst));
			cv::vconcat(slc.re, tmp.re, slc.re);
			cv::vconcat(slc.im, tmp.im, slc.im);
		}
		conversion.write_slc_to_h5(backgeocoding.outFiles[i].c_str(), slc);
		emit updateProcess(90 + 10 / burstCount * (i + 1), QStringLiteral("deburst……"));
	}

	FormatConversion FC;
	/*获取主星参数*/
	cv::Mat outArray;
	int rows, cols;
	FC.read_array_from_h5(SAR_images_regis.at(masterIndex - 1).c_str(), "s_re", outArray);
	rows = outArray.rows; cols = outArray.cols;
	offset_row = 0;
	int offset_col = 0;

	/*写入辅助参数到h5*/
	for (int i = 0; i < images_number; i++)
	{
		FC.Copy_para_from_h5_2_h5(SAR_images.at(i).c_str(), SAR_images_regis.at(i).c_str());
		FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "process_state", "coregistration");
		FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "comment", "complex-2.0");
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_row", 0);
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_col", 0);
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "azimuth_len", rows);
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "range_len", cols);
	}
	}
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Registration parameters copied successfully to H5 files.");

	QMetaObject::invokeMethod(model, [=]() {
		QList<QStandardItem*> foundProjects = model->findItems(dstProject);
		if (foundProjects.isEmpty()) return;
		QStandardItem* project = foundProjects.first();

		/*建立配准根节点*/
		QStandardItem* regis = NULL;
		for (int i = 0; i < project->rowCount(); i++)
		{
			if (project->child(i, 0)->text() == dstNode)
			{
				regis = project->child(i, 0);
				break;
			}
		}

		if (!regis)
		{
			regis = new QStandardItem(dstNode);
			regis->setToolTip(dstProject);
			int insert = 0;
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0)
					continue;
				else
					break;
			}
			regis->setIcon(QIcon(FOLDER_ICON));
			project->insertRow(insert, regis);
			QStandardItem* regis_Rank = new QStandardItem("complex-2.0");
			project->setChild(insert, 1, regis_Rank);
		}

		/*添加图像到model中并复制h5参数*/
		for (int i = 0; i < images_number; i++)
		{
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
		}

		/*写入XML*/
		XMLFile xmlfile;
		if (xmlfile.XMLFile_load((savePath + "/" + dstProject).toStdString().c_str()) >= 0)
		{
			TiXmlElement* root = nullptr;
			xmlfile.get_root(root);
			if (root)
			{
				for (int i = 0; i < images_number; i++)
				{
					QString relativePath = QString("/%1/%2").arg(dstNode).arg(origin.at(i) + "_regis.h5");
					QString dataName = origin.at(i) + "_regis";
					
					// 查找是否已存在该 DataNode
					TiXmlElement* dataNodeElem = nullptr;
					for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
						const char* nameAttr = p->Attribute("name");
						if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
							dataNodeElem = p;
							break;
						}
					}
					
					if (!dataNodeElem) {
						// 创建新的 DataNode
						dataNodeElem = new TiXmlElement("DataNode");
						dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
						dataNodeElem->SetAttribute("data_count", "1");
						dataNodeElem->SetAttribute("data_processing", "coregistration");
						dataNodeElem->SetAttribute("rank", "complex-2.0");
						
						int index = 1;
						TiXmlElement* root_child = root->FirstChildElement();
						if (root_child) {
							root_child = root_child->NextSiblingElement(); // 略过 project_info
						}
						
						TiXmlElement* insertBeforeNode = nullptr;
						for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
							const char* rankAttr = p->Attribute("rank");
							if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
											 strcmp(rankAttr, "complex-1.0") == 0 ||
											 strcmp(rankAttr, "complex-2.0") == 0)) {
								continue;
							} else {
								insertBeforeNode = p;
								break;
							}
						}
						dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());
						
						TiXmlElement* dataElem = new TiXmlElement("Data");
						
						TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
						dataNameNode->LinkEndChild(new TiXmlText(dataName.toStdString().c_str()));
						dataElem->LinkEndChild(dataNameNode);
						
						TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
						dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
						dataElem->LinkEndChild(dataRankNode);
						
						TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
						dataIndexNode->LinkEndChild(new TiXmlText("1"));
						dataElem->LinkEndChild(dataIndexNode);
						
						TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
						dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
						dataElem->LinkEndChild(dataPathNode);
						
						TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
						rowOffsetNode->LinkEndChild(new TiXmlText("0"));
						dataElem->LinkEndChild(rowOffsetNode);
						
						TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
						colOffsetNode->LinkEndChild(new TiXmlText("0"));
						dataElem->LinkEndChild(colOffsetNode);
						
						dataNodeElem->LinkEndChild(dataElem);
						
						TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
						TiXmlElement* masterImageElem = new TiXmlElement("master_image");
						masterImageElem->LinkEndChild(new TiXmlText(QString::number(masterIndex).toStdString().c_str()));
						paramsElem->LinkEndChild(masterImageElem);
						dataNodeElem->LinkEndChild(paramsElem);
						
						if (insertBeforeNode) {
							root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
							delete dataNodeElem;
							
							for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement()) {
								index++;
								p->SetAttribute("index", QString::number(index).toStdString().c_str());
							}
						} else {
							root->LinkEndChild(dataNodeElem);
						}
					} else {
						// 成果节点已存在，追加新的 Data 元素
						const char* countAttr = dataNodeElem->Attribute("data_count");
						int count = countAttr ? QString(countAttr).toInt() : 0;
						count++;
						dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());
						
						TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;
						
						TiXmlElement* dataElem = new TiXmlElement("Data");
						
						TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
						dataNameNode->LinkEndChild(new TiXmlText(dataName.toStdString().c_str()));
						dataElem->LinkEndChild(dataNameNode);
						
						TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
						dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
						dataElem->LinkEndChild(dataRankNode);
						
						TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
						dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
						dataElem->LinkEndChild(dataIndexNode);
						
						TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
						dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
						dataElem->LinkEndChild(dataPathNode);
						
						TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
						rowOffsetNode->LinkEndChild(new TiXmlText("0"));
						dataElem->LinkEndChild(rowOffsetNode);
						
						TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
						colOffsetNode->LinkEndChild(new TiXmlText("0"));
						dataElem->LinkEndChild(colOffsetNode);
						
						if (lastChildNode) {
							dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
							delete dataElem;
						} else {
							dataNodeElem->LinkEndChild(dataElem);
						}
					}
				}
			}
			xmlfile.XMLFile_save((savePath + "/" + dstProject).toStdString().c_str());
			InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Registration results successfully saved in project XML file.");
		}
		else
		{
			InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", "Failed to load project XML file: " + savePath + "/" + dstProject);
		}
	}, Qt::BlockingQueuedConnection);
	emit sendModel(model);
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

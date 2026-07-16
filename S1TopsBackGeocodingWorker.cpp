#include "S1TopsBackGeocodingWorker.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <Utils.h>
#include <FormatConversion.h>
#include <Registration.h>
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
#pragma comment(lib, "Registration_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Registration.lib")
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
	for (size_t i = 0; i < SAR_images.size(); ++i) {
		InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("  Input Image [%1]: %2 -> Output: %3")
			.arg(i + 1).arg(QString::fromStdString(SAR_images[i])).arg(QString::fromStdString(SAR_images_regis[i])));
	}

	// 临时调试：输出配准环境诊断信息到 Console
	cv::Mat test_orbit;
	bool hasPreciseOrbit = false;
	{
		NodeUtils::Hdf5Locker locker(SAR_images[0]);
		FormatConversion temp_conv;
		hasPreciseOrbit = (temp_conv.read_array_from_h5(SAR_images[0].c_str(), "fine_state_vec", test_orbit) == 0);
	}
	printf("[InSAR_DEBUG_COREG] [Worker] ============ Coregistration Environment Diagnostics ============\n");
	printf("[InSAR_DEBUG_COREG] [Worker] 1. Precise Orbit Detection (POEORB/RESORB): %s\n", hasPreciseOrbit ? "Precise Orbit Loaded (fine_state_vec available)" : "No Precise Orbit (using default orbit)");
	printf("[InSAR_DEBUG_COREG] [Worker] 2. ESD Azimuth Refinement: %s\n", b_ESD ? "ENABLED" : "DISABLED");
	printf("[InSAR_DEBUG_COREG] [Worker] 3. Range Amplitude Refinement: %s\n", m_bRangeRefine ? "ENABLED" : "DISABLED");
	printf("[InSAR_DEBUG_COREG] [Worker] 4. Resampling Interpolator: Sinc Interpolation (8-point windowed kernel)\n");
	printf("[InSAR_DEBUG_COREG] [Worker] ==================================================================\n");

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
	}
	if (ret < 0) {
		emit errorProcess("Failed to load Sentinel-1 images metadata.");
		return;
	}
	emit updateProcess(11, QStringLiteral("主从影像数据元数据加载完毕……"));
	ret = backgeocoding.setDEMPath(tmpDem.c_str());
	ret = backgeocoding.loadOutFiles(SAR_images_regis);
	ret = backgeocoding.setMasterIndex(masterIndex);
	if (backgeocoding.numOfImages < 2) {
		emit errorProcess("Number of loaded images is less than 2.");
		return;
	}
	{
		NodeUtils::Hdf5Locker locker(backgeocoding.su[masterIndex - 1]->h5File);
		ret = conversion.read_slc_from_h5(backgeocoding.su[masterIndex - 1]->h5File.c_str(), tmp);
	}
	if (ret < 0) {
		emit errorProcess("Failed to read master SLC from H5.");
		return;
	}
	emit updateProcess(12, QStringLiteral("主影像 SLC 数据读取完毕，正在初始化配准空间……"));
	tmp.convertTo(tmp, CV_32F);
	{
		NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[masterIndex - 1]);
		ret = conversion.creat_new_h5(backgeocoding.outFiles[masterIndex - 1].c_str());
		if (ret >= 0) {
			ret = conversion.write_slc_to_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), tmp);
		}
	}
	if (ret < 0) {
		emit errorProcess("Failed to create or write master registration H5 file.");
		return;
	}
	emit updateProcess(14, QStringLiteral("主影像注册 H5 空间初始化完毕……"));
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Successfully loaded Master SLC data and set DEM path.");

	// 从影像不再预写入整个全 0 的大矩阵，改为调用 create_empty_dataset 延迟分配物理磁盘空间
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
		if (i == masterIndex - 1) continue;
		emit updateProcess(14 + i, QStringLiteral("正在初始化从影像 %1/%2 的 H5 空间……").arg(i + 1).arg(backgeocoding.numOfImages));
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			ret = conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
			if (ret >= 0) {
				ret = conversion.create_empty_dataset(backgeocoding.outFiles[i].c_str(), "s_re", tmp.GetRows(), tmp.GetCols(), CV_32F);
			}
			if (ret >= 0) {
				ret = conversion.create_empty_dataset(backgeocoding.outFiles[i].c_str(), "s_im", tmp.GetRows(), tmp.GetCols(), CV_32F);
			}
		}
		if (ret < 0) {
			emit errorProcess("Failed to create empty datasets in slave registration H5 file: " + QString::fromStdString(backgeocoding.outFiles[i]));
			return;
		}
	}
	emit updateProcess(18, QStringLiteral("所有从影像 H5 空间初始化完毕，准备执行后向投影……"));

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
				ret = backgeocoding.computeBurstOffset();
				if (ret < 0) {
					emit errorProcess("Failed to compute burst offset.");
					return;
				}
			}
			int mBurstIndex = i + 1; int slaveImageIndex = j + 1;
			int sBurstIndex = mBurstIndex + backgeocoding.su[slaveImageIndex - 1]->burstOffset;
			if (sBurstIndex < 1 || sBurstIndex > backgeocoding.su[slaveImageIndex - 1]->burstCount) {
				continue;
			}
			double a0Rg = 0.0, a1Rg = 0.0, a2Rg = 0.0, a0Az = 0.0, a1Az = 0.0, a2Az = 0.0;
			cv::Mat coef(1, 6, CV_64F);
			ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
			if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
			cv::Mat derampDemodPhase;
			ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
			ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
			ret = backgeocoding.computeSlavePosition(slaveImageIndex, mBurstIndex);
			cv::Mat slaveAzimuthOffset, slaveRangeOffset;
			ret = backgeocoding.computeSlaveOffset(slaveAzimuthOffset, slaveRangeOffset);
			
			int fitRetAz = backgeocoding.fitSlaveOffset(slaveAzimuthOffset, &a0Az, &a1Az, &a2Az);
			if (fitRetAz < 0) {
				InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", QString("Failed to fit slave azimuth offset for burst %1, slave %2. Using default 0.0.").arg(mBurstIndex).arg(slaveImageIndex));
				a0Az = 0.0; a1Az = 0.0; a2Az = 0.0;
			}
			int fitRetRg = backgeocoding.fitSlaveOffset(slaveRangeOffset, &a0Rg, &a1Rg, &a2Rg);
			if (fitRetRg < 0) {
				InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", QString("Failed to fit slave range offset for burst %1, slave %2. Using default 0.0.").arg(mBurstIndex).arg(slaveImageIndex));
				a0Rg = 0.0; a1Rg = 0.0; a2Rg = 0.0;
			}

			coef.at<double>(0) = a0Rg;
			coef.at<double>(1) = a1Rg;
			coef.at<double>(2) = a2Rg;
			coef.at<double>(3) = a0Az;
			coef.at<double>(4) = a1Az;
			coef.at<double>(5) = a2Az;
			ret = backgeocoding.performSincResampling(slaveSLC, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
				a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
			tmp.SetRe(derampDemodPhase); tmp.SetIm(derampDemodPhase);
			ret = backgeocoding.performSincResampling(tmp, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
				a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
			tmp.re.copyTo(derampDemodPhase);
			util.phase2cos(derampDemodPhase, tmp.re, tmp.im);
			slaveSLC.Mul(tmp, slaveSLC, true);//reramp
			slaveSLC.convertTo(slaveSLC, CV_32F);
			char str[256];
			sprintf(str, "burst_%d_coef", i + 1);
			{
				NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
				conversion.write_array_to_h5(backgeocoding.outFiles[j].c_str(), str, coef);
				ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_re", slaveSLC.re,
					offset_row, 0, linesPerBurst, samplesPerBurst);
				if (ret >= 0) {
					ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
						offset_row, 0, linesPerBurst, samplesPerBurst);
				}
			}
			if (ret < 0) {
				emit errorProcess("Failed to write slave SLC real or imag part to H5.");
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

	if (b_ESD || m_bRangeRefine)
	{
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
					{
						NodeUtils::Hdf5Locker locker_master(backgeocoding.outFiles[masterIndex - 1]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_up.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_up.im);
					}

					{
						NodeUtils::Hdf5Locker locker_slave(backgeocoding.outFiles[j]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_up.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_up.im);
					}

					offset_row = linesPerBurst * i + backgeocoding.su[masterIndex - 1]->firstValidLine.at<int>(i, 0) - 1;

					{
						NodeUtils::Hdf5Locker locker_master(backgeocoding.outFiles[masterIndex - 1]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_down.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_master_down.im);
					}

					{
						NodeUtils::Hdf5Locker locker_slave(backgeocoding.outFiles[j]);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_re", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_down.re);
						conversion.read_subarray_from_h5(backgeocoding.outFiles[j].c_str(), "s_im", offset_row, offset_col, overlapMat.at<int>(i - 1, 0),
							samplesPerBurst, overlap_slave_down.im);
					}

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
				if (output.type() != CV_64F) output.convertTo(output, CV_64F);
				if (out_x.type() != CV_64F) out_x.convertTo(out_x, CV_64F);

				//拉格朗日插值
				double x0, x1, x2, x3, y0, y1, y2, y3, x; x = 31;
				x0 = 29; x1 = 30; x2 = 32; x3 = 33;
				if (output.total() > 33) {
					y0 = output.at<double>(29); y1 = output.at<double>(30); y2 = output.at<double>(32); y3 = output.at<double>(33);
					output.at<double>(31) = (x - x1) * (x - x2) * (x - x3) / ((x0 - x1) * (x0 - x2) * (x0 - x3)) * y0 +
						(x - x0) * (x - x2) * (x - x3) / ((x1 - x0) * (x1 - x2) * (x1 - x3)) * y1 +
						(x - x0) * (x - x1) * (x - x3) / ((x2 - x0) * (x2 - x1) * (x2 - x3)) * y2 +
						(x - x0) * (x - x1) * (x - x2) / ((x3 - x0) * (x3 - x1) * (x3 - x2)) * y3;
				}
				cv::minMaxLoc(output, &t1, &t2, NULL, &p);
				double offset = 0.0;
				if (p.x >= 0 && p.x < out_x.total()) {
					offset = out_x.at<double>(p.x);
				}
				double offset_a = offset / (2 * 3.1415926535 * 4500) * 486;
				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					conversion.write_double_to_h5(backgeocoding.outFiles[j].c_str(), "offset_a", offset_a);
				}
				InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Slave image %1 (index %2) ESD azimuth offset calculated: %3")
					.arg(origin[j]).arg(j + 1).arg(offset_a));
			}
		}

		if (m_bRangeRefine)
		{
			InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", "Starting Range Amplitude Refinement offset estimation...");
			for (int j = 0; j < backgeocoding.numOfImages; j++)
			{
				if (j == masterIndex - 1) continue;

				std::string masterPath = backgeocoding.outFiles[masterIndex - 1];
				std::string slavePath = backgeocoding.outFiles[j];

				Point2D pts[5];
				int detectRet = DetectAdaptiveSamplingPoints(masterPath.c_str(), pts, 5);
				double offset_r = 0.0;
				if (detectRet >= 0)
				{
					AlignmentResult res[5];
					for (int k = 0; k < 5; ++k) {
						res[k].heatmap_rgb = nullptr;
						res[k].overlay_rgb = nullptr;
					}
					int calcRet = CalculateOffsetAndCoherence(
						masterPath.c_str(),
						slavePath.c_str(),
						pts, 5, 200, 206, res
					);
					if (calcRet == 0)
					{
						double sumOffsetRg = 0.0;
						int validCount = 0;
						std::vector<int> validOffsets;
						for (int k = 0; k < 5; ++k)
						{
							// 1. 物理残差合理性约束：相干系数 >= 0.15 且距离向像素级偏差在 [-1, 1] 之间
							if (res[k].maxCorrelation >= 0.15 && std::abs(res[k].offsetX) <= 1)
							{
								validOffsets.push_back(res[k].offsetX);
								sumOffsetRg += res[k].offsetX;
								validCount++;
							}
						}

						// 2. 多点一致性投票判定
						if (validCount >= 2)
						{
							// 检查所有有效偏差的最大最小值差值是否 <= 1
							int min_val = *std::min_element(validOffsets.begin(), validOffsets.end());
							int max_val = *std::max_element(validOffsets.begin(), validOffsets.end());
							if (max_val - min_val <= 1)
							{
								offset_r = sumOffsetRg / validCount;
							}
							else
							{
								// 偏差不一致，判定为含噪伪匹配，放弃纠偏，安全回退到 0
								offset_r = 0.0;
								InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", "Range offsets are inconsistent, skipping Range correction.");
							}
						}
						else if (validCount == 1)
						{
							// 单个有效点，需要其相关系数更高（如 >= 0.20）以确保置信度
							int idx = -1;
							for (int k = 0; k < 5; ++k) {
								if (res[k].maxCorrelation >= 0.15 && std::abs(res[k].offsetX) <= 1) {
									idx = k;
									break;
								}
							}
							if (idx != -1 && res[idx].maxCorrelation >= 0.20)
							{
								offset_r = res[idx].offsetX;
							}
							else
							{
								offset_r = 0.0;
							}
						}
						else
						{
							offset_r = 0.0;
						}
					}
					FreeAlignmentResults(res, 5);
				}

				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					conversion.write_double_to_h5(backgeocoding.outFiles[j].c_str(), "offset_r", offset_r);
				}
				InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Slave image %1 (index %2) Range matching offset calculated: %3")
					.arg(origin[j]).arg(j + 1).arg(offset_r));
				printf("[InSAR_DEBUG_COREG] [Worker] S1_TOPS_BackGeocoding(): Slave %d Range matching offset calculated = %f\n", j + 1, offset_r);
			}
		}

		offset_row = 0;
		for (int i = 0; i < burstCount; i++)
		{
			for (int j = 0; j < backgeocoding.numOfImages; j++)
			{
				if (j == masterIndex - 1) continue;
				double offset_a = 0.0;
				double offset_r = 0.0;
				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					if (b_ESD)
					{
						conversion.read_double_from_h5(backgeocoding.outFiles[j].c_str(), "offset_a", &offset_a);
					}
					if (m_bRangeRefine)
					{
						conversion.read_double_from_h5(backgeocoding.outFiles[j].c_str(), "offset_r", &offset_r);
					}
				}
				if (fabs(offset_a) < 0.0001 && fabs(offset_r) < 0.01) continue;
				if (!backgeocoding.burstOffsetComputed)
				{
					ret = backgeocoding.computeBurstOffset();
				}
				int mBurstIndex = i + 1; int slaveImageIndex = j + 1;
				int sBurstIndex = mBurstIndex + backgeocoding.su[slaveImageIndex - 1]->burstOffset;
				if (sBurstIndex < 1 || sBurstIndex > backgeocoding.su[slaveImageIndex - 1]->burstCount) {
					continue;
				}
				double a0Rg = 0.0, a1Rg = 0.0, a2Rg = 0.0, a0Az = 0.0, a1Az = 0.0, a2Az = 0.0;
				cv::Mat coef(1, 6, CV_64F);
				ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
				if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
				cv::Mat derampDemodPhase;
				ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
				ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
				char str[256];
				sprintf(str, "burst_%d_coef", i + 1);
				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					conversion.read_array_from_h5(backgeocoding.outFiles[j].c_str(), str, coef);
				}
				
				if (coef.rows == 1 && coef.cols == 6)
				{
					a0Rg = coef.at<double>(0) + offset_r;
					a1Rg = coef.at<double>(1);
					a2Rg = coef.at<double>(2);
					a0Az = coef.at<double>(3) + offset_a;
					a1Az = coef.at<double>(4);
					a2Az = coef.at<double>(5);
				}
				else
				{
					InSARLogManager::LogWarning("S1TopsBackGeocodingWorker", QString("Failed to read valid registration coefficients from H5 for burst %1, slave %2. Using default 0.0.").arg(i + 1).arg(j + 1));
					a0Rg = offset_r;
					a1Rg = 0.0;
					a2Rg = 0.0;
					a0Az = offset_a;
					a1Az = 0.0;
					a2Az = 0.0;
				}
				ret = backgeocoding.performSincResampling(slaveSLC, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
					a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
				tmp.SetRe(derampDemodPhase); tmp.SetIm(derampDemodPhase);
				ret = backgeocoding.performSincResampling(tmp, backgeocoding.su[masterIndex - 1]->linesPerBurst, backgeocoding.su[masterIndex - 1]->samplesPerBurst,
					a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az);
				tmp.re.copyTo(derampDemodPhase);
				util.phase2cos(derampDemodPhase, tmp.re, tmp.im);
				slaveSLC.Mul(tmp, slaveSLC, true);//reramp
				slaveSLC.convertTo(slaveSLC, CV_32F);

				{
					NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[j]);
					ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_re", slaveSLC.re,
						offset_row, 0, linesPerBurst, samplesPerBurst);
					if (ret >= 0) {
						ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
							offset_row, 0, linesPerBurst, samplesPerBurst);
					}
				}
				if (ret < 0) {
					emit errorProcess("Failed to write ESD compensated SLC real or imag part to H5.");
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
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			conversion.read_slc_from_h5(backgeocoding.outFiles[i].c_str(), slaveSLC);
		}
		int expectedRows = backgeocoding.su[masterIndex - 1]->linesPerBurst * backgeocoding.su[masterIndex - 1]->burstCount;
		if (slaveSLC.isEmpty() || slaveSLC.GetRows() < expectedRows)
		{
			emit errorProcess(QString("Slave SLC image %1 has invalid dimensions (rows: %2, expected: %3). Deburst failed. Please check if DEM covers the full image or if registration succeeded.")
				.arg(origin[i]).arg(slaveSLC.GetRows()).arg(expectedRows));
			return;
		}
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
		}
		slc = slaveSLC(cv::Range(backgeocoding.start.at<int>(0, 0), backgeocoding.end.at<int>(0, 0)),
			cv::Range(0, backgeocoding.su[masterIndex - 1]->samplesPerBurst));
		for (int j = 1; j < backgeocoding.su[masterIndex - 1]->burstCount; j++)
		{
			tmp = slaveSLC(cv::Range(backgeocoding.start.at<int>(j, 0), backgeocoding.end.at<int>(j, 0)),
				cv::Range(0, backgeocoding.su[masterIndex - 1]->samplesPerBurst));
			// 使用临时变量存储拼接结果，避免 OpenCV vconcat 目标矩阵与输入矩阵相同导致的内存重叠/重分配异常
			cv::Mat concat_re, concat_im;
			cv::vconcat(slc.re, tmp.re, concat_re);
			cv::vconcat(slc.im, tmp.im, concat_im);
			slc.re = concat_re;
			slc.im = concat_im;
		}
		{
			NodeUtils::Hdf5Locker locker(backgeocoding.outFiles[i]);
			conversion.write_slc_to_h5(backgeocoding.outFiles[i].c_str(), slc);
		}
		emit updateProcess(90 + 10 / burstCount * (i + 1), QStringLiteral("deburst……"));
	}

	FormatConversion FC;
	/*获取主星参数*/
	cv::Mat outArray;
	const std::string& masterOutputPath = SAR_images_regis.at(masterIndex - 1);
	int readRet = 0;
	{
		NodeUtils::Hdf5Locker locker(masterOutputPath);
		readRet = FC.read_array_from_h5(masterOutputPath.c_str(), "s_re", outArray);
	}
	if (readRet != 0 || outArray.empty())
	{
		emit errorProcess("Failed to read valid s_re dataset from master output H5.");
		return;
	}
	int rows = outArray.rows;
	int cols = outArray.cols;
	offset_row = 0;
	int offset_col = 0;

	/*写入辅助参数到h5*/
	for (int i = 0; i < images_number; i++)
	{
		{
			NodeUtils::Hdf5Locker locker_src(SAR_images.at(i));
			NodeUtils::Hdf5Locker locker_dst(SAR_images_regis.at(i));
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

	QStringList regisH5Paths;
	for (const auto& pathStr : SAR_images_regis)
	{
		regisH5Paths.append(QString::fromStdString(pathStr));
	}

	emit registrationFinished(regisH5Paths, dstNode, dstProject, savePath, masterIndex);
	emit sendModel(model);
	InSARLogManager::LogInfo("S1TopsBackGeocodingWorker", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

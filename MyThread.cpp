#include"MyThread.h"
#include"icon_source.h"
#include"BM3DWrapper.h"
#include<Utils.h>
#include<Deflat.h>
#include<Filter.h>
#include<Registration.h>
#include<Unwrap.h>
#include<Dem.h>
#include "SBAS.h"
#include "TargetDetection.h"
#include<QMessageBox>
#include<qcoreapplication.h>
#include<QFile>
#include<QFileInfo>
#include<QDebug>
#include "InSARLogManager.h"
#include "NodeUtils.h"
#include "Sentinel1ImportHelper.h"
#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "Deflat_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Registration_d.lib")
#pragma comment(lib, "Filter_d.lib")
#pragma comment(lib, "Unwrap_d.lib")
#pragma comment(lib, "Dem_d.lib")
#pragma comment(lib, "SBAS_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "Deflat.lib")
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Registration.lib")
#pragma comment(lib, "Filter.lib")
#pragma comment(lib, "Unwrap.lib")
#pragma comment(lib, "Dem.lib")
#pragma comment(lib, "SBAS.lib")
#endif
#define PI 3.141592653589793238

using namespace cv;
MyThread::MyThread(QObject *parent)
{
	qRegisterMetaType<QList<double>>("QList<double>");
	stop_flag = true;
	DOC = NULL;
}

MyThread::~MyThread()
{
	if (DOC != NULL)
	{
		delete(DOC);
		DOC = NULL;
	}
		

}

void MyThread::Import()
{
}



void MyThread::import_sentinel(
	QString PODFile,
	QString manifest_file,
	QString subswath,
	QString polarization,
	QString project_path,
	QString folder,
	QString filename,
	QString project_name,
	QStandardItemModel* model
)
{
	Sentinel1ImportHelper::importSentinel(
		this,
		PODFile,
		manifest_file,
		subswath,
		polarization,
		project_path,
		folder,
		filename,
		project_name,
		model
	);
}

void MyThread::import_sentinel_patch(
	vector<QString> original_filelist, 
	vector<QString> import_namelist, 
	QString subswath, 
	QString polarization,
	QString savepath, 
	QString dst_node,
	QString dst_project,
	QStandardItemModel* model
)
{
	Sentinel1ImportHelper::importSentinelPatch(
		this,
		original_filelist,
		import_namelist,
		subswath,
		polarization,
		savepath,
		dst_node,
		dst_project,
		model
	);
}

	void MyThread::import_TSX(
	QString polarization,
	QString xml_filename, 
	QString project_path,
	QString folder, 
	QString filename,
	QString project_name,
	QStandardItemModel* model
)
{
	if (xml_filename.isEmpty() ||
		folder.isEmpty() ||
		project_path.isEmpty() ||
		filename.isEmpty() ||
		project_name.isEmpty() ||
		model == NULL
		)
	{
		return;
	}

	int ret;
	QDir dir(project_path);
	if (!dir.exists(folder))
		ret = dir.mkdir(folder);
	QString temp_folder = QString("/") + folder + QString("/");
	QString relative_path = temp_folder + filename + ".h5";
	QString h5_path = QString("%1%2%3.h5").arg(project_path).arg(temp_folder).arg(filename);
	emit updateProcess(20, QStringLiteral("正在导入数据，请耐心等待……"));
	FormatConversion conversion;
	ret = conversion.TSX2h5(xml_filename.toStdString().c_str(), 
		h5_path.toStdString().c_str(),
		polarization.toStdString().c_str());
	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
		QFile::remove(h5_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	emit updateProcess(90, QStringLiteral("即将完成……"));

	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) {
		QFile::remove(h5_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	QModelIndex pro_index = model->indexFromItem(project);
	QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
	QStandardItem* origin = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (folder == project->child(i)->text() && project->child(i, 1)->text() == "complex-0.0")
		{
			origin = project->child(i); break;
		}
	}
	if (!origin)
	{
		origin = new QStandardItem(folder);
		origin->setIcon(QIcon(FOLDER_ICON));
		project->appendRow(origin);
		QStandardItem* Rank = new QStandardItem("complex-0.0");
		project->setChild(project->rowCount() - 1, 1, Rank);
	}
	QStandardItem* img = NULL;
	for (int i = 0; i < origin->rowCount(); i++)
	{
		if (origin->child(i)->text() == filename)
		{
			img = origin->child(i);
			break;
		}
	}
	if (!img)
	{
		img = new QStandardItem(filename);
		img->setToolTip("complex");
		QStandardItem* img_path = new QStandardItem(h5_path);
		img->setIcon(QIcon(IMAGEDATA_ICON));
		origin->appendRow(img);
		origin->setChild(origin->rowCount() - 1, 1, img_path);

		DOC = new XMLFile;
		ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_add_origin(folder.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "TSX");
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
	}
	else
	{
		origin->setChild(img->row(), 1, new QStandardItem(h5_path));
	}
	emit sendModel(model);
	InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

void MyThread::import_TSX_patch(
	QString polarization,
	QString savepath,
	vector<QString> original_file_list, 
	vector<QString> import_namelist,
	QString dst_node,
	QString dst_project,
	QStandardItemModel* model
)
{
	if (savepath.isEmpty() ||
		dst_node.isEmpty() ||
		dst_project.isEmpty() ||
		original_file_list.empty() ||
		import_namelist.empty() ||
		model == NULL
		)
	{
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
		return;
	}

	int ret;
	QDir dir(savepath);
	if (!dir.exists(dst_node))
		ret = dir.mkdir(dst_node);
	int n_images = original_file_list.size();
	int process = 2;
	FormatConversion conversion;
	DOC = new XMLFile;
	emit updateProcess(process, QStringLiteral("正在导入..."));
	for (int i = 0; i < n_images; i++)
	{
		if (!stop_flag) break;
		QString filename = import_namelist[i];
		QString xml_filename = original_file_list[i];
		QString temp_folder = QString("/") + dst_node + QString("/");
		QString relative_path = temp_folder + filename + ".h5";
		QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);
		
		ret = conversion.TSX2h5(xml_filename.toStdString().c_str(), h5_path.toStdString().c_str(), 
			polarization.toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}

		QStandardItem* project = model->findItems(dst_project)[0];
		if (!project) {
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		QModelIndex pro_index = model->indexFromItem(project);
		QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
		QStandardItem* origin = NULL;
		for (int i = 0; i < project->rowCount(); i++)
		{
			if (dst_node == project->child(i)->text() && project->child(i, 1)->text() == "complex-0.0")
			{
				origin = project->child(i); break;
			}
		}
		if (!origin)
		{
			origin = new QStandardItem(dst_node);
			origin->setIcon(QIcon(FOLDER_ICON));
			project->appendRow(origin);
			QStandardItem* Rank = new QStandardItem("complex-0.0");
			project->setChild(project->rowCount() - 1, 1, Rank);
		}
		QStandardItem* img = NULL;
		for (int j = 0; j < origin->rowCount(); j++)
		{
			if (origin->child(j)->text() == filename)
			{
				img = origin->child(j);
				break;
			}
		}
		if (!img)
		{
			img = new QStandardItem(filename);
			img->setToolTip("complex");
			QStandardItem* img_path = new QStandardItem(h5_path);
			img->setIcon(QIcon(IMAGEDATA_ICON));
			origin->appendRow(img);
			origin->setChild(origin->rowCount() - 1, 1, img_path);

			ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "TSX");
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
		}
		else
		{
			origin->setChild(img->row(), 1, new QStandardItem(h5_path));
		}
		process = double(i + 1) / double(n_images) * 100.0;
		emit updateProcess(process, QStringLiteral("正在导入..."));
	}
	
	emit sendModel(model);
	InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

void MyThread::import_CSK_patch(QString savepath, vector<QString> original_file_list, vector<QString> import_namelist, QString dst_node, QString dst_project, QStandardItemModel* model)
{
	if (savepath.isEmpty() ||
		dst_node.isEmpty() ||
		dst_project.isEmpty() ||
		original_file_list.empty() ||
		import_namelist.empty() ||
		model == NULL
		)
	{
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
		return;
	}

	int ret=0;
	QDir dir(savepath);
	if (!dir.exists(dst_node))
		ret = dir.mkdir(dst_node);
	int n_images = original_file_list.size();
	int process = 2;
	FormatConversion conversion;
	DOC = new XMLFile;
	emit updateProcess(process, QStringLiteral("正在导入..."));
	for (int i = 0; i < n_images; i++)
	{
		if (!stop_flag) break;
		QString filename = import_namelist[i];
		QString CSK_filename = original_file_list[i];
		QString temp_folder = QString("/") + dst_node + QString("/");
		QString relative_path = temp_folder + filename + ".h5";
		QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);
		CSK_reader csk_reader(CSK_filename.toStdString().c_str());
		ret = csk_reader.init();
		ret += csk_reader.write_to_h5(h5_path.toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			InSARLogManager::LogError("MyThread", "unknown format!");
			emit errorProcess("unknown format!");
			return;
		}

		QStandardItem* project = model->findItems(dst_project)[0];
		if (!project) {
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		QModelIndex pro_index = model->indexFromItem(project);
		QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
		QStandardItem* origin = NULL;
		for (int i = 0; i < project->rowCount(); i++)
		{
			if (dst_node == project->child(i)->text() && project->child(i, 1)->text() == "complex-0.0")
			{
				origin = project->child(i); break;
			}
		}
		if (!origin)
		{
			origin = new QStandardItem(dst_node);
			origin->setIcon(QIcon(FOLDER_ICON));
			project->appendRow(origin);
			QStandardItem* Rank = new QStandardItem("complex-0.0");
			project->setChild(project->rowCount() - 1, 1, Rank);
		}
		QStandardItem* img = NULL;
		for (int j = 0; j < origin->rowCount(); j++)
		{
			if (origin->child(j)->text() == filename)
			{
				img = origin->child(j);
				break;
			}
		}
		if (!img)
		{
			img = new QStandardItem(filename);
			img->setToolTip("complex");
			QStandardItem* img_path = new QStandardItem(h5_path);
			img->setIcon(QIcon(IMAGEDATA_ICON));
			origin->appendRow(img);
			origin->setChild(origin->rowCount() - 1, 1, img_path);

			ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "CSG-2");
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
		}
		else
		{
			origin->setChild(img->row(), 1, new QStandardItem(h5_path));
		}
		process = double(i + 1) / double(n_images) * 100.0;
		emit updateProcess(process, QStringLiteral("正在导入..."));
	}

	emit sendModel(model);
	InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

void MyThread::import_ALOS2_patch(
	QString savepath,
	vector<QString> IMG_file_list,
	vector<QString> LED_file_list,
	vector<QString> import_namelist,
	QString dst_node,
	QString dst_project,
	QStandardItemModel* model
)
{
	if (savepath.isEmpty() ||
		dst_node.isEmpty() ||
		dst_project.isEmpty() ||
		IMG_file_list.empty() ||
		LED_file_list.empty() ||
		import_namelist.empty() ||
		model == NULL
		)
	{
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
		return;
	}

	int ret;
	QDir dir(savepath);
	if (!dir.exists(dst_node))
		ret = dir.mkdir(dst_node);
	int n_images = IMG_file_list.size();
	int process = 2;
	FormatConversion conversion;
	DOC = new XMLFile;
	emit updateProcess(process, QStringLiteral("正在导入..."));
	for (int i = 0; i < n_images; i++)
	{
		if (!stop_flag) break;
		QString filename = import_namelist[i];
		QString ALOS_IMG_filename = IMG_file_list[i];
		QString ALOS_LED_filename = LED_file_list[i];
		QString temp_folder = QString("/") + dst_node + QString("/");
		QString relative_path = temp_folder + filename + ".h5";
		QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);
		ret = conversion.ALOS2h5(ALOS_IMG_filename.toStdString().c_str(), ALOS_LED_filename.toStdString().c_str(),
			h5_path.toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			InSARLogManager::LogError("MyThread", "unknown format!");
			emit errorProcess("unknown format!");
			return;
		}

		QStandardItem* project = model->findItems(dst_project)[0];
		if (!project) {
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		QModelIndex pro_index = model->indexFromItem(project);
		QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
		QStandardItem* origin = NULL;
		for (int i = 0; i < project->rowCount(); i++)
		{
			if (dst_node == project->child(i)->text() && project->child(i, 1)->text() == "complex-0.0")
			{
				origin = project->child(i); break;
			}
		}
		if (!origin)
		{
			origin = new QStandardItem(dst_node);
			origin->setIcon(QIcon(FOLDER_ICON));
			project->appendRow(origin);
			QStandardItem* Rank = new QStandardItem("complex-0.0");
			project->setChild(project->rowCount() - 1, 1, Rank);
		}
		QStandardItem* img = new QStandardItem(filename);
		img->setToolTip("complex");
		QStandardItem* img_path = new QStandardItem(h5_path);
		img->setIcon(QIcon(IMAGEDATA_ICON));
		origin->appendRow(img);
		origin->setChild(origin->rowCount() - 1, 1, img_path);

		ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "ALOS2");
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			InSARLogManager::LogError("MyThread", QString("Task failed or interrupted in: ") + QString(__FUNCTION__));
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		process = double(i + 1) / double(n_images) * 100.0;
		emit updateProcess(process, QStringLiteral("正在导入..."));
	}

	emit sendModel(model);
	InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}

void MyThread::ShowImage(QString h5_path, QString bmp_path, QString type)
{
	if (h5_path.isEmpty() || bmp_path.isEmpty() || type.isEmpty())
	{
		qDebug() << "MyThread::ShowImage error: one or more inputs are empty.";
		return;
	}

	if (type == "complex" || type == "phase")
	{
		emit updateProcess(20, QStringLiteral("正在生成图像预览……"));
		bool success = NodeUtils::generateJpgPreviewFromH5(h5_path, bmp_path, type);
		if (!success || QThread::currentThread()->isInterruptionRequested())
		{
			qDebug() << "MyThread::ShowImage error: generateJpgPreviewFromH5 failed or thread interrupted. Removing bmp_path.";
			QFile::remove(bmp_path);
			emit endProcess();
			return;
		}
		emit updateProcess(90, QStringLiteral("写入图像文件……"));
		cv::waitKey(500);
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
	}
	else if (type == "coherence")
	{
		FormatConversion FC;
		Utils util;
		Mat coherence;
		Mat image;
		emit updateProcess(20, QStringLiteral("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "coherence", coherence);
		emit updateProcess(50, QStringLiteral("格式转换……"));
		if (coherence.type() != CV_64F) coherence.convertTo(coherence, CV_64F);
		ret = util.savephase(bmp_path.toStdString().c_str(), "gray", coherence);

		if (coherence.rows * coherence.cols > 25e6)
		{
			emit updateProcess(80, QStringLiteral("降采样处理……"));
			int down_sample_times = (int)sqrt(floor(double(coherence.rows * coherence.cols) / 25e6));
			util.resampling(bmp_path.toStdString().c_str(), bmp_path.toStdString().c_str(), (int)(coherence.rows / down_sample_times),
				(int)(coherence.cols / down_sample_times));
		}

		emit updateProcess(90, QStringLiteral("写入图像文件……"));
		
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
	}
	else if (type == "dem")
	{
		FormatConversion FC;
		Utils util;
		Mat phase;
		Mat image;
		emit updateProcess(20, QStringLiteral("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "dem", phase);
		emit updateProcess(50, QStringLiteral("格式转换……"));
		if (phase.type() != CV_64F) phase.convertTo(phase, CV_64F);
		ret = util.savephase(bmp_path.toStdString().c_str(), "jet", phase);
		emit updateProcess(90, QStringLiteral("写入bmp文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
	}
	else if (type == "amplitude")
	{
		FormatConversion FC;
		Utils util;
		Mat phase;
		Mat image;
		emit updateProcess(20, QStringLiteral("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "amplitude", phase);
		emit updateProcess(50, QStringLiteral("格式转换……"));
		if (phase.type() != CV_32F) phase.convertTo(phase, CV_32F);
		ret = util.saveAmplitude(bmp_path.toStdString().c_str(), phase);
		emit updateProcess(90, QStringLiteral("写入bmp文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
	}
	else if (type == "SBAS")
	{
		FormatConversion FC;
		Utils util;

		Mat defomation_velocity, mask;
		Mat image;
		emit updateProcess(20, QStringLiteral("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "defomation_velocity", defomation_velocity);
		ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "mask", mask);
		emit updateProcess(50, QStringLiteral("格式转换……"));
		if (defomation_velocity.type() != CV_64F) defomation_velocity.convertTo(defomation_velocity, CV_64F);
		if(ret == 0) util.savephase_white(bmp_path.toStdString().c_str(), "jet", defomation_velocity, mask);
		else util.savephase(bmp_path.toStdString().c_str(), "jet", defomation_velocity);
		emit updateProcess(90, QStringLiteral("写入图像文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
		emit endProcess();
	}
}

void MyThread::Geocoding(
	int type, 
	int multi_rg, 
	int multi_az,
	QString project_name, 
	QString srcNode, 
	QString dstNode,
	QStandardItemModel* model
)
{
	if (!model) return;

	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) return;
	QString save_path = model->item(project->row(), 1)->text();
	QDir dir(save_path);
	if (!dir.exists(dstNode))
		dir.mkdir(dstNode);
	//外部DEM文件夹
	QString appPath = QCoreApplication::applicationDirPath();
	QString demPath = appPath + "/dem";
	QDir appDir(appPath);
	if (!appDir.exists("dem")) appDir.mkdir("dem");

	vector<string> input_files;
	vector<string> output_files;
	QList<QString> origin;
	QString product_level;
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* images = project->child(i, 0);
		if (images->text() == srcNode)
		{
			product_level = project->child(i, 1)->text();
			for (int j = 0; j < images->rowCount(); j++)
			{
				QFileInfo fileinfo(images->child(j, 1)->text());
				QString origin_name = fileinfo.baseName();
				origin.append(origin_name);
				input_files.push_back(images->child(j, 1)->text().toStdString());
				output_files.push_back(QString("%1/%2/%3_geocoded.h5").arg(save_path).arg(dstNode)
					.arg(origin_name).toStdString());
			}
			break;
		}
	}
	emit updateProcess(2, QStringLiteral("正在地理编码……"));
	FormatConversion conversion; Utils util;
	QString geocode_Rank_level;
	int ret;
	//干涉产品地理编码
	if (type == 1)
	{
		String source_file;
		Mat mapped_lat, mapped_lon, phase, mapped_phase;
		
		ret = conversion.read_array_from_h5(input_files[0].c_str(), "mapped_lon", mapped_lon);
		ret += conversion.read_array_from_h5(input_files[0].c_str(), "mapped_lat", mapped_lat);
		if (ret != 0)
		{
			Deflat flat;
			conversion.read_str_from_h5(input_files[0].c_str(), "source_1", source_file);
			QString src_file = save_path + "/" + QString(source_file.c_str());
			double lonMax, lonMin, latMax, latMin, lon_upperleft, lat_upperleft, rangeSpacing,
				nearRangeTime, wavelength, prf, start, end;
			int sceneHeight, sceneWidth, offset_row, offset_col, multilook_rg, multilook_az;
			Mat lon_coef, lat_coef, dem, mappedDem, statevec;
			string start_time, end_time, master_file;
			master_file = src_file.toStdString();
			ret = conversion.read_int_from_h5(input_files[0].c_str(), "multilook_az", &multilook_az);
			ret = conversion.read_int_from_h5(input_files[0].c_str(), "multilook_rg", &multilook_rg);
			ret = conversion.read_int_from_h5(master_file.c_str(), "range_len", &sceneWidth);
			ret = conversion.read_int_from_h5(master_file.c_str(), "azimuth_len", &sceneHeight);
			ret = conversion.read_int_from_h5(master_file.c_str(), "offset_row", &offset_row);
			ret = conversion.read_int_from_h5(master_file.c_str(), "offset_col", &offset_col);
			ret = conversion.read_array_from_h5(master_file.c_str(), "lon_coefficient", lon_coef);
			ret = conversion.read_array_from_h5(master_file.c_str(), "lat_coefficient", lat_coef);
			ret = conversion.read_double_from_h5(master_file.c_str(), "prf", &prf);
			ret = conversion.read_double_from_h5(master_file.c_str(), "carrier_frequency", &wavelength);
			wavelength = VEL_C / wavelength;
			ret = conversion.read_double_from_h5(master_file.c_str(), "range_spacing", &rangeSpacing);
			ret = conversion.read_double_from_h5(master_file.c_str(), "slant_range_first_pixel", &nearRangeTime);
			nearRangeTime = 2.0 * nearRangeTime / VEL_C;
			ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_start_time", start_time);
			ret = conversion.utc2gps(start_time.c_str(), &start);
			ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_stop_time", end_time);
			ret = conversion.utc2gps(end_time.c_str(), &end);
			ret = conversion.read_array_from_h5(master_file.c_str(), "state_vec", statevec);
			ret = Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
				&lonMax, &latMax, &lonMin, &latMin);
			ret = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem, &lon_upperleft, &lat_upperleft, lonMin, lonMax, latMin, latMax);
			ret = flat.demMapping(dem, mappedDem, mapped_lat, mapped_lon, lon_upperleft, lat_upperleft, offset_row, offset_col, sceneHeight, sceneWidth,
				prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec, 20);
			//多视操作
			if (multilook_rg > 1 || multilook_az > 1)
			{
				int rows_mapped = sceneHeight / multilook_az;
				int cols_mapped = sceneWidth / multilook_rg;
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
				for (int i = 0; i < rows_mapped; i++)
				{
					for (int j = 0; j < cols_mapped; j++)
					{
						lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
							cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
					}
				}
				lon_new.copyTo(mapped_lat);
			}
		}
		emit updateProcess(20, QStringLiteral("正在地理编码……"));
		double lat_north, lat_south, lon_west, lon_east;
		for (int i = 0; i < input_files.size(); i++)
		{
			if (product_level == QString("phase-1.0"))
			{
				ret = conversion.read_array_from_h5(input_files[i].c_str(), "phase", phase);
				geocode_Rank_level = "phase-1.1";
			}
			if (product_level == QString("phase-2.0"))
			{
				ret = conversion.read_array_from_h5(input_files[i].c_str(), "phase", phase);
				geocode_Rank_level = "phase-2.1";
			}
			if (product_level == QString("phase-3.0"))
			{
				ret = conversion.read_array_from_h5(input_files[i].c_str(), "phase", phase);
				geocode_Rank_level = "phase-3.1";
			}
			if (product_level == QString("coherence-1.0"))
			{
				ret = conversion.read_array_from_h5(input_files[i].c_str(), "coherence", phase);
				geocode_Rank_level = "coherence-1.1";
			}
			if (product_level == QString("dem-1.0"))
			{
				ret = conversion.read_array_from_h5(input_files[i].c_str(), "dem", phase);
				geocode_Rank_level = "dem-1.1";
			}
			if (product_level == QString("SBAS-1.0"))
			{
				ret = conversion.read_array_from_h5(input_files[i].c_str(), "defomation_velocity", phase);
				geocode_Rank_level = "SBAS-1.1";
			}
			ret = util.SAR2UTM(mapped_lon, mapped_lat, phase, mapped_phase, 1, &lon_east, &lon_west, &lat_north, &lat_south);
			ret = conversion.creat_new_h5(output_files[i].c_str());
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_east", lon_east);
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_west", lon_west);
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_north", lat_north);
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_south", lat_south);
			if (product_level == QString("phase-1.0") ||
				product_level == QString("phase-2.0") ||
				product_level == QString("phase-3.0")
				)
			{
				ret = conversion.write_array_to_h5(output_files[i].c_str(), "phase", mapped_phase);
			}
			if (product_level == QString("coherence-1.0"))
			{
				ret = conversion.write_array_to_h5(output_files[i].c_str(), "coherence", mapped_phase);
			}
			if (product_level == QString("dem-1.0"))
			{
				ret = conversion.write_array_to_h5(output_files[i].c_str(), "dem", mapped_phase);
			}
			if (product_level == QString("SBAS-1.0"))
			{
				ret = conversion.write_array_to_h5(output_files[i].c_str(), "defomation_velocity", mapped_phase);
			}
			int process = 20 + double(i + 1) / (double)input_files.size() * 70.0;

			emit updateProcess(process, QStringLiteral("正在地理编码……"));
		}
	}
	//SAR图像地理编码
	else
	{
		String source_file;
		Mat mapped_lat, mapped_lon, amplitude, mapped_amplitude;
		ComplexMat slc;
		geocode_Rank_level = "amplitude-1.1";

		QString project_xmlfile = save_path + "/" + project_name;
		TiXmlElement* pnode = NULL, * pchild = NULL;
		XMLFile* xmldoc = new XMLFile();
		xmldoc->XMLFile_load(project_xmlfile.toStdString().c_str());
		xmldoc->find_node("DataNode", pnode);
		while (pnode)
		{
			if (0 == strcmp(pnode->Attribute("name"), srcNode.toStdString().c_str())) break;
			pnode = pnode->NextSiblingElement();
		}
		xmldoc->_find_node(pnode, "master_image", pchild);
		int masterIndex = 1;
		if (pchild) ret = sscanf(pchild->GetText(), "%d", &masterIndex);

		ret = conversion.read_array_from_h5(input_files[masterIndex - 1].c_str(), "mapped_lon", mapped_lon);
		ret += conversion.read_array_from_h5(input_files[masterIndex - 1].c_str(), "mapped_lat", mapped_lat);
		if (ret != 0)
		{
			Deflat flat;
			double lonMax, lonMin, latMax, latMin, lon_upperleft, lat_upperleft, rangeSpacing,
				nearRangeTime, wavelength, prf, start, end;
			int sceneHeight, sceneWidth, offset_row, offset_col, multilook_rg, multilook_az;
			Mat lon_coef, lat_coef, dem, mappedDem, statevec;
			string start_time, end_time, master_file;
			master_file = input_files[masterIndex - 1];
			ret = conversion.read_int_from_h5(master_file.c_str(), "range_len", &sceneWidth);
			ret = conversion.read_int_from_h5(master_file.c_str(), "azimuth_len", &sceneHeight);
			ret = conversion.read_int_from_h5(master_file.c_str(), "offset_row", &offset_row);
			ret = conversion.read_int_from_h5(master_file.c_str(), "offset_col", &offset_col);
			ret = conversion.read_array_from_h5(master_file.c_str(), "lon_coefficient", lon_coef);
			ret = conversion.read_array_from_h5(master_file.c_str(), "lat_coefficient", lat_coef);
			ret = conversion.read_double_from_h5(master_file.c_str(), "prf", &prf);
			ret = conversion.read_double_from_h5(master_file.c_str(), "carrier_frequency", &wavelength);
			wavelength = VEL_C / wavelength;
			ret = conversion.read_double_from_h5(master_file.c_str(), "range_spacing", &rangeSpacing);
			ret = conversion.read_double_from_h5(master_file.c_str(), "slant_range_first_pixel", &nearRangeTime);
			nearRangeTime = 2.0 * nearRangeTime / VEL_C;
			ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_start_time", start_time);
			ret = conversion.utc2gps(start_time.c_str(), &start);
			ret = conversion.read_str_from_h5(master_file.c_str(), "acquisition_stop_time", end_time);
			ret = conversion.utc2gps(end_time.c_str(), &end);
			ret = conversion.read_array_from_h5(master_file.c_str(), "state_vec", statevec);
			ret = Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
				&lonMax, &latMax, &lonMin, &latMin);
			ret = Utils::getSRTMDEM(demPath.toStdString().c_str(), dem, &lon_upperleft, &lat_upperleft, lonMin, lonMax, latMin, latMax);
			ret = flat.demMapping(dem, mappedDem, mapped_lat, mapped_lon, lon_upperleft, lat_upperleft, offset_row, offset_col, sceneHeight, sceneWidth,
				prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec, 20);
		}

		//多视操作
		if (multi_rg > 1 || multi_az > 1)
		{
			int rows_mapped = mapped_lon.rows / multi_az;
			int cols_mapped = mapped_lon.cols / multi_rg;
			Mat lon_new(rows_mapped, cols_mapped, CV_32F);
			for (int i = 0; i < rows_mapped; i++)
			{
				for (int j = 0; j < cols_mapped; j++)
				{
					lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multi_az, i * multi_az + multi_az),
						cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
				}
			}
			lon_new.copyTo(mapped_lon);
			for (int i = 0; i < rows_mapped; i++)
			{
				for (int j = 0; j < cols_mapped; j++)
				{
					lon_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multi_az, i * multi_az + multi_az),
						cv::Range(j * multi_rg, j * multi_rg + multi_rg)))[0];
				}
			}
			lon_new.copyTo(mapped_lat);
		}

		emit updateProcess(20, QStringLiteral("正在地理编码……"));
		double lat_north, lat_south, lon_west, lon_east;
		for (int i = 0; i < input_files.size(); i++)
		{
			ret = conversion.read_slc_from_h5(input_files[i].c_str(), slc);
			slc.convertTo(slc, CV_64F);
			amplitude = slc.GetMod();
			util.multilook_SAR(amplitude, amplitude, multi_rg, multi_az);
			ret = util.SAR2UTM(mapped_lon, mapped_lat, amplitude, mapped_amplitude, 1, &lon_east, &lon_west, &lat_north, &lat_south);
			ret = conversion.creat_new_h5(output_files[i].c_str());
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_east", lon_east);
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lon_west", lon_west);
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_north", lat_north);
			ret = conversion.write_double_to_h5(output_files[i].c_str(), "lat_south", lat_south);
			ret = conversion.write_array_to_h5(output_files[i].c_str(), "amplitude", mapped_amplitude);
			int process = 20 + double(i + 1) / (double)input_files.size() * 70.0;
			emit updateProcess(process, QStringLiteral("正在地理编码……"));
		}
	}
	/*建立地理编码根节点*/
	QStandardItem* geocode = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dstNode)
		{
			geocode = project->child(i, 0);
			break;
		}
	}

	if (!geocode)
	{
		geocode = new QStandardItem(dstNode);
		geocode->setToolTip(project_name);
		geocode->setIcon(QIcon(FOLDER_ICON));
		project->appendRow(geocode);
		QStandardItem* geocode_Rank = new QStandardItem(geocode_Rank_level);
		project->setChild(project->rowCount() - 1, 1, geocode_Rank);
	}

	XMLFile* xml = new XMLFile();
	QString xml_path = save_path + "/" + project_name;
	xml->XMLFile_load(xml_path.toStdString().c_str());
	for (int i = 0; i < input_files.size(); i++)
	{
		QFileInfo fileinfo = QFileInfo(QString(output_files.at(i).c_str()));
		QString geocode_name = fileinfo.baseName();
		QStandardItem* item_img = NULL;
		for (int j = 0; j < geocode->rowCount(); j++)
		{
			if (geocode->child(j, 0)->text() == geocode_name)
			{
				item_img = geocode->child(j, 0);
				break;
			}
		}

		if (!item_img)
		{
			QStandardItem* geocode_images_name = new QStandardItem(geocode_name);
			if (product_level == QString("coherence-1.0")) geocode_images_name->setToolTip("coherence");
			else if (product_level == QString("phase-1.0") ||
				product_level == QString("phase-2.0") ||
				product_level == QString("phase-3.0")
				)
			{
				geocode_images_name->setToolTip("phase");
			}
			else if (product_level == QString("dem-1.0")) geocode_images_name->setToolTip("dem");
			else if (product_level == QString("SBAS-1.0")) geocode_images_name->setToolTip("SBAS");
			else geocode_images_name->setToolTip("amplitude");
			QStandardItem* geocode_images_path = new QStandardItem(fileinfo.absoluteFilePath());
			geocode_images_name->setIcon(QIcon(IMAGEDATA_ICON));
			geocode->appendRow(geocode_images_name);
			geocode->setChild(geocode->rowCount() - 1, 1, geocode_images_path);

			xml->XMLFile_add_geocoding(dstNode.toStdString().c_str(), geocode_name.toStdString().c_str(),
				("/" + dstNode + "/" + geocode_name + ".h5").toStdString().c_str(), geocode_Rank_level.toStdString().c_str());
		}
		else
		{
			geocode->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
		}
	}
	xml->XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	emit updateProcess(100, QStringLiteral("完成……"));
	InSARLogManager::LogInfo("MyThread", QString("Task completed: ") + QString(__FUNCTION__));
	emit endProcess();
}













int MyThread::complex_coherence(
	const ComplexMat& master_image,
	const ComplexMat& slave_image,
	int est_wndsize_rg,
	int est_wndsize_az,
	Mat& coherence
)
{
	int na = master_image.GetRows();
	int nr = master_image.GetCols();

	if ((na < est_wndsize_az) ||
		(nr < est_wndsize_rg) ||
		master_image.type() != CV_64F ||
		slave_image.type() != CV_64F ||
		master_image.GetCols() != slave_image.GetCols() ||
		master_image.GetRows() != slave_image.GetRows() ||
		est_wndsize_az % 2 == 0 ||
		est_wndsize_rg % 2 == 0 ||
		est_wndsize_rg < 3 ||
		est_wndsize_az < 3
		)
	{
		fprintf(stderr, "complex_coherence(): input check failed!\n\n");
		return -1;
	}

	int win_a = (est_wndsize_az - 1) / 2; //方位窗半径
	int win_r = (est_wndsize_rg - 1) / 2; //距离窗半径

	int na_new = na - 2 * win_a;
	int nr_new = nr - 2 * win_r;

	Mat Coherence(na_new, nr_new, CV_64F, Scalar::all(0));
#pragma omp parallel for schedule(guided)
	for (int i = win_a + 1; i <= na - win_a; i++)
	{
		for (int j = win_r + 1; j <= nr - win_r; j++)
		{
			//if (QThread::currentThread()->isInterruptionRequested())
			//{
			//	return -1;
			//}
			Mat planes_master[] = { Mat::zeros(2 * win_a + 1, 2 * win_r + 1, CV_64F), Mat::zeros(2 * win_a + 1, 2 * win_r + 1, CV_64F) };
			Mat planes_slave[] = { Mat::zeros(2 * win_a + 1, 2 * win_r + 1, CV_64F), Mat::zeros(2 * win_a + 1, 2 * win_r + 1, CV_64F) };
			Mat planes[] = { Mat::zeros(2 * win_a + 1, 2 * win_r + 1, CV_64F), Mat::zeros(2 * win_a + 1, 2 * win_r + 1, CV_64F) };
			Mat s1, s2;
			double up, down, sum1, sum2;
			master_image.re(Range(i - 1 - win_a, i + win_a), Range(j - 1 - win_r, j + win_r)).copyTo(planes_master[0]);
			master_image.im(Range(i - 1 - win_a, i + win_a), Range(j - 1 - win_r, j + win_r)).copyTo(planes_master[1]);

			slave_image.re(Range(i - 1 - win_a, i + win_a), Range(j - 1 - win_r, j + win_r)).copyTo(planes_slave[0]);
			slave_image.im(Range(i - 1 - win_a, i + win_a), Range(j - 1 - win_r, j + win_r)).copyTo(planes_slave[1]);

			merge(planes_master, 2, s1);
			merge(planes_slave, 2, s2);
			mulSpectrums(s1, s2, s1, 0, true);
			split(s1, planes);
			sum1 = sum(planes[0])[0];
			sum2 = sum(planes[1])[0];
			up = sqrt(sum1 * sum1 + sum2 * sum2);
			magnitude(planes_master[0], planes_master[1], planes_master[0]);
			magnitude(planes_slave[0], planes_slave[1], planes_slave[0]);
			sum1 = sum(planes_master[0].mul(planes_master[0]))[0];
			sum2 = sum(planes_slave[0].mul(planes_slave[0]))[0];
			down = sqrt(sum1 * sum2);
			Coherence.at<double>(i - 1 - win_a, j - 1 - win_r) = up / (down + 0.0000001);
		}
	}
	copyMakeBorder(Coherence, Coherence, win_a, win_a, win_r, win_r, BORDER_REFLECT);
	Coherence.copyTo(coherence);
	return 0;
}

int MyThread::change_suffix(const char* input, QString output_str, QString old_suffix, QString new_suffix)
{
	QString input_str = QString(input);
	if (!input_str.endsWith(old_suffix))
		return -1;
	int length = input_str.length();
	output_str = input_str.left(length - sizeof(old_suffix));
	output_str = output_str + new_suffix;
	return 0;
}

void MyThread::StopProcess()
{
	QMutexLocker locker(&lock);
	this->stop_flag = false;
}

bool MyThread::isStopRequested()
{
	QMutexLocker locker(&lock);
	return !stop_flag;
}


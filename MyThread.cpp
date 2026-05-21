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
	if (manifest_file.isEmpty() ||
		subswath.isEmpty() ||
		polarization.isEmpty() ||
		folder.isEmpty() ||
		project_path.isEmpty()||
		filename.isEmpty()||
		project_name.isEmpty()||
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
	emit updateProcess(20, QString::fromLocal8Bit("正在导入数据，请耐心等待……"));
	FormatConversion conversion;
	ret = conversion.import_sentinel(manifest_file.toStdString().c_str(),
		subswath.toStdString().c_str(),
		polarization.toStdString().c_str(),
		h5_path.toStdString().c_str(),
		PODFile.toStdString().c_str()
	);
	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
		QFile::remove(h5_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	emit updateProcess(90, QString::fromLocal8Bit("即将完成……"));

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
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_add_origin(folder.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "sentinel");
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
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
	emit endProcess();
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
	if (original_filelist.size() != import_namelist .size()||
		import_namelist.size() < 1 ||
		subswath.isEmpty() ||
		polarization.isEmpty() ||
		dst_node.isEmpty() ||
		dst_project.isEmpty() ||
		savepath.isEmpty() ||
		model == NULL
		)
	{
		return;
	}
	int ret;
	QDir dir(savepath);
	if (!dir.exists(dst_node))
		ret = dir.mkdir(dst_node);
	QString temp_folder = QString("/") + dst_node + QString("/");
	int n_images = original_filelist.size();
	int process = 2;
	FormatConversion conversion;
	DOC = new XMLFile;
	emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	for (int i = 0; i < n_images; i++)
	{
		if (!stop_flag) break;
		QString filename = import_namelist[i];
		QString manifest_file = original_filelist[i];
		QString relative_path = temp_folder + filename + ".h5";
		QString h5_path = QString("%1%2%3.h5").arg(savepath).arg(temp_folder).arg(filename);
		ret = conversion.import_sentinel(manifest_file.toStdString().c_str(),
			subswath.toStdString().c_str(),
			polarization.toStdString().c_str(),
			h5_path.toStdString().c_str()
		);
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
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
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "sentinel");
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
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
		emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	}
	
	emit sendModel(model);
	emit endProcess();
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
	emit updateProcess(20, QString::fromLocal8Bit("正在导入数据，请耐心等待……"));
	FormatConversion conversion;
	ret = conversion.TSX2h5(xml_filename.toStdString().c_str(), 
		h5_path.toStdString().c_str(),
		polarization.toStdString().c_str());
	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
		QFile::remove(h5_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	emit updateProcess(90, QString::fromLocal8Bit("即将完成……"));

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
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_add_origin(folder.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "TSX");
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			QFile::remove(h5_path);
			QDir tmp_dir(project_path + QString("/") + folder);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(project_name).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
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
	emit endProcess();
}

	void MyThread::import_GenericSAR(
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

	int ret=0;
	QDir dir(project_path);
	if (!dir.exists(folder))
	{
		ret = dir.mkdir(folder);
	}
	emit updateProcess(20, QString::fromLocal8Bit("正在导入数据，请耐心等待……"));
	QFileInfo fileinfo(xml_filename);
    QString suffix = fileinfo.suffix();

    QString temp_folder = QString("/") + folder + QString("/");
    QString relative_path = temp_folder + filename + "." + suffix;
    QString image_path = QString("%1%2%3.%4")
        .arg(project_path)
        .arg(temp_folder)
        .arg(filename)
        .arg(suffix);

    if (QFile::exists(image_path)) {
        QFile::remove(image_path);
    }

    ret = QFile::copy(xml_filename, image_path) ? 0 : -1;


	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
		QFile::remove(image_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	emit updateProcess(90, QString::fromLocal8Bit("即将完成……"));

	auto items = model->findItems(project_name);
	if (items.isEmpty() && !project_name.endsWith(".insar")) {
		items = model->findItems(project_name + ".insar");
	}
	
	if (items.isEmpty()) {
		QFile::remove(image_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	QStandardItem* project = items[0];
	if (!project) {
		QFile::remove(image_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	QModelIndex pro_index = model->indexFromItem(project);
	QString pro_path = model->data(model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
	
	QStandardItem* origin = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* child = project->child(i);
		QStandardItem* secondCol = project->child(i, 1);
		
		// Match folder name, regardless of second column (to be more robust)
		if (child && child->text() == folder)
		{
			origin = child;
			
			// Ensure second column says "complex-0.0" if it's not already set
			if (!secondCol) {
				project->setChild(i, 1, new QStandardItem("complex-0.0"));
			} else if (secondCol->text() != "complex-0.0") {
				secondCol->setText("complex-0.0");
			}
			break;
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
	QStandardItem* img_path = NULL;
	QString trimmedFilename = filename.trimmed();
	
	QList<int> rowsToRemove;
	for(int i=0;i<origin->rowCount();i++)
	{
		QString itemText = origin->child(i)->text().trimmed();
		// Match exact filename or filename with any extension
		if (itemText.compare(trimmedFilename, Qt::CaseInsensitive) == 0 || 
		    itemText.startsWith(trimmedFilename + ".", Qt::CaseInsensitive))
		{
			if (!img) {
				img = origin->child(i);
				img_path = origin->child(i, 1);
			} else {
				rowsToRemove.prepend(i);
			}
		}
	}
	
	foreach(int row, rowsToRemove) {
		origin->removeRow(row);
	}

	if (!img) {
		img = new QStandardItem(trimmedFilename);
		img->setToolTip("complex");
		img_path = new QStandardItem(image_path);
		img->setIcon(QIcon(IMAGEDATA_ICON));
		origin->appendRow(img);
		origin->setChild(origin->rowCount() - 1, 1, img_path);
	} else {
		img_path->setText(image_path);
	}
	
	DOC = new XMLFile;
	QString xmlFileLoadPath = QString("%1/%2").arg(pro_path).arg(project_name);
	ret = DOC->XMLFile_load(xmlFileLoadPath.toStdString().c_str());
	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
		QFile::remove(image_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	ret = DOC->XMLFile_add_origin(folder.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "Generic_SAR");
	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
		QFile::remove(image_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	ret = DOC->XMLFile_save(xmlFileLoadPath.toStdString().c_str());
	if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
	{
		QFile::remove(image_path);
		QDir tmp_dir(project_path + QString("/") + folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	emit sendModel(model);
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
	emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
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
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "TSX");
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
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
		emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	}
	
	emit sendModel(model);
	emit endProcess();
}

void MyThread::import_GenericSAR_patch(
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
		return;
	}

	int ret=0;
	QDir dir(savepath);
	if (!dir.exists(dst_node))
		ret = dir.mkdir(dst_node);
	int n_images = original_file_list.size();
	int process = 2;
	DOC = new XMLFile;
	emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	for (int i = 0; i < n_images; i++)
	{
		if (!stop_flag) break;
		QString filename = import_namelist[i];
        QString image_filename = original_file_list[i];
        QFileInfo fileinfo(image_filename);
        QString suffix = fileinfo.suffix();

        QString temp_folder = QString("/") + dst_node + QString("/");
        QString relative_path = temp_folder + filename + "." + suffix;
        QString image_path = QString("%1%2%3.%4")
            .arg(savepath)
            .arg(temp_folder)
            .arg(filename)
            .arg(suffix);

        ret = QFile::copy(image_filename, image_path) ? 0 : -1;

		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			QFile::remove(image_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}

		QStandardItem* project = model->findItems(dst_project)[0];
		if (!project) {
			QFile::remove(image_path);
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
			QStandardItem* img_path = new QStandardItem(image_path);
			img->setIcon(QIcon(IMAGEDATA_ICON));
			origin->appendRow(img);
			origin->setChild(origin->rowCount() - 1, 1, img_path);

			ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
				QFile::remove(image_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "Generic_SAR");
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
				QFile::remove(image_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
				QFile::remove(image_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
		}
		else
		{
			origin->setChild(img->row(), 1, new QStandardItem(image_path));
		}
		process = double(i + 1) / double(n_images) * 100.0;
		emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	}
	
	emit sendModel(model);
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
	emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
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
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "CSG-2");
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
				QFile::remove(h5_path);
				QDir tmp_dir(savepath + QString("/") + dst_node);
				tmp_dir.removeRecursively();
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
			if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
			{
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
		emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	}

	emit sendModel(model);
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
	emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
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
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_add_origin(dst_node.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "ALOS2");
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(dst_project).toStdString().c_str());
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested())
		{
			QFile::remove(h5_path);
			QDir tmp_dir(savepath + QString("/") + dst_node);
			tmp_dir.removeRecursively();
			return;
		}
		process = double(i + 1) / double(n_images) * 100.0;
		emit updateProcess(process, QString::fromLocal8Bit("正在导入..."));
	}

	emit sendModel(model);
	emit endProcess();
}

void MyThread::ShowImage(QString h5_path, QString bmp_path, QString type)
{
	if (h5_path == NULL ||
		bmp_path == NULL ||
		type == NULL)
	{
		return;
	}

	if (type == "complex")
	{
		Utils util;
		ComplexMat SLC64;
		FormatConversion FC;
		emit updateProcess(10, QString::fromLocal8Bit("准备数据……"));
		FC.read_slc_from_h5(h5_path.toStdString().c_str(), SLC64);
		emit updateProcess(40, QString::fromLocal8Bit("准备图像文件……"));
		util.saveSLC(bmp_path.toStdString().c_str(), /*65*/65, SLC64);
		if (QThread::currentThread()->isInterruptionRequested())
		{
			//emit endProcess();
			QFile::remove(bmp_path.toStdString().c_str());
			return;
		}
		if (SLC64.GetCols() * SLC64.GetRows() > 25e6)
		{
			emit updateProcess(80, QString::fromLocal8Bit("降采样处理……"));
			int down_sample_times = (int)sqrt(floor(double(SLC64.GetCols() * SLC64.GetRows()) / 25e6));
			util.resampling(bmp_path.toStdString().c_str(), bmp_path.toStdString().c_str(), (int)(SLC64.GetRows() / down_sample_times),
				(int)(SLC64.GetCols() / down_sample_times));
		}

		emit updateProcess(90, QString::fromLocal8Bit("写入图像文件……"));
		//if (!ret)
		//{
		//	fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		//}
		cv::waitKey(1000);
		emit endProcess();
	}
	else if (type == "phase")
	{
		FormatConversion FC;
		Utils util;
		Mat phase;
		Mat image;
		emit updateProcess(20, QString::fromLocal8Bit("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "phase", phase);
		emit updateProcess(50, QString::fromLocal8Bit("格式转换……"));
		ret = util.savephase(bmp_path.toStdString().c_str(), "jet", phase);

		if (phase.rows * phase.cols > 25e6)
		{
			emit updateProcess(80, QString::fromLocal8Bit("降采样处理……"));
			int down_sample_times = (int)sqrt(floor(double(phase.rows * phase.cols) / 25e6));
			util.resampling(bmp_path.toStdString().c_str(), bmp_path.toStdString().c_str(), (int)(phase.rows / down_sample_times),
				(int)(phase.cols / down_sample_times));
		}

		emit updateProcess(90, QString::fromLocal8Bit("写入图像文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		emit endProcess();

	}
	else if (type == "coherence")
	{
		FormatConversion FC;
		Utils util;
		Mat coherence;
		Mat image;
		emit updateProcess(20, QString::fromLocal8Bit("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "coherence", coherence);
		emit updateProcess(50, QString::fromLocal8Bit("格式转换……"));
		ret = util.savephase(bmp_path.toStdString().c_str(), "gray", coherence);

		if (coherence.rows * coherence.cols > 25e6)
		{
			emit updateProcess(80, QString::fromLocal8Bit("降采样处理……"));
			int down_sample_times = (int)sqrt(floor(double(coherence.rows * coherence.cols) / 25e6));
			util.resampling(bmp_path.toStdString().c_str(), bmp_path.toStdString().c_str(), (int)(coherence.rows / down_sample_times),
				(int)(coherence.cols / down_sample_times));
		}

		emit updateProcess(90, QString::fromLocal8Bit("写入图像文件……"));
		
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		emit endProcess();
	}
	else if (type == "dem")
	{
		FormatConversion FC;
		Utils util;
		Mat phase;
		Mat image;
		emit updateProcess(20, QString::fromLocal8Bit("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "dem", phase);
		emit updateProcess(50, QString::fromLocal8Bit("格式转换……"));
		ret = util.savephase(bmp_path.toStdString().c_str(), "jet", phase);
		emit updateProcess(90, QString::fromLocal8Bit("写入bmp文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		emit endProcess();
	}
	else if (type == "amplitude")
	{
		FormatConversion FC;
		Utils util;
		Mat phase;
		Mat image;
		emit updateProcess(20, QString::fromLocal8Bit("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "amplitude", phase);
		emit updateProcess(50, QString::fromLocal8Bit("格式转换……"));
		ret = util.saveAmplitude(bmp_path.toStdString().c_str(), phase);
		emit updateProcess(90, QString::fromLocal8Bit("写入bmp文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		emit endProcess();
	}
	else if (type == "SBAS")
	{
		FormatConversion FC;
		Utils util;

		Mat defomation_velocity, mask;
		Mat image;
		emit updateProcess(20, QString::fromLocal8Bit("读取数据……"));
		int ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "defomation_velocity", defomation_velocity);
		ret = FC.read_array_from_h5(h5_path.toStdString().c_str(), "mask", mask);
		emit updateProcess(50, QString::fromLocal8Bit("格式转换……"));
		if(ret == 0) util.savephase_white(bmp_path.toStdString().c_str(), "jet", defomation_velocity, mask);
		else util.savephase(bmp_path.toStdString().c_str(), "jet", defomation_velocity);
		emit updateProcess(90, QString::fromLocal8Bit("写入图像文件……"));
		if (!ret)
		{
			fprintf(stderr, "cv::imwrite(): can't write to %s\n\n", bmp_path.toStdString().c_str());
		}
		emit endProcess();
	}
	else
	{
		return;
	}
		
    
}

void MyThread::Cut(QList<double> para, QString save_path, QString project_name, QString src_node, QString dst_node, QStandardItemModel* model)
{
	if (para.size() != 4 ||
		save_path == NULL ||
		project_name == NULL ||
		src_node == NULL ||
		dst_node == NULL)
	{
		//QMessageBox::warning(NULL, QString::fromLocal8Bit("警告!"), QString::fromLocal8Bit("缺少处理所需参数，请检查是否填写完整！"));
		return;
	}
	DOC = new XMLFile;
    Utils util;
    FormatConversion FC;
    QDir dir(save_path);
    if (!dir.exists(dst_node))
        int ret = dir.mkdir(dst_node);
    QString h5_cut_path = QString("%1/%2").arg(save_path).arg(dst_node);
    QStandardItem* project = model->findItems(project_name)[0];
    QModelIndex pro_index = model->indexFromItem(project);
    QStandardItem* Images_Cut = NULL;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == dst_node)
        {
            Images_Cut = project->child(i, 0);
            break;
        }
    }

    if (!Images_Cut)
    {
        Images_Cut = new QStandardItem(dst_node);
        int insert = 0;
        for (; insert < project->rowCount(); insert++)
        {
            if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-1.0") == 0)
                continue;
            else
                break;
        }
        Images_Cut->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, Images_Cut);
        Images_Cut->setToolTip(project_name);
        QStandardItem* Images_Cut_Rank = new QStandardItem("complex-1.0");
        project->setChild(insert, 1, Images_Cut_Rank);
    }
    
    QModelIndex origin =  model->indexFromItem(project->child(0, 0));
    int src_node_index = 0;
    /*找到源节点并计算其节点下图像数量*/
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == src_node)
        {
            src_node_index = i;
            break;
        }
    }
    int image_number = project->child(src_node_index, 0)->rowCount();

    emit updateProcess(10, QString::fromLocal8Bit("正在读取图片信息……"));
    QByteArray file_abs_path = QString("%1/%2").arg(save_path).arg(project_name).toLocal8Bit();
    DOC->XMLFile_load(file_abs_path.data());
	
    for (int i = 0; i < image_number; i++)
    {
		if (QThread::currentThread()->isInterruptionRequested())
		{
			dir.remove(dst_node);
			return;
		}
        ComplexMat SLC;
        int offset_row = 0;
        int offset_col = 0;
        QString path = model->data(model->index(i, 1, origin)).toString();
        QFileInfo fileinfo = QFileInfo(path);
        QString name = fileinfo.baseName();
        QByteArray path_str = path.toLocal8Bit();
        util.get_AOI_from_h5slc(path_str.data(),
            para.at(0),//lon
            para.at(1),//lat
            para.at(2),//width
            para.at(3),//height
            SLC, &offset_row, &offset_col
        );
        QString Cut_name = QString("%1_cut").arg(name);

        QByteArray cut_h5_path = QString("%1/%2.h5").arg(h5_cut_path).arg(Cut_name).toLocal8Bit();
        FC.creat_new_h5(cut_h5_path.data());
        FC.write_slc_to_h5(cut_h5_path.data(), SLC);
        FC.Copy_para_from_h5_2_h5(path_str.data(), cut_h5_path.data());

        FC.write_str_to_h5(cut_h5_path.data(), "process_state", "cut");
        FC.write_str_to_h5(cut_h5_path.data(), "comment", "complex-1.0");
        Mat tmp = Mat::zeros(1, 1, CV_32SC1);
        tmp.at<int>(0, 0) = SLC.GetRows();
        FC.write_array_to_h5(cut_h5_path.data(), "azimuth_len", tmp);
        tmp.at<int>(0, 0) = SLC.GetCols();
        FC.write_array_to_h5(cut_h5_path.data(), "range_len", tmp);
        tmp.at<int>(0, 0) = offset_row;
        FC.write_array_to_h5(cut_h5_path.data(), "offset_row", tmp);
        tmp.at<int>(0, 0) = offset_col;
        FC.write_array_to_h5(cut_h5_path.data(), "offset_col", tmp);

        QStandardItem* item_img = NULL;
        for (int j = 0; j < Images_Cut->rowCount(); j++)
        {
            if (Images_Cut->child(j, 0)->text() == Cut_name)
            {
                item_img = Images_Cut->child(j, 0);
                break;
            }
        }

        if (!item_img)
        {
            QStandardItem* Image_Cut_Name = new QStandardItem(Cut_name);
            QString full_cut_path = QString("%1/%2.h5").arg(h5_cut_path).arg(Cut_name);
            QStandardItem* Image_Cut_Path = new QStandardItem(full_cut_path);
            Image_Cut_Name->setIcon(QIcon(IMAGEDATA_ICON));
            Images_Cut->appendRow(Image_Cut_Name);
            Image_Cut_Name->setToolTip("complex");
            Images_Cut->setChild(Images_Cut->rowCount() - 1, 1, Image_Cut_Path);

            QByteArray dir_name = dst_node.toLocal8Bit();
            QByteArray filename = QString("%1").arg(Cut_name).toLocal8Bit();
            QByteArray file_relative_path = QString("/%1/%2.h5").arg(dst_node).arg(Cut_name).toLocal8Bit();
            DOC->XMLFile_add_cut(dir_name.data(), -1, filename.data(),
                file_relative_path.data(),
                offset_row, offset_col, para.at(0), para.at(1),
                para.at(2), para.at(3), "complex-1.0");
        }
        else
        {
            QString full_cut_path = QString("%1/%2.h5").arg(h5_cut_path).arg(Cut_name);
            Images_Cut->setChild(item_img->row(), 1, new QStandardItem(full_cut_path));
        }
		
       emit updateProcess(10 + i * 90 / (image_number), QString::fromLocal8Bit("正在裁剪第%1个文件").arg(i+1));
    }
	DOC->XMLFile_save(file_abs_path.data());
	emit sendModel(model);
	emit endProcess();

}

void MyThread::Cut2(double h5_left, double h5_right, double h5_top, double h5_bottom, QString save_path, QString project_name, QString node_name, QString dst_node, QStandardItemModel* model)
{
	if (h5_left < 0 || h5_right < 0 || h5_top < 0 || h5_bottom < 0 ||
		h5_left > 1 || h5_right > 1 || h5_top > 1 || h5_bottom > 1 ||
		save_path == NULL ||
		project_name == NULL ||
		node_name == NULL ||
		dst_node == NULL)
	{
		return;
	}
	DOC = new XMLFile;
	Utils util;
	FormatConversion FC;
	QDir dir(save_path);
	if (!dir.exists(dst_node))
		int ret = dir.mkdir(dst_node);
	QString result_path = QString("%1/%2").arg(save_path).arg(dst_node);

	QByteArray file_abs_path = QString("%1/%2").arg(save_path).arg(project_name).toLocal8Bit();
	DOC->XMLFile_load(file_abs_path.data());

	//查找被裁剪节点是否存在主节点
	int master_index = -1;
	TiXmlElement* DataNode = NULL;
	int ret = DOC->find_node_with_attribute("DataNode", "name", node_name.toStdString().c_str(), DataNode);
	if (ret == 0 && DataNode)
	{
		TiXmlElement* pnode = NULL;
		ret = DOC->_find_node(DataNode, "master_image", pnode);
		if (ret == 0 && pnode)
		{
			ret = sscanf(pnode->GetText(), "%d", &master_index);
			if (ret != 1) master_index = -1;
		}
	}

	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* node;
	int src_node_index = 0;
	/*找到源节点并计算其节点下图像数量*/
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == node_name)
		{
			node = project->child(i, 0);
			src_node_index = i;
			break;
		}
			
	}
	int image_number = node->rowCount();
	QModelIndex pro_index = model->indexFromItem(project);
	QStandardItem* Images_Cut = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dst_node)
		{
			Images_Cut = project->child(i, 0);
			break;
		}
	}
	if (!Images_Cut)
	{
		Images_Cut = new QStandardItem(dst_node);
		int insert = 0;
		/*获取源节点数据等级信息*/
		QString src_data_rank = project->child(src_node_index, 1)->text();
		if (src_data_rank == QString("complex-0.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("complex-1.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0
					)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("complex-2.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0
					)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("complex-3.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-3.0") == 0
					)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("phase-1.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-1.0") == 0
					)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("phase-2.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-2.0") == 0
					)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("phase-3.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-3.0") == 0
					)continue;
				else break;
			}
		}
		else if (src_data_rank == QString("dem-1.0"))
		{
			for (; insert < project->rowCount(); insert++)
			{
				if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
					project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
					project->child(insert, 1)->text().compare("phase-3.0") == 0 ||
					project->child(insert, 1)->text().compare("dem-1.0") == 0
					)continue;
				else break;
			}
		}
		Images_Cut->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, Images_Cut);
		Images_Cut->setToolTip(project_name);
		QStandardItem* Images_Cut_Rank = new QStandardItem(src_data_rank);
		project->setChild(insert, 1, Images_Cut_Rank);
	}
	
	emit updateProcess(10, QString::fromLocal8Bit("正在读取图片信息……"));

	
	
	for (int i = 0; i < image_number; i++)
	{
		if (QThread::currentThread()->isInterruptionRequested())
		{
			dir.remove(dst_node);
			return;
		}

		ComplexMat SLC;
		int offset_row = 0;
		int offset_col = 0;
		QString path = node->child(i, 1)->text();
		QFileInfo fileinfo = QFileInfo(path);
		QString name = fileinfo.baseName();
		QByteArray path_str = path.toLocal8Bit();
		int rows, cols;
		FC.read_int_from_h5(path_str.toStdString().c_str(), "range_len", &cols);
		FC.read_int_from_h5(path_str.toStdString().c_str(), "azimuth_len", &rows);
		offset_row = h5_top * rows; offset_row = offset_row < 0 ? 0 : offset_row;
		offset_col = h5_left * cols; offset_col = offset_col < 0 ? 0 : offset_col;
		int row_end = h5_bottom * rows; row_end = row_end >= rows ? rows : row_end;
		int col_end = h5_right * cols; col_end = col_end >= cols ? col_end : col_end;
		int rows_cut = row_end - offset_row;
		int cols_cut = col_end - offset_col;
		FC.read_subarray_from_h5(path_str.toStdString().c_str(), "s_re", offset_row, offset_col, rows_cut, cols_cut, SLC.re);
		FC.read_subarray_from_h5(path_str.toStdString().c_str(), "s_im", offset_row, offset_col, rows_cut, cols_cut, SLC.im);

		QString Cut_name = QString("%1_cut2").arg(name);

		QByteArray cut_h5_path = QString("%1/%2.h5").arg(result_path).arg(Cut_name).toLocal8Bit();
		FC.creat_new_h5(cut_h5_path.data());
		FC.write_slc_to_h5(cut_h5_path.data(), SLC);
		FC.Copy_para_from_h5_2_h5(path_str.data(), cut_h5_path.data());

		FC.write_str_to_h5(cut_h5_path.data(), "process_state", "cut");
		QString src_data_rank = project->child(src_node_index, 1)->text();
		FC.write_str_to_h5(cut_h5_path.data(), "comment", src_data_rank.toStdString().c_str());
		FC.write_int_to_h5(cut_h5_path.data(), "range_len", SLC.GetCols());
		FC.write_int_to_h5(cut_h5_path.data(), "azimuth_len", SLC.GetRows());

		if (src_data_rank != QString("complex-0.0"))
		{
			int offset_row_old = 0, offset_col_old = 0;
			FC.read_int_from_h5(path_str.toStdString().c_str(), "offset_row", &offset_row_old);
			FC.read_int_from_h5(path_str.toStdString().c_str(), "offset_col", &offset_col_old);
			offset_row += offset_row_old;
			offset_col += offset_col_old;
		}
		FC.write_int_to_h5(cut_h5_path.data(), "offset_row", offset_row);
		FC.write_int_to_h5(cut_h5_path.data(), "offset_col", offset_col);

		QStandardItem* item_img = NULL;
		for (int j = 0; j < Images_Cut->rowCount(); j++)
		{
			if (Images_Cut->child(j, 0)->text() == Cut_name)
			{
				item_img = Images_Cut->child(j, 0);
				break;
			}
		}

		if (!item_img)
		{
			QStandardItem* Image_Cut_Name = new QStandardItem(Cut_name);
			QStandardItem* Image_Cut_Path = new QStandardItem(QString("%1/%2.h5").arg(result_path).arg(Cut_name));
			Image_Cut_Name->setIcon(QIcon(IMAGEDATA_ICON));
			Images_Cut->appendRow(Image_Cut_Name);
			Image_Cut_Name->setToolTip("complex");
			Images_Cut->setChild(Images_Cut->rowCount() - 1, 1, Image_Cut_Path);
			QByteArray dir_name = dst_node.toLocal8Bit();
			QByteArray filename = QString("%1").arg(Cut_name).toLocal8Bit();
			QByteArray file_relative_path = QString("/%1/%2.h5").arg(dst_node).arg(Cut_name).toLocal8Bit();
			DOC->XMLFile_add_cut(dir_name.data(), master_index, filename.data(),
				file_relative_path.data(),
				offset_row, offset_col, 0, 0, 0, 0, src_data_rank.toStdString().c_str());
		}
		else
		{
			Images_Cut->setChild(item_img->row(), 1, new QStandardItem(QString("%1/%2.h5").arg(result_path).arg(Cut_name)));
		}
		

		emit updateProcess(10 + i * 90 / (image_number), QString::fromLocal8Bit("正在裁剪第%1个文件").arg(i + 1));
	}

	DOC->XMLFile_save(file_abs_path.data());
	emit sendModel(model);
	emit endProcess();
}

void MyThread::Regis(QList<int> para, QString save_path, QString project_name, QString Cut_name, QString file_name, QStandardItemModel* model)
{
	if (para.size() != 4 ||
		save_path.isEmpty() ||
		project_name.isEmpty() ||
		Cut_name.isEmpty() ||
		file_name.isEmpty())
	{
		//QMessageBox::warning(NULL, QString::fromLocal8Bit("警告!"), QString::fromLocal8Bit("缺少处理所需参数，请检查是否填写完整！"));
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
                SAR_images_regis.push_back(QString("%1/%2/%3_regis.h5").arg(save_path).arg(file_name)
                    .arg(origin_name).toStdString());
            }
			break;
        }
    }
	emit updateProcess(10, QString::fromLocal8Bit("开始进行配准……"));
	Mat offset_row_out, offset_col_out;
    int ret = Registration_copy(SAR_images, SAR_images_regis, offset_row_out, offset_col_out, index, interp_times, block_size);
    if (ret<0 || QThread::currentThread()->isInterruptionRequested())
    {
		return;
    }
    /*建立配准根节点*/
    QStandardItem* regis = NULL;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == file_name)
        {
            regis = project->child(i, 0);
            break;
        }
    }

    if (!regis)
    {
        regis = new QStandardItem(file_name);
        regis->setToolTip(project_name);
        int insert = 0;
        for (; insert < project->rowCount(); insert++)
        {
            if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                project->child(insert, 1)->text().compare("complex-2.0") == 0
                )
                continue;
            else
                break;
        }
        regis->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, regis);
        QStandardItem* regis_Rank = new QStandardItem("complex-2.0");
        project->setChild(insert, 1, regis_Rank);
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
    FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "state_vec", State_Vec_Master);
    FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lon_coefficient", Lon_Coeff_Master);
    FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lat_coefficient", Lat_Coeff_Master);
    FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "prf", tmp_double);
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
    QString temporal_baseline, B_parallel, B_effect;
    /*添加图像到model中并复制h5参数*/
    vector<int> Row_offset;
    vector<int> Col_offset;
	emit updateProcess(90, QString::fromLocal8Bit("写入辅助参数……"));
    for (int i = 0; i < image_number; i++)
    {
		if (QThread::currentThread()->isInterruptionRequested())
		{
			return;
		}
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

        /*写入辅助参数到h5*/
		offset_row = offset_col = 0;
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
            FC.read_array_from_h5(SAR_images.at(i).c_str(), "state_vec", State_Vec_Slave);
            FC.read_array_from_h5(SAR_images.at(i).c_str(), "lon_coefficient", Lon_Coeff_Slave);
            FC.read_array_from_h5(SAR_images.at(i).c_str(), "lat_coefficient", Lat_Coeff_Slave);
            FC.read_array_from_h5(SAR_images.at(i).c_str(), "prf", tmp_double);
            interp_interval_slave = 1 / tmp_double.at<double>(0, 0);
            FC.read_str_from_h5(SAR_images.at(i).c_str(), "acquisition_start_time", time_slave_str);
            FC.utc2gps(time_slave_str.c_str(), &time_Slave);
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
	emit updateProcess(95, QString::fromLocal8Bit("写入工程文件……"));
	xmlfile.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
    for (int i = 0; i < image_number; i++)
    {
		if (QThread::currentThread()->isInterruptionRequested())
		{
			return;
		}
        QString relativePath = QString("/%1/%2").arg(file_name).arg(origin.at(i) + "_regis.h5");

        // Check if regis entry already exists in XML before adding
        // Note: XMLFile_add_regis implementation might handle this, but for safety and UI consistency, 
        // we already checked the UI side. The XML side should ideally also be checked if possible,
        // but here we follow the pattern of the UI check.
        xmlfile.XMLFile_add_regis(file_name.toStdString().c_str(), (origin.at(i) + "_regis").toStdString().c_str(), relativePath.toStdString().c_str(),
            Row_offset.at(i), Col_offset.at(i), index, interp_times, block_size,
            temporal_baseline.toStdString().c_str(), B_effect.toStdString().c_str(), B_parallel.toStdString().c_str());
        
    }
	xmlfile.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
    emit sendModel(model);
	emit endProcess();
}

void MyThread::DEMAssistCoregistration(
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
	QString appPath = QCoreApplication::applicationDirPath();
	QString demPath = appPath + "/dem";
	string dempath = demPath.toStdString();
	QDir appDir(appPath);
	if (!appDir.exists("dem")) appDir.mkdir("dem");

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
				SAR_images_regis.push_back(QString("%1/%2/%3_regis.h5").arg(savepath).arg(dstNode)
					.arg(origin_name).toStdString());
			}
			break;
		}
	}
	if (SAR_images.size() < 2) return;

	emit updateProcess(10, QString::fromLocal8Bit("开始进行配准……"));
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
	Mat Row_offset(images_number, 1, CV_32S); Row_offset.at<int>(masterIndex - 1, 0) = 0;
	Mat Col_offset(images_number, 1, CV_32S); Col_offset.at<int>(masterIndex - 1, 0) = 0;
	conversion.creat_new_h5(SAR_images_regis[masterIndex - 1].c_str());
	conversion.write_slc_to_h5(SAR_images_regis[masterIndex - 1].c_str(), slave);
	conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "range_len", slave.GetCols());
	conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "azimuth_len", slave.GetRows());
	conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "offset_row", offset_row);
	conversion.write_int_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "offset_col", offset_col);
	conversion.Copy_para_from_h5_2_h5(SAR_images[masterIndex - 1].c_str(), SAR_images_regis[masterIndex - 1].c_str());
	conversion.write_str_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "process_state", "coregistration");
	conversion.write_str_to_h5(SAR_images_regis[masterIndex - 1].c_str(), "comment", "complex-2.0");
	
	Utils::computeImageGeoBoundry(lat_coef, lon_coef, sceneHeight, sceneWidth, offset_row, offset_col,
		&lonMax, &latMax, &lonMin, &latMin);
	Utils::getSRTMDEM(dempath.c_str(), dem, &lon_upperleft, &lat_upperleft, lonMin, lonMax, latMin, latMax);
	coregis.getDEMRgAzPos(dem, statevec, rangePos, azimuthPos, lon_upperleft, lat_upperleft, offset_row, offset_col,
		sceneHeight, sceneWidth, prf, rangeSpacing, wavelength, nearRangeTime, start, end, 5.0 / 6000.0, 5.0 / 6000.0);
	int count = 0;
	for (int i = 0; i < images_number; i++)
	{
		if (i == masterIndex - 1) continue;
		int offset_r, offset_c;
		offset_row2 = offset_col2 = 0;
		slave_file = SAR_images[i].c_str();
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

		coregis.getDEMRgAzPos(dem, statevec2, rangePos2, azimuthPos2, lon_upperleft, lat_upperleft, offset_row2, offset_col2,
			sceneHeight2, sceneWidth2, prf2, rangeSpacing2, wavelength, nearRangeTime2, start2, end2, 5.0 / 6000.0, 5.0 / 6000.0);

		coregis.computeSlaveOffset(rangePos, azimuthPos, rangePos2, azimuthPos2, slaveAzimuthOffset, slaveRangeOffset);
		coregis.fitSlaveOffset(slaveAzimuthOffset, rangePos, azimuthPos, &a0, &a1, &a2);
		coregis.fitSlaveOffset(slaveRangeOffset, rangePos, azimuthPos, &b0, &b1, &b2);
		coregis.performBilinearResampling(slave, sceneHeight, sceneWidth, b0, b1, b2, a0, a1, a2, &offset_r, &offset_c);
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
		count++;
		emit updateProcess(10 + double(count) / double(images_number - 1) * 80, QString::fromLocal8Bit("正在处理..."));
	}


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
		regis->setToolTip(project_name);
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
	
	QString temporal_baseline, B_parallel, B_effect;
	for (int i = 0; i < images_number; i++)
	{
		if (QThread::currentThread()->isInterruptionRequested())
		{
			return;
		}
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
		
		temporal_baseline += "0 ";
		B_parallel += "0 ";
		B_effect += "0 ";
	}
	/*写入XML*/
	XMLFile xmlfile;
	emit updateProcess(95, QString::fromLocal8Bit("写入工程文件……"));
	xmlfile.XMLFile_load((QString(savepath) + "/" + project_name).toStdString().c_str());
	for (int i = 0; i < images_number; i++)
	{
		if (QThread::currentThread()->isInterruptionRequested())
		{
			return;
		}
		QString relativePath = QString("/%1/%2").arg(dstNode).arg(origin.at(i) + "_regis.h5");
		xmlfile.XMLFile_add_regis(dstNode.toStdString().c_str(), (origin.at(i) + "_regis").toStdString().c_str(), 
			relativePath.toStdString().c_str(),
			Row_offset.at<int>(i, 0), Col_offset.at<int>(i, 0), masterIndex, -1, -1,
			temporal_baseline.toStdString().c_str(), B_effect.toStdString().c_str(), B_parallel.toStdString().c_str());

	}
	xmlfile.XMLFile_save((QString(savepath) + "/" + project_name).toStdString().c_str());
	emit sendModel(model);
	emit endProcess();

}

void MyThread::S1_Deburst(
	QString savePath, 
	QString dstProject, 
	QString srcNode, 
	QString dstNode, 
	QStandardItemModel* model
)
{
	if (
		savePath.isEmpty() ||
		dstProject.isEmpty() ||
		dstNode.isEmpty() ||
		srcNode.isEmpty() ||
		!model
		)
	{
		return;
	}
	int ret;
	QDir dir(savePath);
	if (!dir.exists(dstNode))dir.mkdir(dstNode);
	QDir outDir(savePath + "/" + dstNode);
	vector<string> SAR_images;
	vector<string> SAR_images_deburst;
	QList<QString> origin;
	QStandardItem* project = model->findItems(dstProject)[0];
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* images = project->child(i, 0);
		if (images->text() == srcNode)
		{
			for (int j = 0; j < images->rowCount(); j++)
			{
				QFileInfo fileinfo(images->child(j, 1)->text());
				QString origin_name = fileinfo.baseName();
				origin.append(origin_name);
				SAR_images.push_back(images->child(j, 1)->text().toStdString());
				SAR_images_deburst.push_back(QString("%1/%2/%3_deburst.h5").arg(savePath).arg(dstNode)
					.arg(origin_name).toStdString());
			}
		}
	}
	emit updateProcess(10, QString::fromLocal8Bit("开始burst拼接……"));
	//burst拼接
	for (int i = 1; i <= SAR_images.size(); i++)
	{
		Sentinel1Utils su(SAR_images[i - 1].c_str());
		ret = su.init();
		if (ret < 0) return;
		ret = su.deburst(SAR_images_deburst[i - 1].c_str());
		if (ret < 0) return;
		emit updateProcess(10.0 + 80.0 / SAR_images.size() * i, QString::fromLocal8Bit("burst拼接进度%1……").arg(10.0 + 80.0 / SAR_images.size() * i));
	}
	/*建立deburst根节点*/
	QStandardItem* deburst = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dstNode)
		{
			deburst = project->child(i, 0);
			break;
		}
	}

	if (!deburst)
	{
		deburst = new QStandardItem(dstNode);
		deburst->setToolTip(dstProject);
		int insert = 0;
		for (; insert < project->rowCount(); insert++)
		{
			if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-1.0") == 0)
				continue;
			else
				break;
		}
		deburst->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, deburst);
		QStandardItem* deburst_Rank = new QStandardItem("complex-1.0");
		project->setChild(insert, 1, deburst_Rank);
	}
	
	/*写入XML*/
	XMLFile xmlfile;
	emit updateProcess(95, QString::fromLocal8Bit("写入工程文件……"));
	xmlfile.XMLFile_load((savePath + "/" + dstProject).toStdString().c_str());
	for (int i = 0; i < SAR_images_deburst.size(); i++)
	{
		QFileInfo fileinfo = QFileInfo(QString(SAR_images_deburst.at(i).c_str()));
		QString deburst_name = fileinfo.baseName();
		QStandardItem* item_img = NULL;
		for (int j = 0; j < deburst->rowCount(); j++)
		{
			if (deburst->child(j, 0)->text() == deburst_name)
			{
				item_img = deburst->child(j, 0);
				break;
			}
		}

		if (!item_img)
		{
			QStandardItem* deburst_images_name = new QStandardItem(deburst_name);
			deburst_images_name->setToolTip("complex");
			QStandardItem* deburst_images_path = new QStandardItem(fileinfo.absoluteFilePath());
			deburst_images_name->setIcon(QIcon(IMAGEDATA_ICON));
			deburst->appendRow(deburst_images_name);
			deburst->setChild(deburst->rowCount() - 1, 1, deburst_images_path);
		}
		else
		{
			deburst->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
		}

		QString relativePath = QString("/%1/%2").arg(dstNode).arg(origin.at(i) + "_deburst.h5");
		ret = xmlfile.XMLFile_add_S1_Deburst(dstNode.toStdString().c_str(), (origin.at(i) + "_deburst").toStdString().c_str(),
			relativePath.toStdString().c_str());

	}
	xmlfile.XMLFile_save((savePath + "/" + dstProject).toStdString().c_str());
	emit sendModel(model);
	emit endProcess();
}

void MyThread::S1_TOPS_BackGeocoding(
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
		return;
	}
	int ret;
	QDir dir(savePath);
	if (!dir.exists(dstNode))dir.mkdir(dstNode);
	QDir outDir(savePath + "/" + dstNode);
	vector<string> SAR_images;
	vector<string> SAR_images_regis;
	QList<QString> origin;
	QStandardItem* project = model->findItems(dstProject)[0];
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* images = project->child(i, 0);
		if (images->text() == srcNode)
		{
			for (int j = 0; j < images->rowCount(); j++)
			{
				QFileInfo fileinfo(images->child(j, 1)->text());
				QString origin_name = fileinfo.baseName();
				origin.append(origin_name);
				SAR_images.push_back(images->child(j, 1)->text().toStdString());
				SAR_images_regis.push_back(QString("%1/%2/%3_regis.h5").arg(savePath).arg(dstNode)
					.arg(origin_name).toStdString());
			}
		}
	}
	emit updateProcess(10, QString::fromLocal8Bit("开始后向地理编码配准……"));

	//外部DEM文件夹
	QString appPath = QCoreApplication::applicationDirPath();
	QString demPath = appPath + "/dem";
	QDir appDir(appPath);
	if (!appDir.exists("dem")) appDir.mkdir("dem");

	//后向地理编码配准
	Sentinel1BackGeocoding backgeocoding; FormatConversion conversion;
	ComplexMat slaveSLC, tmp;
	Utils util;
	string tmpDem = demPath.toStdString();
	std::replace(tmpDem.begin(), tmpDem.end(), '/', '\\');

	ret = backgeocoding.loadData(SAR_images);
	ret = backgeocoding.setDEMPath(tmpDem.c_str());
	ret = backgeocoding.loadOutFiles(SAR_images_regis);
	ret = backgeocoding.setMasterIndex(masterIndex);
	if (backgeocoding.numOfImages < 2) return;
	ret = conversion.read_slc_from_h5(backgeocoding.su[masterIndex - 1]->h5File.c_str(), tmp);
	tmp.convertTo(tmp, CV_32F);
	conversion.creat_new_h5(backgeocoding.outFiles[masterIndex - 1].c_str());
	ret = conversion.write_slc_to_h5(backgeocoding.outFiles[masterIndex - 1].c_str(), tmp);
	tmp.re = 0.0; tmp.im = 0.0;
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
		if (i == masterIndex - 1) continue;
		conversion.creat_new_h5(backgeocoding.outFiles[i].c_str());
		ret = conversion.write_slc_to_h5(backgeocoding.outFiles[i].c_str(), tmp);
	}

	Mat start(backgeocoding.su[masterIndex - 1]->burstCount, 1, CV_32S), end(backgeocoding.su[masterIndex - 1]->burstCount, 1, CV_32S);
	start.at<int>(0, 0) = 1;
	end.at<int>(0, 0) = backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(0, 0);
	double lastValidTime = backgeocoding.su[masterIndex - 1]->burstAzimuthTime.at<double>(0, 0) +
		(backgeocoding.su[masterIndex - 1]->lastValidLine.at<int>(0, 0) - 1) * backgeocoding.su[masterIndex - 1]->azimuthTimeInterval;
	double firstValidTime;
	int overlap;
	Mat overlapMat = Mat::zeros(backgeocoding.su[masterIndex - 1]->burstCount - 1, 1, CV_32S);
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
	int offset_row = 0, lines = 0;
	double lonMin, lonMax, latMin, latMax;
	int burstCount = backgeocoding.su[masterIndex - 1]->burstCount;
	for (int i = 0; i < burstCount; i++)
	{
		ret = backgeocoding.su[masterIndex - 1]->computeImageGeoBoundry(&lonMin, &lonMax, &latMin, &latMax, i + 1);
		if (ret < 0) return;
		ret = backgeocoding.loadDEM(backgeocoding.DEMPath.c_str(), lonMin, lonMax, latMin, latMax);
		if (ret < 0) return;
		
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
			Mat coef(1, 6, CV_64F);
			ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
			if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
			Mat derampDemodPhase;
			ret = backgeocoding.su[slaveImageIndex - 1]->computeDerampDemodPhase(sBurstIndex, derampDemodPhase);
			ret = backgeocoding.performDerampDemod(derampDemodPhase, slaveSLC);
			ret = backgeocoding.computeSlavePosition(slaveImageIndex, mBurstIndex);
			Mat slaveAzimuthOffset, slaveRangeOffset;
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
			ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
				offset_row, 0, linesPerBurst, samplesPerBurst);
		}
		offset_row += linesPerBurst;
		backgeocoding.isMasterRgAzComputed = false;
		if (QThread::currentThread()->isInterruptionRequested())
		{
			outDir.removeRecursively();
			return;
		}
		emit updateProcess(10.0 + 50.0 / burstCount * (i + 1), QString::fromLocal8Bit("后向地理编码配准……"));
	}

	if (b_ESD)
	{
		Mat overlap_phase(cv::sum(overlapMat)[0], samplesPerBurst, CV_64F);
		Mat phase; int count_sum = 0;
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
			Mat phase0, coh;
			util.multilook(overlap_phase, phase0, 16, 4);
			//if (j == 1)
			//{
			//	util.cvmat2bin("E:\\working_dir\\projects\\software\\InSAR\\bin\\phase1.bin", phase0);
			//	util.savephase("E:\\working_dir\\projects\\software\\InSAR\\bin\\phase1.jpg", "jet", phase0);
			//}
			//if (j == 2)
			//{
			//	util.cvmat2bin("E:\\working_dir\\projects\\software\\InSAR\\bin\\phase2.bin", phase0);
			//	util.savephase("E:\\working_dir\\projects\\software\\InSAR\\bin\\phase2.jpg", "jet", phase0);
			//}
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
			Mat output, out_x;
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
			//if(j == 1) util.cvmat2bin("E:\\working_dir\\projects\\software\\InSAR\\bin\\output1.bin", output);
			//if (j == 2) util.cvmat2bin("E:\\working_dir\\projects\\software\\InSAR\\bin\\output2.bin", output);
			conversion.write_double_to_h5(backgeocoding.outFiles[j].c_str(), "offset_a", offset_a);
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
				//ComplexMat tmp;
				double a0Rg, a1Rg, a2Rg, a0Az, a1Az, a2Az;
				Mat coef(1, 6, CV_64F);
				ret = backgeocoding.su[slaveImageIndex - 1]->getBurst(sBurstIndex, slaveSLC);
				if (slaveSLC.type() != CV_64F) slaveSLC.convertTo(slaveSLC, CV_64F);
				Mat derampDemodPhase;
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
				if (ret < 0) return;
				ret = conversion.write_subarray_to_h5(backgeocoding.outFiles[j].c_str(), "s_im", slaveSLC.im,
					offset_row, 0, linesPerBurst, samplesPerBurst);
				if (ret < 0) return;

			}
			offset_row += linesPerBurst;
			backgeocoding.isMasterRgAzComputed = false;

			emit updateProcess(60 + 30 / burstCount * (i + 1), QString::fromLocal8Bit("增强谱分集校正……"));
		}
	}

	//deburst
	ComplexMat slc;
	for (int i = 0; i < backgeocoding.numOfImages; i++)
	{
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
		emit updateProcess(90 + 10 / burstCount * (i + 1), QString::fromLocal8Bit("deburst……"));
	}

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
	
	FormatConversion FC;
	/*获取主星参数*/
	Mat outArray;
	int rows, cols;
	FC.read_array_from_h5(SAR_images_regis.at(masterIndex - 1).c_str(), "s_re", outArray);
	rows = outArray.rows; cols = outArray.cols;
	offset_row = 0;
	int offset_col = 0;
	/*添加图像到model中并复制h5参数*/
	//emit updateProcess(90, QString::fromLocal8Bit("写入辅助参数……"));
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

		/*写入辅助参数到h5*/
		FC.Copy_para_from_h5_2_h5(SAR_images.at(i).c_str(), SAR_images_regis.at(i).c_str());
		FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "process_state", "coregistration");
		FC.write_str_to_h5(SAR_images_regis.at(i).c_str(), "comment", "complex-2.0");
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_row", 0);
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "offset_col", 0);
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "azimuth_len", rows);
		FC.write_int_to_h5(SAR_images_regis.at(i).c_str(), "range_len", cols);
	}
	/*写入XML*/
	XMLFile xmlfile;
	//emit updateProcess(95, QString::fromLocal8Bit("写入工程文件……"));
	xmlfile.XMLFile_load((savePath + "/" + dstProject).toStdString().c_str());
	for (int i = 0; i < images_number; i++)
	{
		QString relativePath = QString("/%1/%2").arg(dstNode).arg(origin.at(i) + "_regis.h5");
		ret = xmlfile.XMLFile_add_backgeocoding(dstNode.toStdString().c_str(), (origin.at(i) + "_regis").toStdString().c_str(),
			relativePath.toStdString().c_str(), masterIndex);

	}
	xmlfile.XMLFile_save((savePath + "/" + dstProject).toStdString().c_str());
	emit sendModel(model);
	emit endProcess();
}

void MyThread::SLC_deramp(
	int masterIndex,
	QString project_name,
	QString src_node,
	QString dst_node,
	QStandardItemModel* model
)
{
	if (masterIndex < 1 ||
		project_name.isEmpty() ||
		dst_node.isEmpty() ||
		src_node.isEmpty() ||
		!model
		)
	{
		return;
	}
	//确定外部DEM文件夹
	QString appPath = QCoreApplication::applicationDirPath();
	QString demPath = appPath + "/dem";
	QDir appDir(appPath);
	if (!appDir.exists("dem")) appDir.mkdir("dem");

	//确定待处理数据文件
	Utils util; FormatConversion conversion; Deflat flat;
	vector<string> SAR_images, SAR_images_deramp;
	QList<QString> origin;
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* image = NULL;
	if (!project) return;
	QString save_path = model->item(project->row(), 1)->text();
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == src_node)
		{
			image = project->child(i, 0);
			break;
		}
	}
	if (!image) return;
	int image_number = image->rowCount();
	for (int i = 0; i < image->rowCount(); i++)
	{
		SAR_images.push_back(image->child(i, 1)->text().toStdString());
		QFileInfo fileinfo(image->child(i, 1)->text());
		QString origin_name = fileinfo.baseName();
		origin.append(origin_name);
		SAR_images_deramp.push_back(QString("%1/%2/%3_deramp.h5").arg(save_path).arg(dst_node)
			.arg(origin_name).toStdString());
	}
	emit updateProcess(10, QString::fromLocal8Bit("开始计算……"));
	int ret;
	QDir dir(save_path);
	if (!dir.exists(dst_node))dir.mkdir(dst_node);
	for (int i = 0; i < image_number; i++)
	{
		ret = conversion.creat_new_h5(SAR_images_deramp[i].c_str());
	}
	double lonMax, lonMin, latMax, latMin, lon_upperleft, lat_upperleft, rangeSpacing,
		nearRangeTime, wavelength, prf, start, end;
	int sceneHeight, sceneWidth, offset_row, offset_col;
	Mat lon_coef, lat_coef, dem, mappedDem, statevec;
	ComplexMat slc;
	string start_time, end_time, master_file;
	master_file = SAR_images[masterIndex - 1];
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
	Mat mappedLon, mappedLat;
	ret = flat.demMapping(dem, mappedDem, mappedLat, mappedLon, lon_upperleft, lat_upperleft, offset_row, offset_col, sceneHeight, sceneWidth,
		prf, rangeSpacing, wavelength, nearRangeTime, start, end, statevec, 20);
	//mappedDem = 0;
	/*建立deramp根节点*/
	QStandardItem* deramp = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dst_node)
		{
			deramp = project->child(i, 0);
			break;
		}
	}

	if (!deramp)
	{
		deramp = new QStandardItem(dst_node);
		deramp->setToolTip(project_name);
		int insert = 0;
		for (; insert < project->rowCount(); insert++)
		{
			if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-3.0") == 0
				)
				continue;
			else
				break;
		}
		deramp->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, deramp);
		QStandardItem* deramp_Rank = new QStandardItem("complex-3.0");
		project->setChild(insert, 1, deramp_Rank);
	}
	
	ret = conversion.write_array_to_h5(SAR_images_deramp[masterIndex - 1].c_str(), "mapped_lat", mappedLat);
	ret = conversion.write_array_to_h5(SAR_images_deramp[masterIndex - 1].c_str(), "mapped_lon", mappedLon);
	for (int i = 0; i < image_number; i++)
	{
		ret = flat.SLC_deramp(slc, mappedDem, mappedLat, mappedLon, SAR_images[i].c_str());
		ret = conversion.write_slc_to_h5(SAR_images_deramp[i].c_str(), slc);
		ret = conversion.Copy_para_from_h5_2_h5(SAR_images[i].c_str(), SAR_images_deramp[i].c_str());
		//ret = conversion.write_array_to_h5(SAR_images_deramp[i].c_str(), "mapped_lat", mappedLat);
		//ret = conversion.write_array_to_h5(SAR_images_deramp[i].c_str(), "mapped_lon", mappedLon);
		ret = conversion.read_int_from_h5(SAR_images[i].c_str(), "offset_row", &offset_row);
		ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "offset_row", offset_row);
		ret = conversion.read_int_from_h5(SAR_images[i].c_str(), "offset_col", &offset_col);
		ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "offset_col", offset_col);
		ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "range_len", sceneWidth);
		ret = conversion.write_int_to_h5(SAR_images_deramp[i].c_str(), "azimuth_len", sceneHeight);
		double process = 10 + 80 / (double(image_number)) * double(i + 1);
		//写入到工程管理树模型中
		QFileInfo fileinfo = QFileInfo(QString(SAR_images_deramp.at(i).c_str()));
		QString deramp_name = fileinfo.baseName();
		QStandardItem* item_img = NULL;
		for (int j = 0; j < deramp->rowCount(); j++)
		{
			if (deramp->child(j, 0)->text() == deramp_name)
			{
				item_img = deramp->child(j, 0);
				break;
			}
		}

		if (!item_img)
		{
			QStandardItem* deramp_images_name = new QStandardItem(deramp_name);
			deramp_images_name->setToolTip("complex");
			QStandardItem* deramp_images_path = new QStandardItem(fileinfo.absoluteFilePath());
			deramp_images_name->setIcon(QIcon(IMAGEDATA_ICON));
			deramp->appendRow(deramp_images_name);
			deramp->setChild(deramp->rowCount() - 1, 1, deramp_images_path);
		}
		else
		{
			deramp->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
		}

		emit updateProcess(process, QString::fromLocal8Bit("进度..."));
	}

	/*写入XML*/
	XMLFile xmlfile;
	emit updateProcess(95, QString::fromLocal8Bit("写入工程文件……"));
	xmlfile.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
	for (int i = 0; i < image_number; i++)
	{
		QString relativePath = QString("/%1/%2").arg(dst_node).arg(origin.at(i) + "_deramp.h5");
		ret = xmlfile.XMLFile_add_SLC_deramp(dst_node.toStdString().c_str(), (origin.at(i) + "_deramp").toStdString().c_str(),
			relativePath.toStdString().c_str(), masterIndex);

	}
	xmlfile.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	emit sendModel(model);

	emit endProcess();
	

}

void MyThread::Baseline_Formation(
	int masterIndex, 
	QString project_name,
	QString src_node,
	QStandardItemModel* model
)
{
	if (masterIndex < 1 ||
		project_name.isEmpty() || 
		src_node.isEmpty() || 
		!model)
	{
		return;
	}
	Utils util;
	vector<string> SAR_images;
	QList<QString> origin;
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* image = NULL;
	if (!project) return;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == src_node)
		{
			image = project->child(i, 0);
		}
	}
	if (!image) return;
	int image_number = image->rowCount();
	QList<double> spatial_baseline;
	QList<double> temporal_baseline;
	for (int i = 0; i < image->rowCount(); i++)
	{
		SAR_images.push_back(image->child(i, 1)->text().toStdString());
	}
	emit updateProcess(10, QString::fromLocal8Bit("开始基线估计……"));
	FormatConversion FC;
	/*获取主星参数*/
	Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
	double interp_interval;
	int offset_row, offset_col;
	int Rows, Cols;
	double time_Master = 0;
	string time_master_str;
	FC.read_array_from_h5(SAR_images.at(masterIndex - 1).c_str(), "state_vec", State_Vec_Master);
	FC.read_array_from_h5(SAR_images.at(masterIndex - 1).c_str(), "lon_coefficient", Lon_Coeff_Master);
	FC.read_array_from_h5(SAR_images.at(masterIndex - 1).c_str(), "lat_coefficient", Lat_Coeff_Master);
	FC.read_double_from_h5(SAR_images.at(masterIndex - 1).c_str(), "prf", &interp_interval);
	interp_interval = 1 / interp_interval;
	FC.read_int_from_h5(SAR_images.at(masterIndex - 1).c_str(), "offset_row", &offset_row);
	FC.read_int_from_h5(SAR_images.at(masterIndex - 1).c_str(), "offset_col", &offset_col);
	FC.read_str_from_h5(SAR_images.at(masterIndex - 1).c_str(), "acquisition_start_time", time_master_str);
	FC.utc2gps(time_master_str.c_str(), &time_Master);
	FC.read_int_from_h5(SAR_images.at(masterIndex - 1).c_str(), "range_len", &Cols);
	FC.read_int_from_h5(SAR_images.at(masterIndex - 1).c_str(), "azimuth_len", &Rows);
	/*添加图像到model中并复制h5参数*/
	vector<int> Row_offset;
	vector<int> Col_offset;

	for (int i = 0; i < image_number; i++)
	{

		if (QThread::currentThread()->isInterruptionRequested())
		{
			return;
		}
		/*估计时空基线*/
		if (i == masterIndex - 1)  //主图像
		{
			temporal_baseline.push_back(0);
			spatial_baseline.push_back(0);
		}
		else
		{
			Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
			double interp_interval_slave;
			double V_baseline = 0, H_baseline = 0;
			double sigma_V = 0, sigma_H = 0;
			double time_Slave = 0;
			string time_slave_str;
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "state_vec", State_Vec_Slave);
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "lon_coefficient", Lon_Coeff_Slave);
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "lat_coefficient", Lat_Coeff_Slave);
			FC.read_double_from_h5(SAR_images.at(i).c_str(), "prf", &interp_interval_slave);
			interp_interval_slave = 1 / interp_interval_slave;
			FC.read_str_from_h5(SAR_images.at(i).c_str(), "acquisition_start_time", time_slave_str);
			FC.utc2gps(time_slave_str.c_str(), &time_Slave);
			double delta = (time_Slave - time_Master) / 60 / 60 / 24;
			temporal_baseline.push_back(delta);
			util.baseline_estimation(State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
				offset_row, offset_col, Rows, Cols, interp_interval, interp_interval_slave, &V_baseline, &H_baseline, &sigma_V, &sigma_H);
			spatial_baseline.push_back(V_baseline);
		}
		emit updateProcess(20 + (i + 1) * 80 / image_number, QString::fromLocal8Bit("正在计算时空基线……"));
	}
	
	emit sendBL(temporal_baseline, spatial_baseline, masterIndex);
	emit endProcess();
}

void MyThread::SBAS_time_series(
	double temporal_thresh_low,
	double temporal_thresh, 
	double spatial_thresh,
	int multilook_rg, 
	int multilook_az,
	int unwrap_method, 
	double alpha,
	double coherence_thresh, 
	double temporal_coherence_thresh, 
	double refinement_coh_thresh,
	double refinemen_def_thresh,
	QString project_name,
	QString srcNode,
	QString dstNode,
	QString csv_path,
	QStandardItemModel* model
)
{
	/*创建csv文件*/
	QDir csv(csv_path);
	if (!csv.exists()) //判断文件是否存在，不存在则创建
	{
		if (!csv.mkpath(csv.absolutePath()))
		{
			QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("创建csv文件失败，请检查路径是否正确!"));
			return;
		}
	}
	QFile csv_file(csv_path);
	QTextStream in(&csv_file);;
	if (!csv_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
	{	
		QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("创建csv文件失败，请检查路径是否正确!"));
		return;
	}

	/*获取SAR图像数据堆栈文件信息*/

	Utils util; SBAS sbas; FormatConversion conversion; Unwrap unwrap;
	int ret;
	vector<string> SAR_images;
	QList<QString> origin;
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* image = NULL;
	if (!project) return;
	QString save_path = model->item(project->row(), 1)->text();
	string save_path_std_string = save_path.toStdString();
	std::replace(save_path_std_string.begin(), save_path_std_string.end(), '/', '\\');
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == srcNode)
		{
			image = project->child(i, 0); break;
		}
	}
	if (!image) return;
	int image_number = image->rowCount();
	for (int i = 0; i < image->rowCount(); i++)
	{
		SAR_images.push_back(image->child(i, 1)->text().toStdString());
	}
	//确定应用程序路径
	string appPath = QCoreApplication::applicationDirPath().toStdString();
	std::replace(appPath.begin(), appPath.end(), '/', '\\');
	/*干涉相位生成*/
	emit updateProcess(10, QString::fromLocal8Bit("差分干涉相位生成……"));
	Mat temporal, spatial, formation_matrix, spatial_baseline, temporal_baseline;
	util.spatialTemporalBaselineEstimation(SAR_images, 1, temporal, spatial);
	sbas.get_formation_matrix(spatial, temporal, spatial_thresh, temporal_thresh_low, temporal_thresh / 365.0,
		formation_matrix, spatial_baseline, temporal_baseline);
	QString ifgSavePath = save_path + "/" + dstNode;
	string path1 = ifgSavePath.toStdString();
	std::replace(path1.begin(), path1.end(), '/', '\\');
	QDir dir(save_path);
	if (!dir.exists(dstNode))dir.mkdir(dstNode);
	sbas.generate_interferograms(SAR_images, formation_matrix, spatial_baseline, temporal_baseline, multilook_az, multilook_rg,
		path1.c_str(), true, alpha);

	/*计算高相干点*/
	vector<SBAS_edge> edges;
	vector<SBAS_node> nodes;
	vector<SBAS_triangle> triangles;
	vector<int> node_neighbours;
	vector<string> phaseFiles;
	int n_images = formation_matrix.rows;
	char str[256];
	for (int i = 0; i < n_images; i++)
	{
		for (int j = 0; j < i; j++)
		{
			if (formation_matrix.at<int>(i, j) == 1)
			{
				memset(str, 0, 256);
				sprintf(str, "\\%d_%d.h5", i + 1, j + 1);
				string str2 = path1 + str;
				phaseFiles.push_back(str2);
			}
		}
	}
	Mat mask; 
	Mat coherence, phase;
	string mcf_problem = path1 + "\\mcf_problem.net";
	string mcf_solution = path1 + "\\mcf_problem.net.sol";
	if (unwrap_method == 1)
	{
		/*生成高相干三角网络*/
		sbas.generate_high_coherence_mask(phaseFiles, 3, 3, coherence_thresh, 0.5, mask);
		int nonzero = cv::countNonZero(mask);
		string node_file = path1 + "\\high_coherence.node";
		string edge_file = path1 + "\\high_coherence.1.edge";
		string ele_file = path1 + "\\high_coherence.1.ele";
		string neigh_file = path1 + "\\high_coherence.1.neigh";
		sbas.write_high_coherence_node(mask, node_file.c_str());
		util.gen_delaunay(node_file.c_str(), appPath.c_str());
		sbas.read_edges(edge_file.c_str(), nonzero, edges, node_neighbours);
		sbas.init_SBAS_node(nodes, edges, node_neighbours);
		sbas.init_SBAS_triangle(ele_file.c_str(), neigh_file.c_str(), triangles, edges, nodes);
		sbas.set_high_coherence_node_coordinate(mask, nodes);

		
		double obj;
		//三角网络解缠
		for (int i = 0; i < phaseFiles.size(); i++)
		{
			conversion.read_array_from_h5(phaseFiles[i].c_str(), "phase", phase);
			ret = conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coherence);
			if (ret < 0)
			{
				util.phase_coherence(phase, coherence);
			}
			sbas.set_high_coherence_node_phase(mask, nodes, edges, phase);
			sbas.set_weight_by_coherence(coherence, nodes, edges);
			sbas.compute_high_coherence_residue(nodes, edges, triangles);
			int num_residues = 0;
			sbas.residue_num(triangles, &num_residues);
			if (num_residues > 0)
			{
				sbas.writeDIMACS_spatial(mcf_problem.c_str(), nodes, edges, triangles);
				unwrap.mcf_delaunay(mcf_problem.c_str(),appPath.c_str());
				sbas.readDIMACS(mcf_solution.c_str(), nodes, edges, triangles, &obj);
			}
			sbas.floodFillUnwrap(nodes, edges, 1, false);
			sbas.retrieve_unwrapped_phase(nodes, phase);
			conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase);
			for (int j = 0; j < nodes.size(); j++)
			{
				nodes[j].b_unwrapped = false;
			}
			int process = double(i + 1) / phaseFiles.size() * 100.0 * 0.5;
			emit updateProcess(10 + process, QString::fromLocal8Bit("相位解缠中……"));
		}
	}
	else
	{
		//规则网络解缠
		mask = 1;
		for (int i = 0; i < phaseFiles.size(); i++)
		{
			conversion.read_array_from_h5(phaseFiles[i].c_str(), "phase", phase);
			ret = conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coherence);
			if (ret < 0)
			{
				util.phase_coherence(phase, coherence);
			}
			Mat residue, phase2;
			util.residue(phase, residue);
			if (unwrap_method == 2)//SNAPHU方法
			{
				unwrap.snaphu(phase, phase2, path1.c_str());
			}
			else//MCF方法
			{
				unwrap.MCF(phase, phase2, coherence, residue, mcf_problem.c_str(), appPath.c_str());
			}
			//phase2 = phase2 - phase2.at<double>(0, 0);
			conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase2);
			int process = double(i + 1) / phaseFiles.size() * 100.0 * 0.5;
			emit updateProcess(10 + process, QString::fromLocal8Bit("相位解缠中……"));
		}
	}
	/*第一次轨道精炼和重去平*/
	emit updateProcess(65, QString::fromLocal8Bit("轨道精炼和重去平……"));
	//根据参考点进行相位校正，参考点默认为最左上角的点
	int ref_i = 0, ref_j = 0;
	bool b_break = false;
	for (int i = 0; i < phase.rows; i++)
	{
		for (int j = 0; j < phase.cols; j++)
		{
			if (mask.at<int>(i, j) == 1)
			{
				ref_i = i; ref_j = j;
				b_break = true;
				break;
			}
		}
		if (b_break) break;
	}
	for (int i = 0; i < phaseFiles.size(); i++)
	{
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase);
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coherence);
		sbas.refinement_and_reflattening(phase, mask, coherence, refinement_coh_thresh);
		phase = phase - phase.at<double>(ref_i, ref_j);
		conversion.write_array_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase);
	}

	/*最小二乘法求解线性形变速率和高程残差*/
	//首先确定矩阵B
	int M = phaseFiles.size();//干涉图幅数
	int N = SAR_images.size() - 1;//时间序列数
	Mat B(M, N, CV_64F); B = 0.0;
	Mat one = Mat::ones(N, 1, CV_64F);
	for (int i = 0; i < M; i++)
	{
		int ii, jj;
		string temp_str = phaseFiles[i];
		std::replace(temp_str.begin(), temp_str.end(), '\\', '/');
		QFileInfo fileinfo(QString(temp_str.c_str()));
		sscanf(fileinfo.baseName().toStdString().c_str(), "%d_%d", &ii, &jj);
		for (int j = jj; j < ii; j++)
		{
			B.at<double>(i, j - 1) = (temporal.at<double>(0, j) - temporal.at<double>(0, j - 1)) / 365.0;
		}
	}
	Mat B1 = B * one;
	//确定矩阵c
	Mat c(M, 1, CV_64F), col(nodes.size(), 1, CV_64F); c = 0.0; col = 0.0;
	vector<Mat> phase_vec, phase_vec2, coh_vec;
	phase_vec.resize(M); phase_vec2.resize(N + 1); coh_vec.resize(M);

	int offset_col, row, count = 0;
	double nearRange, theta = 32.412, spacing, wavelength, B_spatial, B_temporal;
	for (int i = 0; i < M; i++)
	{
		Mat temp;
		conversion.read_int_from_h5(phaseFiles[i].c_str(), "offset_col", &offset_col);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "slant_range_first_pixel", &nearRange);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "range_spacing", &spacing);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "B_spatial", &B_spatial);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "B_temporal", &B_temporal);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "carrier_frequency", &wavelength);
		wavelength = VEL_C / wavelength;
		ret = conversion.read_array_from_h5(phaseFiles[i].c_str(), "inc_coefficient", temp);
		if (ret == 0) {
			theta = temp.at<double>(0, 0) / 180.0 * PI;
		}
		else
		{
			conversion.read_double_from_h5(phaseFiles[i].c_str(), "inc_center", &theta);
			theta = theta / 180.0 * PI;
		}
		double r = nearRange + double(offset_col) * spacing;
		c.at<double>(i, 0) = 4 * PI / wavelength * B_spatial / sin(theta) / r;
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase_vec[i]);
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coh_vec[i]);
	}
	Mat dummy = Mat::zeros(phase.rows, phase.cols, CV_64F);
	for (int i = 0; i < N + 1; i++)
	{
		dummy.copyTo(phase_vec2[i]);
	}
	Mat BMc;
	Mat v(phase.rows, phase.cols, CV_64F), z(phase.rows, phase.cols, CV_64F), temporal_coh(phase.rows, phase.cols, CV_64F);
	v = 0.0; z = 0.0; temporal_coh = 0.0;
	cv::hconcat(B1, c, BMc);
	count = 0;
	Mat coh_variation(1, M, CV_64F); coh_variation = 0.0;
	
	emit updateProcess(70, QString::fromLocal8Bit("时间序列分析……"));
#pragma omp parallel for schedule(guided)
	for (int i = 0; i < phase.rows; i++)
	{
		Mat temp(M, 1, CV_64F), temp_coh(M, M, CV_64F); temp = 0.0, temp_coh = 0.0; double coh;
		for (int j = 0; j < phase.cols; j++)
		{
			if (mask.at<int>(i, j) > 0)
			{
				for (int k = 0; k < M; k++)
				{
					temp.at<double>(k, 0) = phase_vec[k].at<double>(i, j);
					temp_coh.at<double>(k, k) = coh_vec[k].at<double>(i, j);
				}
				//最小二乘法求解
				Mat A_t, A, b;
				BMc.copyTo(A);
				temp.copyTo(b);
				cv::transpose(A, A_t);
				A = A_t * temp_coh * A;
				b = A_t * temp_coh * b;
				Mat x;
				if (!cv::solve(A, b, x, cv::DECOMP_LU))
				{
					fprintf(stderr, "SBAS_time_series(): can't solve least square problem!\n");
				}
				else
				{
					v.at<double>(i, j) = x.at<double>(0, 0);
					z.at<double>(i, j) = x.at<double>(1, 0);
				}
			}
		}
	}

	/*第二次轨道精炼和重去平*/
	v = v / 4 / PI * wavelength;
	Mat refinement_mask; mask.copyTo(refinement_mask); refinement_mask = 0;
	for (int i = 0; i < phase.rows; i++)
	{
		for (int j = 0; j < phase.cols; j++)
		{
			if (mask.at<int>(i, j) == 1 && fabs(v.at<double>(i, j)) < refinemen_def_thresh)
			{
				refinement_mask.at<int>(i, j) = 1;
			}
		}
	}
	emit updateProcess(75, QString::fromLocal8Bit("第二次轨道精炼和重去平……"));
	
	for (int i = 0; i < phaseFiles.size(); i++)
	{
		//没有找到足够的点进行重去平则直接使用第一次重去平的结果
		if (cv::countNonZero(refinement_mask) < 4)
		{
			conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase_vec[i]);
		}
		else
		{
			b_break = false;
			for (int i = 0; i < phase.rows; i++)
			{
				for (int j = 0; j < phase.cols; j++)
				{
					if (refinement_mask.at<int>(i, j) == 1)
					{
						ref_i = i; ref_j = j;
						b_break = true;
						break;
					}
				}
				if (b_break) break;
			}
			conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase);
			conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coherence);
			sbas.refinement_and_reflattening(phase, refinement_mask, coherence, refinement_coh_thresh);
			phase = phase - phase.at<double>(ref_i, ref_j);
			phase.copyTo(phase_vec[i]);
			conversion.write_subarray_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase, 0, 0, phase.rows, phase.cols);
		}
		
	}
#pragma omp parallel for schedule(guided)
	for (int i = 0; i < phase.rows; i++)
	{
		Mat temp(M, 1, CV_64F), temp_coh(M, M, CV_64F); temp = 0.0, temp_coh = 0.0; double coh;
		for (int j = 0; j < phase.cols; j++)
		{
			if (mask.at<int>(i, j) > 0)
			{
				for (int k = 0; k < M; k++)
				{
					temp.at<double>(k, 0) = phase_vec[k].at<double>(i, j);
					temp_coh.at<double>(k, k) = coh_vec[k].at<double>(i, j);
				}
				//最小二乘法求解
				Mat A_t, A, b;
				BMc.copyTo(A);
				temp.copyTo(b);
				cv::transpose(A, A_t);
				A = A_t * temp_coh * A;
				b = A_t * temp_coh * b;
				Mat x;
				if (!cv::solve(A, b, x, cv::DECOMP_LU))
				{
					fprintf(stderr, "SBAS_time_series(): can't solve least square problem!\n");
				}
				else
				{
					v.at<double>(i, j) = x.at<double>(0, 0);
					z.at<double>(i, j) = x.at<double>(1, 0);
				}
				//减去地形误差相位
				temp = temp - x.at<double>(1, 0) * c;
				B.copyTo(A);
				temp.copyTo(b);
				cv::transpose(A, A_t);
				A = A_t * temp_coh * A;
				b = A_t * temp_coh * b;
				if (!cv::solve(A, b, x, cv::DECOMP_SVD))
				{
					fprintf(stderr, "SBAS_time_series(): can't solve SVD!\n");
				}
				else
				{
					for (int k = 1; k < N + 1; k++)
					{
						phase_vec2[k].at<double>(i, j) = x.at<double>(k - 1, 0) *
							(temporal.at<double>(0, k) - temporal.at<double>(0, k - 1)) / 365.0
							+ phase_vec2[k - 1].at<double>(i, j);
					}
				}
				//计算时间相关系数
				x = B * x;
				coh = 0.0;
				sbas.compute_temporal_coherence(x, temp, &coh);
				temporal_coh.at<double>(i, j) = coh;
			}
		}
	}

	
	//保存时序分析结果
	emit updateProcess(80, QString::fromLocal8Bit("结果筛选……"));
	Mat out_mask, mask_count_map;
	mask.copyTo(out_mask);
	mask.copyTo(mask_count_map);
	out_mask = 0; mask_count_map = 0;
	string times_series_h5 = path1 + "\\SBAS_time_series.h5";
	conversion.creat_new_h5(times_series_h5.c_str());
	int nr, nc;
	nr = mask.rows; nc = mask.cols;
	int valide_count = 0;
	for (int i = 0; i < nr; i++)
	{
		for (int j = 0; j < nc; j++)
		{
			if (temporal_coh.at<double>(i, j) > temporal_coherence_thresh) 
			{ 
				out_mask.at<int>(i, j) = 1; 
				mask_count_map.at<int>(i, j) = valide_count;
				valide_count++;
			}
		}
	}
	//如果模型相关系数阈值太高导致没有点被选出，则全选
	if (valide_count == 0)
	{
		valide_count = cv::countNonZero(mask);
		mask.copyTo(out_mask);
		int count_temp = 0;
		for (int i = 0; i < nr; i++)
		{
			for (int j = 0; j < nc; j++)
			{
				if (out_mask.at<int>(i, j) == 1)
				{
					mask_count_map.at<int>(i, j) = count_temp;
					count_temp++;
				}
			}
		}
	}
	Mat time_series(valide_count, N + 1, CV_64F); time_series = 0.0; valide_count = 0;
	
	for (int i = 0; i < nr; i++)
	{
		for (int j = 0; j < nc; j++)
		{
			if (out_mask.at<int>(i, j) == 1)
			{
				in << qSetFieldWidth(6) << i << j;
				Mat series(1, N + 1, CV_64F); Mat temp_A(N + 1, 2, CV_64F); temp_A = 1.0;Mat temp_b(N + 1, 1, CV_64F);
				for (int k = 0; k < N + 1; k++)
				{
					series.at<double>(0, k) = phase_vec2[k].at<double>(i, j);
					temp_A.at<double>(k, 0) = temporal.at<double>(0, k) / 365.0;
					temp_b.at<double>(k, 0) = phase_vec2[k].at<double>(i, j);
					in << qSetFieldWidth(6)<< qSetRealNumberPrecision(5)<< series.at<double>(0, k);
					
				}
				series.copyTo(time_series(cv::Range(valide_count, valide_count + 1), cv::Range(0, N + 1)));
				valide_count++;
				//最小二乘法拟合线性形变速率
				Mat temp_A_t, temp_x;
				cv::transpose(temp_A, temp_A_t);
				temp_A = temp_A_t * temp_A;
				temp_b = temp_A_t * temp_b;
				if (cv::solve(temp_A, temp_b, temp_x, cv::DECOMP_LU))
				{
					v.at<double>(i, j) = temp_x.at<double>(0, 0);
					in << qSetFieldWidth(6) << qSetRealNumberPrecision(5) << v.at<double>(i, j);
				}
				in << "\n";
			}
		}
	}
	csv_file.close();
	emit updateProcess(95, QString::fromLocal8Bit("结果保存……"));
	Mat mapped_lat, mapped_lon;
	double max_def, min_def;
	
	conversion.write_int_to_h5(times_series_h5.c_str(), "ref_row", ref_i);
	conversion.write_int_to_h5(times_series_h5.c_str(), "ref_col", ref_j);
	conversion.write_int_to_h5(times_series_h5.c_str(), "multilook_rg", multilook_rg);
	conversion.write_int_to_h5(times_series_h5.c_str(), "multilook_az", multilook_az);
	conversion.write_array_to_h5(times_series_h5.c_str(), "temporal_baseline", temporal);
	conversion.write_array_to_h5(times_series_h5.c_str(), "spatial_baseline", spatial);
	conversion.write_array_to_h5(times_series_h5.c_str(), "formation_matrix", formation_matrix);
	conversion.write_array_to_h5(times_series_h5.c_str(), "mask", out_mask);
	conversion.write_array_to_h5(times_series_h5.c_str(), "mask_count_map", mask_count_map);
	time_series = time_series / 4 / PI * wavelength;
	cv::minMaxLoc(time_series, &min_def, &max_def);
	conversion.write_double_to_h5(times_series_h5.c_str(), "max_deformation", max_def);
	conversion.write_double_to_h5(times_series_h5.c_str(), "min_deformation", min_def);
	conversion.write_array_to_h5(times_series_h5.c_str(), "deformation_time_series", time_series);
	conversion.write_array_to_h5(times_series_h5.c_str(), "temporal_coherence", temporal_coh);
	v = v / 4 / PI * wavelength;
	conversion.write_array_to_h5(times_series_h5.c_str(), "defomation_velocity", v);
	conversion.write_array_to_h5(times_series_h5.c_str(), "residue_topography", z);
	for (int ii = 0; ii < phaseFiles.size(); ii++)
	{
		ret = conversion.read_array_from_h5(phaseFiles[ii].c_str(), "mapped_lat", mapped_lat);
		if (ret == 0)
		{
			conversion.read_array_from_h5(phaseFiles[ii].c_str(), "mapped_lon", mapped_lon);
			Mat lon_new(temporal_coh.rows, temporal_coh.cols, CV_32F), lat_new(temporal_coh.rows, temporal_coh.cols, CV_32F);
			for (int i = 0; i < temporal_coh.rows; i++)
			{
				for (int j = 0; j < temporal_coh.cols; j++)
				{
					lon_new.at<float>(i, j) = cv::mean(mapped_lon(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
						cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
					lat_new.at<float>(i, j) = cv::mean(mapped_lat(cv::Range(i * multilook_az, i * multilook_az + multilook_az),
						cv::Range(j * multilook_rg, j * multilook_rg + multilook_rg)))[0];
				}
			}
			conversion.write_array_to_h5(times_series_h5.c_str(), "mapped_lat", lat_new);
			conversion.write_array_to_h5(times_series_h5.c_str(), "mapped_lon", lon_new);
			break;
		}
	}
	
	


	/*建立SBAS时间序列分析根节点*/
	QStandardItem* SBAS_series = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dstNode)
		{
			SBAS_series = project->child(i, 0);
			break;
		}
	}

	if (!SBAS_series)
	{
		SBAS_series = new QStandardItem(dstNode);
		SBAS_series->setToolTip(project_name);
		int insert = 0;
		for (; insert < project->rowCount(); insert++)
		{
			if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-3.0") == 0 ||
				project->child(insert, 1)->text().compare("dem-1.0") == 0 ||
				project->child(insert, 1)->text().compare("SBAS-1.0") == 0
				)
				continue;
			else
				break;
		}
		SBAS_series->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, SBAS_series);
		QStandardItem* SBAS_series_Rank = new QStandardItem("SBAS-1.0");
		project->setChild(insert, 1, SBAS_series_Rank);
	}

	//写入到工程管理树模型中
	QStandardItem* item_img = NULL;
	for (int j = 0; j < SBAS_series->rowCount(); j++)
	{
		if (SBAS_series->child(j, 0)->text() == "SBAS_time_series")
		{
			item_img = SBAS_series->child(j, 0);
			break;
		}
	}

	if (!item_img)
	{
		QStandardItem* SBAS_series_name = new QStandardItem(QString("SBAS_time_series"));
		SBAS_series_name->setToolTip("SBAS");
		std::replace(times_series_h5.begin(), times_series_h5.end(), '\\', '/');
		QStandardItem* SBAS_series_name_path = new QStandardItem(QString(times_series_h5.c_str()));
		std::replace(times_series_h5.begin(), times_series_h5.end(), '/', '\\');
		SBAS_series_name->setIcon(QIcon(IMAGEDATA_ICON));
		SBAS_series->appendRow(SBAS_series_name);
		SBAS_series->setChild(SBAS_series->rowCount() - 1, 1, SBAS_series_name_path);

		/*写入XML*/
		XMLFile xmlfile;
		xmlfile.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
		QString relativePath = QString("/%1/SBAS_time_series.h5").arg(dstNode);
		xmlfile.XMLFile_add_SBAS(dstNode.toStdString().c_str(), "SBAS_time_series", relativePath.toStdString().c_str());
		xmlfile.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	}
	else
	{
		std::replace(times_series_h5.begin(), times_series_h5.end(), '\\', '/');
		SBAS_series->setChild(item_img->row(), 1, new QStandardItem(QString(times_series_h5.c_str())));
		std::replace(times_series_h5.begin(), times_series_h5.end(), '/', '\\');
	}
	emit sendModel(model);
	emit endProcess();

}

void MyThread::SBAS_reference_reselection(QString project_name, QString srcNode, int ref_row, int ref_col, QList<QPoint> GCPs, QStandardItemModel* model)
{
	/*获取SAR图像数据堆栈文件信息*/

	Utils util; SBAS sbas; FormatConversion conversion;
	int ret;
	string times_series_h5;
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* image = NULL;
	if (!project) return;
	QString save_path = model->item(project->row(), 1)->text();
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == srcNode)
		{
			image = project->child(i, 0); break;
		}
	}
	if (!image) return;
	times_series_h5 = image->child(0, 1)->text().toStdString();
	Mat formation_matrix, mask, temporal_baseline, reflattening_mask;
	ret = conversion.read_array_from_h5(times_series_h5.c_str(), "formation_matrix", formation_matrix);
	ret = conversion.read_array_from_h5(times_series_h5.c_str(), "mask", mask);
	ret = conversion.read_array_from_h5(times_series_h5.c_str(), "temporal_baseline", temporal_baseline);
	//确定应用程序路径
	string appPath = QCoreApplication::applicationDirPath().toStdString();
	std::replace(appPath.begin(), appPath.end(), '/', '\\');
	QString ifgSavePath = save_path + "/" + srcNode;
	string path1 = ifgSavePath.toStdString();
	std::replace(path1.begin(), path1.end(), '/', '\\');

	int num_GCPs = GCPs.size();
	mask.copyTo(reflattening_mask); reflattening_mask = 0;
	for (int i = 0; i < num_GCPs; i++)
	{
		reflattening_mask.at<int>(GCPs[i].x(), GCPs[i].y()) = 1;
	}

	vector<string> phaseFiles;
	int n_images = formation_matrix.rows;
	char str[256];
	for (int i = 0; i < n_images; i++)
	{
		for (int j = 0; j < i; j++)
		{
			if (formation_matrix.at<int>(i, j) == 1)
			{
				memset(str, 0, 256);
				sprintf(str, "\\%d_%d.h5", i + 1, j + 1);
				string str2 = path1 + str;
				phaseFiles.push_back(str2);
			}
		}
	}
	
	Mat coherence, phase;
	/*轨道精炼和重去平*/
	emit updateProcess(10, QString::fromLocal8Bit("轨道精炼和重去平……"));
	for (int i = 0; i < phaseFiles.size(); i++)
	{
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_1", phase);
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coherence);
		sbas.refinement_and_reflattening(phase, reflattening_mask, coherence, 0.0);
		phase = phase - phase.at<double>(ref_row, ref_col);
		conversion.write_subarray_to_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase, 0, 0, phase.rows, phase.cols);
	}

	/*最小二乘法求解线性形变速率和高程残差*/
	//首先确定矩阵B
	int M = phaseFiles.size();//干涉图幅数
	int N = temporal_baseline.cols - 1;//时间序列数
	Mat B(M, N, CV_64F); B = 0.0;
	Mat one = Mat::ones(N, 1, CV_64F);
	for (int i = 0; i < M; i++)
	{
		int ii, jj;
		string temp_str = phaseFiles[i];
		std::replace(temp_str.begin(), temp_str.end(), '\\', '/');
		QFileInfo fileinfo(QString(temp_str.c_str()));
		sscanf(fileinfo.baseName().toStdString().c_str(), "%d_%d", &ii, &jj);
		for (int j = jj; j < ii; j++)
		{
			B.at<double>(i, j - 1) = (temporal_baseline.at<double>(0, j) - temporal_baseline.at<double>(0, j - 1)) / 365.0;
		}
	}
	Mat B1 = B * one;
	//确定矩阵c
	Mat c(M, 1, CV_64F); c = 0.0;
	vector<Mat> phase_vec, phase_vec2, coh_vec;
	phase_vec.resize(M); phase_vec2.resize(N + 1); coh_vec.resize(M);

	int offset_col, row, count = 0;
	double nearRange, theta = 32.412, spacing, wavelength, B_spatial, B_temporal;
	for (int i = 0; i < M; i++)
	{
		Mat temp;
		conversion.read_int_from_h5(phaseFiles[i].c_str(), "offset_col", &offset_col);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "slant_range_first_pixel", &nearRange);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "range_spacing", &spacing);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "B_spatial", &B_spatial);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "B_temporal", &B_temporal);
		conversion.read_double_from_h5(phaseFiles[i].c_str(), "carrier_frequency", &wavelength);
		wavelength = VEL_C / wavelength;
		ret = conversion.read_array_from_h5(phaseFiles[i].c_str(), "inc_coefficient", temp);
		if (ret == 0) {
			theta = temp.at<double>(0, 0) / 180.0 * PI;
		}
		else
		{
			conversion.read_double_from_h5(phaseFiles[i].c_str(), "inc_center", &theta);
			theta = theta / 180.0 * PI;
		}
		double r = nearRange + double(offset_col) * spacing;
		c.at<double>(i, 0) = 4 * PI / wavelength * B_spatial / sin(theta) / r;
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "unwrapped_phase_2", phase_vec[i]);
		conversion.read_array_from_h5(phaseFiles[i].c_str(), "coherence", coh_vec[i]);
	}
	Mat dummy = Mat::zeros(phase.rows, phase.cols, CV_64F);
	for (int i = 0; i < N + 1; i++)
	{
		dummy.copyTo(phase_vec2[i]);
	}
	Mat BMc;
	Mat v(phase.rows, phase.cols, CV_64F), z(phase.rows, phase.cols, CV_64F), temporal_coh(phase.rows, phase.cols, CV_64F);
	v = 0.0; z = 0.0; temporal_coh = 0.0;
	cv::hconcat(B1, c, BMc);
	count = 0;
	
	emit updateProcess(30, QString::fromLocal8Bit("时间序列分析……"));
#pragma omp parallel for schedule(guided)
	for (int i = 0; i < phase.rows; i++)
	{
		Mat temp(M, 1, CV_64F), temp_coh(M, M, CV_64F); temp = 0.0, temp_coh = 0.0; double coh;
		for (int j = 0; j < phase.cols; j++)
		{
			if (mask.at<int>(i, j) > 0)
			{
				for (int k = 0; k < M; k++)
				{
					temp.at<double>(k, 0) = phase_vec[k].at<double>(i, j);
					temp_coh.at<double>(k, k) = coh_vec[k].at<double>(i, j);
				}
				//最小二乘法求解
				Mat A_t, A, b;
				BMc.copyTo(A);
				temp.copyTo(b);
				cv::transpose(A, A_t);
				A = A_t * temp_coh * A;
				b = A_t * temp_coh * b;
				Mat x;
				if (!cv::solve(A, b, x, cv::DECOMP_LU))
				{
					fprintf(stderr, "SBAS_time_series(): can't solve least square problem!\n");
				}
				else
				{
					v.at<double>(i, j) = x.at<double>(0, 0);
					z.at<double>(i, j) = x.at<double>(1, 0);
				}
				//减去地形误差相位
				temp = temp - x.at<double>(1, 0) * c;
				B.copyTo(A);
				temp.copyTo(b);
				cv::transpose(A, A_t);
				A = A_t * temp_coh * A;
				b = A_t * temp_coh * b;
				if (!cv::solve(A, b, x, cv::DECOMP_SVD))
				{
					fprintf(stderr, "SBAS_time_series(): can't solve SVD!\n");
				}
				else
				{
					for (int k = 1; k < N + 1; k++)
					{
						phase_vec2[k].at<double>(i, j) = x.at<double>(k - 1, 0) *
							(temporal_baseline.at<double>(0, k) - temporal_baseline.at<double>(0, k - 1)) / 365.0
							+ phase_vec2[k - 1].at<double>(i, j);
					}
				}
				//计算时间相关系数
				x = B * x;
				coh = 0.0;
				sbas.compute_temporal_coherence(x, temp, &coh);
				temporal_coh.at<double>(i, j) = coh;
			}
		}
	}


	//保存时序分析结果
	emit updateProcess(80, QString::fromLocal8Bit("结果筛选……"));
	int nr, nc;
	nr = mask.rows; nc = mask.cols;
	int valide_count = cv::countNonZero(mask);


	Mat time_series(valide_count, N + 1, CV_64F); time_series = 0.0; valide_count = 0;
	for (int i = 0; i < nr; i++)
	{
		for (int j = 0; j < nc; j++)
		{
			if (mask.at<int>(i, j) == 1)
			{
				Mat series(1, N + 1, CV_64F); Mat temp_A(N + 1, 2, CV_64F); temp_A = 1.0; Mat temp_b(N + 1, 1, CV_64F);
				for (int k = 0; k < N + 1; k++)
				{
					series.at<double>(0, k) = phase_vec2[k].at<double>(i, j);
					temp_A.at<double>(k, 0) = temporal_baseline.at<double>(0, k) / 365.0;
					temp_b.at<double>(k, 0) = phase_vec2[k].at<double>(i, j);
				}
				series.copyTo(time_series(cv::Range(valide_count, valide_count + 1), cv::Range(0, N + 1)));
				valide_count++;
				//最小二乘法拟合线性形变速率
				Mat temp_A_t, temp_x;
				cv::transpose(temp_A, temp_A_t);
				temp_A = temp_A_t * temp_A;
				temp_b = temp_A_t * temp_b;
				if (cv::solve(temp_A, temp_b, temp_x, cv::DECOMP_LU))
				{
					v.at<double>(i, j) = temp_x.at<double>(0, 0);
				}
			}
		}
	}
	emit updateProcess(95, QString::fromLocal8Bit("结果保存……"));
	time_series = time_series / 4 / PI * wavelength;
	double max_def, min_def;
	Mat Max(1, 1, CV_64F), Min(1, 1, CV_64F);
	cv::minMaxLoc(time_series, &min_def, &max_def);
	Max.at<double>(0, 0) = max_def;
	Min.at<double>(0, 0) = min_def;
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "max_deformation", Max, 0, 0, 1, 1);
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "min_deformation", Min, 0, 0, 1, 1);
	Mat ref_i(1, 1, CV_32S), ref_j(1, 1, CV_32S);
	ref_i.at<int>(0, 0) = ref_row;
	ref_j.at<int>(0, 0) = ref_col;
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "ref_row", ref_i, 0, 0, 1, 1);
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "ref_col", ref_j, 0, 0, 1, 1);
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "deformation_time_series", time_series, 0, 0, time_series.rows, time_series.cols);
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "temporal_coherence", temporal_coh, 0, 0, temporal_coh.rows, temporal_coh.cols);
	v = v / 4 / PI * wavelength;
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "defomation_velocity", v, 0, 0, v.rows, v.cols);
	conversion.write_subarray_to_h5(times_series_h5.c_str(), "residue_topography", z, 0, 0, v.rows, v.cols);

	emit endProcess();
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
	emit updateProcess(2, QString::fromLocal8Bit("正在地理编码……"));
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
		emit updateProcess(20, QString::fromLocal8Bit("正在地理编码……"));
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

			emit updateProcess(process, QString::fromLocal8Bit("正在地理编码……"));
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
		XMLFile xmldoc;
		xmldoc.XMLFile_load(project_xmlfile.toStdString().c_str());
		xmldoc.find_node("DataNode", pnode);
		while (pnode)
		{
			if (0 == strcmp(pnode->Attribute("name"), srcNode.toStdString().c_str())) break;
			pnode = pnode->NextSiblingElement();
		}
		xmldoc._find_node(pnode, "master_image", pchild);
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

		emit updateProcess(20, QString::fromLocal8Bit("正在地理编码……"));
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
			emit updateProcess(process, QString::fromLocal8Bit("正在地理编码……"));
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

	XMLFile xml;
	QString xml_path = save_path + "/" + project_name;
	xml.XMLFile_load(xml_path.toStdString().c_str());
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

			xml.XMLFile_add_geocoding(dstNode.toStdString().c_str(), geocode_name.toStdString().c_str(),
				("/" + dstNode + "/" + geocode_name + ".h5").toStdString().c_str(), geocode_Rank_level.toStdString().c_str());
		}
		else
		{
			geocode->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
		}
	}
	xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	emit updateProcess(100, QString::fromLocal8Bit("完成……"));
	emit endProcess();
}

void MyThread::S1_swath_merge(
	int index1, int index2, int index3, 
	QString project_name,
	QString srcNode1, QString srcNode2, QString srcNode3,
	QString dstNode,
	QStandardItemModel* model)
{
	if (!model) return;

	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) return;
	QString save_path = model->item(project->row(), 1)->text();
	QDir dir(save_path);
	if (!dir.exists(dstNode))
		dir.mkdir(dstNode);

	//确定三个子带相位文件
	QString IW1_h5, IW2_h5, IW3_h5;
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* node = project->child(i, 0);
		if (node->text() == srcNode1)
		{
			if (node->rowCount() >= index1) IW1_h5 = node->child(index1 - 1, 1)->text();
			continue;
		}
		if (node->text() == srcNode2)
		{
			if (node->rowCount() >= index2) IW2_h5 = node->child(index2 - 1, 1)->text();
			continue;
		}
		if (node->text() == srcNode3)
		{
			if (node->rowCount() >= index3) IW3_h5 = node->child(index3 - 1, 1)->text();
			continue;
		}
	}
	emit updateProcess(30, QString::fromLocal8Bit("正在拼接……"));
	int ret;
	FormatConversion conversion; Utils util;
	Mat merged_phase;
	QString merged_h5 = save_path + "/" + dstNode + "/merged_phase.h5";
	ret = util.S1_subswath_merge(IW1_h5.toStdString().c_str(), IW2_h5.toStdString().c_str(), IW3_h5.toStdString().c_str(), 
		merged_h5.toStdString().c_str());
	if (ret < 0)
	{
		emit errorProcess(QString::fromLocal8Bit("输入不符合要求，请重试！"));
		//emit endProcess();
		return;
	}
	emit updateProcess(90, QString::fromLocal8Bit("正在拼接……"));
	/*建立子带拼接根节点*/
	QStandardItem* swath_merge = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dstNode)
		{
			swath_merge = project->child(i, 0);
			break;
		}
	}

	if (!swath_merge)
	{
		swath_merge = new QStandardItem(dstNode);
		swath_merge->setToolTip(project_name);
		swath_merge->setIcon(QIcon(FOLDER_ICON));
		project->appendRow(swath_merge);
		QStandardItem* swath_merge_Rank = new QStandardItem("phase-1.0");
		project->setChild(project->rowCount() - 1, 1, swath_merge_Rank);
	}

	QStandardItem* item_img = NULL;
	for (int j = 0; j < swath_merge->rowCount(); j++)
	{
		if (swath_merge->child(j, 0)->text() == "merged_phase")
		{
			item_img = swath_merge->child(j, 0);
			break;
		}
	}

	if (!item_img)
	{
		QStandardItem* swath_merge_images_name = new QStandardItem("merged_phase");
		swath_merge_images_name->setToolTip("phase");
		QStandardItem* swath_merge_images_path = new QStandardItem(merged_h5);
		swath_merge_images_name->setIcon(QIcon(IMAGEDATA_ICON));
		swath_merge->appendRow(swath_merge_images_name);
		swath_merge->setChild(swath_merge->rowCount() - 1, 1, swath_merge_images_path);

		XMLFile xml;
		QString xml_path = save_path + "/" + project_name;
		xml.XMLFile_load(xml_path.toStdString().c_str());
		QString relative_path = "/" + dstNode + "/merged_phase.h5";
		xml.XMLFile_add_interferometric_phase(dstNode.toStdString().c_str(), "merged_phase",
			relative_path.toStdString().c_str(), "unknown", "phase-1.0", 0, 0, 0, 0, 0, 0, 0, 0, 0);
		xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	}
	else
	{
		swath_merge->setChild(item_img->row(), 1, new QStandardItem(merged_h5));
	}
	emit updateProcess(100, QString::fromLocal8Bit("完成……"));
	emit endProcess();
}

void MyThread::S1_frame_merge(int index1, int index2, QString project_name, QString srcNode1, QString srcNode2, QString dstNode, QStandardItemModel* model)
{
	if (!model) return;

	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) return;
	QString save_path = model->item(project->row(), 1)->text();
	QDir dir(save_path);
	if (!dir.exists(dstNode))
		dir.mkdir(dstNode);

	//确定相邻单视复图像文件
	QString IW1_h5, IW2_h5;
	bool b_dstNode_existed = false;
	QStandardItem* frame_merge = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* node = project->child(i, 0);
		if (node->text() == dstNode && project->child(i, 1)->text() == QString("complex-0.0"))
		{
			b_dstNode_existed = true;
			frame_merge = project->child(i, 0);
		}
		if (node->text() == srcNode1)
		{
			if (node->rowCount() >= index1) IW1_h5 = node->child(index1 - 1, 1)->text();
			continue;
		}
		if (node->text() == srcNode2)
		{
			if (node->rowCount() >= index2) IW2_h5 = node->child(index2 - 1, 1)->text();
			continue;
		}
	}
	QFileInfo fileinfo1(IW1_h5);
	QFileInfo fileinfo2(IW1_h5);
	QString filename = fileinfo1.baseName() + "_" + fileinfo2.baseName();
	emit updateProcess(30, QString::fromLocal8Bit("正在拼接……"));
	int ret;
	FormatConversion conversion; Utils util;
	Mat merged_phase;
	QString merged_h5 = save_path + "/" + dstNode + "/" + filename + ".h5";
	
	ret = util.S1_frame_merge(IW1_h5.toStdString().c_str(), IW2_h5.toStdString().c_str(), merged_h5.toStdString().c_str());
	if (ret < 0)
	{
		emit errorProcess(QString::fromLocal8Bit("输入不符合要求，请重试！"));
		return;
	}
	emit updateProcess(90, QString::fromLocal8Bit("正在拼接……"));
	/*建立子带拼接根节点*/
	
	if (!b_dstNode_existed)
	{
		frame_merge = new QStandardItem(dstNode);
		frame_merge->setToolTip(project_name);
		frame_merge->setIcon(QIcon(FOLDER_ICON));
		project->appendRow(frame_merge);
		QStandardItem* frame_merge_Rank = new QStandardItem("complex-0.0");
		project->setChild(project->rowCount() - 1, 1, frame_merge_Rank);
	}
	
	QStandardItem* item_img = NULL;
	for (int j = 0; j < frame_merge->rowCount(); j++)
	{
		if (frame_merge->child(j, 0)->text() == filename)
		{
			item_img = frame_merge->child(j, 0);
			break;
		}
	}

	if (!item_img)
	{
		QStandardItem* frame_merge_images_name = new QStandardItem(filename);
		frame_merge_images_name->setToolTip("complex");
		QStandardItem* frame_merge_images_path = new QStandardItem(merged_h5);
		frame_merge_images_name->setIcon(QIcon(IMAGEDATA_ICON));
		frame_merge->appendRow(frame_merge_images_name);
		frame_merge->setChild(frame_merge->rowCount() - 1, 1, frame_merge_images_path);

		XMLFile xml;
		QString xml_path = save_path + "/" + project_name;
		xml.XMLFile_load(xml_path.toStdString().c_str());
		QString relative_path = "/" + dstNode + "/" + filename + ".h5";

		xml.XMLFile_add_origin(dstNode.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "sentinel");
		xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	}
	else
	{
		frame_merge->setChild(item_img->row(), 1, new QStandardItem(merged_h5));
	}
	emit updateProcess(100, QString::fromLocal8Bit("完成……"));
	emit endProcess();
}

void MyThread::Baseline_Estimate(int index, QString project_name, QString dst_node, const QStandardItemModel* model)
{
	if (index < 1 ||
		project_name.isEmpty() || dst_node.isEmpty())
	{
		return;
	}
	Utils util;
	vector<cv::String> SAR_images;
	QList<QString> origin;
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* image = NULL;
	if (!project) return;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == dst_node)
		{
			image = project->child(i, 0);
		}
	}
	if (!image) return;
	int image_number = image->rowCount();
	QList<double> spatial_baseline;
	QList<double> temporal_baseline;
	for (int i = 0; i < image->rowCount(); i++)
	{
		SAR_images.push_back(image->child(i, 1)->text().toStdString());			
	}
	emit updateProcess(10, QString::fromLocal8Bit("开始进行基线估计……"));
	FormatConversion FC;
	/*获取主星参数*/
	Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
	Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
	double interp_interval;
	int offset_row, offset_col;
	int Rows, Cols;
	double time_Master = 0;
	string time_master_str;
	FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "state_vec", State_Vec_Master);
	FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lon_coefficient", Lon_Coeff_Master);
	FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "lat_coefficient", Lat_Coeff_Master);
	FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "prf", tmp_double);
	interp_interval = 1 / tmp_double.at<double>(0, 0);
	Mat tmp = Mat::zeros(1, 1, CV_32SC1);
	FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "offset_row", tmp);
	offset_row = tmp.at<int>(0, 0);
	FC.read_array_from_h5(SAR_images.at(index - 1).c_str(), "offset_col", tmp);
	offset_col = tmp.at<int>(0, 0);
	FC.read_str_from_h5(SAR_images.at(index - 1).c_str(), "acquisition_start_time", time_master_str);
	FC.utc2gps(time_master_str.c_str(), &time_Master);
	ComplexMat SLC;
	FC.read_slc_from_h5(SAR_images.at(index - 1).c_str(), SLC);
	Rows = SLC.GetRows();
	Cols = SLC.GetCols();
	/*添加图像到model中并复制h5参数*/
	vector<int> Row_offset;
	vector<int> Col_offset;
	Mat cc = Mat::zeros(2, image_number, CV_64F);
	for (int i = 0; i < image_number; i++)
	{
		
		if (QThread::currentThread()->isInterruptionRequested())
		{
			return;
		}
		/*估计时空基线*/
		if (i == index - 1)  //主图像
		{
			temporal_baseline.push_back(0);
			spatial_baseline.push_back(0);
		}
		else
		{
			Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
			double interp_interval_slave;
			double V_baseline = 0, H_baseline = 0;
			double sigma_V = 0, sigma_H = 0;
			double time_Slave = 0;
			string time_slave_str;
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "state_vec", State_Vec_Slave);
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "lon_coefficient", Lon_Coeff_Slave);
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "lat_coefficient", Lat_Coeff_Slave);
			FC.read_array_from_h5(SAR_images.at(i).c_str(), "prf", tmp_double);
			interp_interval_slave = 1 / tmp_double.at<double>(0, 0);
			FC.read_str_from_h5(SAR_images.at(i).c_str(), "acquisition_start_time", time_slave_str);
			FC.utc2gps(time_slave_str.c_str(), &time_Slave);
			double delta = (time_Slave - time_Master) / 60 / 60 / 24;
			temporal_baseline.push_back(delta);
			util.baseline_estimation(State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
				offset_row, offset_col, Rows, Cols, interp_interval, interp_interval_slave, &V_baseline, &H_baseline, &sigma_V, &sigma_H);
			spatial_baseline.push_back(V_baseline);
			cc.at<double>(0, i) = V_baseline;
			cc.at<double>(1, i) = delta;
		}
		emit updateProcess(20 + (i + 1) * 80 / image_number, QString::fromLocal8Bit("正在计算时空基线……"));
	}
	//util.cvmat2bin("E:\\working_dir\\papers\\multibaseline_polarimetric\\beijing\\baseline_distribution.bin", cc);
	emit sendBL(temporal_baseline, spatial_baseline, index);
	emit endProcess();
}

void MyThread::Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence, int master_index, int win_width, int win_height, int multilook_rg, int multilook_az, QString save_path, QString project_name, QString node_name,QString file_name, QStandardItemModel* model)
{
	FormatConversion FC;
	Deflat flat; Utils util;
	QStandardItem* project = model->findItems(project_name)[0];
	if (!project) return;
	save_path = model->item(project->row(), 1)->text();
	QStandardItem* origin_node = NULL;
	QDir dir(save_path);
	QString absolute_path;
	if (!dir.exists(file_name))
	{
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	//外部DEM文件夹
	QString appPath = QCoreApplication::applicationDirPath();
	QString demPath = appPath + "/dem";
	QDir appDir(appPath);
	if (!appDir.exists("dem")) appDir.mkdir("dem");

	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i,0)->text() == node_name)
		{
			origin_node = project->child(i, 0);
			break;
		}
	}
	if (!origin_node) return;
	QString master_regis_name = origin_node->child(master_index, 0)->text();
	QString master_path = origin_node->child(master_index, 1)->text();
	QFileInfo fileinfo(master_path);
	QString master_name = fileinfo.baseName();
	/*建立根节点*/
	QStandardItem* interferometric_phase = NULL;
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
		//interferometric_phase_Rank->setToolTip(master_name);
		project->setChild(insert, 1, interferometric_phase_Rank);
	}
	
	emit updateProcess(2, QString::fromLocal8Bit("开始处理……"));

	ComplexMat Master;
	int ret = FC.read_slc_from_h5(master_path.toStdString().c_str(), Master);
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
	//地理编码信息
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
	xml.XMLFile_load(xml_path.toStdString().c_str());
	
	int count = origin_node->rowCount();
	int pair = 1;
	for (int i = 0; i < count; i++)
	{
		if (i == master_index)
			continue;
		else
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
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
				int ret = flat.deflat(statevec, statevec2, lon_coef, lat_coef, phase, offset_row, offset_col, 0,
					1 / prf, 1 / prf2, 1, wavelength, phase_deflatted, flat_phase_coefficient);
				phase_deflatted.copyTo(phase);
				if (ret < 0) return;
				FC.write_array_to_h5(h5_path.toStdString().c_str(), "flat_phase_coefficient", flat_phase_coefficient);
			}
			if (istopo_removal)
			{
				ret = flat.topography_simulation(phase_deflatted, statevec, statevec2, lon_coef, lat_coef, inc_coef, prf, prf2,
					sceneHeight, sceneWidth, offset_row, offset_col, nearRangeTime, rangeSpacing, wavelength,
					acquisitionStartTime, acquisitionStopTime, demPath.toStdString().c_str());
				if (ret < 0) {

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
			QStandardItem* item_img = NULL;
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

			if (iscoherence)
			{
				if (QThread::currentThread()->isInterruptionRequested())
				{
					return;
				}
				Mat coherence;
				util.phase_coherence(phase, win_width, win_height, coherence);

				QStandardItem* item_coh = NULL;
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
				ret = FC.write_array_to_h5(h5_path.toStdString().c_str(), "coherence", coherence);
			}
			emit updateProcess(10 + pair * 80 / (count - 1), QString::fromLocal8Bit("生成第1%幅干涉图……").arg(pair));
			pair++;




		}
	}
	xml.XMLFile_save(xml_path.toStdString().c_str());
	emit sendModel(model);
	emit endProcess();
}

void MyThread::Denoise(QList<int> para, double alpha, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
	if (para.size()<5 ||
		save_path == NULL ||
		project_name == NULL ||
		node_name == NULL ||
		file_name ==NULL)
	{
		return;
	}
	QDir dir(save_path);
	QString absolute_path;
	if (!dir.exists(file_name))
	{
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	else
	{
		dir.remove(file_name);
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	int method = para.at(4);
	//int image_number = para.at(5);
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* node = NULL;
	QList<QString> phase_name;
	QList<QString> phase_path;
	QList<QString> filter_name;
	QList<QString> relative_filter_path;
	QList<QString> absolute_filter_path;
	emit updateProcess(10, QString::fromLocal8Bit("准备数据……"));
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == node_name)
		{
			node = project->child(i, 0);
			for (int j = 0; j < node->rowCount(); j++)
			{
				if (node->child(j, 0)->toolTip() == "phase")
				{
					QString change_name;
					QString origin_name = node->child(j, 0)->text();
					phase_name.append(node->child(j, 0)->text());
					phase_path.append(node->child(j, 1)->text());
					//if (!origin_name.endsWith("phase"))
					//	return ;
					//int length = origin_name.length();
					change_name = origin_name.append("_denoised");
					//change_name = change_name + "filtered";
						filter_name.append(change_name);
						relative_filter_path.append("/" + file_name + "/" + change_name + ".h5");
						absolute_filter_path.append(save_path + "/" + file_name + "/" + change_name + ".h5");
				}
			}
			break;
		}
	}
	/*建立根节点*/
	QStandardItem* Denoise = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == file_name)
		{
			Denoise = project->child(i, 0);
			break;
		}
	}

	if (!Denoise)
	{
		Denoise = new QStandardItem(file_name);
		Denoise->setToolTip(project_name);
		int insert = 0;
		for (; insert < project->rowCount(); insert++)
		{
			if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-2.0") == 0)
				continue;
			else
				break;
		}
		Denoise->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, Denoise);
		QStandardItem* Denoise_Rank = new QStandardItem("phase-2.0");
		//Denoise_Rank->setToolTip(master_name);
		project->setChild(insert, 1, Denoise_Rank);
	}
	
	int image_number = phase_name.size();
	Filter filter;
	FormatConversion FC;
	XMLFile xml;
	xml.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
	if (method == 1)
	{
		int pre_win = para.at(0);
		int slop_win = para.at(1);
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10+i*80/image_number, QString::fromLocal8Bit("第%1幅图像滤波中……").arg(i+1));
			Mat phase;
			int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_filter;
			ret = filter.slope_adaptive_filter(phase, phase_filter, slop_win, pre_win);
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_filter_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "phase", phase_filter);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if(0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_denoise(file_name.toStdString().c_str(), filter_name.at(i).toStdString().c_str(),
				relative_filter_path.at(i).toStdString().c_str(), offset_row, offset_col, "Slope", slop_win, pre_win,
				0, 0, 0, "", "", "");

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Denoise->rowCount(); j++)
			{
				if (Denoise->child(j, 0)->text() == filter_name.at(i))
				{
					item_img = Denoise->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(filter_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Denoise->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_filter_path.at(i));
				Denoise->setChild(Denoise->rowCount() - 1, 1, image_path);
			}
			else
			{
				Denoise->setChild(item_img->row(), 1, new QStandardItem(absolute_filter_path.at(i)));
			}
		}
	}
	else if (method == 2)
	{
		int goldstein_win = para.at(2);
		int n_pad = para.at(3);
		
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像滤波中……").arg(i + 1));
			Mat phase;
			int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_filter;
			ret = filter.Goldstein_filter(phase, phase_filter, alpha, goldstein_win, n_pad);
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_filter_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "phase", phase_filter);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_denoise(file_name.toStdString().c_str(), filter_name.at(i).toStdString().c_str(),
				relative_filter_path.at(i).toStdString().c_str(), offset_row, offset_col, "Goldstein", 0, 0,
				goldstein_win, n_pad, alpha, "", "", "");

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Denoise->rowCount(); j++)
			{
				if (Denoise->child(j, 0)->text() == filter_name.at(i))
				{
					item_img = Denoise->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(filter_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Denoise->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_filter_path.at(i));
				Denoise->setChild(Denoise->rowCount() - 1, 1, image_path);
			}
			else
			{
				Denoise->setChild(item_img->row(), 1, new QStandardItem(absolute_filter_path.at(i)));
			}
		}
	}
	else if (method == 3)
	{
		QString dl_path = QCoreApplication::applicationDirPath();
		QString model_path = QCoreApplication::applicationDirPath() + QString("\\other\\net.pt");
		QString tmp_path = QDir::toNativeSeparators(absolute_path);
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像滤波中……").arg(i + 1));
			Mat phase;
			int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_filter;
			ret = filter.filter_dl(dl_path.toStdString().c_str(),tmp_path.toStdString().c_str(),
				model_path.toStdString().c_str(), phase, phase_filter);
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_filter_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "phase", phase_filter);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_filter_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_denoise(file_name.toStdString().c_str(), filter_name.at(i).toStdString().c_str(),
				relative_filter_path.at(i).toStdString().c_str(), offset_row, offset_col, "DL", 0, 0,
				0, 0, 0, dl_path.toStdString().c_str(), model_path.toStdString().c_str(), tmp_path.toStdString().c_str());

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Denoise->rowCount(); j++)
			{
				if (Denoise->child(j, 0)->text() == filter_name.at(i))
				{
					item_img = Denoise->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(filter_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Denoise->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_filter_path.at(i));
				Denoise->setChild(Denoise->rowCount() - 1, 1, image_path);
			}
			else
			{
				Denoise->setChild(item_img->row(), 1, new QStandardItem(absolute_filter_path.at(i)));
			}
		}
	}
	else
	{
		return;
	}
	xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	emit sendModel(model);
	emit endProcess();
}

void MyThread::QUnwrap(int method, double coherence_threshold, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
	if (save_path == NULL ||
		project_name == NULL ||
		node_name == NULL ||
		file_name == NULL)
	{
		return;
	}
	QDir dir(save_path);
	QString absolute_path;
	if (!dir.exists(file_name))
	{
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	else
	{
		dir.remove(file_name);
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* node = NULL;
	QList<QString> phase_name;
	QList<QString> phase_path;
	QList<QString> unwrap_name;
	QList<QString> relative_unwrap_path;
	QList<QString> absolute_unwrap_path;
	emit updateProcess(10, QString::fromLocal8Bit("准备数据……"));
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == node_name)
		{
			node = project->child(i, 0);
			for (int j = 0; j < node->rowCount(); j++)
			{
				if (node->child(j, 0)->toolTip() == "phase")
				{
					QString change_name;
					QString origin_name = node->child(j, 0)->text();
					phase_name.append(node->child(j, 0)->text());
					phase_path.append(node->child(j, 1)->text());
					//if (origin_name.endsWith("phase"))
					//{
					//	int length = origin_name.length();
					//	change_name = origin_name.left(length - sizeof("phase") + 1);
					//	change_name = change_name + "unwrapped";
					//}
					//else if (origin_name.endsWith("filtered"))
					//{
					//	int length = origin_name.length();
					//	change_name = origin_name.left(length - sizeof("filtered") + 1);
					//	change_name = change_name + "unwrapped";
					//}
					//else
					//	return;
					change_name = origin_name.append("_unwrapped");
					unwrap_name.append(change_name);
					relative_unwrap_path.append("/" + file_name + "/" + change_name + ".h5");
					absolute_unwrap_path.append(save_path + "/" + file_name + "/" + change_name + ".h5");
				}
			}
			break;
		}
	}
	/*建立根节点*/
	QStandardItem* Unwrap_node = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == file_name)
		{
			Unwrap_node = project->child(i, 0);
			break;
		}
	}

	if (!Unwrap_node)
	{
		Unwrap_node = new QStandardItem(file_name);
		Unwrap_node->setToolTip(project_name);
		int insert = 0;
		for (; insert < project->rowCount(); insert++)
		{
			if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-3.0") == 0)
				continue;
			else
				break;
		}
		Unwrap_node->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, Unwrap_node);
		QStandardItem* Unwrap_node_Rank = new QStandardItem("phase-3.0");
		//Unwrap_node_Rank->setToolTip(master_name);
		project->setChild(insert, 1, Unwrap_node_Rank);
	}
	
	int image_number = phase_name.size();
	Unwrap unwrap;
	FormatConversion FC;
	Utils util;
	XMLFile xml;
	int ret = 0;
	xml.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
	if (method == 1)
	{
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像解缠中……").arg(i + 1));
			Mat phase;
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_unwrap;
			ret = unwrap.SPD_Guided_Unwrap(phase, phase_unwrap);
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_unwrap_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_unwrap(file_name.toStdString().c_str(), unwrap_name.at(i).toStdString().c_str(),
				relative_unwrap_path.at(i).toStdString().c_str(), offset_row, offset_col, "SPD_Guided", 0);

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Unwrap_node->rowCount(); j++)
			{
				if (Unwrap_node->child(j, 0)->text() == unwrap_name.at(i))
				{
					item_img = Unwrap_node->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(unwrap_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Unwrap_node->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_unwrap_path.at(i));
				Unwrap_node->setChild(Unwrap_node->rowCount() - 1, 1, image_path);
			}
			else
			{
				Unwrap_node->setChild(item_img->row(), 1, new QStandardItem(absolute_unwrap_path.at(i)));
			}
		}
	}
	else if (method == 2)
	{
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像解缠中……").arg(i + 1));
			Mat phase;
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_unwrap;
			Mat coherence, residue;
			ret = util.phase_coherence(phase, coherence);
			ret = util.residue(phase, residue);
			QString app_path = QCoreApplication::applicationDirPath();
			ret = unwrap.MCF(phase, phase_unwrap, coherence, residue, (absolute_path + "/MCF.net").toStdString().c_str(), app_path.toStdString().c_str());
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_unwrap_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_unwrap(file_name.toStdString().c_str(), unwrap_name.at(i).toStdString().c_str(),
				relative_unwrap_path.at(i).toStdString().c_str(), offset_row, offset_col, "MCF", 0);

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Unwrap_node->rowCount(); j++)
			{
				if (Unwrap_node->child(j, 0)->text() == unwrap_name.at(i))
				{
					item_img = Unwrap_node->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(unwrap_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Unwrap_node->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_unwrap_path.at(i));
				Unwrap_node->setChild(Unwrap_node->rowCount() - 1, 1, image_path);
			}
			else
			{
				Unwrap_node->setChild(item_img->row(), 1, new QStandardItem(absolute_unwrap_path.at(i)));
			}
		}
	}
	else if (method == 3)
	{
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像解缠中……").arg(i + 1));
			Mat phase;
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_unwrap;
			QString app_path = QCoreApplication::applicationDirPath();
			ret = unwrap.snaphu(phase_path.at(i).toStdString().c_str(), phase_unwrap, save_path.toStdString().c_str(), absolute_path.toStdString().c_str(), app_path.toStdString().c_str());
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_unwrap_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_unwrap(file_name.toStdString().c_str(), unwrap_name.at(i).toStdString().c_str(),
				relative_unwrap_path.at(i).toStdString().c_str(), offset_row, offset_col, "Snaphu", 0);

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Unwrap_node->rowCount(); j++)
			{
				if (Unwrap_node->child(j, 0)->text() == unwrap_name.at(i))
				{
					item_img = Unwrap_node->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(unwrap_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Unwrap_node->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_unwrap_path.at(i));
				Unwrap_node->setChild(Unwrap_node->rowCount() - 1, 1, image_path);
			}
			else
			{
				Unwrap_node->setChild(item_img->row(), 1, new QStandardItem(absolute_unwrap_path.at(i)));
			}
		}
	}
	else if (method == 4)
	{
		double distance_threshold = 5.0;
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像解缠中……").arg(i + 1));
			Mat phase;
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_unwrap;
			QString app_path = QCoreApplication::applicationDirPath();
			ret = unwrap.QualityGuided_MCF(phase, phase_unwrap, coherence_threshold, distance_threshold, absolute_path.toStdString().c_str(), app_path.toStdString().c_str());
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_unwrap_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "phase", phase_unwrap);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "flat_phase_coefficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lon", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lon", tmp);
			if (0 == FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "mapped_lat", tmp))
				FC.write_array_to_h5(absolute_unwrap_path.at(i).toStdString().c_str(), "mapped_lat", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_unwrap(file_name.toStdString().c_str(), unwrap_name.at(i).toStdString().c_str(),
				relative_unwrap_path.at(i).toStdString().c_str(), offset_row, offset_col, "QualityGuided_MCF", 0);

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Unwrap_node->rowCount(); j++)
			{
				if (Unwrap_node->child(j, 0)->text() == unwrap_name.at(i))
				{
					item_img = Unwrap_node->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(unwrap_name.at(i));
				image->setToolTip("phase");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Unwrap_node->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_unwrap_path.at(i));
				Unwrap_node->setChild(Unwrap_node->rowCount() - 1, 1, image_path);
			}
			else
			{
				Unwrap_node->setChild(item_img->row(), 1, new QStandardItem(absolute_unwrap_path.at(i)));
			}
		}
	}
	else
	{
		
	}
	xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	emit sendModel(model);
	emit endProcess();
}


void MyThread::QDem(int method, int times, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
	if (save_path == NULL ||
		project_name == NULL ||
		node_name == NULL ||
		file_name == NULL)
	{
		return;
	}
	QDir dir(save_path);
	QString absolute_path;
	if (!dir.exists(file_name))
	{
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	else
	{
		dir.remove(file_name);
		dir.mkdir(file_name);
		absolute_path = save_path + "/" + file_name;
	}
	QStandardItem* project = model->findItems(project_name)[0];
	QStandardItem* node = NULL;
	QList<QString> phase_name;
	QList<QString> phase_path;
	QList<QString> dem_name;
	QList<QString> relative_dem_path;
	QList<QString> absolute_dem_path;
	emit updateProcess(10, QString::fromLocal8Bit("准备数据……"));
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == node_name)
		{
			node = project->child(i, 0);
			for (int j = 0; j < node->rowCount(); j++)
			{
				if (node->child(j, 0)->toolTip() == "phase")
				{
					QString change_name;
					QString origin_name = node->child(j, 0)->text();
					phase_name.append(node->child(j, 0)->text());
					phase_path.append(node->child(j, 1)->text());
					//if (origin_name.endsWith("unwrapped"))
					//{
					//	int length = origin_name.length();
					//	change_name = origin_name.left(length - sizeof("unwrapped") + 1);
					//	change_name = change_name + "dem";
					//}
					//else
					//	return;
					change_name = origin_name.append("_dem");
					dem_name.append(change_name);
					relative_dem_path.append("/" + file_name + "/" + change_name + ".h5");
					absolute_dem_path.append(save_path + "/" + file_name + "/" + change_name + ".h5");
				}
			}
			break;
		}
	}
	/*建立根节点*/
	QStandardItem* Dem_node = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		if (project->child(i, 0)->text() == file_name)
		{
			Dem_node = project->child(i, 0);
			break;
		}
	}

	if (!Dem_node)
	{
		Dem_node = new QStandardItem(file_name);
		Dem_node->setToolTip(project_name);
		int insert = 0;
		for (; insert < project->rowCount(); insert++)
		{
			if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-1.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-2.0") == 0 ||
				project->child(insert, 1)->text().compare("complex-3.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-1.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-2.0") == 0 ||
				project->child(insert, 1)->text().compare("phase-3.0") == 0 ||
				project->child(insert, 1)->text().compare("dem-1.0") == 0
				)
				continue;
			else
				break;
		}
		Dem_node->setIcon(QIcon(FOLDER_ICON));
		project->insertRow(insert, Dem_node);
		QStandardItem* Dem_node_Rank = new QStandardItem("dem-1.0");
		//Dem_node_Rank->setToolTip(master_name);
		project->setChild(insert, 1, Dem_node_Rank);
	}
	
	int image_number = phase_name.size();
	Dem dem;
	FormatConversion FC;
	Utils util;
	XMLFile xml;
	xml.XMLFile_load((save_path + "/" + project_name).toStdString().c_str());
	if (method == 1)
	{
		for (int i = 0; i < image_number; i++)
		{
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			emit updateProcess(10 + i * 80 / image_number, QString::fromLocal8Bit("第%1幅图像解析高程中……").arg(i + 1));
			Mat phase;
			int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
			Mat phase_dem;
			ret = dem.dem_newton_iter(phase_path.at(i).toStdString().c_str(), phase_dem, save_path.toStdString().c_str(), times, 1);
			/*写入h5*/
			ret = FC.creat_new_h5(absolute_dem_path.at(i).toStdString().c_str());
			ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "dem", phase_dem);
			string tmp_str;
			Mat tmp;
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_1", tmp_str);
			ret = FC.write_str_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "source_1", tmp_str.c_str());
			QString master_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_str_from_h5(phase_path.at(i).toStdString().c_str(), "source_2", tmp_str);
			ret = FC.write_str_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "source_2", tmp_str.c_str());
			QString slave_path = QDir::toNativeSeparators(save_path) + QString(tmp_str.c_str());
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "flat_phase_coefficientficient", tmp);
			ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "flat_phase_coefficientficient", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "range_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "azimuth_len", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "multilook_rg", tmp);
			FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			FC.write_array_to_h5(absolute_dem_path.at(i).toStdString().c_str(), "multilook_az", tmp);
			if (QThread::currentThread()->isInterruptionRequested())
			{
				return;
			}
			/*行列偏移量*/
			Mat tmp_int = Mat::zeros(1, 1, CV_32SC1);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_row", tmp_int);
			int offset_row = tmp_int.at<int>(0, 0);
			ret = FC.read_array_from_h5(master_path.toStdString().c_str(), "offset_col", tmp_int);
			int offset_col = tmp_int.at<int>(0, 0);
			xml.XMLFile_add_dem(file_name.toStdString().c_str(), dem_name.at(i).toStdString().c_str(),
				relative_dem_path.at(i).toStdString().c_str(), offset_row, offset_col, "Iteration", times);

			/*工程树*/
			QStandardItem* item_img = NULL;
			for (int j = 0; j < Dem_node->rowCount(); j++)
			{
				if (Dem_node->child(j, 0)->text() == dem_name.at(i))
				{
					item_img = Dem_node->child(j, 0);
					break;
				}
			}

			if (!item_img)
			{
				QStandardItem* image = new QStandardItem(dem_name.at(i));
				image->setToolTip("dem");
				image->setIcon(QIcon(IMAGEDATA_ICON));
				Dem_node->appendRow(image);
				QStandardItem* image_path = new QStandardItem(absolute_dem_path.at(i));
				Dem_node->setChild(Dem_node->rowCount() - 1, 1, image_path);
			}
			else
			{
				Dem_node->setChild(item_img->row(), 1, new QStandardItem(absolute_dem_path.at(i)));
			}
		}
	}
	else
	{

	}
	xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str());
	emit sendModel(model);
	emit endProcess();
}



int MyThread::Registration_copy(
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
		emit updateProcess(10 + (70.0 / (double)n_images) * (double(ii) + 0.5), QString::fromLocal8Bit("第%1对图像处理中……").arg(ii + 1));
		int mm, nn;
		mm = images_rows.at<int>(ii, 0) / blocksize;
		nn = images_cols.at<int>(ii, 0) / blocksize;
		if (!b_block)
		{
# pragma omp parallel for schedule(guided)

			for (int j = 0; j < m; j++)
			{
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
						//if (return_check(ret, "interp_paddingzero()", error_head)) return -1;
						ret = regis.interp_paddingzero(slave, slave_interp, interp_times);
						//if (return_check(ret, "interp_paddingzero()", error_head)) return -1;
						ret = regis.real_coherent(master_interp, slave_interp, &move_r, &move_c);
						//if (return_check(ret, "real_coherent()", error_head)) return -1;
						offset_r.at<double>(j, k) = double(move_r) / double(interp_times);
						offset_c.at<double>(j, k) = double(move_c) / double(interp_times);
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
				for (int k = 0; k < n; k++)
				{
					offset_row = j * blocksize; offset_col = k * blocksize;
					if ((j + 1) * blocksize < images_rows.at<int>(ii, 0) && (k + 1) * blocksize < images_cols.at<int>(ii, 0))
					{
						//mm = j + 1; nn = k + 1;//记录实际的子块行列数
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
		//if (slave1.type() != CV_16S) slave1.convertTo(slave1, CV_16S);
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
		if (ret < 0 || QThread::currentThread()->isInterruptionRequested()) return -1;
		emit updateProcess(10 + (70.0 / (double)n_images) * (double(ii) + 1.0), QString::fromLocal8Bit("第%1对图像处理中……").arg(ii + 1));
	}
	return 0;
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

void MyThread::processBM3DEnhancement(
    QString tag,
    QString inputPath,
    QString outputPath,
    QString nodeName,
    QString fileName,
    QString projectPath,
    QString projectName,
    QStandardItemModel* model,
    bool saveToProject,
    XMLFile* projectXml
)
{
    emit updateProcess(0, QStringLiteral("加载图像..."));

    cv::Mat inputGray = cv::imread(inputPath.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty()) {
        emit errorProcess(QStringLiteral("无法读取输入图像"));
        return;
    }

    emit updateProcess(20, QStringLiteral("准备BM3D计算..."));

    const double noiseGain = 1.1;
    cv::Mat imgDouble;
    inputGray.convertTo(imgDouble, CV_64F);
    cv::Mat imgLog;
    cv::log(imgDouble + 1.0, imgLog);

    auto calcMedian = [](const cv::Mat& img) {
        cv::Mat imgCopy = img.clone();
        imgCopy = imgCopy.reshape(0, 1);
        std::sort(imgCopy.begin<double>(), imgCopy.end<double>());
        int n = imgCopy.total();
        if (n % 2 == 0 && n > 0) {
            return (imgCopy.at<double>(n / 2 - 1) + imgCopy.at<double>(n / 2)) / 2.0;
        } else if (n > 0) {
            return imgCopy.at<double>(n / 2);
        }
        return 0.0;
    };

    double minV = 0.0, maxV = 0.0;
    cv::minMaxLoc(imgLog, &minV, &maxV);
    double rangeV = maxV - minV;
    if (rangeV <= 0.0) rangeV = 1.0;
    cv::Mat imgNorm = (imgLog - minV) / rangeV;

    double medianValue = calcMedian(imgLog);
    cv::Mat absDiff;
    cv::absdiff(imgLog, medianValue, absDiff);
    double sigmaEst = calcMedian(absDiff) / 0.6745;
    double sigmaFinal = (sigmaEst * noiseGain) / rangeV;

    QString processMsg = (tag == "SpeckleDenoise") ? QStringLiteral("执行BM3D去噪 (可能耗时较长)...") : QStringLiteral("执行BM3D去杂波 (可能耗时较长)...");
    emit updateProcess(40, processMsg);

    cv::Mat img8U;
    imgNorm.convertTo(img8U, CV_8U, 255.0);
    double sigma8 = sigmaFinal * 255.0;
    cv::Mat den8U = BM3DWrapper::DenoiseGray(img8U, sigma8);

    if (den8U.empty()) {
        emit errorProcess(QStringLiteral("BM3D处理失败"));
        return;
    }

    emit updateProcess(80, QStringLiteral("后处理及保存..."));

    cv::Mat denNorm;
    den8U.convertTo(denNorm, CV_64F, 1.0 / 255.0);
    cv::Mat imgDen = denNorm * rangeV + minV;
    cv::Mat imgOut;
    cv::exp(imgDen, imgOut);
    imgOut = imgOut - 1.0;

    double meanInput = cv::mean(imgDouble)[0];
    double meanOutput = cv::mean(imgOut)[0];
    if (meanOutput != 0.0) {
        imgOut = imgOut * (meanInput / meanOutput);
    }

    cv::min(imgOut, 255.0, imgOut);
    cv::max(imgOut, 0.0, imgOut);

    cv::Mat output8U;
    imgOut.convertTo(output8U, CV_8U);

    if (saveToProject) {
        QString projDirStr = projectPath;
        if (projectPath.endsWith(".insar", Qt::CaseInsensitive)) {
            projDirStr = QFileInfo(projectPath).absolutePath();
        }

        QDir dir(projDirStr);
        if (!dir.exists(nodeName)) {
            dir.mkdir(nodeName);
        }

        QString suffix = (tag == "SpeckleDenoise") ? "_denoised.png" : "_clutter.png";
        QString finalFileName;
        if (fileName.isEmpty()) {
            finalFileName = QFileInfo(inputPath).baseName() + suffix;
        } else {
            if (QFileInfo(fileName).suffix().isEmpty()) {
                finalFileName = fileName + ".png";
            } else {
                finalFileName = fileName;
            }
        }
        QString finalPath = projDirStr + "/" + nodeName + "/" + finalFileName;
        cv::imwrite(finalPath.toStdString(), output8U);

        QStandardItem* projectItem = nullptr;
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->item(i, 0)->text() == projectName) {
                projectItem = model->item(i, 0);
                break;
            }
        }

        if (projectItem) {
            QStandardItem* dataNode = nullptr;
            for (int i = 0; i < projectItem->rowCount(); ++i) {
                if (projectItem->child(i, 0)->text() == nodeName) {
                    dataNode = projectItem->child(i, 0);
                    break;
                }
            }
            if (!dataNode) {
                dataNode = new QStandardItem(nodeName);
                dataNode->setIcon(QIcon(FOLDER_ICON));
                projectItem->appendRow(dataNode);
            }

            QString defaultDisplay = (tag == "SpeckleDenoise") ? QStringLiteral("denoised") : QStringLiteral("clutter_suppressed");
            QString displayName = fileName.isEmpty() ? defaultDisplay : fileName;
            
            QStandardItem* item_img = NULL;
            for (int j = 0; j < dataNode->rowCount(); j++)
            {
                if (dataNode->child(j, 0)->text() == displayName)
                {
                    item_img = dataNode->child(j, 0);
                    break;
                }
            }

            if (!item_img)
            {
                QStandardItem* nameItem = new QStandardItem(displayName);
                nameItem->setIcon(QIcon(IMAGEDATA_ICON));
                nameItem->setToolTip(QStringLiteral("image"));
                QStandardItem* pathItem = new QStandardItem(finalPath);
                dataNode->appendRow({ nameItem, pathItem });

                // Update XML for persistence
                {
                    QString relativePath = "/" + nodeName + "/" + finalFileName;
                    XMLFile localXml;
                    if (!projectPath.isEmpty()) {
                        localXml.XMLFile_load(projectPath.toStdString().c_str());
                    }
                    localXml.XMLFile_add_origin(
                        nodeName.toStdString().c_str(),
                        displayName.toStdString().c_str(),
                        relativePath.toStdString().c_str(),
                        tag.toStdString().c_str()
                    );
                    localXml.XMLFile_save(projectPath.toStdString().c_str());
                }
            }
            else
            {
                dataNode->setChild(item_img->row(), 1, new QStandardItem(finalPath));
            }
        } else {
            // Error handled silently or via other means
        }
    } else {
        cv::imwrite(outputPath.toStdString(), output8U);
    }

    emit updateProcess(100, QStringLiteral("完成"));
    emit sendModel(model);
    emit endProcess();
}

void MyThread::Speckle_Denoise(
    QString inputPath,
    QString outputPath,
    QString nodeName,
    QString fileName,
    QString projectPath,
    QString projectName,
    QStandardItemModel* model,
    bool saveToProject,
    XMLFile* projectXml
)
{
    processBM3DEnhancement("SpeckleDenoise", inputPath, outputPath, nodeName, fileName, projectPath, projectName, model, saveToProject, projectXml);
}

void MyThread::Clutter_Suppression(
    QString inputPath,
    QString outputPath,
    QString nodeName,
    QString fileName,
    QString projectPath,
    QString projectName,
    QStandardItemModel* model,
    bool saveToProject,
    XMLFile* projectXml
)
{
    processBM3DEnhancement("ClutterSuppression", inputPath, outputPath, nodeName, fileName, projectPath, projectName, model, saveToProject, projectXml);
}


void MyThread::Target_Detection(QString imagePath, QString modelPath, float thresholdValue)
{
    lock.lock();
    stop_flag = false;
    lock.unlock();

    emit updateProcess(0, "正在初始化目标检测...");

    float shipProb = 0.0f;
    QString resultText;
    QString errorMsg;

    emit updateProcess(50, "正在运行推理...");
    bool ok = TargetDetection::runDetectionTask(imagePath, modelPath, thresholdValue, shipProb, resultText, errorMsg);

    lock.lock();
    if (stop_flag) {
        emit errorProcess("目标检测已停止");
        lock.unlock();
        return;
    }
    lock.unlock();

    if (!ok) {
        emit errorProcess(errorMsg);
        emit sendTargetDetectionResult(false, 0.0f, "", errorMsg);
        return;
    }

    emit updateProcess(100, "目标检测完成");
    emit sendTargetDetectionResult(true, shipProb, resultText, "");
    emit endProcess();
}



#include <memory>
#include "GenericSARImportTask.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include "icon_source.h"
GenericSARImportTask::GenericSARImportTask(
    QString xml_filename,
    QString project_path,
    QString folder,
    QString filename,
    QString project_name,
    QStandardItemModel* model
) : m_xmlFilename(xml_filename),
    m_projectPath(project_path),
    m_folder(folder),
    m_filename(filename),
    m_projectName(project_name),
    m_model(model)
{
}

GenericSARImportTask::~GenericSARImportTask()
{
}

void GenericSARImportTask::stop()
{
    m_stopFlag = true;
}

void GenericSARImportTask::run()
{
	if (m_xmlFilename.isEmpty() ||
		m_folder.isEmpty() ||
		m_projectPath.isEmpty() ||
		m_filename.isEmpty() ||
		m_projectName.isEmpty() ||
		m_model == NULL
		)
	{
		return;
	}

	int ret=0;
	QDir dir(m_projectPath);
	if (!dir.exists(m_folder))
	{
		ret = dir.mkdir(m_folder);
	}
	emit updateProcess(20, QStringLiteral("正在导入数据，请耐心等待……"));
	QFileInfo fileinfo(m_xmlFilename);
    QString suffix = fileinfo.suffix();

    QString temp_m_folder = QString("/") + m_folder + QString("/");
    QString relative_path = temp_m_folder + m_filename + "." + suffix;
    QString image_path = QString("%1%2%3.%4")
        .arg(m_projectPath)
        .arg(temp_m_folder)
        .arg(m_filename)
        .arg(suffix);

    if (QFile::exists(image_path)) {
        QFile::remove(image_path);
    }

    ret = QFile::copy(m_xmlFilename, image_path) ? 0 : -1;


	if (ret < 0 || m_stopFlag)
	{
		QFile::remove(image_path);
		QDir tmp_dir(m_projectPath + QString("/") + m_folder);
		tmp_dir.removeRecursively();
		return;
	}
	emit updateProcess(90, QStringLiteral("即将完成……"));

	auto items = m_model->findItems(m_projectName);
	if (items.isEmpty() && !m_projectName.endsWith(".insar")) {
		items = m_model->findItems(m_projectName + ".insar");
	}
	
	if (items.isEmpty()) {
		QFile::remove(image_path);
		QDir tmp_dir(m_projectPath + QString("/") + m_folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	QStandardItem* project = items[0];
	if (!project) {
		QFile::remove(image_path);
		QDir tmp_dir(m_projectPath + QString("/") + m_folder);
		tmp_dir.removeRecursively();
		return;
	}
	QModelIndex pro_index = m_model->indexFromItem(project);
	QString pro_path = m_model->data(m_model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
	
	QStandardItem* origin = NULL;
	for (int i = 0; i < project->rowCount(); i++)
	{
		QStandardItem* child = project->child(i);
		QStandardItem* secondCol = project->child(i, 1);
		
		// Match m_folder name, regardless of second column (to be more robust)
		if (child && child->text() == m_folder)
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
		origin = new QStandardItem(m_folder);
		origin->setIcon(QIcon(FOLDER_ICON));
		project->appendRow(origin);
		QStandardItem* Rank = new QStandardItem("complex-0.0");
		project->setChild(project->rowCount() - 1, 1, Rank);
	}

	QStandardItem* img = NULL;
	QStandardItem* img_path = NULL;
	QString trimmedFilename = m_filename.trimmed();
	
	QList<int> rowsToRemove;
	for(int i=0;i<origin->rowCount();i++)
	{
		QString itemText = origin->child(i)->text().trimmed();
		// Match exact m_filename or m_filename with any extension
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
	
	XMLFile* DOC = new XMLFile();
	QString xmlFileLoadPath = QString("%1/%2").arg(pro_path).arg(m_projectName);
	ret = DOC->XMLFile_load(xmlFileLoadPath.toStdString().c_str());
	if (ret < 0 || m_stopFlag)
	{
		QFile::remove(image_path);
		QDir tmp_dir(m_projectPath + QString("/") + m_folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	ret = DOC->XMLFile_add_origin(m_folder.toStdString().c_str(), m_filename.toStdString().c_str(), relative_path.toStdString().c_str(), "Generic_SAR");
	if (ret < 0 || m_stopFlag)
	{
		QFile::remove(image_path);
		QDir tmp_dir(m_projectPath + QString("/") + m_folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	ret = DOC->XMLFile_save(xmlFileLoadPath.toStdString().c_str());
	if (ret < 0 || m_stopFlag)
	{
		QFile::remove(image_path);
		QDir tmp_dir(m_projectPath + QString("/") + m_folder);
		tmp_dir.removeRecursively();
		return;
	}
	
	emit sendModel(m_model);
	emit endProcess();
}



GenericSARBatchImportTask::GenericSARBatchImportTask(
    QString savepath,
    std::vector<QString> original_file_list,
    std::vector<QString> import_namelist,
    QString dst_node,
    QString dst_project,
    QStandardItemModel* model
) : m_savepath(savepath),
    m_originalFileList(original_file_list),
    m_importNamelist(import_namelist),
    m_dstNode(dst_node),
    m_dstProject(dst_project),
    m_model(model)
{
}

GenericSARBatchImportTask::~GenericSARBatchImportTask()
{
}

void GenericSARBatchImportTask::stop()
{
    m_stopFlag = true;
}

void GenericSARBatchImportTask::run()
{








	if (m_savepath.isEmpty() ||
		m_dstNode.isEmpty() ||
		m_dstProject.isEmpty() ||
		m_originalFileList.empty() ||
		m_importNamelist.empty() ||
		m_model == NULL
		)
	{







		emit endProcess();
		return;
	}

	int ret=0;
	QDir dir(m_savepath);

	if (!dir.exists(m_dstNode))
	{
		ret = dir.mkdir(m_dstNode);

	}
	int n_images = m_originalFileList.size();
	int process = 2;
	emit updateProcess(process, QStringLiteral("正在导入..."));

	for (int i = 0; i < n_images; i++)
	{
		if (m_stopFlag) break;
		XMLFile* DOC = new XMLFile();
		QString filename = m_importNamelist[i];
        QString image_filename = m_originalFileList[i];
        QFileInfo fileinfo(image_filename);
        QString suffix = fileinfo.suffix();



        QString temp_folder = QString("/") + m_dstNode + QString("/");
        QString relative_path = temp_folder + filename + "." + suffix;
        QString image_path = QString("%1%2%3.%4")
            .arg(m_savepath, temp_folder, filename, suffix);

		if (QFile::exists(image_path))
		{
			QFile::remove(image_path);
		}
		
		ret = QFile::copy(image_filename, image_path) ? 0 : -1;

		if (ret < 0 || m_stopFlag)
		{

			QFile::remove(image_path);
			QDir tmp_dir(m_savepath + QString("/") + m_dstNode);
			tmp_dir.removeRecursively();
			emit errorProcess(QStringLiteral("文件复制失败"));
			return;
		}

		QList<QStandardItem*> foundItems = m_model->findItems(m_dstProject);
		if (foundItems.isEmpty() && !m_dstProject.endsWith(".insar")) {
			foundItems = m_model->findItems(m_dstProject + ".insar");
		}

		if (foundItems.isEmpty()) {
			QFile::remove(image_path);
			QDir tmp_dir(m_savepath + QString("/") + m_dstNode);
			tmp_dir.removeRecursively();
			emit errorProcess(QStringLiteral("找不到项目节点"));
			return;
		}
		
		QStandardItem* project = foundItems[0];
		QModelIndex pro_index = m_model->indexFromItem(project);
		QString pro_path = m_model->data(m_model->index(pro_index.row(), pro_index.column() + 1, pro_index.parent())).toString();
		QStandardItem* origin = NULL;
		for (int i = 0; i < project->rowCount(); i++)
		{
			QStandardItem* child = project->child(i);
			QStandardItem* secondCol = project->child(i, 1);
			
			if (child && child->text() == m_dstNode)
			{
				origin = child;
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
			origin = new QStandardItem(m_dstNode);
			origin->setIcon(QIcon(FOLDER_ICON));
			project->appendRow(origin);
			QStandardItem* Rank = new QStandardItem("complex-0.0");
			project->setChild(project->rowCount() - 1, 1, Rank);
		}
		QStandardItem* img = NULL;
		QList<int> rowsToRemove;
		for (int j = 0; j < origin->rowCount(); j++)
		{
			QStandardItem* item = origin->child(j);
			if (item && item->text() == filename)
			{
				if (!img) {
					img = item;
				} else {
					rowsToRemove.prepend(j);
				}
			}
		}
		
		foreach(int row, rowsToRemove) {
			origin->removeRow(row);
		}
		if (!img)
		{
			img = new QStandardItem(filename);
			img->setToolTip("complex");
			QStandardItem* img_path = new QStandardItem(image_path);
			img->setIcon(QIcon(IMAGEDATA_ICON));
			origin->appendRow(img);
			origin->setChild(origin->rowCount() - 1, 1, img_path);

			ret = DOC->XMLFile_load(QString("%1/%2").arg(pro_path).arg(m_dstProject).toStdString().c_str());
			if (ret < 0 || m_stopFlag)
			{
				QFile::remove(image_path);
				QDir tmp_dir(m_savepath + QString("/") + m_dstNode);
				tmp_dir.removeRecursively();
				emit errorProcess(QStringLiteral("加载项目XML失败"));
				return;
			}
			ret = DOC->XMLFile_add_origin(m_dstNode.toStdString().c_str(), filename.toStdString().c_str(), relative_path.toStdString().c_str(), "Generic_SAR");
			if (ret < 0 || m_stopFlag)
			{
				QFile::remove(image_path);
				QDir tmp_dir(m_savepath + QString("/") + m_dstNode);
				tmp_dir.removeRecursively();
				emit errorProcess(QStringLiteral("添加origin节点失败"));
				return;
			}
			ret = DOC->XMLFile_save(QString("%1/%2").arg(pro_path).arg(m_dstProject).toStdString().c_str());
			if (ret < 0 || m_stopFlag)
			{
				QFile::remove(image_path);
				QDir tmp_dir(m_savepath + QString("/") + m_dstNode);
				tmp_dir.removeRecursively();
				emit errorProcess(QStringLiteral("保存项目XML失败"));
				return;
			}
		}
		else
		{
			origin->setChild(img->row(), 1, new QStandardItem(image_path));
		}
		process = double(i + 1) / double(n_images) * 100.0;
		emit updateProcess(process, QStringLiteral("正在导入..."));
	}

	emit sendModel(m_model);
	emit endProcess();

}



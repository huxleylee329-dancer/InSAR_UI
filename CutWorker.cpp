#include "CutWorker.h"
#include "icon_source.h"
#include <Utils.h>
#include <FormatConversion.h>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QDebug>
#include "InSARLogManager.h"

CutWorker::CutWorker(QObject* parent)
    : QObject(parent)
{
}

CutWorker::~CutWorker()
{
}

void CutWorker::Cut(QList<double> para,
                    QString save_path,
                    QString project_name,
                    QString src_node,
                    QString dst_node,
                    QStandardItemModel* model)
{
    if (para.size() != 4 ||
        save_path.isEmpty() ||
        project_name.isEmpty() ||
        src_node.isEmpty() ||
        dst_node.isEmpty() ||
        !model)
    {
        emit errorProcess(QStringLiteral("参数错误：缺少输入参数或模型为空！"));
        return;
    }

    InSARLogManager::LogInfo("CutWorker", QString("Starting Coordinate Cut on node '%1' -> '%2'").arg(src_node).arg(dst_node));

    XMLFile doc;
    Utils util;
    FormatConversion FC;
    QDir dir(save_path);
    if (!dir.exists(dst_node))
    {
        dir.mkdir(dst_node);
    }
    QString h5_cut_path = QString("%1/%2").arg(save_path).arg(dst_node);
    QList<QStandardItem*> found = model->findItems(project_name);
    if (found.isEmpty())
    {
        emit errorProcess(QStringLiteral("未找到项目节点：") + project_name);
        return;
    }
    QStandardItem* project = found[0];
    QStandardItem* Images_Cut = nullptr;
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
    QModelIndex origin = model->indexFromItem(project->child(src_node_index, 0));
    int image_number = project->child(src_node_index, 0)->rowCount();

    emit updateProcess(10, QStringLiteral("正在读取图片信息……"));
    QByteArray file_abs_path = QString("%1/%2").arg(save_path).arg(project_name).toLocal8Bit();
    doc.XMLFile_load(file_abs_path.data());

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
        cv::Mat tmp = cv::Mat::zeros(1, 1, CV_32SC1);
        tmp.at<int>(0, 0) = SLC.GetRows();
        FC.write_array_to_h5(cut_h5_path.data(), "azimuth_len", tmp);
        tmp.at<int>(0, 0) = SLC.GetCols();
        FC.write_array_to_h5(cut_h5_path.data(), "range_len", tmp);
        tmp.at<int>(0, 0) = offset_row;
        FC.write_array_to_h5(cut_h5_path.data(), "offset_row", tmp);
        tmp.at<int>(0, 0) = offset_col;
        FC.write_array_to_h5(cut_h5_path.data(), "offset_col", tmp);

        QStandardItem* item_img = nullptr;
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
            doc.XMLFile_add_cut(dir_name.data(), -1, filename.data(),
                file_relative_path.data(),
                offset_row, offset_col, para.at(0), para.at(1),
                para.at(2), para.at(3), "complex-1.0");
        }
        else
        {
            QString full_cut_path = QString("%1/%2.h5").arg(h5_cut_path).arg(Cut_name);
            Images_Cut->setChild(item_img->row(), 1, new QStandardItem(full_cut_path));
        }

        emit updateProcess(10 + i * 90 / (image_number), QStringLiteral("正在裁剪第%1个文件").arg(i+1));
    }
    doc.XMLFile_save(file_abs_path.data());
    emit sendModel(model);
    emit updateProcess(100, QStringLiteral("裁剪完成"));
    InSARLogManager::LogInfo("CutWorker", "Coordinate Cut completed successfully.");
    emit endProcess();
}

void CutWorker::Cut2(double h5_left,
                     double h5_right,
                     double h5_top,
                     double h5_bottom,
                     QString save_path,
                     QString project_name,
                     QString src_node,
                     QString dst_node,
                     QStandardItemModel* model)
{
    if (h5_left < 0 || h5_right < 0 || h5_top < 0 || h5_bottom < 0 ||
        h5_left > 1 || h5_right > 1 || h5_top > 1 || h5_bottom > 1 ||
        save_path.isEmpty() ||
        project_name.isEmpty() ||
        src_node.isEmpty() ||
        dst_node.isEmpty() ||
        !model)
    {
        emit errorProcess(QStringLiteral("参数错误：边界比例值无效或模型为空！"));
        return;
    }

    InSARLogManager::LogInfo("CutWorker", QString("Starting Ratio-based Cut on node '%1' -> '%2'").arg(src_node).arg(dst_node));

    XMLFile doc;
    Utils util;
    FormatConversion FC;
    QDir dir(save_path);
    if (!dir.exists(dst_node))
    {
        dir.mkdir(dst_node);
    }
    QString result_path = QString("%1/%2").arg(save_path).arg(dst_node);

    QByteArray file_abs_path = QString("%1/%2").arg(save_path).arg(project_name).toLocal8Bit();
    doc.XMLFile_load(file_abs_path.data());

    //查找被裁剪节点是否存在主节点
    int master_index = -1;
    TiXmlElement* DataNode = nullptr;
    int ret = doc.find_node_with_attribute("DataNode", "name", src_node.toStdString().c_str(), DataNode);
    if (ret == 0 && DataNode)
    {
        TiXmlElement* pnode = nullptr;
        ret = doc._find_node(DataNode, "master_image", pnode);
        if (ret == 0 && pnode)
        {
            ret = sscanf(pnode->GetText(), "%d", &master_index);
            if (ret != 1) master_index = -1;
        }
    }

    QList<QStandardItem*> found2 = model->findItems(project_name);
    if (found2.isEmpty())
    {
        emit errorProcess(QStringLiteral("未找到项目节点：") + project_name);
        return;
    }
    QStandardItem* project = found2[0];
    QStandardItem* node = nullptr;
    int src_node_index = 0;
    /*找到源节点并计算其节点下图像数量*/
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == src_node)
        {
            node = project->child(i, 0);
            src_node_index = i;
            break;
        }
    }
    
    if (!node)
    {
        emit errorProcess(QStringLiteral("源数据节点不存在！"));
        return;
    }
    
    int image_number = node->rowCount();
    QStandardItem* Images_Cut = nullptr;
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
                if (project->child(insert, 1)->text().compare("complex-0.0") == 0) continue;
                else break;
            }
        }
        else if (src_data_rank == QString("complex-1.0"))
        {
            for (; insert < project->rowCount(); insert++)
            {
                if (project->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                    project->child(insert, 1)->text().compare("complex-1.0") == 0
                    ) continue;
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
                    ) continue;
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
                    ) continue;
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
                    ) continue;
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
                    ) continue;
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
                    ) continue;
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
                    ) continue;
                else break;
            }
        }
        Images_Cut->setIcon(QIcon(FOLDER_ICON));
        project->insertRow(insert, Images_Cut);
        Images_Cut->setToolTip(project_name);
        QStandardItem* Images_Cut_Rank = new QStandardItem(src_data_rank);
        project->setChild(insert, 1, Images_Cut_Rank);
    }

    emit updateProcess(10, QStringLiteral("正在读取图片信息……"));

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
        qDebug() << "[CutWorker] Cut2:" << name << "H5=" << rows << "x" << cols
                 << "ratio: L=" << h5_left << "R=" << h5_right << "T=" << h5_top << "B=" << h5_bottom;
        offset_row = h5_top * rows; offset_row = offset_row < 0 ? 0 : offset_row;
        offset_col = h5_left * cols; offset_col = offset_col < 0 ? 0 : offset_col;
        int row_end = h5_bottom * rows; row_end = row_end >= rows ? rows : row_end;
        int col_end = h5_right * cols; col_end = col_end >= cols ? cols : col_end;
        int rows_cut = row_end - offset_row;
        int cols_cut = col_end - offset_col;
        qDebug() << "[CutWorker] Cut2:" << name << "crop region: row[" << offset_row << "," << row_end << "] col[" << offset_col << "," << col_end << "] size=" << rows_cut << "x" << cols_cut;
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

        QStandardItem* item_img = nullptr;
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
            doc.XMLFile_add_cut(dir_name.data(), master_index, filename.data(),
                file_relative_path.data(),
                offset_row, offset_col, 0, 0, 0, 0, src_data_rank.toStdString().c_str());
        }
        else
        {
            Images_Cut->setChild(item_img->row(), 1, new QStandardItem(QString("%1/%2.h5").arg(result_path).arg(Cut_name)));
        }

        emit updateProcess(10 + i * 90 / (image_number), QStringLiteral("正在裁剪第%1个文件").arg(i + 1));
    }

    doc.XMLFile_save(file_abs_path.data());
    emit sendModel(model);
    emit updateProcess(100, QStringLiteral("裁剪完成"));
    InSARLogManager::LogInfo("CutWorker", "Ratio-based Cut completed successfully.");
    emit endProcess();
}

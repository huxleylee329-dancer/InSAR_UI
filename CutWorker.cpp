#include "CutWorker.h"
#include "icon_source.h"
#include <Utils.h>
#include <FormatConversion.h>
#include <QDir>
#include <QFileInfo>
#include "NodeUtils.h"
#include <QThread>
#include "InSARLogManager.h"

CutWorker::CutWorker(QObject* parent)
    : BaseWorker(parent)
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
                    QStringList inputPaths,
                    QString src_data_rank,
                    bool outputDirectoryIsStaging)
{
    if (para.size() != 4 ||
        save_path.isEmpty() ||
        project_name.isEmpty() ||
        src_node.isEmpty() ||
        dst_node.isEmpty() ||
        inputPaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("参数错误：缺少输入参数或输入路径为空！"));
        return;
    }

    InSARLogManager::LogInfo("CutWorker", QString("Starting Coordinate Cut on node '%1' -> '%2'").arg(src_node).arg(dst_node));

    Utils util;
    FormatConversion FC;
    QString h5_cut_path = QString("%1/%2").arg(save_path).arg(dst_node);
    if (outputDirectoryIsStaging && !QDir(h5_cut_path).exists()) {
        emit errorProcess(QStringLiteral("staging输出目录不存在: ") + h5_cut_path);
        return;
    }
    if (!outputDirectoryIsStaging && !QDir().mkpath(h5_cut_path)) {
        emit errorProcess(QStringLiteral("无法创建输出目录: ") + h5_cut_path);
        return;
    }
    int image_number = inputPaths.size();
    QStringList outputPaths;

    emit updateProcess(10, QStringLiteral("正在读取图片信息……"));

    for (int i = 0; i < image_number; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            emit cancelled();
            return;
        }
        ComplexMat SLC;
        int offset_row = 0;
        int offset_col = 0;
        QString path = inputPaths.at(i);
        QFileInfo fileinfo = QFileInfo(path);
        QString name = fileinfo.baseName();
        QByteArray path_str = path.toLocal8Bit();
        
        {
            NodeUtils::Hdf5Locker locker(path, 5000);
            if (!locker.isLocked())
            {
                emit errorProcess(QStringLiteral("获取源文件锁超时：%1").arg(path));
                return;
            }
            util.get_AOI_from_h5slc(path_str.data(),
                para.at(0),//lon
                para.at(1),//lat
                para.at(2),//width
                para.at(3),//height
                SLC, &offset_row, &offset_col
            );
        }
        if (SLC.re.empty() || SLC.im.empty()) {
            emit errorProcess(QStringLiteral("读取裁剪区域失败：%1").arg(path));
            return;
        }
        
        QString Cut_name = QString("%1_cut").arg(name);
        QString cutH5 = QString("%1/%2.h5").arg(h5_cut_path).arg(Cut_name);
        QByteArray cut_h5_path = cutH5.toLocal8Bit();
        {
            // 按字典序依次获取源文件和目标文件锁，防止死锁并确保双边文件安全
            QString p1 = (path < cutH5) ? path : cutH5;
            QString p2 = (path < cutH5) ? cutH5 : path;
            NodeUtils::Hdf5Locker lock1(p1, 5000);
            if (!lock1.isLocked())
            {
                emit errorProcess(QStringLiteral("获取文件锁超时：%1").arg(p1));
                return;
            }
            NodeUtils::Hdf5Locker lock2(p2, 5000);
            if (!lock2.isLocked())
            {
                emit errorProcess(QStringLiteral("获取文件锁超时：%1").arg(p2));
                return;
            }

            if (FC.creat_new_h5(cut_h5_path.data()) < 0) {
                emit errorProcess(QStringLiteral("创建裁剪输出文件失败：%1").arg(cutH5));
                return;
            }
            if (FC.write_slc_to_h5(cut_h5_path.data(), SLC) < 0) {
                emit errorProcess(QStringLiteral("写入裁剪复数影像失败：%1").arg(cutH5));
                return;
            }
            FC.Copy_para_from_h5_2_h5(path_str.data(), cut_h5_path.data());

            FC.write_str_to_h5(cut_h5_path.data(), "process_state", "cut");
            FC.write_str_to_h5(cut_h5_path.data(), "comment", "complex-1.0");
            cv::Mat tmp = cv::Mat::zeros(1, 1, CV_32SC1);
            tmp.at<int>(0, 0) = SLC.GetRows();
            QString writeError;
            if (!NodeUtils::writeMatToH5(cutH5, "azimuth_len", tmp, &writeError)) {
                emit errorProcess(QStringLiteral("写入 azimuth_len 失败：%1").arg(writeError));
                return;
            }
            tmp.at<int>(0, 0) = SLC.GetCols();
            if (!NodeUtils::writeMatToH5(cutH5, "range_len", tmp, &writeError)) {
                emit errorProcess(QStringLiteral("写入 range_len 失败：%1").arg(writeError));
                return;
            }
            tmp.at<int>(0, 0) = offset_row;
            if (!NodeUtils::writeMatToH5(cutH5, "offset_row", tmp, &writeError)) {
                emit errorProcess(QStringLiteral("写入 offset_row 失败：%1").arg(writeError));
                return;
            }
            tmp.at<int>(0, 0) = offset_col;
            if (!NodeUtils::writeMatToH5(cutH5, "offset_col", tmp, &writeError)) {
                emit errorProcess(QStringLiteral("写入 offset_col 失败：%1").arg(writeError));
                return;
            }
        }

        emit fileCropped(Cut_name, cutH5, offset_row, offset_col, -1, "complex-1.0", para);
        outputPaths.append(cutH5);

        emit updateProcess(10 + i * 90 / (image_number), QStringLiteral("正在裁剪第%1个文件").arg(i+1));
    }
    
    emit updateProcess(100, QStringLiteral("裁剪完成"));
    InSARLogManager::LogInfo("CutWorker", "Coordinate Cut completed successfully.");
    emit outputsGenerated(outputPaths);
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
                     QStringList inputPaths,
                     QString src_data_rank,
                     int master_index,
                     bool outputDirectoryIsStaging)
{
    if (h5_left < 0 || h5_right < 0 || h5_top < 0 || h5_bottom < 0 ||
        h5_left > 1 || h5_right > 1 || h5_top > 1 || h5_bottom > 1 ||
        save_path.isEmpty() ||
        project_name.isEmpty() ||
        src_node.isEmpty() ||
        dst_node.isEmpty() ||
        inputPaths.isEmpty())
    {
        emit errorProcess(QStringLiteral("参数错误：边界比例值无效或输入路径为空！"));
        return;
    }

    InSARLogManager::LogInfo("CutWorker", QString("Starting Ratio-based Cut on node '%1' -> '%2'").arg(src_node).arg(dst_node));

    FormatConversion FC;
    QString result_path = QString("%1/%2").arg(save_path).arg(dst_node);
    if (outputDirectoryIsStaging && !QDir(result_path).exists()) {
        emit errorProcess(QStringLiteral("staging输出目录不存在: ") + result_path);
        return;
    }
    if (!outputDirectoryIsStaging && !QDir().mkpath(result_path)) {
        emit errorProcess(QStringLiteral("无法创建输出目录: ") + result_path);
        return;
    }
    int image_number = inputPaths.size();
    QStringList outputPaths;

    emit updateProcess(10, QStringLiteral("正在读取图片信息……"));

    for (int i = 0; i < image_number; i++)
    {
        if (QThread::currentThread()->isInterruptionRequested() || isStopRequested())
        {
            emit cancelled();
            return;
        }

        ComplexMat SLC;
        int offset_row = 0;
        int offset_col = 0;
        QString path = inputPaths.at(i);
        QFileInfo fileinfo = QFileInfo(path);
        QString name = fileinfo.baseName();
        QByteArray path_str = path.toLocal8Bit();
        int rows = 0, cols = 0;
        {
            NodeUtils::Hdf5Locker locker(path, 5000);
            if (!locker.isLocked())
            {
                emit errorProcess(QStringLiteral("获取源文件锁超时：%1").arg(path));
                return;
            }
            if (!NodeUtils::readScalarFromH5(path, "range_len", cols) ||
                !NodeUtils::readScalarFromH5(path, "azimuth_len", rows) ||
                rows <= 0 || cols <= 0) {
                emit errorProcess(QStringLiteral("读取输入影像尺寸失败：%1").arg(path));
                return;
            }
        }
        offset_row = h5_top * rows; offset_row = offset_row < 0 ? 0 : offset_row;
        offset_col = h5_left * cols; offset_col = offset_col < 0 ? 0 : offset_col;
        int row_end = h5_bottom * rows; row_end = row_end >= rows ? rows : row_end;
        int col_end = h5_right * cols; col_end = col_end >= cols ? cols : col_end;
        int rows_cut = row_end - offset_row;
        int cols_cut = col_end - offset_col;
        
        {
            NodeUtils::Hdf5Locker locker(path, 5000);
            if (!locker.isLocked())
            {
                emit errorProcess(QStringLiteral("获取源文件锁超时：%1").arg(path));
                return;
            }
            FC.read_subarray_from_h5(path.toStdString().c_str(), "s_re", offset_row, offset_col, rows_cut, cols_cut, SLC.re);
            FC.read_subarray_from_h5(path.toStdString().c_str(), "s_im", offset_row, offset_col, rows_cut, cols_cut, SLC.im);
        }
        if (SLC.re.empty() || SLC.im.empty()) {
            emit errorProcess(QStringLiteral("读取裁剪区域失败：%1").arg(path));
            return;
        }

        QString Cut_name = QString("%1_cut2").arg(name);
        QString cutH5 = QString("%1/%2.h5").arg(result_path).arg(Cut_name);
        
        {
            // 按字典序依次获取源文件和目标文件锁，防止死锁并确保双边文件安全
            QString p1 = (path < cutH5) ? path : cutH5;
            QString p2 = (path < cutH5) ? cutH5 : path;
            NodeUtils::Hdf5Locker lock1(p1, 5000);
            if (!lock1.isLocked())
            {
                emit errorProcess(QStringLiteral("获取文件锁超时：%1").arg(p1));
                return;
            }
            NodeUtils::Hdf5Locker lock2(p2, 5000);
            if (!lock2.isLocked())
            {
                emit errorProcess(QStringLiteral("获取文件锁超时：%1").arg(p2));
                return;
            }

            if (FC.creat_new_h5(cutH5.toStdString().c_str()) < 0) {
                emit errorProcess(QStringLiteral("创建裁剪输出文件失败：%1").arg(cutH5));
                return;
            }
            if (FC.write_slc_to_h5(cutH5.toStdString().c_str(), SLC) < 0) {
                emit errorProcess(QStringLiteral("写入裁剪复数影像失败：%1").arg(cutH5));
                return;
            }
            FC.Copy_para_from_h5_2_h5(path.toStdString().c_str(), cutH5.toStdString().c_str());

            FC.write_str_to_h5(cutH5.toStdString().c_str(), "process_state", "cut");
            FC.write_str_to_h5(cutH5.toStdString().c_str(), "comment", src_data_rank.toStdString().c_str());
            QString writeError;
            if (!NodeUtils::writeScalarToH5(cutH5, "range_len", SLC.GetCols(), &writeError) ||
                !NodeUtils::writeScalarToH5(cutH5, "azimuth_len", SLC.GetRows(), &writeError)) {
                emit errorProcess(QStringLiteral("写入裁剪影像尺寸失败：%1").arg(writeError));
                return;
            }

            if (src_data_rank != QString("complex-0.0"))
            {
                int offset_row_old = 0, offset_col_old = 0;
                if (!NodeUtils::readScalarFromH5(path, "offset_row", offset_row_old) ||
                    !NodeUtils::readScalarFromH5(path, "offset_col", offset_col_old)) {
                    emit errorProcess(QStringLiteral("读取输入影像偏移量失败：%1").arg(path));
                    return;
                }
                offset_row += offset_row_old;
                offset_col += offset_col_old;
            }
            if (!NodeUtils::writeScalarToH5(cutH5, "offset_row", offset_row, &writeError) ||
                !NodeUtils::writeScalarToH5(cutH5, "offset_col", offset_col, &writeError)) {
                emit errorProcess(QStringLiteral("写入裁剪影像偏移量失败：%1").arg(writeError));
                return;
            }
        }

        emit fileCropped(Cut_name, cutH5, offset_row, offset_col, master_index, src_data_rank, {});
        outputPaths.append(cutH5);

        emit updateProcess(10 + i * 90 / (image_number), QStringLiteral("正在裁剪第%1个文件").arg(i + 1));
    }

    emit updateProcess(100, QStringLiteral("裁剪完成"));
    InSARLogManager::LogInfo("CutWorker", "Ratio-based Cut completed successfully.");
    emit outputsGenerated(outputPaths);
    emit endProcess();
}

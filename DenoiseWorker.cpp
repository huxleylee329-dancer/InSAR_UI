#include "DenoiseWorker.h"
#include "NodeUtils.h"
#include <Filter.h>
#include <FormatConversion.h>
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QStandardItem>

#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#pragma comment(lib, "ComplexMat_d.lib")
#pragma comment(lib, "Filter_d.lib")
#pragma comment(lib, "Utils_d.lib")
#else
#pragma comment(lib, "FormatConversion.lib")
#pragma comment(lib, "ComplexMat.lib")
#pragma comment(lib, "Filter.lib")
#pragma comment(lib, "Utils.lib")
#endif

using namespace cv;
using namespace std;

thread_local DenoiseWorker* t_currentWorker = nullptr;
thread_local int t_currentImageIndex = 0;
thread_local int t_totalImagesCount = 1;
thread_local int t_lastLoggedProgress = -10;

static bool __stdcall denoiseProgressCallback(int progress, const char* message)
{
    if (t_currentWorker)
    {
        if (t_currentWorker->thread()->isInterruptionRequested())
        {
            return false;
        }

        int start_prog = 10 + t_currentImageIndex * 80 / t_totalImagesCount;
        int end_prog = 10 + (t_currentImageIndex + 1) * 80 / t_totalImagesCount;
        int mapped_prog = start_prog + progress * (end_prog - start_prog) / 100;

        QString msgStr = QString::fromLocal8Bit(message);
        emit t_currentWorker->updateProcess(mapped_prog, QStringLiteral("第%1幅图像滤波中：%2% (%3)")
            .arg(t_currentImageIndex + 1).arg(progress).arg(msgStr));

        if (progress == 0 || progress == 100 || (progress - t_lastLoggedProgress) >= 10)
        {
            InSARLogManager::LogInfo("DenoiseWorker", QString("Denoise progress: %1% (Total: %2%) - %3")
                .arg(progress).arg(mapped_prog).arg(msgStr));
            t_lastLoggedProgress = progress;
        }
    }
    return true;
}

struct ThreadLocalGuard {
    ThreadLocalGuard(DenoiseWorker* worker, int total) {
        t_currentWorker = worker;
        t_currentImageIndex = 0;
        t_totalImagesCount = total;
        t_lastLoggedProgress = -10;
    }
    ~ThreadLocalGuard() {
        t_currentWorker = nullptr;
        t_currentImageIndex = 0;
        t_totalImagesCount = 1;
        t_lastLoggedProgress = -10;
    }
};

DenoiseWorker::DenoiseWorker(QObject* parent)
    : BaseWorker(parent)
{
}

DenoiseWorker::~DenoiseWorker()
{
}

void DenoiseWorker::Denoise(QList<int> para, double alpha, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model)
{
    NodeUtils::Hdf5Locker locker;
    InSARLogManager::LogInfo("DenoiseWorker", QString("Denoise task started. Output folder: %1").arg(file_name));

    if (para.size() < 5 ||
        save_path.isEmpty() ||
        project_name.isEmpty() ||
        node_name.isEmpty() ||
        file_name.isEmpty())
    {
        emit errorProcess(QStringLiteral("无效的参数或输入路径为空"));
        return;
    }

    if (!model) {
        emit errorProcess(QStringLiteral("项目模型为空"));
        return;
    }

    QString absolute_path = save_path + "/" + file_name;
    QDir target_dir(absolute_path);
    if (target_dir.exists())
    {
        target_dir.removeRecursively();
    }
    QDir(save_path).mkdir(file_name);

    int method = para.at(4);
    QList<QStandardItem*> foundProjects = model->findItems(project_name);
    if (foundProjects.isEmpty()) {
        emit errorProcess(QStringLiteral("未找到对应的工程: ") + project_name);
        return;
    }

    QStandardItem* project = foundProjects.first();
    QStandardItem* node = NULL;
    QList<QString> phase_name;
    QList<QString> phase_path;
    QList<QString> filter_name;
    QList<QString> relative_filter_path;
    QList<QString> absolute_filter_path;

    emit updateProcess(10, QStringLiteral("准备数据……"));
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
                    change_name = origin_name + "_denoised";
                    filter_name.append(change_name);
                    relative_filter_path.append("/" + file_name + "/" + change_name + ".h5");
                    absolute_filter_path.append(save_path + "/" + file_name + "/" + change_name + ".h5");
                }
            }
            break;
        }
    }

    if (!node) {
        emit errorProcess(QStringLiteral("未找到输入节点: ") + node_name);
        return;
    }

    if (phase_name.isEmpty()) {
        emit errorProcess(QStringLiteral("输入节点下无可处理的干涉相位数据"));
        return;
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
        project->setChild(insert, 1, Denoise_Rank);
    }

    int image_number = phase_name.size();
    ThreadLocalGuard tlGuard(this, image_number);
    Filter filter;
    FormatConversion FC;
    XMLFile xml;
    QString xml_path = save_path + "/" + project_name;
    if (!xml_path.endsWith(".Insar", Qt::CaseInsensitive)) {
        xml_path += ".Insar";
    }

    if (xml.XMLFile_load(xml_path.toStdString().c_str()) < 0) {
        emit errorProcess(QStringLiteral("加载项目XML文件失败: ") + xml_path);
        return;
    }

    if (method == 1)
    {
        int pre_win = para.at(0);
        int slop_win = para.at(1);
        for (int i = 0; i < image_number; i++)
        {
            t_currentImageIndex = i;
            if (QThread::currentThread()->isInterruptionRequested())
            {
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像滤波中……").arg(i + 1));
            Mat phase;
            int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("读取H5相位数据失败: ") + phase_path.at(i));
                return;
            }
            // 临时转换为双精度以匹配 DLL 的数学处理要求，保证算法精度并避免崩溃
            if (phase.type() != CV_64F) {
                phase.convertTo(phase, CV_64F);
            }
            Mat phase_filter;
            ret = filter.slope_adaptive_filter(phase, phase_filter, slop_win, pre_win, denoiseProgressCallback);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("斜坡自适应滤波处理失败，请检查图像数据或窗口参数"));
                return;
            }
            /*写入h5*/
            ret = FC.creat_new_h5(absolute_filter_path.at(i).toStdString().c_str());
            // 存入磁盘前重新转换回单精度 float，以保持标准存储能效并防止文件臃肿
            if (phase_filter.type() != CV_32F) {
                phase_filter.convertTo(phase_filter, CV_32F);
            }
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
                relative_filter_path.at(i).toStdString().c_str(), offset_row, offset_col, "Slope", slop_win, pre_win,
                0, 0, 0, "", "", "");

            /*工程树*/
            if (Denoise && Denoise->model()) {
                QMetaObject::invokeMethod(Denoise->model(), [=]() {
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
                }, Qt::BlockingQueuedConnection);
            }
        }
    }
    else if (method == 2)
    {
        int goldstein_win = para.at(2);
        int n_pad = para.at(3);

        for (int i = 0; i < image_number; i++)
        {
            t_currentImageIndex = i;
            if (QThread::currentThread()->isInterruptionRequested())
            {
                return;
            }
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像滤波中……").arg(i + 1));
            Mat phase;
            int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("读取H5相位数据失败: ") + phase_path.at(i));
                return;
            }
            // 临时转换为双精度以匹配 DLL 的数学处理要求，保证算法精度并避免崩溃
            if (phase.type() != CV_64F) {
                phase.convertTo(phase, CV_64F);
            }
            Mat phase_filter;
            ret = filter.Goldstein_filter(phase, phase_filter, alpha, goldstein_win, n_pad, denoiseProgressCallback);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("Goldstein滤波处理失败，请检查图像数据或窗口参数"));
                return;
            }
            /*写入h5*/
            ret = FC.creat_new_h5(absolute_filter_path.at(i).toStdString().c_str());
            // 存入磁盘前重新转换回单精度 float，以保持标准存储能效并防止文件臃肿
            if (phase_filter.type() != CV_32F) {
                phase_filter.convertTo(phase_filter, CV_32F);
            }
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
            if (Denoise && Denoise->model()) {
                QMetaObject::invokeMethod(Denoise->model(), [=]() {
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
                }, Qt::BlockingQueuedConnection);
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
            emit updateProcess(10 + i * 80 / image_number, QStringLiteral("第%1幅图像滤波中……").arg(i + 1));
            Mat phase;
            int ret = FC.read_array_from_h5(phase_path.at(i).toStdString().c_str(), "phase", phase);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("读取H5相位数据失败: ") + phase_path.at(i));
                return;
            }
            Mat phase_filter;
            ret = filter.filter_dl(dl_path.toStdString().c_str(), tmp_path.toStdString().c_str(),
                model_path.toStdString().c_str(), phase, phase_filter);
            if (ret < 0) {
                emit errorProcess(QStringLiteral("深度学习滤波处理失败，请检查深度学习依赖或环境"));
                return;
            }
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
            if (Denoise && Denoise->model()) {
                QMetaObject::invokeMethod(Denoise->model(), [=]() {
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
                }, Qt::BlockingQueuedConnection);
            }
        }
    }
    else
    {
        emit errorProcess(QStringLiteral("未知的滤波方法"));
        return;
    }

    xml.XMLFile_save(xml_path.toStdString().c_str());

    emit sendModel(model);
    InSARLogManager::LogInfo("DenoiseWorker", QString("Task completed: ") + QString(__FUNCTION__));
    emit endProcess();
}

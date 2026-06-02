#ifndef MYTHREAD_H
#define MYTHREAD_H

#include <QMetaType>
#include <QObject>
#include <QDebug>
#include <QThread>
#include <QMutex>
#include <QList>
#include <QStandardItemModel>
#include <QDir>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include "FormatConversion.h"

class MyThread : public QObject
{
    Q_OBJECT
    XMLFile* DOC;

public:
    MyThread(QObject* parent = nullptr);
    ~MyThread();

public slots:
    void Import();
    void import_sentinel(QString PODFile, QString manifest_file, QString subswath, QString polarization, QString project_path, QString folder, QString filename, QString project_name, QStandardItemModel* model);
    void import_sentinel_patch(std::vector<QString> original_namelist, std::vector<QString> import_namelist, QString subswath, QString polarization, QString savepath, QString dst_node, QString dst_project, QStandardItemModel* model);
    void import_TSX(QString polarization, QString xml_filename, QString project_path, QString folder, QString filename, QString project_name, QStandardItemModel* model);
    void import_TSX_patch(QString polarization, QString savepath, std::vector<QString> original_file_list, std::vector<QString> import_namelist, QString dst_node, QString dst_project, QStandardItemModel* model);
    void import_CSK_patch(QString savepath, std::vector<QString> original_file_list, std::vector<QString> import_namelist, QString dst_node, QString dst_project, QStandardItemModel* model);
    void import_ALOS2_patch(QString savepath, std::vector<QString> IMG_file_list, std::vector<QString> LED_file_list, std::vector<QString> import_namelist, QString dst_node, QString dst_project, QStandardItemModel* model);
    void ShowImage(QString h5_path, QString bmp_path, QString type);
    void Cut(QList<double> range, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void Cut2(double h5_left, double h5_right, double h5_top, double h5_bottom, QString save_path, QString project_name, QString node_name, QString file, QStandardItemModel* model);
    void Regis(QList<int> para, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void DEMAssistCoregistration(int masterIndex, QString savepath, QString project, QString srcNode, QString dstNode, QStandardItemModel* model);
    void SLC_deramp(int masterIndex, QString project_name, QString src_node, QString dst_node, QStandardItemModel* model);
    void Baseline_Formation(int masterIndex, QString project_name, QString src_node, QStandardItemModel* model);
    void SBAS_time_series(double temporal_thresh_low, double temporal_thresh, double spatial_thresh, int multilook_rg, int multilook_az, int unwrap_method, double alpha, double coherence_thresh, double temporal_coherence_thresh, double refinement_coh_thresh, double refinemen_def_thresh, QString project, QString srcNode, QString dstNode, QString csv_path, QStandardItemModel* model);
    void SBAS_reference_reselection(QString project, QString srcNode, int ref_row, int ref_col, QList<QPoint> GCPs, QStandardItemModel* model);
    void Geocoding(int type, int multi_rg, int multi_az, QString project, QString srcNode, QString dstNode, QStandardItemModel* model);
    void Baseline_Estimate(int index, QString project_name, QString dst_node, const QStandardItemModel* model);
    void Interferometric(bool isdeflat, bool istopo_removal, bool iscoherence, int master_index, int win_width, int win_height, int multilook_rg, int multilook_az, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void Denoise(QList<int> para, double alpha, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void QUnwrap(int method, double coherence_threshold, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void QDem(int method, int times, QString save_path, QString project_name, QString node_name, QString file_name, QStandardItemModel* model);
    void StopProcess();
    bool isStopRequested();

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);
    void sendBL(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
    void askUserError(QString error_msg, bool* skip);

private:
    QMutex lock;
    bool stop_flag;
    int Registration_copy(std::vector<std::string>& SAR_images, std::vector<std::string>& SAR_images_out, cv::Mat& offset_row_out, cv::Mat& offset_col_out, int Master_index, int interp_times, int blocksize);
    int complex_coherence(const ComplexMat& master_image, const ComplexMat& slave_image, int est_wndsize_rg, int est_wndsize_az, cv::Mat& coherence);
    int change_suffix(const char* input, QString output_str, QString old_suffix, QString new_suffix);
};

#endif // MYTHREAD_H

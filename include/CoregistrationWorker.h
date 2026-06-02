#pragma once

#include <QObject>
#include <QList>
#include <QStandardItemModel>
#include <QString>
#include <QThread>
#include <QDir>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include "FormatConversion.h"

class CoregistrationWorker : public QObject
{
    Q_OBJECT
public:
    explicit CoregistrationWorker(QObject* parent = nullptr);
    ~CoregistrationWorker();

    void setDemPath(const QString& path) { m_demPath = path; }
    void setFilePattern(const QString& pattern) { m_filePattern = pattern; }

public slots:
    void Regis(QList<int> para, QString save_path, QString project_name, QString Cut_name, QString file_name, QStandardItemModel* model);
    void DEMAssistCoregistration(int masterIndex, QString savepath, QString project, QString srcNode, QString dstNode, QStandardItemModel* model);

signals:
    void updateProcess(int progress, QString message);
    void endProcess();
    void errorProcess(QString error_msg);
    void sendModel(QStandardItemModel* model);

private:
    int Registration_copy(std::vector<std::string>& SAR_images, std::vector<std::string>& SAR_images_out, cv::Mat& offset_row_out, cv::Mat& offset_col_out, int Master_index, int interp_times, int blocksize);
    QString resolveOutputFileName(const QString& originalName) const;

    QString m_demPath;
    QString m_filePattern;
};

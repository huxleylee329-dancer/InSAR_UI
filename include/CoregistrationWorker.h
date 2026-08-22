#pragma once

#include "BaseWorker.h"
#include <QList>
#include <QStringList>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include "FormatConversion.h"

class CoregistrationWorker : public BaseWorker
{
    Q_OBJECT
public:
    explicit CoregistrationWorker(QObject* parent = nullptr);
    ~CoregistrationWorker();

    void setDemPath(const QString& path) { m_demPath = path; }
    void setDemValidMaskPath(const QString& path) { m_demValidMaskPath = path; }
    double getStageStart() const { return m_stageStart; }
    double getStageWidth() const { return m_stageWidth; }
    void setStage(double start, double width) { m_stageStart = start; m_stageWidth = width; }

public slots:
    void Regis(QList<int> para, QString savePath, QString projectName, QString dstNode, QStringList inputPaths);
    void DEMAssistCoregistration(int masterIndex, QString savePath, QString projectName, QString dstNode, QStringList inputPaths);

signals:
    void cancelled();
    void outputsGenerated(const QStringList& outputNames, const QStringList& outputPaths,
                          const QList<int>& offsetRows, const QList<int>& offsetCols,
                          const QString& temporalBaseline, const QString& effectiveBaseline,
                          const QString& parallelBaseline);

private:
    int Registration_copy(std::vector<std::string>& SAR_images, std::vector<std::string>& SAR_images_out, cv::Mat& offset_row_out, cv::Mat& offset_col_out, int Master_index, int interp_times, int blocksize);
    static void ResampleSlaveInverseWithAffineOffset(const ComplexMat& slave, ComplexMat& out,
        int outputRows, int outputCols, const cv::Mat& coefRows, const cv::Mat& coefCols,
        double offsetX, double offsetY, double scaleX, double scaleY, CoregistrationWorker* worker,
        int progressStart, int progressEnd, int imageIndex, int imageCount);
    QString resolveOutputFileName(const QString& originalName) const;

    QString m_demPath;
    QString m_demValidMaskPath;
    double m_stageStart = 60.0;
    double m_stageWidth = 30.0;
};

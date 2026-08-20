#pragma once

#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_SpeckleDenoise.h"
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QImage>
#include <opencv2/opencv.hpp>
#include <QMouseEvent>
#include <QEvent>
#include <QRect>
#include <QPoint>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>



class SpeckleDenoise : public QWidget
{
    Q_OBJECT

public:
    explicit SpeckleDenoise(QWidget* parent = Q_NULLPTR);
    ~SpeckleDenoise();

public slots:
    void ShowProjectList(QStandardItemModel* model);

signals:
    void sendCopy(QStandardItemModel* model);

private slots:
    void on_loadImageButton_clicked();
    void on_startRoiButton_clicked();
    void on_clearRoiButton_clicked();
    void on_runFilterButton_clicked();
    void on_imageTypeComboBox_currentIndexChanged(int index);

    void on_projectComboBox_currentIndexChanged(int index);
    void on_nodeComboBox_currentIndexChanged(int index);
    void on_inputImageComboBox_currentIndexChanged(int index);

    void on_deleteFilterButton_clicked();
    void on_calculateEnlButton_clicked();


private:
    Ui::SpeckleDenoise* ui;
    QStandardItemModel* copy;
    QString save_path;
    QString project_name;
    QString input_node_name;
    QString input_image_name;
    QString input_image_path;
    QLabel* imageDisplayLabel;
    QPixmap originalPixmap;
    QPixmap filteredPixmap;
    QComboBox* filterMethodComboBox = nullptr;
    QSpinBox* filterRadiusSpinBox = nullptr;
    QDoubleSpinBox* filterLooksSpinBox = nullptr;
    QDoubleSpinBox* frostDerampSpinBox = nullptr;
    QLabel* filterRadiusLabel = nullptr;
    QLabel* filterLooksLabel = nullptr;
    QLabel* frostDerampLabel = nullptr;

    void updateDisplayedImage();

    cv::Mat runSelectedFilter(const cv::Mat& inputGray) const;
    cv::Mat runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const;
    double calcMedian(const cv::Mat& input) const;
    QString currentFilterSuffix() const;
    void updateFilterParameterVisibility();

    bool saveFilteredImage(const cv::Mat& filteredImage, QString& outputPath, QString& outputImageName);
    bool registerFilteredImage(const QString& outputNodeName,
                               const QString& outputImageName,
                               const QString& outputPath);

    cv::Mat originalGrayMat;
    cv::Mat filteredGrayMat;

    bool roiModeEnabled;
    bool roiSelecting;
    QRect currentRoiImageRect;
    QPoint roiStartImagePoint;
    QPoint roiEndImagePoint;


    bool eventFilter(QObject* watched, QEvent* event) override;

    QRect getDisplayedPixmapRect() const;
    QPoint mapLabelPointToImagePoint(const QPoint& labelPoint) const;
    void updateRoiDisplay();
    void updateEnlResults();
    double calculateEnl(const cv::Mat& roiGray) const;

};

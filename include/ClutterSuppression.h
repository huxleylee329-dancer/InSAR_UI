#pragma once

#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_ClutterSuppression.h"
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QImage>
#include <QMouseEvent>
#include <QEvent>
#include <QRect>
#include <QPoint>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTextBrowser>
#include <QFutureWatcher>
#include <opencv2/opencv.hpp>
#include "ClutterSuppressionAlgorithms.h"


class ClutterSuppression : public QWidget
{
    Q_OBJECT

public:
    explicit ClutterSuppression(QWidget* parent = Q_NULLPTR);
    ~ClutterSuppression();

public slots:
    void ShowProjectList(QStandardItemModel* model);

private slots:
    void on_loadImageButton_clicked();
    void on_startRoiButton_clicked();
    void on_clearRoiButton_clicked();
    void on_runFilterButton_clicked();
    void on_imageTypeComboBox_currentIndexChanged(int index);

    void on_projectComboBox_currentIndexChanged(int index);
    void on_InputComboBox_currentIndexChanged(int index);
    void on_inputImageComboBox_currentIndexChanged(int index);

    void on_startRoiButton_2_clicked();
    void on_clearRoiButton_2_clicked();
    void on_deleteFilterButton_clicked();
    void onFilterFinished();

signals:
    void sendCopy(QStandardItemModel* copy);


private:

    Ui::ClutterSuppression* ui;
    QStandardItemModel* copy;

    QString save_path;
    QString project_name;
    QString input_node_name;
    QString input_image_name;
    QString input_image_path;
    QString loaded_image_path;
    QString pending_output_node_name;
    QString pending_output_image_name;
    QString pending_output_path;

    QLabel* imageDisplayLabel;
    QPixmap originalPixmap;
    QPixmap filteredPixmap;
    QPixmap targetMaskPixmap;

    cv::Mat originalGrayMat;
    cv::Mat filteredGrayMat;
    cv::Mat targetMaskMat;

    QComboBox* methodComboBox;
    QSpinBox* guardRadiusSpinBox;
    QSpinBox* clutterRadiusSpinBox;
    QDoubleSpinBox* pfaSpinBox;
    QDoubleSpinBox* censoringSpinBox;
    QSpinBox* mixtureCountSpinBox;
    QTextBrowser* methodDescriptionBrowser;
    QFutureWatcher<ClutterSuppressionResult>* filterWatcher = nullptr;

    bool targetRoiModeEnabled;
    bool clutterRoiModeEnabled;
    bool roiSelecting;
    bool showingClutterRoiInfo;

    QRect targetRoiImageRect;
    QRect clutterRoiImageRect;

    QPoint roiStartImagePoint;
    QPoint roiEndImagePoint;

    void updateDisplayedImage();

    bool eventFilter(QObject* watched, QEvent* event) override;
    QRect getDisplayedPixmapRect() const;
    QPoint mapLabelPointToImagePoint(const QPoint& labelPoint) const;
    void updateCurrentRoiDisplay();

    double calculateScr(const cv::Mat& targetGray, const cv::Mat& clutterGray) const;
    void updateScrResults();

    cv::Mat runClutterSuppressionCoreLogic(const cv::Mat& inputGray);
    ClutterSuppressionParameters currentParameters() const;
    QString currentMethodSuffix() const;
    QString currentOutputImageName() const;
    QString inputFingerprintToken() const;
    void updateMethodControls();
    void updateMethodDescription();
    void setFilterRunning(bool running);
    void clearScrResults();
    bool refreshCurrentResult();
    void resetLoadedImageState();
    cv::Mat runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const;
    double calcMedian(const cv::Mat& input) const;


    bool saveFilteredImage(const cv::Mat& filteredImage, QString& outputPath, QString& outputImageName);
    bool registerFilteredImage(const QString& outputNodeName,
                            const QString& outputImageName,
                            const QString& outputPath);


};

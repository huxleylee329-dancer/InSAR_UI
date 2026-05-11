#pragma once

#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_BatchTargetRecognition.h"
#include <QVector>
#include <QLabel>
#include <QString>

struct BatchTargetItem
{
    QString fileName;
    QString imagePath;
    QString truth;
    QString prediction;
    float confidence = 0.0f;
    bool correct = false;
};


class BatchTargetRecognition : public QWidget
{
    Q_OBJECT

public:
    explicit BatchTargetRecognition(QWidget* parent = Q_NULLPTR);
    ~BatchTargetRecognition();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    

private slots:
    void on_addImageButton_clicked();
    void on_clearImageButton_clicked();
    void on_runDetectionButton_clicked();
    void on_stopDetectionButton_clicked();
    void on_exportResultButton_clicked();
    void on_projectComboBox_currentIndexChanged(int index);
    void on_nodeComboBox_currentIndexChanged(int index);

    void on_resultTableWidget_cellClicked(int row, int column);


private:
    Ui::BatchTargetRecognition* ui;
    QStandardItemModel* copy;
    void populateProjects();
    void populateNodesForCurrentProject();
    void collectImagesFromCurrentNode();

    QString selectedModelPath() const;
    float threshold() const;
    QString truthFromFileName(const QString& fileName) const;

    void updateStatistics();
    void updateCurrentSampleDisplay(const BatchTargetItem& item);
    void showPreviewImage(const QString& imagePath);

    bool runSingleDetection(const QString& imagePath,
                            const QString& modelPath,
                            float thresholdValue,
                            float& shipProb,
                            QString& resultText);

    QVector<BatchTargetItem> batchItems;
    QLabel* previewLabel;
    bool stopRequested;

};

#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_TargetDetection.h"
#include <QString>
#include <QLabel>

class TargetDetection : public QWidget
{
    Q_OBJECT

public:
    explicit TargetDetection(QWidget *parent = Q_NULLPTR);
    ~TargetDetection();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    
private slots:
    void on_projectComboBox_currentIndexChanged(int index);
    void on_loadImageButton_clicked();
    void on_runDetectionButton_clicked();


private:
    void populateDataNodes();
    void populateImagesForCurrentNode();
    QString selectedImagePath() const;
    QString selectedModelPath() const;
    float threshold() const;
    void showPreviewImage(const QString& imagePath);
    bool runSingleDetection(const QString& imagePath,
                            const QString& modelPath,
                            float thresholdValue,
                            float& shipProb,
                            QString& resultText);

    Ui::TargetDetection* ui;
    QStandardItemModel* copy;
    QLabel* previewLabel;

};

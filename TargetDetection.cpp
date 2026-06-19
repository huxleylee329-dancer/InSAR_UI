#include "TargetDetection.h"
#include "SARProcessor.h"

#include <QMessageBox>
#include <QVBoxLayout>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <opencv2/opencv.hpp>


#include "InSARLogManager.h"
TargetDetection::TargetDetection(QWidget* parent)
    : QWidget(parent),
      ui(new Ui::TargetDetection),
      copy(nullptr),
      previewLabel(nullptr)
{
    ui->setupUi(this);

    previewLabel = new QLabel(ui->originalImageWidget);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setScaledContents(true);

    QVBoxLayout* previewLayout = new QVBoxLayout(ui->originalImageWidget);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(previewLabel);

    ui->lineEdit->setText("0.65");

    ui->modelComboBox->clear();
    ui->modelComboBox->addItem(
        "SAR Ship Model 0429",
        QDir::currentPath() + "/sar_ship_model0429.onnx"
    );

    ui->ResultIndexlabel->setText("--");
    ui->confidenceIndexlabel->setText("--");
    ui->label_7->setText("--");
}


TargetDetection::~TargetDetection()
{
    delete ui;
}

void TargetDetection::ShowProjectList(QStandardItemModel* model)
{
    if (model == nullptr || model->rowCount() == 0)
    {
        InSARLogManager::LogWarning("UI", "Please import or open project data first");
        QMessageBox::warning(this, "Warning!", "Please import or open project data first");
        this->deleteLater();
        return;
    }

    copy = model;
    populateDataNodes();
}



void TargetDetection::populateDataNodes()
{
    ui->projectComboBox->clear();

    if (!copy)
        return;

    for (int projectRow = 0; projectRow < copy->rowCount(); ++projectRow)
    {
        QStandardItem* projectItem = copy->item(projectRow, 0);
        if (!projectItem)
            continue;

        for (int nodeRow = 0; nodeRow < projectItem->rowCount(); ++nodeRow)
        {
            QStandardItem* nodeItem = projectItem->child(nodeRow, 0);
            if (!nodeItem)
                continue;

            QString displayName = projectItem->text() + " / " + nodeItem->text();

            ui->projectComboBox->addItem(displayName);
            int index = ui->projectComboBox->count() - 1;
            ui->projectComboBox->setItemData(index, projectRow, Qt::UserRole);
            ui->projectComboBox->setItemData(index, nodeRow, Qt::UserRole + 1);
        }
    }

    populateImagesForCurrentNode();
}

void TargetDetection::populateImagesForCurrentNode()
{
    ui->inputImageComboBox->clear();

    if (!copy || ui->projectComboBox->currentIndex() < 0)
        return;

    int projectRow = ui->projectComboBox->currentData(Qt::UserRole).toInt();
    int nodeRow = ui->projectComboBox->currentData(Qt::UserRole + 1).toInt();

    QStandardItem* projectItem = copy->item(projectRow, 0);
    if (!projectItem)
        return;

    QStandardItem* nodeItem = projectItem->child(nodeRow, 0);
    if (!nodeItem)
        return;

    for (int imageRow = 0; imageRow < nodeItem->rowCount(); ++imageRow)
    {
        QStandardItem* imageItem = nodeItem->child(imageRow, 0);
        QStandardItem* pathItem = nodeItem->child(imageRow, 1);

        if (!imageItem || !pathItem)
            continue;

        ui->inputImageComboBox->addItem(imageItem->text(), pathItem->text());
    }
}

void TargetDetection::on_projectComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    populateImagesForCurrentNode();

    if (previewLabel)
        previewLabel->clear();

    ui->ResultIndexlabel->setText("--");
    ui->confidenceIndexlabel->setText("--");
    ui->label_7->setText("--");
}


void TargetDetection::on_loadImageButton_clicked()
{
    QString imagePath = selectedImagePath();

    if (imagePath.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Please select an input image.");
        QMessageBox::warning(this, "Warning", "Please select an input image.");
        return;
    }

    if (!QFileInfo::exists(imagePath))
    {
        InSARLogManager::LogWarning("UI", "Image file does not exist:\n" + imagePath);
        QMessageBox::warning(this, "Warning", "Image file does not exist:\n" + imagePath);
        return;
    }

    showPreviewImage(imagePath);

    ui->ResultIndexlabel->setText("--");
    ui->confidenceIndexlabel->setText("--");
    ui->label_7->setText(QString::number(threshold(), 'f', 2));
}


QString TargetDetection::selectedImagePath() const
{
    return ui->inputImageComboBox->currentData().toString();
}

QString TargetDetection::selectedModelPath() const
{
    return ui->modelComboBox->currentData().toString();
}

float TargetDetection::threshold() const
{
    bool ok = false;
    float value = ui->lineEdit->text().trimmed().toFloat(&ok);

    if (!ok)
        return 0.65f;

    if (value < 0.0f)
        value = 0.0f;
    if (value > 1.0f)
        value = 1.0f;

    return value;
}

void TargetDetection::showPreviewImage(const QString& imagePath)
{
    if (!previewLabel)
        return;

    QPixmap pixmap(imagePath);
    if (pixmap.isNull())
    {
        previewLabel->setText("Preview failed");
        return;
    }

    previewLabel->setPixmap(
        pixmap.scaled(previewLabel->size(),
                      Qt::KeepAspectRatio,
                      Qt::SmoothTransformation)
    );
}


void TargetDetection::on_runDetectionButton_clicked()
{
    QString imagePath = selectedImagePath();
    QString modelPath = selectedModelPath();
    float thresholdValue = threshold();

    if (imagePath.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Please select an input image.");
        QMessageBox::warning(this, "Warning", "Please select an input image.");
        return;
    }

    if (!QFileInfo::exists(imagePath))
    {
        InSARLogManager::LogWarning("UI", "Image file does not exist:\n" + imagePath);
        QMessageBox::warning(this, "Warning", "Image file does not exist:\n" + imagePath);
        return;
    }

    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath))
    {
        InSARLogManager::LogWarning("UI", "Model file does not exist:\n" + modelPath);
        QMessageBox::warning(this, "Warning", "Model file does not exist:\n" + modelPath);
        return;
    }

    showPreviewImage(imagePath);

    float shipProb = 0.0f;
    QString resultText;
    QString errorMsg;

    bool ok = runDetectionTask(imagePath, modelPath, thresholdValue, shipProb, resultText, errorMsg);
    if (!ok)
    {
        InSARLogManager::LogWarning("UI", errorMsg);
        QMessageBox::warning(this, "Detection Error", errorMsg);
        return;
    }

    ui->ResultIndexlabel->setText(resultText);
    ui->confidenceIndexlabel->setText(QString::number(shipProb * 100.0f, 'f', 2) + "%");
    ui->label_7->setText(QString::number(thresholdValue, 'f', 2));
}


bool TargetDetection::runDetectionTask(const QString& imagePath,
                                         const QString& modelPath,
                                         float thresholdValue,
                                         float& shipProb,
                                         QString& resultText,
                                         QString& errorMsg)
{
    char resultBuf[256] = {0};
    bool ok = SARProcessor::DetectShip(
        imagePath.toLocal8Bit().constData(),
        modelPath.toLocal8Bit().constData(),
        thresholdValue,
        shipProb,
        resultBuf,
        sizeof(resultBuf)
    );
    if (!ok) {
        errorMsg = QString::fromLocal8Bit(resultBuf);
        return false;
    }
    resultText = QString::fromLocal8Bit(resultBuf);
    return true;
}

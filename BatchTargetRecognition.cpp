#include "BatchTargetRecognition.h"
#include <QMessageBox>
#include <QVBoxLayout>
#include <QPixmap>
#include <QFileInfo>
#include <QFileDialog>
#include <QTextStream>
#include <QApplication>
#include <QDir>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include "basic2.h"
#include "diff_boxcount.h"
#include <QTableWidgetItem>
#include <QFile>
#include <QAbstractItemView>
#include <QHeaderView>



BatchTargetRecognition::BatchTargetRecognition(QWidget* parent)
    : QWidget(parent),
      ui(new Ui::BatchTargetRecognition),
      copy(nullptr),
      previewLabel(nullptr),
      stopRequested(false)
{
    ui->setupUi(this);

    previewLabel = new QLabel(ui->imagePreviewWidget);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setScaledContents(true);

    QVBoxLayout* previewLayout = new QVBoxLayout(ui->imagePreviewWidget);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(previewLabel);

    ui->modelComboBox->addItem(
        "SAR Ship Model 0429",
        QDir::currentPath() + "/sar_ship_model0429.onnx"
    );


    ui->ConfidenceLineEdit->setText("0.65");
    ui->progressBar->setValue(0);

    ui->resultTableWidget->setColumnCount(5);
    ui->resultTableWidget->setRowCount(0);

    ui->resultTableWidget->setHorizontalHeaderLabels(
        QStringList() << "File Name" << "Truth" << "Prediction" << "Confidence" << "Correct"
    );
        ui->resultTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->resultTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->resultTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->resultTableWidget->horizontalHeader()->setStretchLastSection(true);
        ui->resultTableWidget->setAlternatingRowColors(true);
}


BatchTargetRecognition::~BatchTargetRecognition()
{
    delete ui;
}

void BatchTargetRecognition::ShowProjectList(QStandardItemModel* model)
{
    if (model == nullptr || model->rowCount() == 0)
    {
        QMessageBox::warning(this, "Warning!", "Please import or open project data first");
        this->deleteLater();
        return;
    }

    copy = model;
    populateProjects();
}

void BatchTargetRecognition::on_addImageButton_clicked()
{
    collectImagesFromCurrentNode();
}


void BatchTargetRecognition::on_clearImageButton_clicked()
{
    batchItems.clear();
    ui->resultTableWidget->setRowCount(0);
    ui->progressBar->setValue(0);

    if (previewLabel)
        previewLabel->clear();

    ui->fileNameValueLabel->setText("--");
    ui->truthValueLabel->setText("--");
    ui->predictionValueLabel->setText("--");
    ui->confidenceValueLabel->setText("--");
    ui->correctnessValueLabel->setText("--");
    ui->resultDisplayValueLabel->setText("--");

    updateStatistics();
}


void BatchTargetRecognition::on_runDetectionButton_clicked()
{
    if (batchItems.isEmpty())
    {
        QMessageBox::warning(this, "Warning", "Please add input data first.");
        return;
    }

    QString modelPath = selectedModelPath();
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath))
    {
        QMessageBox::warning(this, "Warning", "Model file does not exist:\n" + modelPath);
        return;
    }

    stopRequested = false;
    float thresholdValue = threshold();

    for (int i = 0; i < batchItems.size(); ++i)
    {
        if (stopRequested)
            break;

        BatchTargetItem& item = batchItems[i];

        float shipProb = 0.0f;
        QString resultText;

        bool ok = runSingleDetection(item.imagePath, modelPath, thresholdValue, shipProb, resultText);
        if (!ok)
            continue;

        item.prediction = resultText;
        item.confidence = shipProb;
        item.correct = !item.truth.isEmpty() && item.truth == item.prediction;

        ui->resultTableWidget->setItem(i, 0, new QTableWidgetItem(item.fileName));
        ui->resultTableWidget->setItem(i, 1, new QTableWidgetItem(item.truth));
        ui->resultTableWidget->setItem(i, 2, new QTableWidgetItem(item.prediction));
        ui->resultTableWidget->setItem(i, 3, new QTableWidgetItem(QString::number(item.confidence * 100.0f, 'f', 2) + "%"));
        ui->resultTableWidget->setItem(i, 4, new QTableWidgetItem(item.correct ? "Yes" : "No"));

        updateCurrentSampleDisplay(item);
        showPreviewImage(item.imagePath);
        updateStatistics();

        ui->progressBar->setValue(i + 1);
        QApplication::processEvents();
    }
}


void BatchTargetRecognition::on_stopDetectionButton_clicked()
{
    stopRequested = true;
}


void BatchTargetRecognition::on_exportResultButton_clicked()
{
    QString path = QFileDialog::getSaveFileName(this, "Export Result", "", "CSV Files (*.csv)");
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "Warning", "Failed to create result file.");
        return;
    }

    QTextStream out(&file);
    out << "File Name,Image Path,Truth,Prediction,Confidence,Correct\n";

    for (const BatchTargetItem& item : batchItems)
    {
        out << item.fileName << ","
            << item.imagePath << ","
            << item.truth << ","
            << item.prediction << ","
            << QString::number(item.confidence, 'f', 6) << ","
            << (item.correct ? "Yes" : "No") << "\n";
    }
}


void BatchTargetRecognition::on_resultTableWidget_cellClicked(int row, int column)
{
    Q_UNUSED(column);

    if (row < 0 || row >= batchItems.size())
        return;

    const BatchTargetItem& item = batchItems[row];

    if (item.imagePath.isEmpty() || !QFileInfo::exists(item.imagePath))
    {
        QMessageBox::warning(this, "Warning", "Image file does not exist:\n" + item.imagePath);
        return;
    }

    showPreviewImage(item.imagePath);
    updateCurrentSampleDisplay(item);
}



void BatchTargetRecognition::populateProjects()
{
    ui->projectComboBox->clear();

    if (!copy)
        return;

    for (int projectRow = 0; projectRow < copy->rowCount(); ++projectRow)
    {
        QStandardItem* projectItem = copy->item(projectRow, 0);
        if (!projectItem)
            continue;

        ui->projectComboBox->addItem(projectItem->text(), projectRow);
    }

    populateNodesForCurrentProject();
}

void BatchTargetRecognition::collectImagesFromCurrentNode()
{
    batchItems.clear();
    ui->resultTableWidget->setRowCount(0);

    if (!copy || ui->projectComboBox->currentIndex() < 0 || ui->nodeComboBox->currentIndex() < 0)
        return;

    int projectRow = ui->projectComboBox->currentData().toInt();
    int nodeRow = ui->nodeComboBox->currentData().toInt();

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

        BatchTargetItem item;
        item.fileName = imageItem->text();
        item.imagePath = pathItem->text();
        item.truth = truthFromFileName(item.fileName);
        item.prediction = "--";

        if (!item.imagePath.isEmpty())
            batchItems.append(item);
    }

    ui->resultTableWidget->setRowCount(batchItems.size());

    for (int i = 0; i < batchItems.size(); ++i)
    {
        const BatchTargetItem& item = batchItems[i];

        ui->resultTableWidget->setItem(i, 0, new QTableWidgetItem(item.fileName));
        ui->resultTableWidget->setItem(i, 1, new QTableWidgetItem(item.truth));
        ui->resultTableWidget->setItem(i, 2, new QTableWidgetItem("--"));
        ui->resultTableWidget->setItem(i, 3, new QTableWidgetItem("--"));
        ui->resultTableWidget->setItem(i, 4, new QTableWidgetItem("--"));
    }
    ui->progressBar->setRange(0, batchItems.size());
    ui->progressBar->setValue(0);
    updateStatistics();
}


void BatchTargetRecognition::on_projectComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    populateNodesForCurrentProject();
}

void BatchTargetRecognition::populateNodesForCurrentProject()
{
    ui->nodeComboBox->clear();

    if (!copy || ui->projectComboBox->currentIndex() < 0)
        return;

    int projectRow = ui->projectComboBox->currentData().toInt();
    QStandardItem* projectItem = copy->item(projectRow, 0);
    if (!projectItem)
        return;

    for (int nodeRow = 0; nodeRow < projectItem->rowCount(); ++nodeRow)
    {
        QStandardItem* nodeItem = projectItem->child(nodeRow, 0);
        if (!nodeItem)
            continue;

        ui->nodeComboBox->addItem(nodeItem->text(), nodeRow);
    }
}

void BatchTargetRecognition::on_nodeComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    batchItems.clear();
    ui->resultTableWidget->setRowCount(0);
    updateStatistics();
}

QString BatchTargetRecognition::selectedModelPath() const
{
    return ui->modelComboBox->currentData().toString();
}

float BatchTargetRecognition::threshold() const
{
    bool ok = false;
    float value = ui->ConfidenceLineEdit->text().trimmed().toFloat(&ok);

    if (!ok)
        return 0.65f;

    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;

    return value;
}

QString BatchTargetRecognition::truthFromFileName(const QString& fileName) const
{
    QString lower = fileName.toLower();

    if (lower.contains("ship"))
        return "Ship";

    if (lower.contains("sea") || lower.contains("background"))
        return "Sea";

    return "Unknown";
}


void BatchTargetRecognition::updateStatistics()
{
    int finishedCount = 0;
    int correctCount = 0;

    for (const BatchTargetItem& item : batchItems)
    {
        if (item.prediction != "--" && !item.prediction.isEmpty())
            ++finishedCount;

        if (item.correct)
            ++correctCount;
    }

    ui->imageCountValueLabel->setText(QString::number(batchItems.size()));
    ui->AlldataIndexLabel->setText(QString::number(batchItems.size()));
    ui->sampleCountValueLabel->setText(QString::number(batchItems.size()));
    ui->finishedCountValueLabel->setText(QString::number(finishedCount));
    ui->detecteddataIndexLabel->setText(QString::number(finishedCount));
    ui->correctCountValueLabel->setText(QString::number(correctCount));

    double accuracy = finishedCount > 0 ? double(correctCount) / finishedCount * 100.0 : 0.0;
    ui->accuracyValueLabel->setText(QString::number(accuracy, 'f', 2) + "%");
}


void BatchTargetRecognition::updateCurrentSampleDisplay(const BatchTargetItem& item)
{
    ui->fileNameValueLabel->setText(item.fileName);
    ui->truthValueLabel->setText(item.truth.isEmpty() ? "--" : item.truth);
    ui->predictionValueLabel->setText(item.prediction);
    ui->confidenceValueLabel->setText(QString::number(item.confidence * 100.0f, 'f', 2) + "%");
    ui->correctnessValueLabel->setText(item.correct ? "Yes" : "No");
    ui->resultDisplayValueLabel->setText(item.prediction);
}

void BatchTargetRecognition::showPreviewImage(const QString& imagePath)
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
        pixmap.scaled(previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)
    );
}


bool BatchTargetRecognition::runSingleDetection(const QString& imagePath,
                                                const QString& modelPath,
                                                float thresholdValue,
                                                float& shipProb,
                                                QString& resultText)
{
    cv::Mat img = cv::imread(imagePath.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE | cv::IMREAD_ANYDEPTH);
    if (img.empty())
    {
        QMessageBox::warning(this, "Warning", "Failed to read image.");
        return false;
    }

    BasicFeatures feats = extract_basic_features(img);
    double difbox = extract_diffbox_feature(img);

    std::vector<float> inputTensorValues = {
        static_cast<float>(feats.fphr),
        static_cast<float>(difbox),
        static_cast<float>(feats.correlation),
        static_cast<float>(feats.contrast),
        static_cast<float>(feats.asm_val)
    };

    try
    {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "BatchShipDetection");
        Ort::SessionOptions sessionOptions;

        std::wstring modelPathW = QDir::toNativeSeparators(modelPath).toStdWString();
        Ort::Session session(env, modelPathW.c_str(), sessionOptions);

        const char* inputNames[] = { "float_input" };
        const char* outputNames[] = { "label", "probabilities" };

        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<int64_t> inputShape = { 1, 5 };

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            inputTensorValues.data(),
            inputTensorValues.size(),
            inputShape.data(),
            inputShape.size()
        );

        auto outputTensors = session.Run(
            Ort::RunOptions{ nullptr },
            inputNames,
            &inputTensor,
            1,
            outputNames,
            2
        );

        auto type_info = outputTensors[1].GetTensorTypeAndShapeInfo();
        size_t elem_count = type_info.GetElementCount();
        float* probArr = outputTensors[1].GetTensorMutableData<float>();
        
        if (elem_count >= 2) {
            shipProb = probArr[1];
        } else if (elem_count == 1) {
            shipProb = probArr[0];
        } else {
            shipProb = 0.0f;
        }

        resultText = shipProb >= thresholdValue ? "Ship" : "Sea";
        return true;
    }
    catch (const Ort::Exception& e)
    {
        QMessageBox::warning(this, "ONNX Runtime Error", e.what());
        return false;
    }
    catch (const std::exception& e)
    {
        QMessageBox::warning(this, "Detection Error", e.what());
        return false;
    }
}

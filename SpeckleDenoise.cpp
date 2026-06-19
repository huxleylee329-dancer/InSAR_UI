#include "SpeckleDenoise.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QtMath>
#include <algorithm>
#include "FormatConversion.h"
#include "icon_source.h"
#include <vector>
#include "SARProcessor.h"

#include "InSARLogManager.h"
SpeckleDenoise::SpeckleDenoise(QWidget* parent)
    : QWidget(parent),
      ui(new Ui::SpeckleDenoise),
      copy(nullptr)
{
    ui->setupUi(this);
    imageDisplayLabel = new QLabel(ui->imageDisplayWidget);
    imageDisplayLabel->setAlignment(Qt::AlignCenter);
    imageDisplayLabel->setScaledContents(false);
    imageDisplayLabel->installEventFilter(this);



    QVBoxLayout* imageLayout = new QVBoxLayout(ui->imageDisplayWidget);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->addWidget(imageDisplayLabel);
    ui->imageDisplayWidget->setLayout(imageLayout);


    ui->projectComboBox->clear();
    ui->nodeComboBox->clear();
    ui->inputImageComboBox->clear();

    ui->imageTypeComboBox->clear();
    ui->imageTypeComboBox->addItem("Original");
    ui->imageTypeComboBox->addItem("Filtered");

    roiModeEnabled = false;
    roiSelecting = false;
    currentRoiImageRect = QRect();
    roiStartImagePoint = QPoint();
    roiEndImagePoint = QPoint();

    ui->roiTopLeftValueLabel->setText("--");
    ui->label_4->setText("--");
    ui->roiHeightValueLabel->setText("--");
    ui->roiPixelCountValueLabel->setText("--");

    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");


    ui->FilterProgressBar->setMinimum(0);
    ui->FilterProgressBar->setMaximum(100);
    ui->FilterProgressBar->setValue(0);

}

SpeckleDenoise::~SpeckleDenoise()
{
    delete ui;
}

void SpeckleDenoise::ShowProjectList(QStandardItemModel* model)
{
    if (model == nullptr || model->rowCount() == 0)
    {
        InSARLogManager::LogWarning("UI", "Please import or open project data first");
        QMessageBox::warning(this, "Warning!", "Please import or open project data first");
        this->deleteLater();
        return;
    }

    copy = model;

    ui->projectComboBox->clear();
    ui->nodeComboBox->clear();
    ui->inputImageComboBox->clear();

    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* project = model->item(i, 0);
        if (project)
        {
            ui->projectComboBox->addItem(project->text());
        }
    }

    if (ui->projectComboBox->count() > 0)
    {
        ui->projectComboBox->setCurrentIndex(0);
        on_projectComboBox_currentIndexChanged(0);
    }
}

void SpeckleDenoise::on_projectComboBox_currentIndexChanged(int index)
{
    if (!copy || index < 0)
    {
        return;
    }

    ui->nodeComboBox->clear();
    ui->inputImageComboBox->clear();

    QStandardItem* project = copy->item(index, 0);
    QStandardItem* projectPath = copy->item(index, 1);

    if (!project || !projectPath)
    {
        return;
    }

    project_name = project->text();
    save_path = projectPath->text();

    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node)
        {
            ui->nodeComboBox->addItem(node->text());
        }
    }

    if (ui->nodeComboBox->count() > 0)
    {
        ui->nodeComboBox->setCurrentIndex(0);
        on_nodeComboBox_currentIndexChanged(0);
    }
}

void SpeckleDenoise::on_nodeComboBox_currentIndexChanged(int index)
{
    if (!copy || index < 0)
    {
        return;
    }

    ui->inputImageComboBox->clear();

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0)
    {
        return;
    }

    QStandardItem* project = copy->item(projectIndex, 0);
    if (!project)
    {
        return;
    }

    input_node_name = ui->nodeComboBox->currentText();

    QStandardItem* selectedNode = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == input_node_name)
        {
            selectedNode = node;
            break;
        }
    }

    if (!selectedNode)
    {
        return;
    }

    for (int i = 0; i < selectedNode->rowCount(); i++)
    {
        QStandardItem* image = selectedNode->child(i, 0);
        if (image)
        {
            ui->inputImageComboBox->addItem(image->text());
        }
    }

    if (ui->inputImageComboBox->count() > 0)
    {
        ui->inputImageComboBox->setCurrentIndex(0);
        on_inputImageComboBox_currentIndexChanged(0);
    }
}


void SpeckleDenoise::on_inputImageComboBox_currentIndexChanged(int index)
{
    if (!copy || index < 0)
    {
        return;
    }

    input_image_path.clear();

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0)
    {
        return;
    }

    QStandardItem* project = copy->item(projectIndex, 0);
    if (!project)
    {
        return;
    }

    input_node_name = ui->nodeComboBox->currentText();
    input_image_name = ui->inputImageComboBox->currentText();

    QStandardItem* selectedNode = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == input_node_name)
        {
            selectedNode = node;
            break;
        }
    }

    if (!selectedNode)
    {
        return;
    }

    for (int i = 0; i < selectedNode->rowCount(); i++)
    {
        QStandardItem* image = selectedNode->child(i, 0);
        QStandardItem* path = selectedNode->child(i, 1);

        if (image && path && image->text() == input_image_name)
        {
            input_image_path = path->text();
            return;
        }
    }
}


void SpeckleDenoise::on_loadImageButton_clicked()
{
    if (input_image_path.isEmpty() || !QFile::exists(input_image_path))
    {
        InSARLogManager::LogWarning("UI", "Please select a valid input image.");
        QMessageBox::warning(this, "Warning!", "Please select a valid input image.");
        return;
    }

    originalPixmap.load(input_image_path);
    originalGrayMat = cv::imread(input_image_path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (originalGrayMat.empty())
    {
        InSARLogManager::LogWarning("UI", "Failed to load input image as grayscale.");
        QMessageBox::warning(this, "Warning!", "Failed to load input image as grayscale.");
        return;
    }

    currentRoiImageRect = QRect();
    ui->roiTopLeftValueLabel->setText("--");
    ui->label_4->setText("--");
    ui->roiHeightValueLabel->setText("--");
    ui->roiPixelCountValueLabel->setText("--");
    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");

    filteredPixmap = QPixmap();
    filteredGrayMat.release();
    ui->imageTypeComboBox->setCurrentText("Original");


    if (originalPixmap.isNull())
    {
        InSARLogManager::LogWarning("UI", "Failed to load input image.");
        QMessageBox::warning(this, "Warning!", "Failed to load input image.");
        return;
    }

    filteredPixmap = QPixmap();
    filteredGrayMat.release();
    ui->imageTypeComboBox->setCurrentText("Original");
    updateDisplayedImage();
}

void SpeckleDenoise::updateDisplayedImage()
{
    if (!imageDisplayLabel)
    {
        return;
    }

    QPixmap pixmap;

    if (ui->imageTypeComboBox->currentText() == "Original")
    {
        pixmap = originalPixmap;
    }
    else if (ui->imageTypeComboBox->currentText() == "Filtered")
    {
        pixmap = filteredPixmap;
    }

    if (pixmap.isNull())
    {
        imageDisplayLabel->clear();
        return;
    }


    QPixmap displayPixmap = pixmap.copy();

    if (!currentRoiImageRect.isNull() &&
        currentRoiImageRect.width() > 0 &&
        currentRoiImageRect.height() > 0)
    {
        QPainter painter(&displayPixmap);
        QPen pen(Qt::red);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawRect(currentRoiImageRect);
        painter.end();
    }

    imageDisplayLabel->setPixmap(
        displayPixmap.scaled(
            ui->imageDisplayWidget->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        )
    );

}


void SpeckleDenoise::on_startRoiButton_clicked()
{
    if (originalPixmap.isNull())
    {
        InSARLogManager::LogWarning("UI", "Please load an image first.");
        QMessageBox::warning(this, "Warning!", "Please load an image first.");
        return;
    }

    roiModeEnabled = true;
    roiSelecting = false;
}


void SpeckleDenoise::on_clearRoiButton_clicked()
{
    roiModeEnabled = false;
    roiSelecting = false;
    currentRoiImageRect = QRect();
    roiStartImagePoint = QPoint();
    roiEndImagePoint = QPoint();

    ui->roiTopLeftValueLabel->setText("--");
    ui->label_4->setText("--");
    ui->roiHeightValueLabel->setText("--");
    ui->roiPixelCountValueLabel->setText("--");

    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");

    updateDisplayedImage();
}


void SpeckleDenoise::on_runFilterButton_clicked()
{

    ui->FilterProgressBar->show();
    ui->FilterProgressBar->setRange(0, 0);

   
    if (input_image_path.isEmpty() || !QFile::exists(input_image_path))
    {
        InSARLogManager::LogWarning("UI", "Please load an input image first.");
        QMessageBox::warning(this, "Warning!", "Please load an input image first.");
        ui->FilterProgressBar->show();
        ui->FilterProgressBar->setRange(0, 0);
        return;
    }

    QString outputNodeName = ui->NodeWindowSpinBox->text();
    if (outputNodeName.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Please input output node name.");
        QMessageBox::warning(this, "Warning!", "Please input output node name.");
        ui->FilterProgressBar->show();
        ui->FilterProgressBar->setRange(0, 0);
        return;
    }

    cv::Mat inputGray = cv::imread(input_image_path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty())
    {
        InSARLogManager::LogWarning("UI", "Failed to read input image.");
        QMessageBox::warning(this, "Warning!", "Failed to read input image.");
        ui->FilterProgressBar->show();
        ui->FilterProgressBar->setRange(0, 0);
        return;
    }

    cv::Mat filteredImage = runBm3dCoreLogic(inputGray);
    if (filteredImage.empty())
    {
        InSARLogManager::LogWarning("UI", "Speckle denoise failed.");
        QMessageBox::warning(this, "Warning!", "Speckle denoise failed.");
        ui->FilterProgressBar->show();
        ui->FilterProgressBar->setRange(0, 0);
        return;
    }

    filteredGrayMat = filteredImage.clone();

    QFileInfo inputInfo(input_image_name);
    QString expectedImageName = inputInfo.completeBaseName() + "_BM3D";
    QString expectedPath = save_path + "/" + outputNodeName + "/" + expectedImageName + ".jpg";

    if (QFile::exists(expectedPath))
    {
        QMessageBox::information(
            this,
            "Info",
        QStringLiteral("该结果文件已存在：\n%1").arg(expectedPath)

        );
        return;
    }

    QString outputPath;
    QString outputImageName;
    if (!saveFilteredImage(filteredImage, outputPath, outputImageName))
    {
        InSARLogManager::LogWarning("UI", "Failed to save filtered image.");
        QMessageBox::warning(this, "Warning!", "Failed to save filtered image.");
        return;
    }

    if (!registerFilteredImage(outputNodeName, outputImageName, outputPath))
    {
        InSARLogManager::LogWarning("UI", "Failed to register filtered image.");
        QMessageBox::warning(this, "Warning!", "Failed to register filtered image.");
        return;
    }

    filteredPixmap.load(outputPath);
    ui->imageTypeComboBox->setCurrentText("Filtered");
    updateDisplayedImage();

    ui->FilterProgressBar->setRange(0, 100);
    ui->FilterProgressBar->setValue(100);

    emit sendCopy(copy);
}


void SpeckleDenoise::on_imageTypeComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    updateDisplayedImage();
}

cv::Mat SpeckleDenoise::runBm3dCoreLogic(const cv::Mat& inputGray) const
{
    if (inputGray.empty()) return cv::Mat();
    return SARProcessor::DenoiseGray(inputGray, 0.0);
}

cv::Mat SpeckleDenoise::runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const
{
    // 已由 runBm3dCoreLogic 统一调用 SARProcessor，此方法保留接口兼容
    Q_UNUSED(imgNorm);
    Q_UNUSED(sigmaFinal);
    return cv::Mat();
}



bool SpeckleDenoise::saveFilteredImage(const cv::Mat& filteredImage,
                                       QString& outputPath,
                                       QString& outputImageName)
{
    QString outputNodeName = ui->NodeWindowSpinBox->text();
    if (save_path.isEmpty() || outputNodeName.isEmpty() || input_image_name.isEmpty())
    {
        return false;
    }

    QDir projectDir(save_path);
    if (!projectDir.exists(outputNodeName))
    {
        if (!projectDir.mkdir(outputNodeName))
        {
            return false;
        }
    }

    QFileInfo inputInfo(input_image_name);
    outputImageName = inputInfo.completeBaseName() + "_BM3D";
    outputPath = save_path + "/" + outputNodeName + "/" + outputImageName + ".jpg";

    if (QFile::exists(outputPath))
    {
        return false;
    }

    return cv::imwrite(outputPath.toStdString(), filteredImage);
}

double SpeckleDenoise::calcMedian(const cv::Mat& input) const
{
    if (input.empty())
    {
        return 0.0;
    }

    std::vector<double> values;
    values.reserve(input.rows * input.cols);

    for (int r = 0; r < input.rows; r++)
    {
        const double* row = input.ptr<double>(r);
        for (int c = 0; c < input.cols; c++)
        {
            values.push_back(row[c]);
        }
    }

    if (values.empty())
    {
        return 0.0;
    }

    std::sort(values.begin(), values.end());

    int n = static_cast<int>(values.size());
    if (n % 2 == 1)
    {
        return values[n / 2];
    }

    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

bool SpeckleDenoise::registerFilteredImage(const QString& outputNodeName,
                                           const QString& outputImageName,
                                           const QString& outputPath)
{
    if (!copy || outputNodeName.isEmpty() || outputImageName.isEmpty() || outputPath.isEmpty())
    {
        return false;
    }

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0)
    {
        return false;
    }

    QStandardItem* project = copy->item(projectIndex, 0);
    if (!project)
    {
        return false;
    }

    QStandardItem* outputNode = nullptr;

    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == outputNodeName)
        {
            outputNode = node;
            break;
        }
    }

    if (!outputNode)
    {
        outputNode = new QStandardItem(outputNodeName);
        outputNode->setIcon(QIcon(FOLDER_ICON));
        project->appendRow(outputNode);

        QStandardItem* rank = new QStandardItem("complex-0.0");
        project->setChild(project->rowCount() - 1, 1, rank);
    }

    for (int i = 0; i < outputNode->rowCount(); i++)
    {
        QStandardItem* image = outputNode->child(i, 0);
        if (image && image->text() == outputImageName)
        {
            return false;
        }
    }


    QString relativePath = "/" + outputNodeName + "/" + outputImageName + ".jpg";

    XMLFile xml;
    if (xml.XMLFile_load((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        return false;
    }

    if (xml.XMLFile_add_origin(outputNodeName.toStdString().c_str(),
                               outputImageName.toStdString().c_str(),
                               relativePath.toStdString().c_str(),
                               "SpeckleDenoise") < 0)
    {
        return false;
    }

    if (xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        return false;
    }

    QStandardItem* imageItem = new QStandardItem(outputImageName);
    imageItem->setToolTip("complex");
    imageItem->setIcon(QIcon(IMAGEDATA_ICON));

    QStandardItem* pathItem = new QStandardItem(outputPath);

    outputNode->appendRow(imageItem);
    outputNode->setChild(outputNode->rowCount() - 1, 1, pathItem);


    return true;
}

void SpeckleDenoise::on_deleteFilterButton_clicked()
{
    if (!copy)
    {
        InSARLogManager::LogWarning("UI", "Project model is invalid.");
        QMessageBox::warning(this, "Warning!", "Project model is invalid.");
        return;
    }

    QString outputNodeName = ui->NodeWindowSpinBox->text();
    if (outputNodeName.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Please input output node name.");
        QMessageBox::warning(this, "Warning!", "Please input output node name.");
        return;
    }

    if (input_image_name.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Please select an input image first.");
        QMessageBox::warning(this, "Warning!", "Please select an input image first.");
        return;
    }

    QFileInfo inputInfo(input_image_name);
    QString outputImageName = inputInfo.completeBaseName() + "_BM3D";
    QString outputPath = save_path + "/" + outputNodeName + "/" + outputImageName + ".jpg";

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0)
    {
        InSARLogManager::LogWarning("UI", "Invalid project selection.");
        QMessageBox::warning(this, "Warning!", "Invalid project selection.");
        return;
    }

    QStandardItem* project = copy->item(projectIndex, 0);
    if (!project)
    {
        InSARLogManager::LogWarning("UI", "Project item not found.");
        QMessageBox::warning(this, "Warning!", "Project item not found.");
        return;
    }

    QStandardItem* outputNode = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == outputNodeName)
        {
            outputNode = node;
            break;
        }
    }

    if (!outputNode)
    {
        QMessageBox::information(this, "Info", "No filtered image node found.");
        return;
    }

    int imageRow = -1;
    for (int i = 0; i < outputNode->rowCount(); i++)
    {
        QStandardItem* image = outputNode->child(i, 0);
        if (image && image->text() == outputImageName)
        {
            imageRow = i;
            break;
        }
    }

    if (imageRow < 0 && !QFile::exists(outputPath))
    {
        QMessageBox::information(this, "Info", "No filtered image to delete.");
        return;
    }

    XMLFile xml;
    if (xml.XMLFile_load((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        InSARLogManager::LogWarning("UI", "Failed to load project XML.");
        QMessageBox::warning(this, "Warning!", "Failed to load project XML.");
        return;
    }

    if (xml.XMLFile_remove_node(outputNodeName.toStdString().c_str(),
                                outputImageName.toStdString().c_str(),
                                outputPath.toStdString().c_str()) < 0)
    {
        InSARLogManager::LogWarning("UI", "Failed to remove node from project XML.");
        QMessageBox::warning(this, "Warning!", "Failed to remove node from project XML.");
        return;
    }

    if (xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        InSARLogManager::LogWarning("UI", "Failed to save project XML.");
        QMessageBox::warning(this, "Warning!", "Failed to save project XML.");
        return;
    }

    if (QFile::exists(outputPath))
    {
        if (!QFile::remove(outputPath))
        {
            InSARLogManager::LogWarning("UI", "Failed to delete filtered image file.");
            QMessageBox::warning(this, "Warning!", "Failed to delete filtered image file.");
            return;
        }
    }

    if (imageRow >= 0)
    {
        outputNode->removeRow(imageRow);
    }

    filteredPixmap = QPixmap();
    ui->imageTypeComboBox->setCurrentText("Original");
    updateDisplayedImage();

    emit sendCopy(copy);

    QMessageBox::information(this, "Info", "Filtered image deleted.");
}


double SpeckleDenoise::calculateEnl(const cv::Mat& roiGray) const
{
    if (roiGray.empty())
    {
        return 0.0;
    }

    cv::Mat roiDouble;
    roiGray.convertTo(roiDouble, CV_64F);

    cv::Scalar meanValue, stdValue;
    cv::meanStdDev(roiDouble, meanValue, stdValue);

    double mean = meanValue[0];
    double variance = stdValue[0] * stdValue[0];

    if (variance <= 1e-12)
    {
        return 0.0;
    }

    return (mean * mean) / variance;
}

void SpeckleDenoise::updateEnlResults()
{
    if (currentRoiImageRect.isNull() ||
        currentRoiImageRect.width() < 2 ||
        currentRoiImageRect.height() < 2 ||
        originalGrayMat.empty())
    {
        ui->originalEnlValueLabel->setText("--");
        ui->filteredEnlValueLabel->setText("--");
        return;
    }

    cv::Rect roi(
        currentRoiImageRect.x(),
        currentRoiImageRect.y(),
        currentRoiImageRect.width(),
        currentRoiImageRect.height()
    );

    roi &= cv::Rect(0, 0, originalGrayMat.cols, originalGrayMat.rows);
    if (roi.width < 2 || roi.height < 2)
    {
        ui->originalEnlValueLabel->setText("--");
        ui->filteredEnlValueLabel->setText("--");
        return;
    }

    cv::Mat originalRoi = originalGrayMat(roi).clone();
    double originalEnl = calculateEnl(originalRoi);
    ui->originalEnlValueLabel->setText(QString::number(originalEnl, 'f', 4));

    if (!filteredGrayMat.empty())
    {
        cv::Rect filteredBounds(0, 0, filteredGrayMat.cols, filteredGrayMat.rows);
        cv::Rect filteredRoiRect = roi & filteredBounds;

        if (filteredRoiRect.width >= 2 && filteredRoiRect.height >= 2)
        {
            cv::Mat filteredRoi = filteredGrayMat(filteredRoiRect).clone();
            double filteredEnl = calculateEnl(filteredRoi);
            ui->filteredEnlValueLabel->setText(QString::number(filteredEnl, 'f', 4));
        }
        else
        {
            ui->filteredEnlValueLabel->setText("--");
        }
    }
    else
    {
        ui->filteredEnlValueLabel->setText("--");
    }
}

void SpeckleDenoise::on_calculateEnlButton_clicked()
{
    if (currentRoiImageRect.isNull() ||
        currentRoiImageRect.width() < 2 ||
        currentRoiImageRect.height() < 2)
    {
        InSARLogManager::LogWarning("UI", "Please select a valid ROI first.");
        QMessageBox::warning(this, "Warning!", "Please select a valid ROI first.");
        return;
    }

    if (originalGrayMat.empty())
    {
        InSARLogManager::LogWarning("UI", "Original image is not loaded.");
        QMessageBox::warning(this, "Warning!", "Original image is not loaded.");
        return;
    }

    updateEnlResults();
}

void SpeckleDenoise::updateRoiDisplay()
{
    if (currentRoiImageRect.isNull() ||
        currentRoiImageRect.width() < 1 ||
        currentRoiImageRect.height() < 1)
    {
        ui->roiTopLeftValueLabel->setText("--");
        ui->label_4->setText("--");
        ui->roiHeightValueLabel->setText("--");
        ui->roiPixelCountValueLabel->setText("--");
        return;
    }

    ui->roiTopLeftValueLabel->setText(
        QString("(%1, %2)")
            .arg(currentRoiImageRect.x())
            .arg(currentRoiImageRect.y())
    );
    ui->label_4->setText(QString::number(currentRoiImageRect.width()));
    ui->roiHeightValueLabel->setText(QString::number(currentRoiImageRect.height()));
    ui->roiPixelCountValueLabel->setText(
        QString::number(currentRoiImageRect.width() * currentRoiImageRect.height())
    );
}

QRect SpeckleDenoise::getDisplayedPixmapRect() const
{
    if (!imageDisplayLabel || imageDisplayLabel->pixmap() == nullptr || imageDisplayLabel->pixmap()->isNull())
    {
        return QRect();
    }

    QSize labelSize = imageDisplayLabel->size();
    QSize pixmapSize = imageDisplayLabel->pixmap()->size();

    QSize scaledSize = pixmapSize.scaled(labelSize, Qt::KeepAspectRatio);

    int x = (labelSize.width() - scaledSize.width()) / 2;
    int y = (labelSize.height() - scaledSize.height()) / 2;

    return QRect(x, y, scaledSize.width(), scaledSize.height());
}


QPoint SpeckleDenoise::mapLabelPointToImagePoint(const QPoint& labelPoint) const
{
    QRect displayRect = getDisplayedPixmapRect();
    if (!displayRect.isValid() || !displayRect.contains(labelPoint))
    {
        return QPoint(-1, -1);
    }

    QPixmap pixmap;
    if (ui->imageTypeComboBox->currentText() == "Original")
    {
        pixmap = originalPixmap;
    }
    else
    {
        pixmap = filteredPixmap.isNull() ? originalPixmap : filteredPixmap;
    }

    if (pixmap.isNull())
    {
        return QPoint(-1, -1);
    }

    double scaleX = double(pixmap.width()) / double(displayRect.width());
    double scaleY = double(pixmap.height()) / double(displayRect.height());

    int imgX = int((labelPoint.x() - displayRect.x()) * scaleX);
    int imgY = int((labelPoint.y() - displayRect.y()) * scaleY);

    imgX = std::max(0, std::min(imgX, pixmap.width() - 1));
    imgY = std::max(0, std::min(imgY, pixmap.height() - 1));

    return QPoint(imgX, imgY);
}


bool SpeckleDenoise::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == imageDisplayLabel && roiModeEnabled)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                QPoint imagePoint = mapLabelPointToImagePoint(mouseEvent->pos());
                if (imagePoint.x() >= 0 && imagePoint.y() >= 0)
                {
                    roiStartImagePoint = imagePoint;
                    roiEndImagePoint = imagePoint;
                    currentRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    roiSelecting = true;

                    updateRoiDisplay();
                    updateDisplayedImage();
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            if (roiSelecting)
            {
                QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
                QPoint imagePoint = mapLabelPointToImagePoint(mouseEvent->pos());
                if (imagePoint.x() >= 0 && imagePoint.y() >= 0)
                {
                    roiEndImagePoint = imagePoint;
                    currentRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();

                    updateRoiDisplay();
                    updateDisplayedImage();
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && roiSelecting)
            {
                QPoint imagePoint = mapLabelPointToImagePoint(mouseEvent->pos());
                if (imagePoint.x() >= 0 && imagePoint.y() >= 0)
                {
                    roiEndImagePoint = imagePoint;
                }

                currentRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                roiSelecting = false;
                roiModeEnabled = false;

                updateRoiDisplay();
                updateDisplayedImage();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

#include "ClutterSuppression.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <vector>
#include "FormatConversion.h"
#include "icon_source.h"
#include "SARProcessor.h"


#include "InSARLogManager.h"
ClutterSuppression::ClutterSuppression(QWidget* parent)
    : QWidget(parent),
      ui(new Ui::ClutterSuppression),
      copy(nullptr),
      methodComboBox(nullptr),
      guardRadiusSpinBox(nullptr),
      clutterRadiusSpinBox(nullptr),
      pfaSpinBox(nullptr),
      censoringSpinBox(nullptr),
      mixtureCountSpinBox(nullptr),
      methodDescriptionBrowser(nullptr)
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

    resize(width(), 1014);
    setMinimumHeight(950);
    if (QWidget* mainContainer = findChild<QWidget*>("layoutWidget", Qt::FindDirectChildrenOnly))
        mainContainer->resize(mainContainer->width(), 911);

    auto* descriptionGroup = new QGroupBox(QStringLiteral("方法说明"), ui->RightPanel);
    descriptionGroup->setGeometry(0, 630, 405, 255);
    auto* descriptionLayout = new QVBoxLayout(descriptionGroup);
    descriptionLayout->setContentsMargins(8, 8, 8, 8);
    methodDescriptionBrowser = new QTextBrowser(descriptionGroup);
    methodDescriptionBrowser->setReadOnly(true);
    methodDescriptionBrowser->setOpenExternalLinks(false);
    methodDescriptionBrowser->setStyleSheet(
        "QTextBrowser { background: #fafafa; border: 1px solid #d7d7d7; padding: 4px; }");
    descriptionLayout->addWidget(methodDescriptionBrowser);

    ui->projectComboBox->clear();
    ui->InputComboBox->clear();
    ui->inputImageComboBox->clear();

    ui->imageTypeComboBox->clear();
    ui->imageTypeComboBox->addItem("Original");
    ui->imageTypeComboBox->addItem("Filtered");
    ui->imageTypeComboBox->addItem("Target mask");

    // The .ui file originally contained only two BM3D buttons. Build the CFAR
    // controls here to keep the generated UI header stable across Qt versions.
    ui->filterGroup->setGeometry(0, 260, 359, 171);
    ui->widget_2->setGeometry(10, 20, 339, 141);
    ui->roiGroup->move(0, 440);
    ui->enlGroup->move(1, 580);

    auto* filterLayout = new QGridLayout(ui->widget_2);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setHorizontalSpacing(5);
    filterLayout->setVerticalSpacing(3);

    methodComboBox = new QComboBox(ui->widget_2);
    const ClutterSuppressionMethod methods[] = {
        ClutterSuppressionMethod::BM3D,
        ClutterSuppressionMethod::CACFAR,
        ClutterSuppressionMethod::ACCFAR,
        ClutterSuppressionMethod::AAFCFAR,
        ClutterSuppressionMethod::VICFAR,
        ClutterSuppressionMethod::RmSATCFAR
    };
    for (ClutterSuppressionMethod method : methods)
        methodComboBox->addItem(ClutterSuppressionAlgorithms::methodName(method),
                                static_cast<int>(method));
    methodComboBox->setCurrentIndex(methodComboBox->findData(
        static_cast<int>(ClutterSuppressionMethod::RmSATCFAR)));

    guardRadiusSpinBox = new QSpinBox(ui->widget_2);
    guardRadiusSpinBox->setRange(0, 32);
    guardRadiusSpinBox->setValue(3);
    clutterRadiusSpinBox = new QSpinBox(ui->widget_2);
    clutterRadiusSpinBox->setRange(1, 128);
    clutterRadiusSpinBox->setValue(12);
    pfaSpinBox = new QDoubleSpinBox(ui->widget_2);
    pfaSpinBox->setDecimals(8);
    pfaSpinBox->setRange(1e-8, 0.1);
    pfaSpinBox->setSingleStep(0.0001);
    pfaSpinBox->setValue(0.0001);
    pfaSpinBox->setToolTip("Probability of false alarm");
    censoringSpinBox = new QDoubleSpinBox(ui->widget_2);
    censoringSpinBox->setDecimals(2);
    censoringSpinBox->setRange(0.01, 0.45);
    censoringSpinBox->setSingleStep(0.05);
    censoringSpinBox->setValue(0.20);
    mixtureCountSpinBox = new QSpinBox(ui->widget_2);
    mixtureCountSpinBox->setRange(1, 4);
    mixtureCountSpinBox->setValue(3);

    filterLayout->addWidget(new QLabel("Method", ui->widget_2), 0, 0);
    filterLayout->addWidget(methodComboBox, 0, 1, 1, 3);
    filterLayout->addWidget(new QLabel("Guard", ui->widget_2), 1, 0);
    filterLayout->addWidget(guardRadiusSpinBox, 1, 1);
    filterLayout->addWidget(new QLabel("Clutter", ui->widget_2), 1, 2);
    filterLayout->addWidget(clutterRadiusSpinBox, 1, 3);
    filterLayout->addWidget(new QLabel("Pfa", ui->widget_2), 2, 0);
    filterLayout->addWidget(pfaSpinBox, 2, 1);
    filterLayout->addWidget(new QLabel("Censor", ui->widget_2), 2, 2);
    filterLayout->addWidget(censoringSpinBox, 2, 3);
    filterLayout->addWidget(new QLabel("Mixtures", ui->widget_2), 3, 0);
    filterLayout->addWidget(mixtureCountSpinBox, 3, 1);
    filterLayout->addWidget(ui->runFilterButton, 4, 0, 1, 2);
    filterLayout->addWidget(ui->deleteFilterButton, 4, 2, 1, 2);

    connect(methodComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateMethodControls(); });
    updateMethodControls();

    targetRoiModeEnabled = false;
    clutterRoiModeEnabled = false;
    roiSelecting = false;
    showingClutterRoiInfo = false;

    targetRoiImageRect = QRect();
    clutterRoiImageRect = QRect();
    roiStartImagePoint = QPoint();
    roiEndImagePoint = QPoint();

    ui->roiTopLeftValueLabel->setText("--");
    ui->label_4->setText("--");
    ui->roiHeightValueLabel->setText("--");
    ui->roiPixelCountValueLabel->setText("--");

    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");
    ui->beishu->setText("--");



}

ClutterSuppression::~ClutterSuppression()
{
    delete ui;
}

void ClutterSuppression::ShowProjectList(QStandardItemModel* model)
{
    if (model == nullptr || model->rowCount() == 0)
    {
        QMessageBox::warning(this, "Warning!", "Please import or open project data first");
        this->deleteLater();
        return;
    }

    copy = model;

    ui->projectComboBox->clear();
    ui->InputComboBox->clear();
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

void ClutterSuppression::on_loadImageButton_clicked()
{
    if (input_image_path.isEmpty() || !QFileInfo::exists(input_image_path))
    {
        QMessageBox::warning(this, "Warning!", "Please select a valid input image.");
        return;
    }

    if (!originalPixmap.load(input_image_path))
    {
        InSARLogManager::LogWarning("UI", "Failed to load input image.");
        QMessageBox::warning(this, "Warning!", "Failed to load input image.");
        return;
    }

    originalGrayMat = cv::imread(input_image_path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (originalGrayMat.empty())
    {
        InSARLogManager::LogWarning("UI", "Failed to load input image as grayscale.");
        QMessageBox::warning(this, "Warning!", "Failed to load input image as grayscale.");
        return;
    }

    targetRoiModeEnabled = false;
    clutterRoiModeEnabled = false;
    roiSelecting = false;

    targetRoiImageRect = QRect();
    clutterRoiImageRect = QRect();
    roiStartImagePoint = QPoint();
    roiEndImagePoint = QPoint();

    filteredPixmap = QPixmap();
    filteredGrayMat.release();

    ui->roiTopLeftValueLabel->setText("--");
    ui->label_4->setText("--");
    ui->roiHeightValueLabel->setText("--");
    ui->roiPixelCountValueLabel->setText("--");

    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");
    ui->beishu->setText("--");

    ui->imageTypeComboBox->setCurrentText("Original");
    updateDisplayedImage();
}



void ClutterSuppression::on_imageTypeComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    updateDisplayedImage();
}


void ClutterSuppression::on_projectComboBox_currentIndexChanged(int index)
{
    ui->InputComboBox->clear();
    ui->inputImageComboBox->clear();

    if (!copy || index < 0 || index >= copy->rowCount())
    {
        return;
    }

    QStandardItem* project = copy->item(index, 0);
    QStandardItem* projectPathItem = copy->item(index, 1);
    if (!project || !projectPathItem)
    {
        return;
    }

    project_name = project->text();
    save_path = projectPathItem->text();

    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node)
        {
            ui->InputComboBox->addItem(node->text());
        }
    }

    if (ui->InputComboBox->count() > 0)
    {
        ui->InputComboBox->setCurrentIndex(0);
        on_InputComboBox_currentIndexChanged(0);
    }
}

void ClutterSuppression::on_InputComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    ui->inputImageComboBox->clear();

    if (!copy)
    {
        return;
    }

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0 || projectIndex >= copy->rowCount())
    {
        return;
    }

    QStandardItem* project = copy->item(projectIndex, 0);
    if (!project)
    {
        return;
    }

    QString nodeName = ui->InputComboBox->currentText();
    if (nodeName.isEmpty())
    {
        return;
    }

    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == nodeName)
        {
            input_node_name = node->text();

            for (int j = 0; j < node->rowCount(); j++)
            {
                QStandardItem* image = node->child(j, 0);
                if (image)
                {
                    ui->inputImageComboBox->addItem(image->text());
                }
            }
            break;
        }
    }

    if (ui->inputImageComboBox->count() > 0)
    {
        ui->inputImageComboBox->setCurrentIndex(0);
        on_inputImageComboBox_currentIndexChanged(0);
    }
}


void ClutterSuppression::on_inputImageComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    input_image_name.clear();
    input_image_path.clear();

    if (!copy)
    {
        return;
    }

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0 || projectIndex >= copy->rowCount())
    {
        return;
    }

    QStandardItem* project = copy->item(projectIndex, 0);
    if (!project)
    {
        return;
    }

    QString nodeName = ui->InputComboBox->currentText();
    QString imageName = ui->inputImageComboBox->currentText();

    if (nodeName.isEmpty() || imageName.isEmpty())
    {
        return;
    }

    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* node = project->child(i, 0);
        if (node && node->text() == nodeName)
        {
            for (int j = 0; j < node->rowCount(); j++)
            {
                QStandardItem* image = node->child(j, 0);
                QStandardItem* pathItem = node->child(j, 1);

                if (image && pathItem && image->text() == imageName)
                {
                    input_image_name = image->text();
                    input_image_path = pathItem->text();
                    return;
                }
            }
        }
    }
}


void ClutterSuppression::updateDisplayedImage()
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
    else if (ui->imageTypeComboBox->currentText() == "Target mask")
    {
        pixmap = targetMaskPixmap;
    }

    if (pixmap.isNull())
    {
        imageDisplayLabel->clear();
        return;
    }

    QPixmap displayPixmap = pixmap.copy();

    if (!targetRoiImageRect.isNull() &&
        targetRoiImageRect.width() > 0 &&
        targetRoiImageRect.height() > 0)
    {
        QPainter painter(&displayPixmap);
        QPen pen(Qt::red);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawRect(targetRoiImageRect);
        painter.end();
    }

    if (!clutterRoiImageRect.isNull() &&
        clutterRoiImageRect.width() > 0 &&
        clutterRoiImageRect.height() > 0)
    {
        QPainter painter(&displayPixmap);
        QPen pen(Qt::green);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawRect(clutterRoiImageRect);
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


void ClutterSuppression::on_startRoiButton_clicked()
{
    if (originalPixmap.isNull())
    {
        QMessageBox::warning(this, "Warning!", "Please load an image first.");
        return;
    }

    targetRoiModeEnabled = true;
    clutterRoiModeEnabled = false;
    roiSelecting = false;
     showingClutterRoiInfo = false;
}

void ClutterSuppression::on_startRoiButton_2_clicked()
{
    if (originalPixmap.isNull())
    {
        QMessageBox::warning(this, "Warning!", "Please load an image first.");
        return;
    }

    targetRoiModeEnabled = false;
    clutterRoiModeEnabled = true;
    roiSelecting = false;
    showingClutterRoiInfo = true;
}

void ClutterSuppression::on_clearRoiButton_clicked()
{
    targetRoiModeEnabled = false;
    targetRoiImageRect = QRect();

    if (!showingClutterRoiInfo)
    {
        updateCurrentRoiDisplay();
    }

    updateDisplayedImage();
    updateScrResults();
}


void ClutterSuppression::on_clearRoiButton_2_clicked()
{
    clutterRoiModeEnabled = false;
    clutterRoiImageRect = QRect();

    if (showingClutterRoiInfo)
    {
        updateCurrentRoiDisplay();
    }

    updateDisplayedImage();
    updateScrResults();
}



QRect ClutterSuppression::getDisplayedPixmapRect() const
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

QPoint ClutterSuppression::mapLabelPointToImagePoint(const QPoint& labelPoint) const
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


void ClutterSuppression::updateCurrentRoiDisplay()
{
    QRect roiRect = showingClutterRoiInfo ? clutterRoiImageRect : targetRoiImageRect;

    if (roiRect.isNull() || roiRect.width() < 1 || roiRect.height() < 1)
    {
        ui->roiTopLeftValueLabel->setText("--");
        ui->label_4->setText("--");
        ui->roiHeightValueLabel->setText("--");
        ui->roiPixelCountValueLabel->setText("--");
        return;
    }

    ui->roiTopLeftValueLabel->setText(
        QString("(%1, %2)")
            .arg(roiRect.x())
            .arg(roiRect.y())
    );
    ui->label_4->setText(QString::number(roiRect.width()));
    ui->roiHeightValueLabel->setText(QString::number(roiRect.height()));
    ui->roiPixelCountValueLabel->setText(
        QString::number(roiRect.width() * roiRect.height())
    );
}


bool ClutterSuppression::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == imageDisplayLabel && (targetRoiModeEnabled || clutterRoiModeEnabled))
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
                    roiSelecting = true;

                    if (targetRoiModeEnabled)
                    {
                        targetRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    }
                    else if (clutterRoiModeEnabled)
                    {
                        clutterRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    }

                    updateCurrentRoiDisplay();
                    updateDisplayedImage();
                    updateScrResults();

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

                    if (targetRoiModeEnabled)
                    {
                        targetRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    }
                    else if (clutterRoiModeEnabled)
                    {
                        clutterRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    }

                    updateCurrentRoiDisplay();
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

                if (targetRoiModeEnabled)
                {
                    targetRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    targetRoiModeEnabled = false;
                }
                else if (clutterRoiModeEnabled)
                {
                    clutterRoiImageRect = QRect(roiStartImagePoint, roiEndImagePoint).normalized();
                    clutterRoiModeEnabled = false;
                }

                roiSelecting = false;

                updateCurrentRoiDisplay();
                updateDisplayedImage();
                updateScrResults();

                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

double ClutterSuppression::calculateScr(const cv::Mat& targetGray,
                                        const cv::Mat& clutterGray) const
{
    return SARProcessor::CalculateSCR(targetGray, clutterGray);
}


void ClutterSuppression::updateScrResults()
{
    if (targetRoiImageRect.isNull() ||
        clutterRoiImageRect.isNull() ||
        targetRoiImageRect.width() < 2 ||
        targetRoiImageRect.height() < 2 ||
        clutterRoiImageRect.width() < 2 ||
        clutterRoiImageRect.height() < 2 ||
        originalGrayMat.empty())
    {
        ui->originalEnlValueLabel->setText("--");
        ui->filteredEnlValueLabel->setText("--");
        ui->beishu->setText("--");
        return;
    }

    cv::Rect targetRect(
        targetRoiImageRect.x(),
        targetRoiImageRect.y(),
        targetRoiImageRect.width(),
        targetRoiImageRect.height()
    );

    cv::Rect clutterRect(
        clutterRoiImageRect.x(),
        clutterRoiImageRect.y(),
        clutterRoiImageRect.width(),
        clutterRoiImageRect.height()
    );

    cv::Rect originalBounds(0, 0, originalGrayMat.cols, originalGrayMat.rows);
    targetRect &= originalBounds;
    clutterRect &= originalBounds;

    if (targetRect.width < 2 || targetRect.height < 2 ||
        clutterRect.width < 2 || clutterRect.height < 2)
    {
        ui->originalEnlValueLabel->setText("--");
        ui->filteredEnlValueLabel->setText("--");
        ui->beishu->setText("--");
        return;
    }

    cv::Mat originalTarget = originalGrayMat(targetRect).clone();
    cv::Mat originalClutter = originalGrayMat(clutterRect).clone();

    double originalScr = calculateScr(originalTarget, originalClutter);
    ui->originalEnlValueLabel->setText(QString::number(originalScr, 'f', 4));

    if (!filteredGrayMat.empty())
    {
        cv::Rect filteredBounds(0, 0, filteredGrayMat.cols, filteredGrayMat.rows);
        cv::Rect filteredTargetRect = targetRect & filteredBounds;
        cv::Rect filteredClutterRect = clutterRect & filteredBounds;

        if (filteredTargetRect.width >= 2 && filteredTargetRect.height >= 2 &&
            filteredClutterRect.width >= 2 && filteredClutterRect.height >= 2)
        {
            cv::Mat filteredTarget = filteredGrayMat(filteredTargetRect).clone();
            cv::Mat filteredClutter = filteredGrayMat(filteredClutterRect).clone();

            double filteredScr = calculateScr(filteredTarget, filteredClutter);
            ui->filteredEnlValueLabel->setText(QString::number(filteredScr, 'f', 4));
            double improvementPercent = 0.0;
            if (std::abs(originalScr) > 1e-12)
            {
                improvementPercent = ((filteredScr - originalScr) / std::abs(originalScr)) * 100.0;
            }

            ui->beishu->setText(QString("%1%").arg(QString::number(improvementPercent, 'f', 2)));
        }
        else
        {
            ui->filteredEnlValueLabel->setText("--");
            ui->beishu->setText("--");
        }
    }
    else
    {
        ui->filteredEnlValueLabel->setText("--");
        ui->beishu->setText("--");
    }
}

ClutterSuppressionParameters ClutterSuppression::currentParameters() const
{
    ClutterSuppressionParameters parameters;
    parameters.method = static_cast<ClutterSuppressionMethod>(methodComboBox->currentData().toInt());
    parameters.guardRadius = guardRadiusSpinBox->value();
    parameters.clutterRadius = clutterRadiusSpinBox->value();
    parameters.probabilityFalseAlarm = pfaSpinBox->value();
    parameters.censoringFraction = censoringSpinBox->value();
    parameters.maximumMixtureCount = mixtureCountSpinBox->value();
    return parameters;
}

QString ClutterSuppression::currentMethodSuffix() const
{
    return QString::fromLatin1(ClutterSuppressionAlgorithms::methodSuffix(currentParameters().method));
}

void ClutterSuppression::updateMethodControls()
{
    const ClutterSuppressionMethod method = currentParameters().method;
    const bool isCfar = method != ClutterSuppressionMethod::BM3D;
    guardRadiusSpinBox->setEnabled(isCfar);
    clutterRadiusSpinBox->setEnabled(isCfar);
    pfaSpinBox->setEnabled(isCfar);
    censoringSpinBox->setEnabled(method == ClutterSuppressionMethod::ACCFAR);
    mixtureCountSpinBox->setEnabled(method == ClutterSuppressionMethod::RmSATCFAR);
    updateMethodDescription();
}

void ClutterSuppression::updateMethodDescription()
{
    if (!methodDescriptionBrowser) return;

    const ClutterSuppressionMethod method = currentParameters().method;
    QString html;
    switch (method)
    {
    case ClutterSuppressionMethod::CACFAR:
        html = QStringLiteral(
            "<b>CA-CFAR</b><br>"
            "<b>适合：</b>背景比较均匀的区域，如远海、开阔水面或稳定地表。<br>"
            "<b>特点：</b>用周围训练单元的平均功率估计杂波，速度快、适合作为基线；海岸和强杂波边缘容易虚警。<br>"
            "<b>参数：</b>保护半径应覆盖目标；杂波半径决定背景样本数量；Pfa 越小，检测越严格。");
        break;
    case ClutterSuppressionMethod::ACCFAR:
        html = QStringLiteral(
            "<b>AC-CFAR</b><br>"
            "<b>适合：</b>训练窗口内含其他目标、亮散射点或少量异常值的场景。<br>"
            "<b>特点：</b>先删去较亮的异常训练单元，再估计杂波，可减少邻近目标对门限的抬高。<br>"
            "<b>参数：</b>Censor 为删失比例；过大可能低估杂波并增加虚警，建议先用 0.10～0.25。");
        break;
    case ClutterSuppressionMethod::AAFCFAR:
        html = QStringLiteral(
            "<b>AAF-CFAR</b><br>"
            "<b>适合：</b>高分辨率、目标密集或局部统计变化明显的 SAR 图像。<br>"
            "<b>特点：</b>根据局部均值和方差自动剔除异常训练单元，无需固定删失比例，兼顾速度和复杂背景适应性。<br>"
            "<b>提示：</b>港区或多船场景通常比普通 CA-CFAR 更稳健。");
        break;
    case ClutterSuppressionMethod::VICFAR:
        html = QStringLiteral(
            "<b>VI-CFAR</b><br>"
            "<b>适合：</b>海岸线、港口、岛礁和地物边界等非均匀杂波区域。<br>"
            "<b>特点：</b>利用局部变化指数识别杂波边缘，并采用更保守的背景估计，通常能降低边界虚警。<br>"
            "<b>局限：</b>门限偏保守，弱小目标可能漏检；可适当增大 Pfa 或减小杂波半径。");
        break;
    case ClutterSuppressionMethod::RmSATCFAR:
        html = QStringLiteral(
            "<b>RmSAT-CFAR</b><br>"
            "<b>适合：</b>包含多种杂波分布的复杂场景，如海陆混合区、港区和强度变化明显的海面。<br>"
            "<b>特点：</b>以 Rayleigh 混合模型描述多峰杂波，并通过局部积分统计加速；适应性强但计算量较大。<br>"
            "<b>参数：</b>Mixtures 一般取 2～3；分量过多可能过拟合小区域。");
        break;
    case ClutterSuppressionMethod::BM3D:
    default:
        html = QStringLiteral(
            "<b>BM3D（原方法）</b><br>"
            "<b>适合：</b>需要整体降低颗粒噪声、改善视觉质量的 SAR 灰度显示图。<br>"
            "<b>特点：</b>属于通用图像降噪，不建立目标附近的杂波统计门限；纹理保持较好但计算较慢。<br>"
            "<b>提示：</b>适合观察和传统 SCR 对比；如果重点是目标检测，优先选择 CFAR 方法。");
        break;
    }
    methodDescriptionBrowser->setHtml(html);
}

cv::Mat ClutterSuppression::runClutterSuppressionCoreLogic(
    const cv::Mat& inputGray)
{
    targetMaskMat.release();

    if (inputGray.empty()) {
        return cv::Mat();
    }

    if (currentParameters().method ==
        ClutterSuppressionMethod::BM3D)
    {
        cv::Mat output;
        const int ret =
            SARProcessor::DenoiseGray(
                inputGray, 0.0, output);

        return ret == 0 ? output : cv::Mat();
    }

    const ClutterSuppressionResult result =
        ClutterSuppressionAlgorithms::process(
            inputGray, currentParameters());

    targetMaskMat = result.targetMask.clone();
    return result.suppressedImage;
}

cv::Mat ClutterSuppression::runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const
{
    // 已由 runClutterSuppressionCoreLogic 统一调用 SARProcessor，此方法保留接口兼容
    Q_UNUSED(imgNorm);
    Q_UNUSED(sigmaFinal);
    return cv::Mat();
}

double ClutterSuppression::calcMedian(const cv::Mat& input) const
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


void ClutterSuppression::on_runFilterButton_clicked()
{
    if (input_image_path.isEmpty() || !QFile::exists(input_image_path))
    {
        QMessageBox::warning(this, "Warning!", "Please load an input image first.");
        return;
    }

    QString outputNodeName = ui->NodeWindowSpinBox->text();
    if (outputNodeName.isEmpty())
    {
        QMessageBox::warning(this, "Warning!", "Please input output node name.");
        return;
    }

    cv::Mat inputGray = cv::imread(input_image_path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty())
    {
        InSARLogManager::LogWarning("UI", "Failed to read input image.");
        QMessageBox::warning(this, "Warning!", "Failed to read input image.");
        return;
    }

    cv::Mat filteredImage = runClutterSuppressionCoreLogic(inputGray);
    if (filteredImage.empty())
    {
        InSARLogManager::LogWarning("UI", "Clutter suppression failed.");
        QMessageBox::warning(this, "Warning!", "Clutter suppression failed.");
        return;
    }

    filteredGrayMat = filteredImage.clone();

    QFileInfo inputInfo(input_image_name);
    QString expectedImageName = inputInfo.completeBaseName() + "_" + currentMethodSuffix() + "_Clutter";
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
    if (!targetMaskMat.empty())
    {
        const QImage maskImage(targetMaskMat.data, targetMaskMat.cols, targetMaskMat.rows,
                               static_cast<int>(targetMaskMat.step), QImage::Format_Grayscale8);
        targetMaskPixmap = QPixmap::fromImage(maskImage.copy());
    }
    else
    {
        targetMaskPixmap = QPixmap();
    }
    ui->imageTypeComboBox->setCurrentText("Filtered");
    updateDisplayedImage();
    updateScrResults();

    if (!filteredPixmap.load(outputPath))
    {
        InSARLogManager::LogWarning("UI", "Failed to load filtered result image.");
        QMessageBox::warning(this, "Warning!", "Failed to load filtered result image.");
        return;
    }

    emit sendCopy(copy);
}

bool ClutterSuppression::saveFilteredImage(const cv::Mat& filteredImage,
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
    outputImageName = inputInfo.completeBaseName() + "_" + currentMethodSuffix() + "_Clutter";
    outputPath = save_path + "/" + outputNodeName + "/" + outputImageName + ".jpg";

    if (QFile::exists(outputPath))
    {
        return false;
    }

    if (!cv::imwrite(outputPath.toStdString(), filteredImage))
        return false;

    if (!targetMaskMat.empty())
    {
        const QString maskPath = save_path + "/" + outputNodeName + "/" +
                                 outputImageName + "_mask.png";
        if (!cv::imwrite(maskPath.toStdString(), targetMaskMat))
            InSARLogManager::LogWarning("UI", "Failed to save CFAR target mask sidecar.");
    }
    return true;
}

bool ClutterSuppression::registerFilteredImage(const QString& outputNodeName,
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
                               "ClutterSuppression") < 0)
    {
        return false;
    }

    if (xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        return false;
    }

    QStandardItem* imageItem = new QStandardItem(outputImageName);
    imageItem->setToolTip("amplitude");
    imageItem->setIcon(QIcon(IMAGEDATA_ICON));

    QStandardItem* pathItem = new QStandardItem(outputPath);

    outputNode->appendRow(imageItem);
    outputNode->setChild(outputNode->rowCount() - 1, 1, pathItem);

    return true;
}

void ClutterSuppression::on_deleteFilterButton_clicked()
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
        QMessageBox::warning(this, "Warning!", "Please select an input image first.");
        return;
    }

    QFileInfo inputInfo(input_image_name);
    QString outputImageName = inputInfo.completeBaseName() + "_" + currentMethodSuffix() + "_Clutter";
    QString outputPath = save_path + "/" + outputNodeName + "/" + outputImageName + ".jpg";

    int projectIndex = ui->projectComboBox->currentIndex();
    if (projectIndex < 0)
    {
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

    const QString maskPath = save_path + "/" + outputNodeName + "/" + outputImageName + "_mask.png";
    if (QFile::exists(maskPath)) QFile::remove(maskPath);

    if (imageRow >= 0)
    {
        outputNode->removeRow(imageRow);
    }

    filteredPixmap = QPixmap();
    targetMaskPixmap = QPixmap();
    filteredGrayMat.release();
    targetMaskMat.release();
    ui->imageTypeComboBox->setCurrentText("Original");
    updateDisplayedImage();
    updateScrResults();

    emit sendCopy(copy);

    QMessageBox::information(this, "Info", "Filtered image deleted.");
}

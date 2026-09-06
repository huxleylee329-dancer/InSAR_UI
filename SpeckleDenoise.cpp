#include "SpeckleDenoise.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QtMath>
#include <algorithm>
#include "FormatConversion.h"
#include "icon_source.h"
#include <vector>
#include "SARProcessor.h"
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <exception>
#include <limits>

#include "InSARLogManager.h"

static bool containsNonAsciiPath(const QString& text)
{
    for (const QChar& ch : text)
    {
        if (ch.unicode() > 127)
        {
            return true;
        }
    }
    return false;
}

static QString parameterToken(double value, int decimals)
{
    QString token = QString::number(value, 'f', decimals);
    token.replace('.', 'p');
    return token;
}

static QString formatEnlValue(double value)
{
    if (std::isinf(value))
    {
        return QStringLiteral("∞");
    }
    if (!std::isfinite(value))
    {
        return "--";
    }
    return QString::number(value, 'f', 4);
}

static cv::Mat runFilterSnapshot(const cv::Mat& inputGray,
                                 int methodIndex,
                                 int radius,
                                 double numberOfLooks,
                                 double frostDeramp)
{
    try
    {
        if (methodIndex == 0)
        {
            cv::Mat output;
            return SARProcessor::DenoiseGray(inputGray, 0.0, output) == 0
                ? output
                : cv::Mat();
        }

        SARProcessor::SpeckleFilterMethod method = SARProcessor::SpeckleFilterMethod::Lee;
        switch (methodIndex)
        {
        case 2:
            method = SARProcessor::SpeckleFilterMethod::Frost;
            break;
        case 3:
            method = SARProcessor::SpeckleFilterMethod::GammaMAP;
            break;
        case 4:
            method = SARProcessor::SpeckleFilterMethod::Kuan;
            break;
        default:
            break;
        }

        return SARProcessor::DespeckleGray(
            inputGray, method, radius, numberOfLooks, frostDeramp);
    }
    catch (const cv::Exception&)
    {
        return cv::Mat();
    }
    catch (const std::exception&)
    {
        return cv::Mat();
    }
}

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

    // 扩展滤波设置区域，并为五种算法提供统一入口。
    resize(width(), 1014);
    setMinimumHeight(950);
    if (QWidget* mainContainer = findChild<QWidget*>("layoutWidget", Qt::FindDirectChildrenOnly))
    {
        mainContainer->resize(mainContainer->width(), 911);
    }
    ui->filterGroup->resize(ui->filterGroup->width(), 221);
    ui->roiGroup->move(ui->roiGroup->x(), 540);
    ui->roiGroup->resize(ui->roiGroup->width(), 91);
    ui->widget->resize(ui->widget->width(), 61);
    ui->enlGroup->move(ui->enlGroup->x(), 640);
    ui->widget_2->move(ui->widget_2->x(), 132);
    ui->FilterProgressBar->move(ui->FilterProgressBar->x(), 180);

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

    QLabel* filterMethodLabel = new QLabel(QStringLiteral("滤波方法"), ui->filterGroup);
    filterMethodLabel->setGeometry(40, 28, 90, 23);
    filterMethodComboBox = new QComboBox(ui->filterGroup);
    filterMethodComboBox->setGeometry(170, 28, 131, 23);
    filterMethodComboBox->addItem("BM3D");
    filterMethodComboBox->addItem("Lee");
    filterMethodComboBox->addItem("Frost");
    filterMethodComboBox->addItem("GammaMAP");
    filterMethodComboBox->addItem("Kuan");

    filterRadiusLabel = new QLabel(QStringLiteral("邻域半径"), ui->filterGroup);
    filterRadiusLabel->setGeometry(40, 58, 90, 23);
    filterRadiusSpinBox = new QSpinBox(ui->filterGroup);
    filterRadiusSpinBox->setGeometry(170, 58, 131, 23);
    filterRadiusSpinBox->setRange(1, 20);
    filterRadiusSpinBox->setValue(3);

    filterLooksLabel = new QLabel(QStringLiteral("等效视数"), ui->filterGroup);
    filterLooksLabel->setGeometry(40, 88, 90, 23);
    filterLooksSpinBox = new QDoubleSpinBox(ui->filterGroup);
    filterLooksSpinBox->setGeometry(170, 88, 131, 23);
    filterLooksSpinBox->setRange(0.1, 100.0);
    filterLooksSpinBox->setDecimals(2);
    filterLooksSpinBox->setValue(1.0);

    frostDerampLabel = new QLabel(QStringLiteral("Frost 衰减"), ui->filterGroup);
    frostDerampLabel->setGeometry(40, 88, 100, 23);
    frostDerampSpinBox = new QDoubleSpinBox(ui->filterGroup);
    frostDerampSpinBox->setGeometry(170, 88, 131, 23);
    frostDerampSpinBox->setRange(0.001, 10.0);
    frostDerampSpinBox->setDecimals(3);
    frostDerampSpinBox->setSingleStep(0.05);
    frostDerampSpinBox->setValue(0.1);

    connect(filterMethodComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateFilterParameterVisibility();
                refreshCurrentResult();
            });
    updateFilterParameterVisibility();

    filterWatcher = new QFutureWatcher<cv::Mat>(this);
    connect(filterWatcher,
            &QFutureWatcher<cv::Mat>::finished,
            this,
            &SpeckleDenoise::onFilterFinished);

    connect(filterRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(filterLooksSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshCurrentResult(); });
    connect(frostDerampSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshCurrentResult(); });
    connect(ui->NodeWindowSpinBox, &QLineEdit::editingFinished,
            this, [this]() { refreshCurrentResult(); });

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
    if (!copy)
    {
        return;
    }

    input_image_path.clear();
    input_image_name.clear();
    resetLoadedImageState();
    if (index < 0)
    {
        return;
    }

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

    loaded_image_path = input_image_path;
    refreshCurrentResult();
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
    clearEnlResults();
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

    clearEnlResults();

    updateDisplayedImage();
}


void SpeckleDenoise::on_runFilterButton_clicked()
{
    if (filterWatcher && filterWatcher->isRunning())
    {
        return;
    }

    if (input_image_path.isEmpty() || !QFile::exists(input_image_path))
    {
        InSARLogManager::LogWarning("UI", "Please load an input image first.");
        QMessageBox::warning(this, "Warning!", "Please load an input image first.");
        return;
    }

    if (save_path.isEmpty() || project_name.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Project path is invalid.");
        QMessageBox::warning(this, "Warning!", "Project path is invalid.");
        return;
    }

    if (containsNonAsciiPath(input_image_path) || containsNonAsciiPath(save_path))
    {
        InSARLogManager::LogWarning("UI", "Image path or project path contains non-ASCII characters.");
        QMessageBox::warning(
            this,
            "Warning!",
            QStringLiteral("当前图像路径或工程路径包含中文或非 ASCII 字符，OpenCV 可能无法读取或保存图像。\n请将工程保存到英文路径下后重试。")
        );
        return;
    }

    QString outputNodeName = ui->NodeWindowSpinBox->text().trimmed();
    if (!isValidOutputNodeName(outputNodeName))
    {
        InSARLogManager::LogWarning("UI", "Invalid output node name.");
        QMessageBox::warning(
            this,
            "Warning!",
            QStringLiteral("输出节点名不能为空，且不能包含 \\ / : * ? \" < > |，也不能以点结尾。"));
        return;
    }
    ui->NodeWindowSpinBox->setText(outputNodeName);

    const QString outputImageName = currentOutputImageName();
    const QString outputDirectory = QDir(save_path).filePath(outputNodeName);
    const QString outputPath = QDir(outputDirectory).filePath(outputImageName + ".jpg");
    if (refreshCurrentResult())
    {
        return;
    }

    cv::Mat inputGray = cv::imread(input_image_path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty())
    {
        InSARLogManager::LogWarning("UI", "Failed to read input image.");
        QMessageBox::warning(this, "Warning!", "Failed to read input image.");
        return;
    }

    if (loaded_image_path != input_image_path || originalGrayMat.empty())
    {
        QPixmap currentPixmap(input_image_path);
        if (currentPixmap.isNull())
        {
            InSARLogManager::LogWarning("UI", "Failed to load input image preview.");
            QMessageBox::warning(this, "Warning!", "Failed to load input image preview.");
            return;
        }

        originalGrayMat = inputGray.clone();
        originalPixmap = currentPixmap;
        loaded_image_path = input_image_path;
        filteredGrayMat.release();
        filteredPixmap = QPixmap();
        currentRoiImageRect = QRect();
        roiSelecting = false;
        roiModeEnabled = false;
        updateRoiDisplay();
        ui->originalEnlValueLabel->setText("--");
        ui->filteredEnlValueLabel->setText("--");
        ui->imageTypeComboBox->setCurrentText("Original");
        updateDisplayedImage();
    }

    pending_output_node_name = outputNodeName;
    pending_output_image_name = outputImageName;
    pending_output_path = outputPath;

    const int methodIndex = filterMethodComboBox->currentIndex();
    const int radius = filterRadiusSpinBox->value();
    const double numberOfLooks = filterLooksSpinBox->value();
    const double frostDeramp = frostDerampSpinBox->value();
    const cv::Mat inputSnapshot = inputGray.clone();

    setFilterRunning(true);
    filterWatcher->setFuture(QtConcurrent::run(
        [inputSnapshot, methodIndex, radius, numberOfLooks, frostDeramp]() {
            return runFilterSnapshot(
                inputSnapshot, methodIndex, radius, numberOfLooks, frostDeramp);
        }));
}

void SpeckleDenoise::onFilterFinished()
{
    const cv::Mat filteredImage = filterWatcher->result();
    setFilterRunning(false);

    if (filteredImage.empty())
    {
        ui->FilterProgressBar->setValue(0);
        InSARLogManager::LogWarning("UI", "Speckle denoise failed.");
        QMessageBox::warning(this, "Warning!", "Speckle denoise failed.");
        return;
    }

    if (!saveFilteredImage(filteredImage, pending_output_path))
    {
        ui->FilterProgressBar->setValue(0);
        InSARLogManager::LogWarning("UI", "Failed to save filtered image.");
        QMessageBox::warning(this, "Warning!", "Failed to save filtered image.");
        return;
    }

    if (!registerFilteredImage(
            pending_output_node_name, pending_output_image_name, pending_output_path))
    {
        const bool rollbackSucceeded = QFile::remove(pending_output_path);
        ui->FilterProgressBar->setValue(0);
        InSARLogManager::LogWarning("UI", "Failed to register filtered image.");
        QMessageBox::warning(
            this,
            "Warning!",
            rollbackSucceeded
                ? QStringLiteral("滤波结果注册失败，已回滚刚生成的图像文件。")
                : QStringLiteral("滤波结果注册失败，且图像文件回滚失败：\n%1")
                      .arg(pending_output_path));
        return;
    }

    filteredGrayMat = filteredImage.clone();
    filteredPixmap.load(pending_output_path);
    ui->imageTypeComboBox->setCurrentText("Filtered");
    updateDisplayedImage();
    updateEnlResults();
    ui->FilterProgressBar->setValue(100);
    emit sendCopy(copy);
}


void SpeckleDenoise::on_imageTypeComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    updateDisplayedImage();
}

QString SpeckleDenoise::currentFilterSuffix() const
{
    if (!filterMethodComboBox)
    {
        return "_BM3D";
    }

    switch (filterMethodComboBox->currentIndex())
    {
    case 1:
        return QString("_Lee_R%1_L%2")
            .arg(filterRadiusSpinBox->value())
            .arg(parameterToken(filterLooksSpinBox->value(), 2));
    case 2:
        return QString("_Frost_R%1_D%2")
            .arg(filterRadiusSpinBox->value())
            .arg(parameterToken(frostDerampSpinBox->value(), 3));
    case 3:
        return QString("_GammaMAP_R%1_L%2")
            .arg(filterRadiusSpinBox->value())
            .arg(parameterToken(filterLooksSpinBox->value(), 2));
    case 4:
        return QString("_Kuan_R%1_L%2")
            .arg(filterRadiusSpinBox->value())
            .arg(parameterToken(filterLooksSpinBox->value(), 2));
    default: return "_BM3D";
    }
}

void SpeckleDenoise::updateFilterParameterVisibility()
{
    const int methodIndex =
        filterMethodComboBox ? filterMethodComboBox->currentIndex() : 0;

    const bool usesRadius = methodIndex > 0;
    const bool usesLooks =
        methodIndex == 1 || methodIndex == 3 || methodIndex == 4;
    const bool usesDeramp = methodIndex == 2;

    filterRadiusLabel->setVisible(usesRadius);
    filterRadiusSpinBox->setVisible(usesRadius);
    filterLooksLabel->setVisible(usesLooks);
    filterLooksSpinBox->setVisible(usesLooks);
    frostDerampLabel->setVisible(usesDeramp);
    frostDerampSpinBox->setVisible(usesDeramp);
    updateFilterDescription();
}

void SpeckleDenoise::updateFilterDescription()
{
    if (!methodDescriptionBrowser) return;

    const int methodIndex = filterMethodComboBox ? filterMethodComboBox->currentIndex() : 0;
    QString html;
    switch (methodIndex)
    {
    case 1:
        html = QStringLiteral(
            "<b>Lee 滤波</b><br>"
            "<b>适合：</b>大面积均匀区域，如平静海面、农田和低纹理地表。<br>"
            "<b>特点：</b>利用局部均值和方差抑制乘性斑点，速度快；强边缘附近可能略有模糊。<br>"
            "<b>推荐参数：</b>邻域半径 3（7×7 窗口）；等效视数优先填写产品标称值，未知时先用 1.0。"
            "均匀区噪声仍强可将半径增至 4，小目标较多时可降至 2。");
        break;
    case 2:
        html = QStringLiteral(
            "<b>Frost 滤波</b><br>"
            "<b>适合：</b>既有均匀区域又有明显边缘的 SAR 图像，如海岸、道路和建筑区。<br>"
            "<b>特点：</b>按距离和局部变化自适应加权，通常比简单均值更能保留边缘。<br>"
            "<b>推荐参数：</b>邻域半径 3、衰减系数 0.10。噪声较强可将半径增至 4；"
            "边缘被抹平时适当增大衰减系数，平滑不足时适当减小。");
        break;
    case 3:
        html = QStringLiteral(
            "<b>Gamma-MAP 滤波</b><br>"
            "<b>适合：</b>符合乘性 Gamma 噪声模型的强度图，尤其适合均匀到中等纹理区域。<br>"
            "<b>特点：</b>采用最大后验估计，在平滑和目标保持之间较稳健；模型或视数不准时效果会下降。<br>"
            "<b>推荐参数：</b>邻域半径 3；等效视数使用产品标称值，单视数据先用 1.0。"
            "均匀区可尝试半径 4，密集小目标区建议半径 2。");
        break;
    case 4:
        html = QStringLiteral(
            "<b>Kuan 滤波</b><br>"
            "<b>适合：</b>需要快速处理的普通单通道 SAR 强度图，以及轻到中等斑点噪声。<br>"
            "<b>特点：</b>通过变异系数把乘性噪声近似为局部线性估计，速度快、细节保持适中。<br>"
            "<b>推荐参数：</b>邻域半径 3；等效视数使用产品标称值，未知时先用 1.0。"
            "弱噪声或小目标场景可将半径降至 2。");
        break;
    default:
        html = QStringLiteral(
            "<b>BM3D</b><br>"
            "<b>适合：</b>纹理丰富、结构细节较多且斑点较强的单通道 SAR 显示图。<br>"
            "<b>特点：</b>寻找相似图块并进行协同滤波，细节保持通常较好，但计算量最大。<br>"
            "<b>推荐参数：</b>当前实现自动估计噪声，无需设置参数；邻域半径、等效视数和 Frost 衰减均不生效。");
        break;
    }
    methodDescriptionBrowser->setHtml(html);
}

void SpeckleDenoise::setFilterRunning(bool running)
{
    ui->runFilterButton->setEnabled(!running);
    ui->deleteFilterButton->setEnabled(!running);
    ui->loadImageButton->setEnabled(!running);
    ui->projectComboBox->setEnabled(!running);
    ui->nodeComboBox->setEnabled(!running);
    ui->inputImageComboBox->setEnabled(!running);
    ui->NodeWindowSpinBox->setEnabled(!running);
    filterMethodComboBox->setEnabled(!running);
    filterRadiusSpinBox->setEnabled(!running);
    filterLooksSpinBox->setEnabled(!running);
    frostDerampSpinBox->setEnabled(!running);

    ui->FilterProgressBar->show();
    if (running)
    {
        ui->FilterProgressBar->setRange(0, 0);
    }
    else
    {
        ui->FilterProgressBar->setRange(0, 100);
    }
}

void SpeckleDenoise::resetLoadedImageState()
{
    loaded_image_path.clear();
    originalGrayMat.release();
    filteredGrayMat.release();
    originalPixmap = QPixmap();
    filteredPixmap = QPixmap();
    currentRoiImageRect = QRect();
    roiSelecting = false;
    roiModeEnabled = false;

    updateRoiDisplay();
    clearEnlResults();
    ui->imageTypeComboBox->setCurrentText("Original");
    updateDisplayedImage();
}

void SpeckleDenoise::clearEnlResults()
{
    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");
}

QString SpeckleDenoise::inputFingerprintToken() const
{
    const QFileInfo info(input_image_path);
    if (!info.isFile()) {
        return QString();
    }

    QByteArray identity = QDir::cleanPath(info.absoluteFilePath()).toLower().toUtf8();
    identity += '|';
    identity += QByteArray::number(info.size());
    identity += '|';
    identity += QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(12));
}

bool SpeckleDenoise::refreshCurrentResult()
{
    if ((filterWatcher && filterWatcher->isRunning()) ||
        originalGrayMat.empty() || loaded_image_path != input_image_path ||
        save_path.isEmpty() || input_image_name.isEmpty()) {
        return false;
    }

    const QString outputNodeName = ui->NodeWindowSpinBox->text().trimmed();
    if (!isValidOutputNodeName(outputNodeName)) {
        filteredGrayMat.release();
        filteredPixmap = QPixmap();
        ui->imageTypeComboBox->setCurrentText("Original");
        clearEnlResults();
        updateDisplayedImage();
        return false;
    }

    QStringList candidateNames;
    candidateNames << currentOutputImageName();
    // Results produced before input fingerprints were added remain readable.
    const QString legacyName = QFileInfo(input_image_name).completeBaseName() +
                               currentFilterSuffix();
    if (!candidateNames.contains(legacyName)) {
        candidateNames << legacyName;
    }

    for (const QString& resultName : candidateNames) {
        const QString resultPath = QDir(QDir(save_path).filePath(outputNodeName))
                                       .filePath(resultName + ".jpg");
        if (!QFileInfo::exists(resultPath)) {
            continue;
        }

        cv::Mat resultGray = cv::imread(resultPath.toStdString(), cv::IMREAD_GRAYSCALE);
        QPixmap resultPixmap(resultPath);
        if (resultGray.empty() || resultPixmap.isNull()) {
            continue;
        }

        filteredGrayMat = resultGray;
        filteredPixmap = resultPixmap;
        ui->imageTypeComboBox->setCurrentText("Filtered");
        updateDisplayedImage();
        updateEnlResults();
        return true;
    }

    filteredGrayMat.release();
    filteredPixmap = QPixmap();
    ui->imageTypeComboBox->setCurrentText("Original");
    clearEnlResults();
    updateDisplayedImage();
    return false;
}

bool SpeckleDenoise::isValidOutputNodeName(const QString& name) const
{
    if (name.isEmpty() || name == "." || name == ".." || name.endsWith('.'))
    {
        return false;
    }

    const QString invalidCharacters = QStringLiteral("\\/:*?\"<>|");
    for (const QChar character : invalidCharacters)
    {
        if (name.contains(character))
        {
            return false;
        }
    }

    const QString deviceName = name.section('.', 0, 0).toUpper();
    static const char* reservedNames[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    for (const char* reservedName : reservedNames)
    {
        if (deviceName == reservedName)
        {
            return false;
        }
    }
    return true;
}

QString SpeckleDenoise::currentOutputImageName() const
{
    return QFileInfo(input_image_name).completeBaseName() + currentFilterSuffix() +
           "_I" + inputFingerprintToken();
}

cv::Mat SpeckleDenoise::runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const
{
    // BM3D 已统一由后台滤波任务调用 SARProcessor，此方法保留接口兼容。
    Q_UNUSED(imgNorm);
    Q_UNUSED(sigmaFinal);
    return cv::Mat();
}



bool SpeckleDenoise::saveFilteredImage(const cv::Mat& filteredImage,
                                       const QString& outputPath)
{
    if (filteredImage.empty() || outputPath.isEmpty())
    {
        return false;
    }

    QDir outputDirectory = QFileInfo(outputPath).dir();
    if (!outputDirectory.exists())
    {
        if (!outputDirectory.mkpath("."))
        {
            return false;
        }
    }

    if (QFile::exists(outputPath))
    {
        return false;
    }

    try
    {
        return cv::imwrite(outputPath.toStdString(), filteredImage);
    }
    catch (const cv::Exception&)
    {
        return false;
    }
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

    for (int i = 0; outputNode && i < outputNode->rowCount(); i++)
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

    if (!outputNode)
    {
        outputNode = new QStandardItem(outputNodeName);
        outputNode->setIcon(QIcon(FOLDER_ICON));
        project->appendRow(outputNode);

        QStandardItem* rank = new QStandardItem("complex-0.0");
        project->setChild(project->rowCount() - 1, 1, rank);
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

    QString outputNodeName = ui->NodeWindowSpinBox->text().trimmed();
    if (!isValidOutputNodeName(outputNodeName))
    {
        InSARLogManager::LogWarning("UI", "Invalid output node name.");
        QMessageBox::warning(this, "Warning!", "Invalid output node name.");
        return;
    }

    if (input_image_name.isEmpty())
    {
        InSARLogManager::LogWarning("UI", "Please select an input image first.");
        QMessageBox::warning(this, "Warning!", "Please select an input image first.");
        return;
    }

    const QString outputImageName = currentOutputImageName();
    const QString outputPath = QDir(QDir(save_path).filePath(outputNodeName))
                                   .filePath(outputImageName + ".jpg");

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

    const QString backupPath = outputPath + ".deleting";
    bool fileMovedToBackup = false;
    if (QFile::exists(outputPath))
    {
        if (QFile::exists(backupPath) || !QFile::rename(outputPath, backupPath))
        {
            InSARLogManager::LogWarning("UI", "Failed to prepare filtered image deletion.");
            QMessageBox::warning(this, "Warning!", "Failed to prepare filtered image deletion.");
            return;
        }
        fileMovedToBackup = true;
    }

    const auto restoreBackup = [&]() {
        return !fileMovedToBackup || QFile::rename(backupPath, outputPath);
    };

    XMLFile xml;
    if (xml.XMLFile_load((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        const bool restored = restoreBackup();
        InSARLogManager::LogWarning("UI", "Failed to load project XML.");
        QMessageBox::warning(
            this,
            "Warning!",
            restored
                ? QStringLiteral("工程 XML 加载失败，图像文件已恢复。")
                : QStringLiteral("工程 XML 加载失败，且图像文件恢复失败：\n%1").arg(backupPath));
        return;
    }

    const QString relativePath = "/" + outputNodeName + "/" + outputImageName + ".jpg";
    if (xml.XMLFile_remove_node(outputNodeName.toStdString().c_str(),
                                outputImageName.toStdString().c_str(),
                                relativePath.toStdString().c_str()) < 0)
    {
        const bool restored = restoreBackup();
        InSARLogManager::LogWarning("UI", "Failed to remove node from project XML.");
        QMessageBox::warning(
            this,
            "Warning!",
            restored
                ? QStringLiteral("工程 XML 删除失败，图像文件已恢复。")
                : QStringLiteral("工程 XML 删除失败，且图像文件恢复失败：\n%1").arg(backupPath));
        return;
    }

    if (xml.XMLFile_save((save_path + "/" + project_name).toStdString().c_str()) < 0)
    {
        const bool restored = restoreBackup();
        InSARLogManager::LogWarning("UI", "Failed to save project XML.");
        QMessageBox::warning(
            this,
            "Warning!",
            restored
                ? QStringLiteral("工程 XML 保存失败，图像文件已恢复。")
                : QStringLiteral("工程 XML 保存失败，且图像文件恢复失败：\n%1").arg(backupPath));
        return;
    }

    bool backupRemoved = true;
    if (fileMovedToBackup)
    {
        backupRemoved = QFile::remove(backupPath);
    }

    if (imageRow >= 0)
    {
        outputNode->removeRow(imageRow);
    }

    filteredPixmap = QPixmap();
    filteredGrayMat.release();
    ui->imageTypeComboBox->setCurrentText("Original");
    updateDisplayedImage();

    emit sendCopy(copy);

    if (backupRemoved)
    {
        QMessageBox::information(this, "Info", "Filtered image deleted.");
    }
    else
    {
        InSARLogManager::LogWarning("UI", "Filtered image metadata was deleted, but backup cleanup failed.");
        QMessageBox::warning(
            this,
            "Warning!",
            QStringLiteral("滤波结果记录已删除，但临时备份文件清理失败：\n%1").arg(backupPath));
    }
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
        return std::numeric_limits<double>::infinity();
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
    ui->originalEnlValueLabel->setText(formatEnlValue(originalEnl));

    if (!filteredGrayMat.empty())
    {
        cv::Rect filteredBounds(0, 0, filteredGrayMat.cols, filteredGrayMat.rows);
        cv::Rect filteredRoiRect = roi & filteredBounds;

        if (filteredRoiRect.width >= 2 && filteredRoiRect.height >= 2)
        {
            cv::Mat filteredRoi = filteredGrayMat(filteredRoiRect).clone();
            double filteredEnl = calculateEnl(filteredRoi);
            ui->filteredEnlValueLabel->setText(formatEnlValue(filteredEnl));
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

                    clearEnlResults();
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

                    clearEnlResults();
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
                updateEnlResults();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

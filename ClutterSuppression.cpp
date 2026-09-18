#include "ClutterSuppression.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QScrollArea>
#include <QFrame>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <vector>
#include "FormatConversion.h"
#include "icon_source.h"
#include "SARProcessor.h"

namespace
{
QString clutterParameterToken(double value)
{
    QString token = QString::number(value, 'g', 8);
    token.replace('-', 'm');
    token.replace('+', 'p');
    token.replace('.', 'd');
    return token;
}

bool isCfarMethod(ClutterSuppressionMethod method)
{
    switch (method)
    {
    case ClutterSuppressionMethod::CACFAR:
    case ClutterSuppressionMethod::ACCFAR:
    case ClutterSuppressionMethod::AAFCFAR:
    case ClutterSuppressionMethod::VICFAR:
    case ClutterSuppressionMethod::RmSATCFAR:
        return true;
    case ClutterSuppressionMethod::BM3D:
    case ClutterSuppressionMethod::MCA:
    default:
        return false;
    }
}

ClutterSuppressionResult runClutterFilterSnapshot(
    const cv::Mat& inputGray,
    const ClutterSuppressionParameters& parameters)
{
    ClutterSuppressionResult result;
    if (inputGray.empty()) {
        return result;
    }

    if (parameters.method == ClutterSuppressionMethod::BM3D) {
        cv::Mat output;
        if (SARProcessor::DenoiseGray(inputGray, 0.0, output) == 0) {
            result.suppressedImage = output;
        }
        return result;
    }

    return ClutterSuppressionAlgorithms::process(inputGray, parameters);
}
}


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
    imageDisplayLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    imageDisplayLabel->installEventFilter(this);
    ui->imageDisplayWidget->installEventFilter(this);

    QVBoxLayout* imageLayout = new QVBoxLayout(ui->imageDisplayWidget);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->addWidget(imageDisplayLabel);
    ui->imageDisplayWidget->setLayout(imageLayout);

    auto* descriptionGroup = new QGroupBox(QStringLiteral("方法说明"), this);
    descriptionGroup->setCheckable(true);
    descriptionGroup->setChecked(true);
    auto* descriptionLayout = new QVBoxLayout(descriptionGroup);
    descriptionLayout->setContentsMargins(8, 8, 8, 8);
    methodDescriptionBrowser = new QTextBrowser(descriptionGroup);
    methodDescriptionBrowser->setReadOnly(true);
    methodDescriptionBrowser->setOpenExternalLinks(false);
    methodDescriptionBrowser->setObjectName(QStringLiteral("methodDescriptionBrowser"));
    methodDescriptionBrowser->setFixedHeight(127);
    descriptionLayout->addWidget(methodDescriptionBrowser);
    connect(descriptionGroup, &QGroupBox::toggled,
            methodDescriptionBrowser, &QTextBrowser::setVisible);

    ui->projectComboBox->clear();
    ui->InputComboBox->clear();
    ui->inputImageComboBox->clear();

    ui->imageTypeComboBox->clear();
    ui->imageTypeComboBox->addItem("Original");
    ui->imageTypeComboBox->addItem("Filtered");
    ui->imageTypeComboBox->addItem("Target mask");

    methodComboBox = new QComboBox(ui->widget_2);
    const ClutterSuppressionMethod methods[] = {
        ClutterSuppressionMethod::BM3D,
        ClutterSuppressionMethod::CACFAR,
        ClutterSuppressionMethod::ACCFAR,
        ClutterSuppressionMethod::AAFCFAR,
        ClutterSuppressionMethod::VICFAR,
        ClutterSuppressionMethod::RmSATCFAR,
        ClutterSuppressionMethod::MCA
    };
    for (ClutterSuppressionMethod method : methods)
        methodComboBox->addItem(ClutterSuppressionAlgorithms::methodName(method),
                                static_cast<int>(method));
    methodComboBox->setCurrentIndex(methodComboBox->findData(
        static_cast<int>(ClutterSuppressionMethod::RmSATCFAR)));

    guardRadiusSpinBox = new QSpinBox(ui->widget_2);
    guardRadiusSpinBox->setRange(0, 32);
    guardRadiusSpinBox->setValue(10);
    clutterRadiusSpinBox = new QSpinBox(ui->widget_2);
    clutterRadiusSpinBox->setRange(1, 128);
    clutterRadiusSpinBox->setValue(16);
    pfaSpinBox = new QDoubleSpinBox(ui->widget_2);
    pfaSpinBox->setDecimals(8);
    pfaSpinBox->setRange(1e-8, 0.1);
    pfaSpinBox->setSingleStep(0.0001);
    pfaSpinBox->setValue(0.00001);
    pfaSpinBox->setToolTip("Probability of false alarm");
    censoringSpinBox = new QDoubleSpinBox(ui->widget_2);
    censoringSpinBox->setDecimals(2);
    censoringSpinBox->setRange(0.01, 0.45);
    censoringSpinBox->setSingleStep(0.05);
    censoringSpinBox->setValue(0.01);
    mixtureCountSpinBox = new QSpinBox(ui->widget_2);
    mixtureCountSpinBox->setRange(1, 4);
    mixtureCountSpinBox->setValue(1);

    mcaPatchSizeSpinBox = new QSpinBox(ui->widget_2);
    mcaPatchSizeSpinBox->setRange(8, 32);
    mcaPatchSizeSpinBox->setValue(20);
    mcaPatchSizeSpinBox->setToolTip(QStringLiteral("稀疏字典使用的方形图块边长"));
    mcaPatchStrideSpinBox = new QSpinBox(ui->widget_2);
    mcaPatchStrideSpinBox->setRange(1, 20);
    mcaPatchStrideSpinBox->setValue(15);
    mcaPatchStrideSpinBox->setToolTip(QStringLiteral("相邻图块的采样步长；越小越平滑但越慢"));
    mcaSparsitySpinBox = new QSpinBox(ui->widget_2);
    mcaSparsitySpinBox->setRange(1, 20);
    mcaSparsitySpinBox->setValue(10);
    mcaSparsitySpinBox->setToolTip(QStringLiteral("OMP 每个图块最多使用的字典原子数"));
    mcaIterationsSpinBox = new QSpinBox(ui->widget_2);
    mcaIterationsSpinBox->setRange(5, 40);
    mcaIterationsSpinBox->setValue(15);
    mcaIterationsSpinBox->setToolTip(QStringLiteral("MCA 分量交替更新次数"));
    mcaThresholdSpinBox = new QDoubleSpinBox(ui->widget_2);
    mcaThresholdSpinBox->setDecimals(1);
    mcaThresholdSpinBox->setRange(0.5, 20.0);
    mcaThresholdSpinBox->setSingleStep(0.5);
    mcaThresholdSpinBox->setValue(4.0);
    mcaThresholdSpinBox->setToolTip(QStringLiteral("迭代末期的方向系数阈值"));
    mcaTvGammaSpinBox = new QDoubleSpinBox(ui->widget_2);
    mcaTvGammaSpinBox->setDecimals(1);
    mcaTvGammaSpinBox->setRange(0.0, 20.0);
    mcaTvGammaSpinBox->setSingleStep(0.5);
    mcaTvGammaSpinBox->setValue(6.0);
    mcaTvGammaSpinBox->setToolTip(QStringLiteral("Haar-TV 软阈值强度；越大抑制越强"));
    mcaScalesSpinBox = new QSpinBox(ui->widget_2);
    mcaScalesSpinBox->setRange(1, 6);
    mcaScalesSpinBox->setValue(4);
    mcaScalesSpinBox->setToolTip(QStringLiteral("多尺度方向分解的尺度数"));
    mcaAnglesSpinBox = new QSpinBox(ui->widget_2);
    mcaAnglesSpinBox->setRange(2, 32);
    mcaAnglesSpinBox->setSingleStep(2);
    mcaAnglesSpinBox->setValue(8);
    mcaAnglesSpinBox->setToolTip(QStringLiteral("每个尺度使用的方向数"));

    connect(mcaPatchSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
                mcaPatchStrideSpinBox->setMaximum(value);
                refreshCurrentResult();
            });

    recommendedParametersButton = new QPushButton(
        QStringLiteral("应用 SSDD 实测推荐参数"), ui->filterGroup);
    recommendedParametersButton->setToolTip(QStringLiteral(
        "使用 341 张船舶正样本和 307 张海面负样本验证得到的 128×128 切片参数起点"));
    connect(recommendedParametersButton, &QPushButton::clicked,
            this, &ClutterSuppression::applyRecommendedParameters);

    connect(methodComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateMethodControls();
                refreshCurrentResult();
            });
    updateMethodControls();

    filterWatcher = new QFutureWatcher<ClutterSuppressionResult>(this);
    connect(filterWatcher,
            &QFutureWatcher<ClutterSuppressionResult>::finished,
            this,
            &ClutterSuppression::onFilterFinished);
    connect(guardRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(clutterRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(pfaSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshCurrentResult(); });
    connect(censoringSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshCurrentResult(); });
    connect(mixtureCountSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(mcaPatchStrideSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(mcaSparsitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(mcaIterationsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(mcaThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshCurrentResult(); });
    connect(mcaTvGammaSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshCurrentResult(); });
    connect(mcaScalesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(mcaAnglesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refreshCurrentResult(); });
    connect(ui->NodeWindowSpinBox, &QLineEdit::editingFinished,
            this, [this]() { refreshCurrentResult(); });

    // Rebuild the dialog as the same responsive workspace used by the
    // speckle-denoise dialog, while keeping all processing logic independent.
    QWidget* legacyContainer =
        findChild<QWidget*>("layoutWidget", Qt::FindDirectChildrenOnly);

    auto* dataLayout = new QGridLayout(ui->dataGroup);
    dataLayout->setContentsMargins(12, 10, 12, 10);
    dataLayout->setHorizontalSpacing(10);
    dataLayout->setVerticalSpacing(8);
    dataLayout->addWidget(new QLabel(QStringLiteral("工程："), ui->dataGroup), 0, 0);
    dataLayout->addWidget(ui->projectComboBox, 0, 1);
    dataLayout->addWidget(new QLabel(QStringLiteral("输入节点："), ui->dataGroup), 0, 2);
    dataLayout->addWidget(ui->InputComboBox, 0, 3);
    dataLayout->addWidget(new QLabel(QStringLiteral("输入影像："), ui->dataGroup), 1, 0);
    dataLayout->addWidget(ui->inputImageComboBox, 1, 1, 1, 3);
    dataLayout->addWidget(new QLabel(QStringLiteral("输出节点："), ui->dataGroup), 2, 0);
    dataLayout->addWidget(ui->NodeWindowSpinBox, 2, 1, 1, 2);
    dataLayout->addWidget(ui->loadImageButton, 2, 3);
    dataLayout->setColumnStretch(1, 2);
    dataLayout->setColumnStretch(3, 3);
    ui->dataGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    ui->dataWidget->hide();

    auto* displayLayout = new QVBoxLayout(ui->imageDisplayGroup);
    displayLayout->setContentsMargins(4, 4, 4, 4);
    displayLayout->addWidget(ui->imageDisplayWidget);
    ui->imageDisplayWidget->setMinimumSize(560, 370);
    ui->imageDisplayWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    imageTabBar = new QTabBar(this);
    imageTabBar->setDocumentMode(true);
    imageTabBar->setExpanding(false);
    imageTabBar->addTab(QStringLiteral("原图"));
    imageTabBar->addTab(QStringLiteral("抑制结果"));
    imageTabBar->addTab(QStringLiteral("目标掩膜"));
    imageTabBar->setTabToolTip(
        2, QStringLiteral("CFAR 检测结果：白色表示判定目标，黑色表示背景"));
    imageTabBar->setTabEnabled(1, false);
    imageTabBar->setTabEnabled(2, false);
    connect(imageTabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (ui->imageTypeComboBox->currentIndex() != index)
            ui->imageTypeComboBox->setCurrentIndex(index);
    });
    connect(ui->imageTypeComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            imageTabBar, &QTabBar::setCurrentIndex);
    ui->imageToolbarWidget->hide();

    auto* roiLayout = new QVBoxLayout(ui->roiGroup);
    roiLayout->setContentsMargins(8, 5, 8, 5);
    roiLayout->setSpacing(5);
    clutterRoiHintLabel = new QLabel(
        QStringLiteral("<b>背景框选提醒：</b>请在目标附近选择同类背景，避免距离过远，"
                       "也不要跨越海陆、岸线等明显边界。参数不确定时可使用右侧 SSDD 实测推荐值。"),
        ui->roiGroup);
    clutterRoiHintLabel->setWordWrap(true);
    clutterRoiHintLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: #fff3cd; border: 1px solid #e0b84f; "
        "border-radius: 3px; padding: 4px 7px; color: #5f4b00; }"));
    clutterRoiHintLabel->setVisible(
        isCfarMethod(currentParameters().method));
    roiLayout->addWidget(clutterRoiHintLabel);

    auto* roiButtonLayout = new QHBoxLayout();
    roiButtonLayout->setSpacing(6);
    roiButtonLayout->addWidget(ui->startRoiButton);
    roiButtonLayout->addWidget(ui->clearRoiButton);
    roiButtonLayout->addWidget(ui->startRoiButton_2);
    roiButtonLayout->addWidget(ui->clearRoiButton_2);
    roiLayout->addLayout(roiButtonLayout);
    auto* roiLegend = new QLabel(
        QStringLiteral("<span style='color:#d32f2f'>■ 目标区域</span>　"
                       "<span style='color:#2e7d32'>■ 杂波区域</span>"),
        ui->roiGroup);
    roiLayout->addWidget(roiLegend);
    auto* roiInfoLayout = new QHBoxLayout();
    roiInfoLayout->setSpacing(4);
    roiInfoLayout->addWidget(new QLabel(QStringLiteral("左上："), ui->roiGroup));
    roiInfoLayout->addWidget(ui->roiTopLeftValueLabel);
    roiInfoLayout->addSpacing(10);
    roiInfoLayout->addWidget(new QLabel(QStringLiteral("尺寸："), ui->roiGroup));
    roiInfoLayout->addWidget(ui->label_4);
    roiInfoLayout->addWidget(new QLabel(QStringLiteral("×"), ui->roiGroup));
    roiInfoLayout->addWidget(ui->roiHeightValueLabel);
    roiInfoLayout->addSpacing(10);
    roiInfoLayout->addWidget(new QLabel(QStringLiteral("像素："), ui->roiGroup));
    roiInfoLayout->addWidget(ui->roiPixelCountValueLabel);
    roiInfoLayout->addStretch();
    roiLayout->addLayout(roiInfoLayout);
    ui->roiGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    ui->roiGroup->setStyleSheet(QStringLiteral("QGroupBox QLabel { font-size: 11px; }"));
    ui->widget->hide();

    auto* filterLayout = new QFormLayout(ui->filterGroup);
    filterLayout->setContentsMargins(10, 8, 10, 8);
    filterLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    filterLayout->addRow(QStringLiteral("方法："), methodComboBox);
    filterLayout->addRow(QStringLiteral("保护半径："), guardRadiusSpinBox);
    filterLayout->addRow(QStringLiteral("杂波半径："), clutterRadiusSpinBox);
    filterLayout->addRow(QStringLiteral("虚警概率 Pfa："), pfaSpinBox);
    filterLayout->addRow(QStringLiteral("删失比例："), censoringSpinBox);
    filterLayout->addRow(QStringLiteral("混合分量数："), mixtureCountSpinBox);
    filterLayout->addRow(QStringLiteral("图块大小："), mcaPatchSizeSpinBox);
    filterLayout->addRow(QStringLiteral("图块步长："), mcaPatchStrideSpinBox);
    filterLayout->addRow(QStringLiteral("OMP 稀疏度："), mcaSparsitySpinBox);
    filterLayout->addRow(QStringLiteral("MCA 迭代次数："), mcaIterationsSpinBox);
    filterLayout->addRow(QStringLiteral("终止阈值："), mcaThresholdSpinBox);
    filterLayout->addRow(QStringLiteral("TV 强度 γ："), mcaTvGammaSpinBox);
    filterLayout->addRow(QStringLiteral("方向尺度数："), mcaScalesSpinBox);
    filterLayout->addRow(QStringLiteral("每尺度方向数："), mcaAnglesSpinBox);
    filterLayout->addRow(recommendedParametersButton);
    ui->widget_2->hide();

    const auto oldResultContainers =
        ui->enlGroup->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : oldResultContainers)
        child->hide();

    auto* resultLayout = new QFormLayout(ui->enlGroup);
    resultLayout->setContentsMargins(10, 8, 10, 8);
    resultLayout->addRow(QStringLiteral("原图 SCR（dB）："), ui->originalEnlValueLabel);
    resultLayout->addRow(QStringLiteral("处理后 SCR（dB）："), ui->filteredEnlValueLabel);
    resultLayout->addRow(QStringLiteral("SCR 提升（线性）："), ui->beishu);
    ui->beishu->setToolTip(QStringLiteral(
        "先将 SCR 从 dB 还原为线性比值，再计算相对提升百分比"));

    auto* viewerPanel = new QWidget(this);
    auto* viewerLayout = new QVBoxLayout(viewerPanel);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(8);
    viewerLayout->addWidget(imageTabBar);
    viewerLayout->addWidget(ui->imageDisplayGroup, 1);
    viewerLayout->addWidget(ui->roiGroup);

    auto* parameterPanel = new QWidget(this);
    auto* parameterLayout = new QVBoxLayout(parameterPanel);
    parameterLayout->setContentsMargins(0, 0, 4, 0);
    parameterLayout->setSpacing(8);
    parameterLayout->addWidget(ui->filterGroup);
    parameterLayout->addWidget(descriptionGroup);
    parameterLayout->addWidget(ui->enlGroup);
    parameterLayout->addStretch();

    auto* parameterScroll = new QScrollArea(this);
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    parameterScroll->setWidget(parameterPanel);
    parameterScroll->setMinimumWidth(330);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(viewerPanel);
    splitter->addWidget(parameterScroll);
    splitter->setStretchFactor(0, 8);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({870, 330});

    auto* footerLayout = new QHBoxLayout();
    auto* processHint = new QLabel(
        QStringLiteral("处理在后台执行，完成后自动显示对应结果。"), this);
    processHint->setObjectName(QStringLiteral("secondaryHintLabel"));
    footerLayout->addWidget(processHint, 1);
    ui->deleteFilterButton->setMinimumWidth(120);
    ui->runFilterButton->setMinimumWidth(120);
    footerLayout->addWidget(ui->deleteFilterButton);
    footerLayout->addWidget(ui->runFilterButton);

    if (legacyContainer)
        legacyContainer->hide();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 12, 14, 12);
    rootLayout->setSpacing(10);
    rootLayout->addWidget(ui->dataGroup);
    rootLayout->addWidget(splitter, 1);
    rootLayout->addLayout(footerLayout);

    resize(1280, 840);
    setMinimumSize(1000, 700);

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
    loaded_image_path = input_image_path;
    refreshCurrentResult();
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
    resetLoadedImageState();

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
    if (!imageDisplayLabel || imageDisplayUpdateInProgress)
    {
        return;
    }
    QScopedValueRollback<bool> updateGuard(imageDisplayUpdateInProgress, true);

    const bool hasFilteredResult = !filteredPixmap.isNull();
    const bool hasTargetMask = !targetMaskPixmap.isNull();
    int selectedIndex = ui->imageTypeComboBox->currentIndex();
    if ((!hasFilteredResult && selectedIndex == 1) ||
        (!hasTargetMask && selectedIndex == 2))
    {
        selectedIndex = 0;
        const QSignalBlocker comboBlocker(ui->imageTypeComboBox);
        ui->imageTypeComboBox->setCurrentIndex(selectedIndex);
    }
    if (imageTabBar)
    {
        const QSignalBlocker tabBlocker(imageTabBar);
        imageTabBar->setTabEnabled(1, hasFilteredResult);
        imageTabBar->setTabEnabled(2, hasTargetMask);
        imageTabBar->setCurrentIndex(selectedIndex);
    }

    QPixmap pixmap;

    if (selectedIndex == 0)
    {
        pixmap = originalPixmap;
    }
    else if (selectedIndex == 1)
    {
        pixmap = filteredPixmap;
    }
    else if (selectedIndex == 2)
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
    clearScrResults();
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
    clearScrResults();
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
    if (watched == ui->imageDisplayWidget && event->type() == QEvent::Resize &&
        !imageDisplayRefreshPending)
    {
        imageDisplayRefreshPending = true;
        QTimer::singleShot(0, this, [this]() {
            imageDisplayRefreshPending = false;
            updateDisplayedImage();
        });
    }

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
                    clearScrResults();

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
                    clearScrResults();

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
            const double linearGain = std::pow(
                10.0, (filteredScr - originalScr) / 20.0);
            if (std::isfinite(linearGain))
            {
                const double improvementPercent = (linearGain - 1.0) * 100.0;
                ui->beishu->setText(
                    QStringLiteral("%1%").arg(QString::number(improvementPercent, 'f', 2)));
            }
            else
            {
                ui->beishu->setText("--");
            }
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
    parameters.mcaPatchSize = mcaPatchSizeSpinBox->value();
    parameters.mcaPatchStride = mcaPatchStrideSpinBox->value();
    parameters.mcaSparsity = mcaSparsitySpinBox->value();
    parameters.mcaIterations = mcaIterationsSpinBox->value();
    parameters.mcaTerminalThreshold = mcaThresholdSpinBox->value();
    parameters.mcaTvGamma = mcaTvGammaSpinBox->value();
    parameters.mcaCurveletScales = mcaScalesSpinBox->value();
    parameters.mcaCurveletAngles = mcaAnglesSpinBox->value();
    return parameters;
}

QString ClutterSuppression::currentMethodSuffix() const
{
    return QString::fromLatin1(ClutterSuppressionAlgorithms::methodSuffix(currentParameters().method));
}

QString ClutterSuppression::inputFingerprintToken() const
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

QString ClutterSuppression::currentOutputImageName() const
{
    const ClutterSuppressionParameters parameters = currentParameters();
    QString parameterSuffix;
    if (isCfarMethod(parameters.method)) {
        parameterSuffix = QString("_G%1_C%2_P%3")
            .arg(parameters.guardRadius)
            .arg(parameters.clutterRadius)
            .arg(clutterParameterToken(parameters.probabilityFalseAlarm));
    }
    if (parameters.method == ClutterSuppressionMethod::ACCFAR) {
        parameterSuffix += "_X" + clutterParameterToken(parameters.censoringFraction);
    }
    if (parameters.method == ClutterSuppressionMethod::RmSATCFAR) {
        parameterSuffix += QString("_M%1").arg(parameters.maximumMixtureCount);
    }
    if (parameters.method == ClutterSuppressionMethod::MCA) {
        parameterSuffix = QString("_B%1_S%2_K%3_I%4_T%5_V%6_CS%7_CA%8")
            .arg(parameters.mcaPatchSize)
            .arg(parameters.mcaPatchStride)
            .arg(parameters.mcaSparsity)
            .arg(parameters.mcaIterations)
            .arg(clutterParameterToken(parameters.mcaTerminalThreshold))
            .arg(clutterParameterToken(parameters.mcaTvGamma))
            .arg(parameters.mcaCurveletScales)
            .arg(parameters.mcaCurveletAngles);
    }
    return QFileInfo(input_image_name).completeBaseName() + "_" +
           currentMethodSuffix() + parameterSuffix + "_I" +
           inputFingerprintToken() + "_Clutter";
}

void ClutterSuppression::clearScrResults()
{
    ui->originalEnlValueLabel->setText("--");
    ui->filteredEnlValueLabel->setText("--");
    ui->beishu->setText("--");
}

bool ClutterSuppression::refreshCurrentResult()
{
    if ((filterWatcher && filterWatcher->isRunning()) ||
        originalGrayMat.empty() || loaded_image_path != input_image_path ||
        save_path.isEmpty() || input_image_name.isEmpty()) {
        return false;
    }

    const QString outputNodeName = ui->NodeWindowSpinBox->text().trimmed();
    if (outputNodeName.isEmpty()) {
        filteredGrayMat.release();
        targetMaskMat.release();
        filteredPixmap = QPixmap();
        targetMaskPixmap = QPixmap();
        ui->imageTypeComboBox->setCurrentText("Original");
        clearScrResults();
        updateDisplayedImage();
        return false;
    }
    QStringList candidateNames;
    candidateNames << currentOutputImageName();
    if (currentParameters().method == ClutterSuppressionMethod::BM3D) {
        candidateNames << QFileInfo(input_image_name).completeBaseName() +
                          "_BM3D_Clutter";
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
        const QString maskPath = QDir(QFileInfo(resultPath).absolutePath())
                                     .filePath(QFileInfo(resultPath).completeBaseName() + "_mask.png");
        targetMaskMat = cv::imread(maskPath.toStdString(), cv::IMREAD_GRAYSCALE);
        targetMaskPixmap = QPixmap(maskPath);
        ui->imageTypeComboBox->setCurrentText("Filtered");
        updateDisplayedImage();
        updateScrResults();
        return true;
    }

    filteredGrayMat.release();
    targetMaskMat.release();
    filteredPixmap = QPixmap();
    targetMaskPixmap = QPixmap();
    ui->imageTypeComboBox->setCurrentText("Original");
    clearScrResults();
    updateDisplayedImage();
    return false;
}

void ClutterSuppression::resetLoadedImageState()
{
    loaded_image_path.clear();
    originalGrayMat.release();
    filteredGrayMat.release();
    targetMaskMat.release();
    originalPixmap = QPixmap();
    filteredPixmap = QPixmap();
    targetMaskPixmap = QPixmap();
    targetRoiImageRect = QRect();
    clutterRoiImageRect = QRect();
    roiSelecting = false;
    targetRoiModeEnabled = false;
    clutterRoiModeEnabled = false;
    {
        const QSignalBlocker comboBlocker(ui->imageTypeComboBox);
        ui->imageTypeComboBox->setCurrentIndex(0);
    }
    if (imageTabBar)
    {
        const QSignalBlocker tabBlocker(imageTabBar);
        imageTabBar->setCurrentIndex(0);
        imageTabBar->setTabEnabled(1, false);
        imageTabBar->setTabEnabled(2, false);
    }
    clearScrResults();
    updateCurrentRoiDisplay();
    updateDisplayedImage();
}

void ClutterSuppression::updateMethodControls()
{
    const ClutterSuppressionMethod method = currentParameters().method;
    const bool isCfar = isCfarMethod(method);
    const bool isMca = method == ClutterSuppressionMethod::MCA;
    QFormLayout* form = qobject_cast<QFormLayout*>(ui->filterGroup->layout());
    const auto setFieldVisible = [form](QWidget* field, bool visible) {
        field->setVisible(visible);
        if (form) {
            if (QWidget* label = form->labelForField(field))
                label->setVisible(visible);
        }
    };

    setFieldVisible(guardRadiusSpinBox, isCfar);
    setFieldVisible(clutterRadiusSpinBox, isCfar);
    setFieldVisible(pfaSpinBox, isCfar);
    setFieldVisible(censoringSpinBox,
                    method == ClutterSuppressionMethod::ACCFAR);
    setFieldVisible(mixtureCountSpinBox,
                    method == ClutterSuppressionMethod::RmSATCFAR);
    setFieldVisible(mcaPatchSizeSpinBox, isMca);
    setFieldVisible(mcaPatchStrideSpinBox, isMca);
    setFieldVisible(mcaSparsitySpinBox, isMca);
    setFieldVisible(mcaIterationsSpinBox, isMca);
    setFieldVisible(mcaThresholdSpinBox, isMca);
    setFieldVisible(mcaTvGammaSpinBox, isMca);
    setFieldVisible(mcaScalesSpinBox, isMca);
    setFieldVisible(mcaAnglesSpinBox, isMca);

    guardRadiusSpinBox->setEnabled(isCfar);
    clutterRadiusSpinBox->setEnabled(isCfar);
    pfaSpinBox->setEnabled(isCfar);
    censoringSpinBox->setEnabled(method == ClutterSuppressionMethod::ACCFAR);
    mixtureCountSpinBox->setEnabled(method == ClutterSuppressionMethod::RmSATCFAR);
    mcaPatchSizeSpinBox->setEnabled(isMca);
    mcaPatchStrideSpinBox->setEnabled(isMca);
    mcaSparsitySpinBox->setEnabled(isMca);
    mcaIterationsSpinBox->setEnabled(isMca);
    mcaThresholdSpinBox->setEnabled(isMca);
    mcaTvGammaSpinBox->setEnabled(isMca);
    mcaScalesSpinBox->setEnabled(isMca);
    mcaAnglesSpinBox->setEnabled(isMca);
    if (clutterRoiHintLabel)
        clutterRoiHintLabel->setVisible(isCfar);
    if (recommendedParametersButton)
        recommendedParametersButton->setEnabled(isCfar || isMca);
    updateMethodDescription();
}

void ClutterSuppression::applyRecommendedParameters()
{
    const QSignalBlocker guardBlocker(guardRadiusSpinBox);
    const QSignalBlocker clutterBlocker(clutterRadiusSpinBox);
    const QSignalBlocker pfaBlocker(pfaSpinBox);
    const QSignalBlocker censorBlocker(censoringSpinBox);
    const QSignalBlocker mixtureBlocker(mixtureCountSpinBox);
    const QSignalBlocker mcaPatchBlocker(mcaPatchSizeSpinBox);
    const QSignalBlocker mcaStrideBlocker(mcaPatchStrideSpinBox);
    const QSignalBlocker mcaSparsityBlocker(mcaSparsitySpinBox);
    const QSignalBlocker mcaIterationsBlocker(mcaIterationsSpinBox);
    const QSignalBlocker mcaThresholdBlocker(mcaThresholdSpinBox);
    const QSignalBlocker mcaTvBlocker(mcaTvGammaSpinBox);
    const QSignalBlocker mcaScalesBlocker(mcaScalesSpinBox);
    const QSignalBlocker mcaAnglesBlocker(mcaAnglesSpinBox);

    switch (currentParameters().method)
    {
    case ClutterSuppressionMethod::CACFAR:
        guardRadiusSpinBox->setValue(32);
        clutterRadiusSpinBox->setValue(12);
        pfaSpinBox->setValue(0.000001);
        break;
    case ClutterSuppressionMethod::ACCFAR:
        guardRadiusSpinBox->setValue(16);
        clutterRadiusSpinBox->setValue(8);
        pfaSpinBox->setValue(0.00000001);
        censoringSpinBox->setValue(0.01);
        break;
    case ClutterSuppressionMethod::AAFCFAR:
        guardRadiusSpinBox->setValue(24);
        clutterRadiusSpinBox->setValue(12);
        pfaSpinBox->setValue(0.000001);
        break;
    case ClutterSuppressionMethod::VICFAR:
        guardRadiusSpinBox->setValue(32);
        clutterRadiusSpinBox->setValue(28);
        pfaSpinBox->setValue(0.00001);
        break;
    case ClutterSuppressionMethod::RmSATCFAR:
        guardRadiusSpinBox->setValue(10);
        clutterRadiusSpinBox->setValue(16);
        pfaSpinBox->setValue(0.00001);
        mixtureCountSpinBox->setValue(1);
        break;
    case ClutterSuppressionMethod::MCA:
        mcaPatchSizeSpinBox->setValue(20);
        mcaPatchStrideSpinBox->setMaximum(20);
        mcaPatchStrideSpinBox->setValue(15);
        mcaSparsitySpinBox->setValue(10);
        mcaIterationsSpinBox->setValue(15);
        mcaThresholdSpinBox->setValue(4.0);
        mcaTvGammaSpinBox->setValue(6.0);
        mcaScalesSpinBox->setValue(4);
        mcaAnglesSpinBox->setValue(8);
        break;
    case ClutterSuppressionMethod::BM3D:
    default:
        return;
    }

    updateMethodControls();
    refreshCurrentResult();
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
            "<b>特点：</b>用周围训练单元的平均功率估计杂波，速度快、适合作为基线；海岸和强杂波边缘容易虚警。");
        break;
    case ClutterSuppressionMethod::ACCFAR:
        html = QStringLiteral(
            "<b>AC-CFAR</b><br>"
            "<b>适合：</b>训练窗口内含其他目标、亮散射点或少量异常值的场景。<br>"
            "<b>特点：</b>先删去较亮的异常训练单元，再估计杂波，可减少邻近目标对门限的抬高。");
        break;
    case ClutterSuppressionMethod::AAFCFAR:
        html = QStringLiteral(
            "<b>AAF-CFAR</b><br>"
            "<b>适合：</b>高分辨率、目标密集或局部统计变化明显的 SAR 图像。<br>"
            "<b>特点：</b>根据局部均值和方差自动剔除异常训练单元，无需固定删失比例，兼顾速度和复杂背景适应性。");
        break;
    case ClutterSuppressionMethod::VICFAR:
        html = QStringLiteral(
            "<b>VI-CFAR</b><br>"
            "<b>适合：</b>海岸线、港口、岛礁和地物边界等非均匀杂波区域。<br>"
            "<b>特点：</b>利用局部变化指数识别杂波边缘，并采用更保守的背景估计，通常能降低边界虚警。");
        break;
    case ClutterSuppressionMethod::RmSATCFAR:
        html = QStringLiteral(
            "<b>RmSAT-CFAR</b><br>"
            "<b>适合：</b>包含多种杂波分布的复杂场景，如海陆混合区、港区和强度变化明显的海面。<br>"
            "<b>特点：</b>以 Rayleigh 混合模型描述多峰杂波，并通过局部积分统计加速；适应性强但计算量较大。");
        break;
    case ClutterSuppressionMethod::MCA:
        html = QStringLiteral(
            "<b>MCA方法</b><br>"
            "<b>适合：</b>从结构化、方向性明显的复杂杂波中分离舰船等亮目标。<br>"
            "<b>特点：</b>利用多尺度方向分解与稀疏字典交替分离结构目标和背景杂波，"
            "对具有明显边缘、方向和形状特征的舰船目标保持较好，但计算量高于 CFAR。<br>"
            "<b>推荐参数：</b>图块 20、步长 15、OMP 稀疏度 10、迭代 15 次、"
            "终止阈值 4.0、TV 强度 6.0、尺度数 4、方向数 8。<br>"
            "<b>参数：</b>迭代次数和方向数越大通常分离更充分但耗时更长；"
            "终止阈值与 TV 强度越大，杂波抑制越强，也更可能削弱弱目标。"
        );
        break;
    case ClutterSuppressionMethod::BM3D:
    default:
        html = QStringLiteral(
            "<b>BM3D（原方法）</b><br>"
            "<b>适合：</b>需要整体降低颗粒噪声、改善视觉质量的 SAR 灰度显示图。<br>"
            "<b>特点：</b>属于通用图像降噪，不建立目标附近的杂波统计门限；纹理保持较好但计算较慢。<br>"
            "<b>推荐参数：</b>当前实现自动估计噪声，无需设置 CFAR 参数。适合观察和传统 SCR 对比；"
            "如果重点是目标检测，优先选择 CFAR 方法。");
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
    if (filterWatcher && filterWatcher->isRunning())
    {
        return;
    }

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

    if (refreshCurrentResult())
    {
        return;
    }

    pending_output_node_name = outputNodeName;
    pending_output_image_name = currentOutputImageName();
    pending_output_path = QDir(QDir(save_path).filePath(outputNodeName))
                              .filePath(pending_output_image_name + ".jpg");

    const cv::Mat inputSnapshot = inputGray.clone();
    const ClutterSuppressionParameters parameterSnapshot = currentParameters();
    setFilterRunning(true);
    filterWatcher->setFuture(QtConcurrent::run(
        [inputSnapshot, parameterSnapshot]() {
            return runClutterFilterSnapshot(inputSnapshot, parameterSnapshot);
        }));
}

void ClutterSuppression::onFilterFinished()
{
    const ClutterSuppressionResult result = filterWatcher->result();
    setFilterRunning(false);

    const cv::Mat filteredImage = result.suppressedImage;
    if (filteredImage.empty())
    {
        InSARLogManager::LogWarning("UI", "Clutter suppression failed.");
        QMessageBox::warning(this, "Warning!", "Clutter suppression failed.");
        return;
    }

    targetMaskMat = result.targetMask.clone();

    const QString outputNodeName = pending_output_node_name;
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
        QFile::remove(outputPath);
        const QFileInfo outputInfo(outputPath);
        QFile::remove(outputInfo.absolutePath() + "/" +
                      outputInfo.completeBaseName() + "_mask.png");
        InSARLogManager::LogWarning("UI", "Failed to register filtered image.");
        QMessageBox::warning(this, "Warning!", "Failed to register filtered image.");
        return;
    }

    filteredGrayMat = filteredImage.clone();
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

    emit sendCopy(copy);
}

void ClutterSuppression::setFilterRunning(bool running)
{
    ui->runFilterButton->setEnabled(!running);
    ui->runFilterButton->setText(running ? QStringLiteral("处理中...")
                                         : QStringLiteral("开始滤波"));
    ui->deleteFilterButton->setEnabled(!running);
    ui->loadImageButton->setEnabled(!running);
    ui->projectComboBox->setEnabled(!running);
    ui->InputComboBox->setEnabled(!running);
    ui->inputImageComboBox->setEnabled(!running);
    ui->NodeWindowSpinBox->setEnabled(!running);
    methodComboBox->setEnabled(!running);
    guardRadiusSpinBox->setEnabled(!running);
    clutterRadiusSpinBox->setEnabled(!running);
    pfaSpinBox->setEnabled(!running);
    censoringSpinBox->setEnabled(!running);
    mixtureCountSpinBox->setEnabled(!running);
    mcaPatchSizeSpinBox->setEnabled(!running);
    mcaPatchStrideSpinBox->setEnabled(!running);
    mcaSparsitySpinBox->setEnabled(!running);
    mcaIterationsSpinBox->setEnabled(!running);
    mcaThresholdSpinBox->setEnabled(!running);
    mcaTvGammaSpinBox->setEnabled(!running);
    mcaScalesSpinBox->setEnabled(!running);
    mcaAnglesSpinBox->setEnabled(!running);

    if (!running) {
        updateMethodControls();
    }
}

bool ClutterSuppression::saveFilteredImage(const cv::Mat& filteredImage,
                                           QString& outputPath,
                                           QString& outputImageName)
{
    QString outputNodeName = pending_output_node_name;
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

    outputImageName = pending_output_image_name;
    outputPath = pending_output_path;

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

    QString outputImageName = currentOutputImageName();
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

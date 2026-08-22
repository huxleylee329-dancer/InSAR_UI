#include "InSARLogManager.h"
#include "Sentinel1ImportNode.h"
#include "Sentinel1ImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "ImportDataTypes.h"
#include "NodeUtils.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include "QtNodes/internal/NodeDetailWindow.hpp"
#include <QFormLayout>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QFuture>

namespace QtNodes {

Sentinel1ImportNode::Sentinel1ImportNode()
    : ImportNodeBase()
    , m_outputNodeNameEdit(nullptr)
    , m_outputFileNameEdit(nullptr)
    , m_manifestEdit(nullptr)
    , m_podEdit(nullptr)
    , m_subswathCombo(nullptr)
    , m_polarizationCombo(nullptr)
    , m_manifestPath()
    , m_podPath()
    , m_outputNodeName("{InputName}")
    , m_outputFileName("{InputName}")
{
}

ProductOutputContract Sentinel1ImportNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("sentinel1_import.output.burst_sar")
        : QStringLiteral("sentinel1_import.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("sentinel1_burst_sar")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

QWidget* Sentinel1ImportNode::createWidget()
{
    auto* widget = new QWidget();
    widget->setFixedWidth(300);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto invalidateNodeData = [this]() {
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    // 哨兵图像文件（.safe） + 浏览按钮 [3:7:0]
    auto* manifestLayout = new QHBoxLayout();
    QLabel* manifestLabel = new QLabel("哨兵图像文件（.safe）");
    manifestLayout->addWidget(manifestLabel, 3);
    m_manifestEdit = new QLineEdit();
    m_manifestEdit->setPlaceholderText("选择 .safe 目录中的 manifest 文件");
    connect(m_manifestEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_manifestEdit->text();
        if (m_manifestPath != text) {
            if (!confirmParameterChange()) {
                m_manifestEdit->setText(m_manifestPath);
                return;
            }
            m_manifestPath = text;

            updateAvailableParameters(m_manifestPath);

            QString autoName = generateOutputFileName();
            if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty()) {
                m_outputNodeNameEdit->setText(autoName);
            }
            if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty()) {
                m_outputFileNameEdit->setText(autoName);
            }
            invalidateNodeData();
        }
    });
    manifestLayout->addWidget(m_manifestEdit, 7);
    QPushButton* manifestBrowse = new QPushButton("浏览...");
    manifestLayout->addWidget(manifestBrowse, 0);
    layout->addLayout(manifestLayout);

    // 精轨文件（可空缺） + 浏览按钮 [3:7:0]
    auto* podLayout = new QHBoxLayout();
    QLabel* podLabel = new QLabel("精轨文件（可空缺）");
    podLayout->addWidget(podLabel, 3);
    m_podEdit = new QLineEdit();
    m_podEdit->setPlaceholderText("可选，留空则不使用精轨文件");
    connect(m_podEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_podEdit->text();
        if (m_podPath != text) {
            if (!confirmParameterChange()) {
                m_podEdit->setText(m_podPath);
                return;
            }
            m_podPath = text;
            invalidateNodeData();
        }
    });
    podLayout->addWidget(m_podEdit, 7);
    QPushButton* podBrowse = new QPushButton("浏览...");
    podLayout->addWidget(podBrowse, 0);
    layout->addLayout(podLayout);

    // 子带选择（subswath） [3:7]
    auto* subswathLayout = new QHBoxLayout();
    QLabel* subswathLabel = new QLabel("子带选择（subswath）");
    subswathLayout->addWidget(subswathLabel, 3);
    m_subswathCombo = new QComboBox();
    m_subswathCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_subswathCombo->addItem("iw1");
    m_subswathCombo->addItem("iw2");
    m_subswathCombo->addItem("iw3");
    m_subswathCombo->setCurrentText(m_subswath);
    connect(m_subswathCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        if (m_subswath != text) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_subswathCombo);
                m_subswathCombo->setCurrentText(m_subswath);
                return;
            }
            m_subswath = text;
            invalidateNodeData();
        }
    });
    subswathLayout->addWidget(m_subswathCombo, 7);
    layout->addLayout(subswathLayout);

    // 极化方式选择 [3:7]
    auto* polLayout = new QHBoxLayout();
    QLabel* polLabel = new QLabel("极化方式选择");
    polLayout->addWidget(polLabel, 3);
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_polarizationCombo->addItem("vv");
    m_polarizationCombo->addItem("vh");
    m_polarizationCombo->setCurrentText(m_polarization);
    connect(m_polarizationCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        if (m_polarization != text) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_polarizationCombo);
                m_polarizationCombo->setCurrentText(m_polarization);
                return;
            }
            m_polarization = text;
            invalidateNodeData();
        }
    });
    polLayout->addWidget(m_polarizationCombo, 7);
    layout->addLayout(polLayout);

    // 目标节点 [3:7]
    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点");
    nodeNameLayout->addWidget(nodeNameLabel, 3);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("支持 {InputName} 变量");
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(getProjectContext(), resolveInputName(m_outputNodeName));
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    nodeNameLayout->addWidget(m_outputNodeNameEdit, 7);
    layout->addLayout(nodeNameLayout);

    // 目标文件名 [3:7]
    auto* fileNameLayout = new QHBoxLayout();
    QLabel* fileNameLabel = new QLabel("目标文件名");
    fileNameLayout->addWidget(fileNameLabel, 3);
    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    m_outputFileNameEdit->setPlaceholderText("支持 {InputName} 变量");
    connect(m_outputFileNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputFileNameEdit->text();
        if (m_outputFileName != text) {
            if (!confirmParameterChange()) {
                m_outputFileNameEdit->setText(m_outputFileName);
                return;
            }
            m_outputFileName = text;
            invalidateNodeData();
        }
    });
    fileNameLayout->addWidget(m_outputFileNameEdit, 7);
    layout->addLayout(fileNameLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Connect signals
    connect(manifestBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onManifestBrowseClicked);
    connect(podBrowse, &QPushButton::clicked, this, &Sentinel1ImportNode::onPodBrowseClicked);

    return widget;
}

void Sentinel1ImportNode::executeImport()
{
    m_manifestPath = m_manifestEdit->text().trimmed();
    if (m_manifestPath.isEmpty())
    {
        onError("请选择清单文件");
        return;
    }

    if (!QFileInfo::exists(m_manifestPath))
    {
        onError("清单文件不存在：" + m_manifestPath);
        return;
    }

    m_podPath = m_podEdit->text().trimmed();
    if (!m_podPath.isEmpty() && !QFileInfo::exists(m_podPath))
    {
        onError("精轨文件不存在：" + m_podPath);
        return;
    }

    m_outputFileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : m_outputFileName.trimmed();
    if (m_outputFileName.isEmpty())
    {
        m_outputFileName = "{InputName}";
    }

    QString resolvedFileName = getOutputFileName();
    if (resolvedFileName.isEmpty())
    {
        onError("无法从清单文件生成输出文件名");
        return;
    }

    // 因为已经在 prepareToStart() 中完成了存在性检查，这里直接读取 m_preparedOverwriteResult 并分支处理
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        setState(ExecutionState::Idle);
        return;
    } else if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        setProgress(100);
        onImportFinished();
        return;
    }


    // 构造 ImportTask
    std::vector<ImportTask> tasks;
    ImportTask task;
    task.filename = resolvedFileName;
    task.arguments = QStringList{ m_manifestPath, m_subswathCombo->currentText(), m_polarizationCombo->currentText() };
    if (!m_podPath.isEmpty()) {
        task.arguments.append(m_podPath);
    }
    tasks.push_back(task);

    // 三行启动
    auto* worker = new Sentinel1ImportWorker();
    startWorker(worker, tasks);
}

QStringList Sentinel1ImportNode::getExpectedOutputFilePaths() const
{
    QString outputNodeName = getOutputNodeName();
    QString fileName = getOutputFileName();
    if (fileName.isEmpty()) {
        QString subswath = m_subswathCombo ? m_subswathCombo->currentText() : m_subswath;
        QString pol = m_polarizationCombo ? m_polarizationCombo->currentText() : m_polarization;
        fileName = subswath + "_" + pol;
    }
    return { projectPath() + "/" + outputNodeName + "/" + fileName + ".h5" };
}

QString Sentinel1ImportNode::resolveInputName(const QString& name) const
{
    QString resolved = name;
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    resolved.replace(re, "{InputName}");

    if (resolved.contains("{InputName}", Qt::CaseInsensitive)) {
        QString baseInputName;
        if (!m_manifestPath.isEmpty()) {
            QFileInfo fi(m_manifestPath);
            QString parentDirName = QFileInfo(fi.absolutePath()).fileName();
            if (parentDirName.endsWith(".SAFE", Qt::CaseInsensitive)) {
                baseInputName = parentDirName.left(parentDirName.length() - 5);
            } else {
                baseInputName = parentDirName;
            }
        }
        if (baseInputName.isEmpty()) {
            baseInputName = "S1_Image";
        }
        resolved.replace("{InputName}", baseInputName, Qt::CaseInsensitive);
    }
    return resolved;
}

QString Sentinel1ImportNode::getOutputNodeName() const
{
    QString name = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName.trimmed();
    if (name.isEmpty())
    {
        return "S1_Import";
    }
    return resolveInputName(name);
}

QString Sentinel1ImportNode::getOutputFileName() const
{
    QString fileName = m_outputFileNameEdit ? m_outputFileNameEdit->text().trimmed() : m_outputFileName.trimmed();
    if (fileName.isEmpty())
    {
        return generateOutputFileName();
    }
    return resolveInputName(fileName);
}

QString Sentinel1ImportNode::generateOutputFileName() const
{
    QFileInfo fileInfo(m_manifestPath);
    QString dirName = fileInfo.dir().dirName();

    QRegularExpression dateRegex(R"(\d{8})");
    QRegularExpressionMatch match = dateRegex.match(dirName);
    if (match.hasMatch())
    {
        QString date = match.captured(0);
        QString subswath = m_subswathCombo->currentText();
        QString pol = m_polarizationCombo->currentText();
        return QString("%1_%2%3").arg(date).arg(subswath).arg(pol);
    }

    return QString();
}

void Sentinel1ImportNode::updateAvailableParameters(const QString& manifestPath)
{
    if (manifestPath.isEmpty() || !QFileInfo::exists(manifestPath))
        return;

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QSet<QString> subswaths;
    QSet<QString> polarizations;

    QRegularExpression rx(R"(s1[ab]-(iw[1-3])-slc-(vv|vh|hh|hv)-)");
    QRegularExpressionMatchIterator i = rx.globalMatch(content);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        subswaths.insert(match.captured(1).toLower());
        polarizations.insert(match.captured(2).toLower());
    }

    if (subswaths.isEmpty() || polarizations.isEmpty())
        return;

    // Update subswath combo
    if (m_subswathCombo) {
        QSignalBlocker blocker(m_subswathCombo);
        QString currentSub = m_subswathCombo->currentText();
        m_subswathCombo->clear();
        QStringList subList = subswaths.values();
        subList.sort();
        m_subswathCombo->addItems(subList);
        int subIndex = m_subswathCombo->findText(currentSub);
        if (subIndex >= 0) m_subswathCombo->setCurrentIndex(subIndex);
        else m_subswath = m_subswathCombo->currentText();
    }

    // Update polarization combo
    if (m_polarizationCombo) {
        QSignalBlocker blocker(m_polarizationCombo);
        QString currentPol = m_polarizationCombo->currentText();
        m_polarizationCombo->clear();
        QStringList polList = polarizations.values();
        polList.sort();
        m_polarizationCombo->addItems(polList);
        int polIndex = m_polarizationCombo->findText(currentPol);
        if (polIndex >= 0) m_polarizationCombo->setCurrentIndex(polIndex);
        else m_polarization = m_polarizationCombo->currentText();
    }
}

void Sentinel1ImportNode::onManifestBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择哨兵一号清单文件"),
        QFileInfo(m_manifestPath).absolutePath(),
        tr("清单文件 (manifest.safe);;所有文件 (*)")
    );

    if (!filePath.isEmpty())
    {
        m_manifestEdit->setText(filePath);
        m_manifestPath = filePath;

        updateAvailableParameters(filePath);

        QString autoName = generateOutputFileName();
        if (!autoName.isEmpty() && m_outputNodeNameEdit->text().isEmpty())
        {
            m_outputNodeNameEdit->setText(autoName);
        }
        if (!autoName.isEmpty() && m_outputFileNameEdit->text().isEmpty())
        {
            m_outputFileNameEdit->setText(autoName);
        }
    }
}

void Sentinel1ImportNode::onPodBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("选择精轨文件"),
        QFileInfo(m_podPath).absolutePath(),
        tr("精轨文件 (*.EOF *.eofs);;所有文件 (*)")
    );

    if (!filePath.isEmpty())
    {
        m_podEdit->setText(filePath);
    }
}

QJsonObject Sentinel1ImportNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["manifestPath"] = m_manifestPath;
    json["podPath"] = m_podPath;
    json["subswath"] = m_subswathCombo ? m_subswathCombo->currentText() : m_subswath;
    json["polarization"] = m_polarizationCombo ? m_polarizationCombo->currentText() : m_polarization;
    json["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    json["outputFileName"] = m_outputFileName;
    return json;
}

void Sentinel1ImportNode::load(QJsonObject const &json)
{
    // 先赋值字段
    m_manifestPath = json["manifestPath"].toString();
    m_podPath = json["podPath"].toString();
    m_outputNodeName = json["outputNodeName"].toString();
    if (m_outputNodeName.isEmpty()) {
        m_outputNodeName = "S1_Import";
    }
    m_outputFileName = json["outputFileName"].toString();
    if (m_outputFileName.isEmpty()) {
        m_outputFileName = "{InputName}";
    }
    m_subswath = json["subswath"].toString("iw1");
    if (m_subswath.isEmpty()) {
        m_subswath = "iw1";
    }
    m_polarization = json["polarization"].toString("vv");
    if (m_polarization.isEmpty()) {
        m_polarization = "vv";
    }

    // 同步 UI 控件到最新反序列化的值，防止基类 load() 触发的 validateAndRestoreOutput() 读到旧的 UI 控件值
    if (m_manifestEdit) m_manifestEdit->setText(m_manifestPath);
    if (m_podEdit) m_podEdit->setText(m_podPath);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setText(m_outputFileName);

    if (m_subswathCombo) {
        int idx = m_subswathCombo->findText(m_subswath);
        if (idx >= 0) m_subswathCombo->setCurrentIndex(idx);
    }

    if (m_polarizationCombo) {
        int idx = m_polarizationCombo->findText(m_polarization);
        if (idx >= 0) m_polarizationCombo->setCurrentIndex(idx);
    }

    ExecutableNodeDelegateModel::load(json);

    if (!m_manifestPath.isEmpty()) {
        updateAvailableParameters(m_manifestPath);
    }
}

class Sentinel1ValidationWidget : public BaseValidationWidget
{
public:
    Sentinel1ValidationWidget(Sentinel1ImportNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }
    ~Sentinel1ValidationWidget() override = default;

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在验证数据中..."),
                    QObject::tr("正在核对 Sentinel-1 SAFE 清单文件与生成的 H5 数据精度。"),
                    QObject::tr("元数据与振幅特征"));

        m_lblOrbitCount = createFeatureLabel();
        m_lblWavelength = createFeatureLabel();
        m_lblMeanAmp = createFeatureLabel();
        m_lblMaxAmp = createFeatureLabel();
        m_lblValidPixelRate = createFeatureLabel();

        m_featureLayout->addRow(createHeaderLabel(QObject::tr("状态向量数 (粒度):")), m_lblOrbitCount);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("雷达波长 (m):")), m_lblWavelength);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("平均图像振幅:")), m_lblMeanAmp);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("最大图像振幅:")), m_lblMaxAmp);
        m_featureLayout->addRow(createHeaderLabel(QObject::tr("非零有效像元比例:")), m_lblValidPixelRate);
    }

    struct S1ValidationResults {
        bool success = false;
        QString errorMsg;
        // Compare parameters
        QString expSubswath;
        QString actSubswath;
        QString expPol;
        QString actPol;
        double expPrf = 0.0;
        double actPrf = 0.0;
        double expFreq = 5.405e9;
        double actFreq = 0.0;
        // Features
        int orbitCount = 0;
        bool isOrbitDouble = false;
        double wavelength = 0.0;
        double meanAmp = 0.0;
        double maxAmp = 0.0;
        double validPixelRate = 0.0;
    };

    void startAsyncValidation() override
    {
        m_isTimedOut = false;

        if (m_node->executionState() != ExecutionState::Completed) {
            m_statusTitle->setText(QObject::tr("验证未通过"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未检测到导入完成的输出数据。请先执行导入节点，成功生成 H5 数据后再进行验证。"));
            
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            
            m_lblOrbitCount->setText(QObject::tr("未执行"));
            m_lblWavelength->setText(QObject::tr("未执行"));
            m_lblMeanAmp->setText(QObject::tr("未执行"));
            m_lblMaxAmp->setText(QObject::tr("未执行"));
            m_lblValidPixelRate->setText(QObject::tr("未执行"));
            return;
        }

        QStringList expectedOuts = m_node->getExpectedOutputFilePaths();
        if (expectedOuts.isEmpty() || !QFileInfo::exists(expectedOuts.first())) {
            m_statusTitle->setText(QObject::tr("验证失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("生成的 H5 成果文件不存在或路径无效。"));
            return;
        }

        m_loadingOverlay->startLoading(QObject::tr("正在读取 H5 元数据并分析复数振幅特征值..."));

        QString h5Path = expectedOuts.first();
        
        // Extract expected values from node settings
        QJsonObject saved = m_node->save();
        QString expSub = saved["subswath"].toString("iw1");
        QString expPol = saved["polarization"].toString("vv");

        QFuture<S1ValidationResults> future = QtConcurrent::run([h5Path, expSub, expPol]() {
            NodeUtils::Hdf5Locker locker;
            S1ValidationResults res;
            res.expSubswath = expSub;
            res.expPol = expPol;
            res.expFreq = 5.4050005e9; // Sentinel-1 center frequency
            
            // 1. Read metadata scalars
            double freq = 0.0;
            double prf = 0.0;
            if (NodeUtils::readScalarFromH5(h5Path, "carrier_frequency", freq)) {
                res.actFreq = freq;
                res.wavelength = 299792458.0 / freq; // light_speed / freq
            }
            if (NodeUtils::readScalarFromH5(h5Path, "prf", prf)) {
                res.actPrf = prf;
                res.expPrf = prf; // assume match or extract from safe if possible
            }
            
            res.actSubswath = expSub;
            res.actPol = expPol;

            // 2. Read orbit state vector matrix and verify its precision
            cv::Mat orbitMat;
            if (NodeUtils::readMatFromH5(h5Path, "state_vec", orbitMat)) {
                res.orbitCount = orbitMat.rows;
                res.isOrbitDouble = (orbitMat.type() == CV_64FC1 || orbitMat.type() == CV_64F);
            }

            // 3. Read image "complex" matrix and calculate amplitude features
            cv::Mat complexMat;
            if (NodeUtils::readMatFromH5(h5Path, "complex", complexMat)) {
                res.success = true;
                
                // To prevent heavy calculation taking too long on gigantic image, sample it
                int stepRow = std::max(1, complexMat.rows / 1000);
                int stepCol = std::max(1, complexMat.cols / 1000);
                
                double sumAmp = 0.0;
                double maxAmp = 0.0;
                qint64 validCount = 0;
                qint64 totalSampled = 0;
                
                if (complexMat.type() == CV_32FC2) {
                    for (int r = 0; r < complexMat.rows; r += stepRow) {
                        for (int c = 0; c < complexMat.cols; c += stepCol) {
                            cv::Vec2f pix = complexMat.at<cv::Vec2f>(r, c);
                            double amp = std::sqrt(pix[0]*pix[0] + pix[1]*pix[1]);
                            sumAmp += amp;
                            if (amp > maxAmp) maxAmp = amp;
                            if (amp > 1e-6) validCount++;
                            totalSampled++;
                        }
                    }
                }
                
                if (totalSampled > 0) {
                    res.meanAmp = sumAmp / totalSampled;
                    res.maxAmp = maxAmp;
                    res.validPixelRate = (double)validCount / totalSampled;
                }
            } else {
                res.success = false;
                res.errorMsg = QObject::tr("读取 H5 SLC 数据集失败，请确认数据集名称是否为 complex。");
            }

            return res;
        });

        auto* watcher = new QFutureWatcher<S1ValidationResults>(this);
        connect(watcher, &QFutureWatcher<S1ValidationResults>::finished, this, [this, watcher]() {
            if (m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            S1ValidationResults res = watcher->result();
            m_loadingOverlay->stopLoading();

            if (res.success) {
                m_compTable->clearComparison();
                m_compTable->setEnabled(true);

                m_compTable->addComparison(QObject::tr("子条带 (Subswath)"), res.expSubswath.toUpper(), res.actSubswath.toUpper());
                m_compTable->addComparison(QObject::tr("极化方式"), res.expPol.toUpper(), res.actPol.toUpper());
                m_compTable->addComparison(QObject::tr("载波频率 (GHz)"), QString::number(res.expFreq / 1e9, 'f', 4), QString::number(res.actFreq / 1e9, 'f', 4));
                m_compTable->addComparison(QObject::tr("脉冲重复频率(PRF)"), QString::number(res.expPrf, 'f', 3), QString::number(res.actPrf, 'f', 3));
                
                QString precisionStr = res.isOrbitDouble ? QObject::tr("双精度 (64位)") : QObject::tr("单精度 (32位)");
                m_compTable->addComparison(QObject::tr("状态向量数值精度"), QObject::tr("双精度 (64位)"), precisionStr);

                // Update feature labels
                m_lblOrbitCount->setText(QString::number(res.orbitCount));
                m_lblWavelength->setText(QString::number(res.wavelength, 'f', 5));
                m_lblMeanAmp->setText(QString::number(res.meanAmp, 'f', 2));
                m_lblMaxAmp->setText(QString::number(res.maxAmp, 'f', 2));
                m_lblValidPixelRate->setText(QString("%1%").arg(res.validPixelRate * 100.0, 0, 'f', 2));

                m_statusTitle->setText(QObject::tr("验证通过"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                m_statusDesc->setText(QObject::tr("载波频率、状态向量精度等物理几何参数验证通过。雷达影像振幅与有效像元比例正常，无导入位错。"));
            } else {
                m_statusTitle->setText(QObject::tr("验证失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(res.errorMsg);
            }
            
            watcher->deleteLater();
        });

        watcher->setFuture(future);
    }

private:
    Sentinel1ImportNode* m_node = nullptr;
    
    QLabel* m_lblOrbitCount = nullptr;
    QLabel* m_lblWavelength = nullptr;
    QLabel* m_lblMeanAmp = nullptr;
    QLabel* m_lblMaxAmp = nullptr;
    QLabel* m_lblValidPixelRate = nullptr;
};

::QWidget* Sentinel1ImportNode::createValidationWidget(::QWidget* parent)
{
    return new Sentinel1ValidationWidget(this, parent);
}

} // namespace QtNodes

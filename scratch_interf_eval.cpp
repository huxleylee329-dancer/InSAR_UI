// --------------------------------------------------------------------------------
// InterferometricFormationEvalWidget - 干涉形成质量评估选项卡组件
// --------------------------------------------------------------------------------

struct InterfEvalThreadResult {
    bool success;
    float meanCoh;
    float medianCoh;
    float maxCoh;
    float highCohPct;
    QString errorMessage;
};

class InterferometricFormationEvalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit InterferometricFormationEvalWidget(InterferometricFormationNode* node, QWidget* parent = nullptr)
        : QWidget(parent), m_node(node), m_hasResults(false)
    {
        // 界面布局
        auto* mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);
        mainLayout->setSpacing(12);

        // 左侧栏：评估参数与定量指标显示
        auto* leftContainer = new QWidget();
        auto* leftLayout = new QVBoxLayout(leftContainer);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(10);

        auto* selectionLayout = new QHBoxLayout();
        auto* selLabel = new QLabel(tr("分析影像对:"));
        selLabel->setStyleSheet("font-weight: bold;");
        selectionLayout->addWidget(selLabel);

        m_slaveCombo = new QComboBox();
        selectionLayout->addWidget(m_slaveCombo, 1);
        leftLayout->addLayout(selectionLayout);

        // 状态评估卡片
        m_statusCard = new QFrame();
        m_statusCard->setFrameShape(QFrame::StyledPanel);
        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        
        auto* cardLayout = new QVBoxLayout(m_statusCard);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        cardLayout->setSpacing(4);

        m_statusCardTitle = new QLabel(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        cardLayout->addWidget(m_statusCardTitle);

        m_statusCardDesc = new QLabel(tr("请点击评估获取相干性及干涉质量诊断结果。"));
        m_statusCardDesc->setWordWrap(true);
        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
        cardLayout->addWidget(m_statusCardDesc);

        leftLayout->addWidget(m_statusCard);

        // 定量指标统计表
        auto* metricsFrame = new QFrame();
        metricsFrame->setFrameShape(QFrame::StyledPanel);
        bool isDark = NodeDetailWindow::isDarkTheme(this);
        metricsFrame->setStyleSheet(QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
            .arg(isDark ? "#374151" : "#FFFFFF")
            .arg(isDark ? "#4B5563" : "#E5E7EB"));
        
        auto* formLayout = new QFormLayout(metricsFrame);
        formLayout->setContentsMargins(12, 12, 12, 12);
        formLayout->setSpacing(10);
        formLayout->setLabelAlignment(Qt::AlignLeft);

        auto createValueLabel = [isDark]() {
            auto* label = new QLabel("-");
            label->setStyleSheet(QString("font-size: 12px; font-weight: bold; color: %1;").arg(isDark ? "#F3F4F6" : "#1F2937"));
            return label;
        };

        m_meanCohLabel = createValueLabel();
        m_medianCohLabel = createValueLabel();
        m_maxCohLabel = createValueLabel();
        m_highCohPctLabel = createValueLabel();

        auto addFormRow = [formLayout, isDark](const QString& title, QWidget* valueWidget) {
            auto* label = new QLabel(title);
            label->setStyleSheet(QString("font-size: 11px; color: %1;").arg(isDark ? "#9CA3AF" : "#6B7280"));
            formLayout->addRow(label, valueWidget);
        };

        addFormRow(tr("平均相干系数:"), m_meanCohLabel);
        addFormRow(tr("中位相干系数:"), m_medianCohLabel);
        addFormRow(tr("最高相干系数:"), m_maxCohLabel);
        addFormRow(tr("高相干占比 (>0.5):"), m_highCohPctLabel);

        leftLayout->addWidget(metricsFrame);

        m_statusLabel = new QLabel(tr("准备就绪。请选择影像对开始评估。"));
        m_statusLabel->setWordWrap(true);
        m_statusLabel->setStyleSheet(isDark ? "color: #9CA3AF; font-size: 11px;" : "color: #6B7280; font-size: 11px;");
        leftLayout->addWidget(m_statusLabel);

        leftLayout->addStretch(1);
        mainLayout->addWidget(leftContainer, 4);

        // 右侧栏：大图显示与双模选择
        auto* rightContainer = new QWidget();
        auto* rightLayout = new QVBoxLayout(rightContainer);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(8);

        auto* topBarLayout = new QHBoxLayout();
        
        m_evalBtn = new QPushButton(tr(" 执行质量评估 "));
        m_evalBtn->setStyleSheet(
            "QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2563EB; }"
            "QPushButton:pressed { background-color: #1D4ED8; }"
            "QPushButton:disabled { background-color: #9CA3AF; }"
        );
        topBarLayout->addWidget(m_evalBtn);
        
        topBarLayout->addStretch(1);

        auto* viewModeLabel = new QLabel(tr("视图切换:"));
        viewModeLabel->setStyleSheet("font-weight: bold;");
        topBarLayout->addWidget(viewModeLabel);

        m_visualModeCombo = new QComboBox();
        m_visualModeCombo->addItem(tr("干涉相位 (Phase)"));
        m_visualModeCombo->addItem(tr("相干系数 (Coherence)"));
        topBarLayout->addWidget(m_visualModeCombo);

        rightLayout->addLayout(topBarLayout);

        m_imageView = new ImageView();
        m_imageView->setStyleSheet(QString("border: 1px solid %1; background-color: %2;")
            .arg(isDark ? "#4B5563" : "#D1D5DB")
            .arg(isDark ? "#111827" : "#F3F4F6"));
        rightLayout->addWidget(m_imageView, 1);

        mainLayout->addWidget(rightContainer, 6);

        // 初始化数据
        updateAvailablePairs();

        connect(m_slaveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::onPairChanged);
        connect(m_visualModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InterferometricFormationEvalWidget::updatePreviewImage);
        connect(m_evalBtn, &QPushButton::clicked, this, &InterferometricFormationEvalWidget::startEvaluation);
        connect(&m_watcher, &QFutureWatcher<InterfEvalThreadResult>::finished, this, &InterferometricFormationEvalWidget::onEvaluationFinished);

        // 默认触发一次
        if (m_slaveCombo->count() > 0) {
            onPairChanged();
        } else {
            m_evalBtn->setEnabled(false);
        }
    }

    ~InterferometricFormationEvalWidget() override
    {
        if (m_watcher.isRunning()) {
            m_watcher.waitForFinished();
        }
    }

private:
    void updateAvailablePairs()
    {
        m_slaveCombo->blockSignals(true);
        m_slaveCombo->clear();
        m_h5PathsPhase.clear();
        m_h5PathsCoh.clear();
        m_jpgPathsPhase.clear();
        m_jpgPathsCoh.clear();

        QStringList previews = m_node->previewImagePaths();
        for (const QString& jpgPath : previews) {
            if (jpgPath.endsWith("_phase.jpg")) {
                m_jpgPathsPhase.append(jpgPath);
                QString h5Path = jpgPath;
                h5Path.replace("_phase.jpg", ".h5");
                m_h5PathsPhase.append(h5Path);
            } else if (jpgPath.endsWith("_coh.jpg")) {
                m_jpgPathsCoh.append(jpgPath);
                QString h5Path = jpgPath;
                h5Path.replace("_coh.jpg", ".h5");
                m_h5PathsCoh.append(h5Path);
            }
        }

        if (m_jpgPathsPhase.isEmpty()) {
            m_slaveCombo->addItem(tr("无干涉对"));
        } else {
            for (const QString& jpg : m_jpgPathsPhase) {
                QString name = QFileInfo(jpg).baseName();
                name.replace("_phase", "");
                m_slaveCombo->addItem(name);
            }
        }
        m_slaveCombo->blockSignals(false);
    }

    void onPairChanged()
    {
        m_hasResults = false;
        resetMetrics();
        updatePreviewImage();
    }

    void updatePreviewImage()
    {
        int pairIdx = m_slaveCombo->currentIndex();
        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
            m_imageView->setImage(QImage());
            return;
        }

        int viewMode = m_visualModeCombo->currentIndex();
        QString pathToLoad;

        if (viewMode == 0) { // Phase
            pathToLoad = m_jpgPathsPhase[pairIdx];
        } else { // Coherence
            // 查找对应的相干系数图
            QString baseName = QFileInfo(m_jpgPathsPhase[pairIdx]).baseName();
            baseName.replace("_phase", "_coh");
            for (const QString& cohPath : m_jpgPathsCoh) {
                if (QFileInfo(cohPath).baseName() == baseName) {
                    pathToLoad = cohPath;
                    break;
                }
            }
        }

        if (!pathToLoad.isEmpty() && QFile::exists(pathToLoad)) {
            QImage img(pathToLoad);
            m_imageView->setImage(img);
        } else {
            m_imageView->setImage(QImage());
        }
    }

    void resetMetrics()
    {
        m_meanCohLabel->setText("-");
        m_medianCohLabel->setText("-");
        m_maxCohLabel->setText("-");
        m_highCohPctLabel->setText("-");
        
        m_statusCardTitle->setText(tr("未评估"));
        m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #6B7280;");
        m_statusCardDesc->setText(tr("请点击评估获取相干性及干涉质量诊断结果。"));
        m_statusCardDesc->setStyleSheet("font-size: 11px; color: #9CA3AF;");
        
        m_statusCard->setStyleSheet("background-color: transparent; border: 1px dashed #E5E7EB; border-radius: 4px;");
        m_statusLabel->setText(tr("准备就绪。请点击执行评估。"));
    }

    void startEvaluation()
    {
        int pairIdx = m_slaveCombo->currentIndex();
        if (pairIdx < 0 || pairIdx >= m_jpgPathsPhase.size()) {
            return;
        }

        // 查找对应的相干系数文件
        QString baseName = QFileInfo(m_jpgPathsPhase[pairIdx]).baseName();
        baseName.replace("_phase", "_coh");
        QString cohH5Path;
        for (const QString& cohPath : m_h5PathsCoh) {
            if (QFileInfo(cohPath).baseName() == baseName) {
                cohH5Path = cohPath;
                break;
            }
        }

        if (cohH5Path.isEmpty() || !QFile::exists(cohH5Path)) {
            m_statusLabel->setText(tr("无法评估：未找到该干涉对的相干系数成果文件。请检查节点是否勾选了“计算相干系数”。"));
            return;
        }

        m_evalBtn->setEnabled(false);
        m_slaveCombo->setEnabled(false);
        m_statusLabel->setText(tr("正在读取 H5 文件并计算相干性统计信息，请稍候..."));

        QFuture<InterfEvalThreadResult> future = QtConcurrent::run([cohH5Path]() {
            InterfEvalThreadResult res;
            res.success = false;
            res.meanCoh = 0.0f;
            res.medianCoh = 0.0f;
            res.maxCoh = 0.0f;
            res.highCohPct = 0.0f;

            cv::Mat cohMat;
            QString errMsg;
            if (!NodeUtils::readMatFromH5(cohH5Path, "coherence", cohMat, CV_32F, &errMsg)) {
                res.errorMessage = QStringLiteral("读取相干系数数据集失败: %1").arg(errMsg);
                return res;
            }

            if (cohMat.empty()) {
                res.errorMessage = QStringLiteral("相干系数矩阵为空。");
                return res;
            }

            // 计算统计信息
            // 为了计算中位数和占比，需要遍历所有有效像素
            std::vector<float> validPixels;
            // 预估大小，如果是超大矩阵，可以隔行采样
            int step = 1;
            if (cohMat.total() > 5000000) {
                step = qMax(1, (int)(cohMat.total() / 5000000));
            }
            
            float maxCoh = 0.0f;
            double sumCoh = 0.0;
            int highCount = 0;

            if (cohMat.isContinuous()) {
                const float* ptr = cohMat.ptr<float>();
                int total = cohMat.total();
                for (int i = 0; i < total; i += step) {
                    float val = ptr[i];
                    if (!std::isnan(val) && val >= 0.0f && val <= 1.0f) {
                        validPixels.push_back(val);
                        sumCoh += val;
                        if (val > maxCoh) maxCoh = val;
                        if (val > 0.5f) highCount++;
                    }
                }
            } else {
                for (int r = 0; r < cohMat.rows; r += step) {
                    const float* ptr = cohMat.ptr<float>(r);
                    for (int c = 0; c < cohMat.cols; c += step) {
                        float val = ptr[c];
                        if (!std::isnan(val) && val >= 0.0f && val <= 1.0f) {
                            validPixels.push_back(val);
                            sumCoh += val;
                            if (val > maxCoh) maxCoh = val;
                            if (val > 0.5f) highCount++;
                        }
                    }
                }
            }

            if (validPixels.empty()) {
                res.errorMessage = QStringLiteral("相干系数矩阵中没有有效数据。");
                return res;
            }

            res.meanCoh = sumCoh / validPixels.size();
            res.maxCoh = maxCoh;
            res.highCohPct = (float)highCount / validPixels.size() * 100.0f;

            size_t n = validPixels.size() / 2;
            std::nth_element(validPixels.begin(), validPixels.begin() + n, validPixels.end());
            res.medianCoh = validPixels[n];

            res.success = true;
            return res;
        });

        m_watcher.setFuture(future);
    }

    void onEvaluationFinished()
    {
        m_evalBtn->setEnabled(true);
        m_slaveCombo->setEnabled(true);

        InterfEvalThreadResult res = m_watcher.result();
        if (!res.success) {
            m_statusLabel->setText(tr("评估失败：%1").arg(res.errorMessage));
            return;
        }

        m_hasResults = true;
        m_statusLabel->setText(tr("评估完成。"));

        m_meanCohLabel->setText(QString::number(res.meanCoh, 'f', 4));
        m_medianCohLabel->setText(QString::number(res.medianCoh, 'f', 4));
        m_maxCohLabel->setText(QString::number(res.maxCoh, 'f', 4));
        m_highCohPctLabel->setText(QString("%1 %").arg(res.highCohPct, 0, 'f', 2));

        // 根据经验阈值更新诊断卡片
        if (res.meanCoh > 0.4f && res.highCohPct > 30.0f) {
            m_statusCardTitle->setText(tr("干涉质量良好"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #059669;"); // 绿色
            m_statusCardDesc->setText(tr("整体相干性较高，干涉条纹预期清晰。符合后续相位解缠和形变提取要求。"));
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #10B981;");
            m_statusCard->setStyleSheet("background-color: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.3); border-radius: 4px;");
        } else if (res.meanCoh >= 0.2f) {
            m_statusCardTitle->setText(tr("相干性一般"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #D97706;"); // 橙色
            m_statusCardDesc->setText(tr("存在一定的去相干（可能受植被覆盖、较长基线或时间跨度影响）。建议在后续节点适当增加滤波强度。"));
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #F59E0B;");
            m_statusCard->setStyleSheet("background-color: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.3); border-radius: 4px;");
        } else {
            m_statusCardTitle->setText(tr("严重去相干"));
            m_statusCardTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #DC2626;"); // 红色
            m_statusCardDesc->setText(tr("整体相干性极低，干涉相位可能完全被噪声掩盖。请检查输入影像的时空基线，或确保前置配准精度达标。"));
            m_statusCardDesc->setStyleSheet("font-size: 11px; color: #EF4444;");
            m_statusCard->setStyleSheet("background-color: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.3); border-radius: 4px;");
        }
    }

    InterferometricFormationNode* m_node;
    QComboBox* m_slaveCombo;
    QComboBox* m_visualModeCombo;
    QPushButton* m_evalBtn;
    ImageView* m_imageView;

    QFrame* m_statusCard;
    QLabel* m_statusCardTitle;
    QLabel* m_statusCardDesc;

    QLabel* m_meanCohLabel;
    QLabel* m_medianCohLabel;
    QLabel* m_maxCohLabel;
    QLabel* m_highCohPctLabel;
    QLabel* m_statusLabel;

    QStringList m_h5PathsPhase;
    QStringList m_h5PathsCoh;
    QStringList m_jpgPathsPhase;
    QStringList m_jpgPathsCoh;

    bool m_hasResults;
    QFutureWatcher<InterfEvalThreadResult> m_watcher;
};

::QWidget* InterferometricFormationNode::createInterferometryWidget(::QWidget* parent)
{
    return new InterferometricFormationEvalWidget(this, parent);
}

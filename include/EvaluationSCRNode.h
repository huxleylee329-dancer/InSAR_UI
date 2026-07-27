#pragma once

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <opencv2/opencv.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <atomic>

namespace QtNodes {

struct SCRSingleResult {
    QString fileName;
    QString origScr;
    QString filtScr;
    QString imp;
    bool success = false;
};

struct SCRResultData {
    QList<SCRSingleResult> results;
    double totalOrigScr = 0.0;
    double totalFiltScr = 0.0;
    double totalImp = 0.0;
    int validOrigCount = 0;
    int validFiltCount = 0;
    int validImpCount = 0;
    int totalCount = 0;
    QString errorMsg;
    bool isCancelled = false;
};

class EvaluationSCRNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    EvaluationSCRNode();
    ~EvaluationSCRNode() override;

    QString caption() const override { return "SCR Evaluation"; }
    QString name() const override { return "EvaluationSCR"; }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    QWidget* embeddedWidget() override;

    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    void collapseDetailedList() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    bool validateAndRestoreOutput() override;
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

    // Dual ROI overrides for detail view
    bool supportsTwoRois() const override { return true; }
    void processTargetRoiSelection(const QRectF& sceneRect, int imageIndex) override;
    void processClutterRoiSelection(const QRectF& sceneRect, int imageIndex) override;
    void clearTargetRoiSelection() override;
    void clearClutterRoiSelection() override;
    bool hasTargetRoi() const override { return m_hasTargetRoi; }
    QRectF targetRoi() const override { return m_targetRoi; }
    bool hasClutterRoi() const override { return m_hasClutterRoi; }
    QRectF clutterRoi() const override { return m_clutterRoi; }
    
    QStringList previewImagePaths() const override;
    
    QStringList detailTableHeaders() const override { return m_detailTableHeaders; }
    QList<QStringList> detectionResults() const override { return m_detectionResults; }

private slots:
    void onRegionChanged(int index);
    void onEvaluationFinished();

private:
    void createWidget();
    void calculateAndDisplaySCR();
    void updateWidgetSize();
    static double calculateScr(const cv::Mat& targetGray, const cv::Mat& clutterGray);

    QWidget* m_widget = nullptr;
    QComboBox* m_regionComboBox = nullptr;

    QWidget* m_simpleResultWidget = nullptr;
    QLabel* m_originalScrLabel = nullptr;
    QLabel* m_filteredScrLabel = nullptr;
    QLabel* m_improvementLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_expandLabel = nullptr;
    bool m_isExpanded = false;

    QTableWidget* m_resultsTable = nullptr;

    std::shared_ptr<ImageInfoData> m_originalData = nullptr;
    std::shared_ptr<ImageInfoData> m_filteredData = nullptr;
    
    // Custom ROI state
    bool m_hasTargetRoi = false;
    QRectF m_targetRoi;
    bool m_hasClutterRoi = false;
    QRectF m_clutterRoi;
    
    QStringList m_detailTableHeaders;
    QList<QStringList> m_detectionResults;

    // Async support
    QFutureWatcher<SCRResultData>* m_watcher = nullptr;
    std::shared_ptr<std::atomic<bool>> m_stopFlagPtr;
    bool m_evaluationActive = false;
    bool m_restartPending = false;
};

} // namespace QtNodes

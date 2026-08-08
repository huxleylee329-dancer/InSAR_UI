#pragma once

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <opencv2/opencv.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <atomic>

namespace QtNodes {

struct ENLSingleResult {
    QString fileName;
    QString origEnl;
    QString filtEnl;
    QString epi;
    bool success = false;
};

struct ENLResultData {
    QList<ENLSingleResult> results;
    double avgOrigEnl = 0.0;
    double avgFiltEnl = 0.0;
    double avgEpi = 0.0;
    int validCount = 0;
    int totalCount = 0;
    QString errorMsg;
    bool isCancelled = false;
};

class EvaluationENLNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    EvaluationENLNode();
    ~EvaluationENLNode() override;

    QString caption() const override { return "ENL Evaluation"; }
    QString name() const override { return "EvaluationENL"; }

    ProductInputContract productInputContract(PortIndex portIndex) const override;
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

protected:
    bool prepareToStart() override;
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }
    bool validateAndRestoreOutput() override;

private slots:
    void onRegionChanged(int index);
    void onEvaluationFinished();

private:
    void createWidget();
    void calculateAndDisplayENL();
    void updateWidgetSize();
    static double calculateENL(const cv::Mat& roiGray);
    static double calculateEPI(const cv::Mat& orig, const cv::Mat& filtered);

    // Detail View ROI and Table interfaces
    bool supportsRoiSelection() const override { return true; }
    void processRoiSelection(const QRectF& sceneRect, int imageIndex) override;
    void clearRoiSelection() override;
    bool hasCustomRoi() const override { return m_hasCustomRoi; }
    QRectF customRoi() const override { return QRectF(m_customRoi.x, m_customRoi.y, m_customRoi.width, m_customRoi.height); }
    QStringList detailTableHeaders() const override;
    QList<QStringList> detectionResults() const override;
    QStringList previewImagePaths() const override;

private:
    QWidget* m_widget;
    QComboBox* m_regionComboBox;
    QLabel* m_originalEnlLabel;
    QLabel* m_filteredEnlLabel;
    QLabel* m_epiLabel;
    QLabel* m_summaryLabel;
    QLabel* m_expandLabel;
    QWidget* m_simpleResultWidget;
    QTableWidget* m_resultsTable;
    bool m_isExpanded = false;

    std::shared_ptr<ImageInfoData> m_originalData;
    std::shared_ptr<ImageInfoData> m_filteredData;

    // ROI support
    bool m_hasCustomRoi = false;
    cv::Rect m_customRoi;
    QList<QStringList> m_savedResults;

    // Async support
    QFutureWatcher<ENLResultData>* m_watcher = nullptr;
    std::shared_ptr<std::atomic<bool>> m_stopFlagPtr;
    bool m_evaluationActive = false;
    bool m_restartPending = false;
};

} // namespace QtNodes

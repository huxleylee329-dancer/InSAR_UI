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

namespace QtNodes {

class EvaluationENLNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    EvaluationENLNode();
    ~EvaluationENLNode() override = default;

    QString caption() const override { return "Evaluation-ENL"; }
    QString name() const override { return "EvaluationENL"; }

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
    bool validateAndRestoreOutput() override;

private slots:
    void onRegionChanged(int index);

private:
    void createWidget();
    void calculateAndDisplayENL();
    void updateWidgetSize();
    double calculateENL(const cv::Mat& roiGray) const;
    double calculateEPI(const cv::Mat& orig, const cv::Mat& filtered) const;

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
};

} // namespace QtNodes

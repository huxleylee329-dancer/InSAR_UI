#pragma once

#include "NodeDataTypes.h"
#include "ImportDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "CutWorker.h"
#include "NodeUtils.h"
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QThread>
#include <QStandardItemModel>

class XMLFile;

namespace QtNodes {

class CutNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CutNode();
    ~CutNode() override;

    QString caption() const override { return "AOI Crop"; }
    QString name() const override { return "AOICrop"; }

    // Port definitions
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    // Data in/out
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // Embedded Widget
    QWidget* embeddedWidget() override;
    void createWidget();
    QStringList previewImagePaths() const override;
    bool supportsRoiSelection() const override;
    bool supportsInterferometry() const override { return true; }
    ::QWidget* createInterferometryWidget(::QWidget* parent) override;
    QStringList getOutputPaths() const { return m_outputPaths; }
    void processRoiSelection(const QRectF& sceneRect, int imageIndex) override;
    void clearRoiSelection() override;
    bool hasCustomRoi() const override { return m_boxSelected; }
    QRectF customRoi() const override;
    std::vector<QString> processingInfo() const override;

    // Execution
    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

    // Serialization
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;
protected:
    bool validateAndRestoreOutput() override;
    bool prepareToStart() override;

private Q_SLOTS:
    void onModeChanged(int index);
    void onPreviewPressed();
    void onBoxSelected(double left, double right, double top, double bottom);
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

private:
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void executeProcessing();
    void updateLabels();
    void updateWidgetSize();
    void updateParameterWidgetsEnableState();
    QString generateDefaultOutputName() const;
    QStringList resolvedInputH5Paths() const;
    QStringList resolvedInputPreviewPaths() const;

    // UI elements
    QWidget* _widget = nullptr;
    QComboBox* m_modeCombo = nullptr;

    // Mode 0: Coordinate Widgets
    QWidget* m_coordinateWidget = nullptr;
    QLineEdit* m_lonEdit = nullptr;
    QLineEdit* m_latEdit = nullptr;
    QLineEdit* m_widthEdit = nullptr;
    QLineEdit* m_heightEdit = nullptr;

    // Mode 1: Box Selection Widgets
    QWidget* m_boxSelectionWidget = nullptr;
    QComboBox* m_previewCombo = nullptr;
    QPushButton* m_previewBtn = nullptr;
    QLabel* m_boundsLabel = nullptr;

    // Mode 2: Auto Center Widgets
    QWidget* m_autoCenterWidget = nullptr;
    QDoubleSpinBox* m_leftSpin = nullptr;
    QDoubleSpinBox* m_rightSpin = nullptr;
    QDoubleSpinBox* m_topSpin = nullptr;
    QDoubleSpinBox* m_bottomSpin = nullptr;

    // Shared Output Widgets
    QCheckBox* m_saveToProjectCheckBox = nullptr;
    QLineEdit* m_outputNodeNameEdit = nullptr;

    // Data members
    std::shared_ptr<ImportedFileData> m_inputData = nullptr;
    std::shared_ptr<ImportedFileData> m_outputData = nullptr;
    std::shared_ptr<ImageInfoData> m_previewData = nullptr;

    int m_mode = 0; // 0: Auto Center, 1: Coord, 2: Box Selection
    bool m_boxSelected = false;
    bool m_coordsSet = false;
    double m_lastInputLon = 0.0;
    double m_lastInputLat = 0.0;

    // Saved parameters
    double m_lon = 0.0;
    double m_lat = 0.0;
    double m_width = 1000.0;
    double m_height = 1000.0;

    double m_left = 0.25;
    double m_right = 0.75;
    double m_top = 0.25;
    double m_bottom = 0.75;

    QString m_outputNodeName = "AOI_Crop";
    bool m_saveToProject = true;
    QStringList m_outputPaths;
    QStringList m_savedOutputFileNames; // 用于load时projDir不可用的情况

    // Threading
    QThread* m_thread = nullptr;
    CutWorker* m_worker = nullptr;
    void cleanUpThreadAndWorker();
    void releaseFinishedThreadAndWorker();

    // Flag for delayed execution state correction (Automatic mode pitfall)
    bool m_isExecuting = false;
    QElapsedTimer m_executionTimer;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QStringList m_preparedInputPaths;
    QStringList m_preparedOutputPaths;
    QString m_preparedDstNodeName;
    QString m_preparedProjDir;
    QString m_preparedProjName;
    QStandardItemModel* m_preparedModel = nullptr;
    bool m_preparedSaveToProject = true;
    XMLFile* m_preparedProjectXmlPtr = nullptr;
};

} // namespace QtNodes

#ifndef PHASEELEVATIONREGRESSIONNODE_H
#define PHASEELEVATIONREGRESSIONNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "PhaseElevationRegressionWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

/**
 * @brief 经验性相位-高程回归校正节点
 * 利用最小二乘拟合干涉图相位与高程/空间坐标的关系，
 * 估计并去除大气延迟相关的相位分量。
 */
class PhaseElevationRegressionNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    PhaseElevationRegressionNode();
    ~PhaseElevationRegressionNode();

    QString caption() const override { return QStringLiteral("Phase-Elevation Regression"); }
    QString name() const override { return QStringLiteral("Phase-Elevation Regression"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;
    bool prepareToStart() override;

private:
    ::QWidget* _widget;
    QComboBox* m_polyOrderCombo;
    QLineEdit* m_windowSizeEdit;
    QDoubleSpinBox* m_coherenceThreshSpin;
    QLineEdit* m_outputNodeNameEdit;

    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;

    QString m_outputNodeName;
    int m_polyOrder;
    int m_windowSize;
    double m_coherenceThresh;

    PhaseElevationRegressionWorker* m_workerThread;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedSrcNode;
    int m_preparedPolyOrder = 1;
    int m_preparedWindowSize = 0;
    double m_preparedCoherenceThresh = 0.3;
    QStringList m_preparedPhaseNames;
    QStringList m_preparedPhasePaths;
    QStringList m_generatedOutputNames;
    QStringList m_generatedOutputPaths;
    QList<int> m_generatedOffsetRows;
    QList<int> m_generatedOffsetCols;

    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    bool validateInputs() const;
    void updateWidgetSize();
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void persistOutputToProject(const QString& outputNodeName, const QStringList& h5Paths,
                                const QStringList& outputNames, const QList<int>& offsetRows,
                                const QList<int>& offsetCols);
    void startPreviewGeneration(const QStringList& h5Paths, const QStringList& generatedJpgPaths,
                                const QStringList& types, const QStringList& resultJpgPaths,
                                bool completeExecution);
    void cleanUpThreadAndWorker();
    void releaseFinishedThreadAndWorker();

    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

signals:
    void startRegression(int polyOrder, int windowSize, double coherenceThresh,
                         QString save_path, QString project_name,
                         QString node_name, QString file_name,
                         QStringList phaseNames, QStringList phasePaths);
};

} // namespace QtNodes

#endif // PHASEELEVATIONREGRESSIONNODE_H

#pragma once

#include "NodeDataTypes.h"
#include "ImportDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "CoregistrationWorker.h"
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFutureWatcher>
#include <QThread>

namespace QtNodes {

class CoregistrationNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CoregistrationNode();
    ~CoregistrationNode() override;

    QString caption() const override { return QStringLiteral("Coregistration"); }
    QString name() const override { return QStringLiteral("Coregistration"); }

    // Port definitions
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    // Data flow
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // Embedded widget UI
    QWidget* embeddedWidget() override;
    void createWidget();
    QStringList previewImagePaths() const override;

    // Execution
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // Serialization
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    bool validateAndRestoreOutput() override;

private Q_SLOTS:
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    void updateMasterImageCombo();
    void updateWidgetSize();

private:
    bool isReady() const;
    void executeProcessing();
    QString getRealSavePath() const;
    QString resolveOutputFileName(const QString& originalName) const;

    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    // UI Widgets
    QWidget* _widget = nullptr;
    QComboBox* m_methodCombo = nullptr;
    QCheckBox* m_defaultFirstMasterCheckBox = nullptr;
    QComboBox* m_masterImageCombo = nullptr;
    
    // Coarse inputs
    QLabel* m_interpLabel = nullptr;
    QComboBox* m_interpCombo = nullptr;
    QLabel* m_blockSizeLabel = nullptr;
    QComboBox* m_blockSizeCombo = nullptr;

    // Fine inputs
    QLabel* m_demPathLabel = nullptr;
    QLineEdit* m_demPathEdit = nullptr;
    QPushButton* m_demBrowseBtn = nullptr;

    // Output node & pattern
    QLineEdit* m_outputNodeNameEdit = nullptr;
    QLineEdit* m_outputFileNameEdit = nullptr;

    // Data
    std::shared_ptr<ImportedFileData> m_inputData = nullptr;
    std::shared_ptr<ImportedFileData> m_outputData = nullptr;
    std::shared_ptr<ImageInfoData> m_previewData = nullptr;

    QStringList m_outputImagePaths;
    QStringList m_outputJpgPaths;
    QStringList m_savedOutputFiles;
    QString m_outputNodeName;
    QString m_outputFileName;

    // Parameters
    QString m_method; // "Coarse" or "Fine"
    bool m_defaultFirstMaster = true;
    int m_masterIndex = 1;
    int m_interpTimes = 4;
    int m_blockSize = 64;
    QString m_demPath;

    // Threading / Watchers
    CoregistrationWorker* m_worker = nullptr;
    QThread* m_thread = nullptr;
    QFutureWatcher<void> m_remedyWatcher;
};

} // namespace QtNodes

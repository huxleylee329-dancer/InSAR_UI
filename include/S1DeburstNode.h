#ifndef S1DEBURSTNODE_H
#define S1DEBURSTNODE_H

#include "ImportDataTypes.h"
#include "MyThread.h"
#include <QtNodes/NodeDelegateModel>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QThread>
#include <memory>

// Forward declarations
class NodeEditorWindow;

namespace QtNodes {

// ============================================================================
// S1DeburstNode - Sentinel-1 Deburst preprocessing node
// ============================================================================
class S1DeburstNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    S1DeburstNode();
    ~S1DeburstNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Deburst"); }
    QString name() const override { return QStringLiteral("S1Deburst"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget* embeddedWidget() override;

private:
    // UI elements
    QWidget* m_widget;
    QLabel* m_inputLabel;
    QLineEdit* m_outputNodeNameEdit;
    QPushButton* m_processButton;
    QPushButton* m_stopButton;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    // Input data storage
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;

    // Helper methods
    void createWidget();
    void onProcessButtonClicked();
    void onStopButtonClicked();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateInputLabel();
    QString generateDefaultOutputName() const;

    // Get NodeEditorWindow reference
    NodeEditorWindow* getNodeEditorWindow() const;
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;

signals:
    void startDeburst(QString savePath, QString dstProject,
                     QString srcNode, QString dstNode, QStandardItemModel* model);
};

} // namespace QtNodes

#endif // S1DEBURSTNODE_H

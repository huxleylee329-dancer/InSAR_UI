#ifndef S1FRAMEMERGENODE_H
#define S1FRAMEMERGENODE_H

#include "ImportDataTypes.h"
#include "MyThread.h"
#include <QtNodes/NodeDelegateModel>
#include <QWidget>
#include <QSpinBox>
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
// S1FrameMergeNode - Sentinel-1 Frame Merge preprocessing node
// ============================================================================
class S1FrameMergeNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    S1FrameMergeNode();
    ~S1FrameMergeNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Frame Merge"); }
    QString name() const override { return QStringLiteral("S1FrameMerge"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    QWidget* embeddedWidget() override;

private:
    // UI elements
    QWidget* m_widget;
    QLabel* m_inputLabels[2];
    QSpinBox* m_indexSpins[2];
    QLineEdit* m_outputNodeNameEdit;
    QPushButton* m_processButton;
    QPushButton* m_stopButton;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    // Input data storage
    std::shared_ptr<ImportedFileData> m_inputs[2];
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
    void updateInputLabels();
    QString generateDefaultOutputName() const;

    // Get NodeEditorWindow reference
    NodeEditorWindow* getNodeEditorWindow() const;
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;

signals:
    void startFrameMerge(int index1, int index2, QString project,
                        QString node1, QString node2, QString dstNode, QStandardItemModel*);
};

} // namespace QtNodes

#endif // S1FRAMEMERGENODE_H

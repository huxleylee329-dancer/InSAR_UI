#ifndef SPACETYIMPORTNODE_H
#define SPACETYIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "SpacetyImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>

namespace QtNodes {

// ============================================================================
// SpacetyImportNode - Batch import Fucheng-1 (Spacety) data
// ============================================================================
class SpacetyImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    SpacetyImportNode();
    ~SpacetyImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Fucheng-1 Import"); }
    QString name() const override { return QStringLiteral("SpacetyImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getImportedFilePaths() const override;
    QString getOutputNodeName() const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    // Thread accessors
    QThread* qThread() const override { return m_thread; }
    void stopExecution() override;

    // Helper methods
    QString generateOutputFileName(const QString& filePath) const;

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startSpacetyImport(QString, std::vector<QString>, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*, bool);

private:
    // UI elements
    QWidget* m_widget;
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;  // Target project label (read-only)
    QCheckBox* m_spotlightCheckBox; // Mode checkbox

    // State
    QStringList m_dataFiles;     // Data file paths
    QStringList m_xmlFiles;      // XML file paths
    QStringList m_importedFilePaths;
    QString m_outputNodeName;
    bool m_spotlightMode;

    // Worker thread
    SpacetyImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // SPACETYIMPORTNODE_H

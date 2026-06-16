#ifndef HTHTIMPORTNODE_H
#define HTHTIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "HTHTImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>

namespace QtNodes {

class HTHTImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    HTHTImportNode();
    ~HTHTImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Hongtu-1 Import"); }
    QString name() const override { return QStringLiteral("HTHTImport"); }

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

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onDataBrowseClicked();
    void onXmlBrowseClicked();
    void onAddTaskClicked();
    void onRemoveTaskClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startHTHTImport(
        QString savepath,
        std::vector<QString> data_files,
        std::vector<QString> xml_files,
        std::vector<int> modes,
        std::vector<QString> import_names,
        QString dst_node,
        QString dst_project,
        QStandardItemModel* model
    );

private:
    // UI elements
    QLineEdit* m_dataEdit;
    QLineEdit* m_xmlEdit;
    QComboBox* m_modeCombo;
    QListWidget* m_fileListWidget;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_projectLabel;

    // State
    QStringList m_dataFiles;
    QStringList m_xmlFiles;
    QList<int> m_modes;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    // Worker thread
    HTHTImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // HTHTIMPORTNODE_H

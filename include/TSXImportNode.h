#ifndef TSXIMPORTNODE_H
#define TSXIMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>

namespace QtNodes {

// ============================================================================
// TSXImportNode - Single file TerraSAR-X import node
// ============================================================================
class TSXImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    TSXImportNode();
    ~TSXImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Import"); }
    QString name() const override { return QStringLiteral("TSXImport"); }

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

    // Thread accessors
    MyThread* workerThread() const override { return m_workerThread; }
    QThread* qThread() const override { return m_thread; }

    // Helper methods
    QString generateOutputFileName() const;

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onXmlBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startTSXImport(QString, QString, QString, QString, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QLineEdit* m_xmlEdit;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QString m_xmlPath;
    QString m_importedFilePath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_polarization = "HH";

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // TSXIMPORTNODE_H

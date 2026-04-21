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

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName() const;

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
    QComboBox* m_projectCombo;

    // State
    QString m_xmlPath;
    QString m_importedFilePath;
    QString m_outputFileName;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // TSXIMPORTNODE_H

#ifndef MACAOBATCHIMPORTNODE_H
#define MACAOBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>

namespace QtNodes {

class MacaoBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    MacaoBatchImportNode();
    ~MacaoBatchImportNode();

    QString caption() const override { return QStringLiteral("Macao Batch Import"); }
    QString name() const override { return QStringLiteral("MacaoBatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    void setExecutionMode(ExecutionMode mode) override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Thread accessors
    MyThread* workerThread() const override { return m_workerThread; }
    QThread* qThread() const override { return m_thread; }

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startMacaoBatchImport(QString, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

private:
    QString generateImportName(const QString& imagePath) const;

    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_projectCombo;

    QStringList m_imagePaths;
    QStringList m_importedFilePaths;

    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // MACAOBATCHIMPORTNODE_H

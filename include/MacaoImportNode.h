#ifndef MACAOIMPORTNODE_H
#define MACAOIMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"
#include "NodeDataTypes.h"

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

class MacaoImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    MacaoImportNode();
    ~MacaoImportNode();

    QString caption() const override { return QStringLiteral("Macao Import"); }
    QString name() const override { return QStringLiteral("MacaoImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

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
    void onImageBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startMacaoImport(QString, QString, QString, QString, QString, QStandardItemModel*);

private:
    QLineEdit* m_imageEdit;
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QComboBox* m_projectCombo;

    QString m_imagePath;
    QString m_importedFilePath;
    QString m_outputFileName;

    std::shared_ptr<ImageInfoData> m_imageInfoData;

    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // MACAOIMPORTNODE_H

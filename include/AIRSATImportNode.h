#ifndef AIRSATIMPORTNODE_H
#define AIRSATIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

namespace QtNodes {

// ============================================================================
// AIRSATImportNode - Batch import AIRSAT data
// ============================================================================
class AIRSATImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    AIRSATImportNode();
    ~AIRSATImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("AIRSAT Import"); }
    QString name() const override { return QStringLiteral("AIRSATImport"); }
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName(const QString& filePath) const;
    void updateWidgetSize();

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();

private:
    // UI elements
    QWidget* m_widget;
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;

    // State
    QStringList m_dataFilePaths;
    QStringList m_xmlFilePaths;
    QString m_outputNodeName;
};

} // namespace QtNodes

#endif // AIRSATIMPORTNODE_H

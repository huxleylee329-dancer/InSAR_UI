#ifndef CSKIMPORTNODE_H
#define CSKIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

namespace QtNodes {

// ============================================================================
// CSKImportNode - Batch import COSMO-SkyMed data
// ============================================================================
class CSKImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    CSKImportNode();
    ~CSKImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("COSMO-SkyMed Import"); }
    QString name() const override { return QStringLiteral("CSKImport"); }
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

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;
    QPushButton* m_importButton;
    QPushButton* m_stopButton;

    // State
    QStringList m_filePaths;
    QString m_outputNodeName;
};

} // namespace QtNodes

#endif // CSKIMPORTNODE_H

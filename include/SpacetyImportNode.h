#ifndef SPACETYIMPORTNODE_H
#define SPACETYIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

// ============================================================================
// SpacetyImportNode - Batch import Fucheng-1 (Spacety) data
// ============================================================================
class SpacetyImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    SpacetyImportNode();
    ~SpacetyImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Fucheng-1 Import"); }
    QString name() const override { return QStringLiteral("SpacetyImport"); }
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
    QWidget* m_widget;
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QCheckBox* m_spotlightCheckBox;

    // State
    QStringList m_dataFiles;
    QStringList m_xmlFiles;
    QString m_outputNodeName;
    bool m_spotlightMode;
};

} // namespace QtNodes

#endif // SPACETYIMPORTNODE_H

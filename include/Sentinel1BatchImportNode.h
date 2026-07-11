#ifndef SENTINEL1BATCHIMPORTNODE_H
#define SENTINEL1BATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>

namespace QtNodes {

// ============================================================================
// Sentinel1BatchImportNode - Batch import Sentinel-1 data
// ============================================================================
class Sentinel1BatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1BatchImportNode();
    ~Sentinel1BatchImportNode() = default;
    // 启用验证面板
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Batch Import"); }
    QString name() const override { return QStringLiteral("Sentinel1BatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;
    QStringList getExpectedOutputFilePaths() const override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    bool prepareToStart() override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateImportName(const QString& manifestPath) const;
    void updateAvailableParameters();

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onOrbitBrowseClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;
    QCheckBox* m_enableOrbitCheckBox;
    QLineEdit* m_orbitDirEdit;
    QPushButton* m_orbitBrowseBtn;

    // State
    QStringList m_manifestPaths;
    QString m_outputNodeName;
    QString m_subswath = "iw1";
    QString m_polarization = "vv";
    bool m_enableOrbitMatch = true;
    QString m_orbitDir;

    std::vector<QString> m_preparedOriginalNameList;
    std::vector<QString> m_preparedImportNameList;
    QString m_preparedOutputNodeName;
};

} // namespace QtNodes

#endif // SENTINEL1BATCHIMPORTNODE_H

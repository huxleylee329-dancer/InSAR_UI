#ifndef SENTINEL1IMPORTNODE_H
#define SENTINEL1IMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

// ============================================================================
// Sentinel1ImportNode - Single file Sentinel-1 import node
// ============================================================================
class Sentinel1ImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1ImportNode();
    ~Sentinel1ImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Import"); }
    QString name() const override { return QStringLiteral("Sentinel1Import"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;
    QStringList getExpectedOutputFilePaths() const override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName() const;
    QString getOutputFileName() const;
    QString resolveInputName(const QString& name) const;
    void updateAvailableParameters(const QString& manifestPath);

private slots:
    void onManifestBrowseClicked();
    void onPodBrowseClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QLabel* m_projectLabel;
    QLineEdit* m_manifestEdit;
    QLineEdit* m_podEdit;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;

    // State
    QString m_manifestPath;
    QString m_podPath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_subswath = "iw1";
    QString m_polarization = "vv";
};

} // namespace QtNodes

#endif // SENTINEL1IMPORTNODE_H

#ifndef ALOS2IMPORTNODE_H
#define ALOS2IMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

namespace QtNodes {

// ============================================================================
// ALOS2ImportNode - Batch import ALOS-2 data
// ============================================================================
class ALOS2ImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    ALOS2ImportNode();
    ~ALOS2ImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("ALOS-2 Import"); }
    QString name() const override { return QStringLiteral("ALOS2Import"); }
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
    QString generateOutputFileName(const QString& imgPath) const;
    QString generateLEDPath(const QString& imgPath) const;

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
    QStringList m_imgPaths;
    QString m_outputNodeName;
};

} // namespace QtNodes

#endif // ALOS2IMPORTNODE_H

#ifndef IMAGEDISPLAYNODE_H
#define IMAGEDISPLAYNODE_H

#include <QtNodes/NodeDelegateModel>
#include "NodeDataTypes.h"
#include "ImageView.h"

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QPixmap>
#include <memory>

namespace QtNodes {

/**
 * @brief Universal Image Display Node.
 * Receives ImageInfoData and displays a preview thumbnail.
 * Supports JPG, PNG, TIF, and H5 (requires dataset metadata).
 */
class ImageDisplayNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    ImageDisplayNode();
    ~ImageDisplayNode() override;

    QString caption() const override;
    QString name() const override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override { return nullptr; }
    void setInData(std::shared_ptr<NodeData> data, PortIndex portIndex) override;

    QWidget *embeddedWidget() override;
    bool resizable() const override { return true; }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onImageLoaded();
    void onWidgetResized();

private:
    void setError(const QString& message);
    void clearError();
    void updateInfo(const QString& info);

    struct LoadedImage {
        QPixmap pixmap;
        QString fileName;
        QString info;
        QString errorMessage;
        bool success = false;
    };

    static LoadedImage loadImageTask(QString filePath, QMap<QString, QString> metadata);

private:
    QWidget* m_widget;
    QVBoxLayout* m_layout;
    QLabel* m_infoLabel;
    ImageView* m_imageView;
    QLabel* m_errorLabel;

    std::weak_ptr<ImageInfoData> m_inputData;
    QFutureWatcher<LoadedImage> m_watcher;
    
    LoadedImage m_currentImage;
};

} // namespace QtNodes

#endif // IMAGEDISPLAYNODE_H

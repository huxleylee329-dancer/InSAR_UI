
#include "ImageDisplayNode.h"
#include "FormatConversion.h"

#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QtConcurrent/QtConcurrent>
#include <QFileInfo>
#include <QDebug>
#include <QBuffer>
#include <algorithm>

namespace QtNodes {

ImageDisplayNode::ImageDisplayNode()
    : m_widget(nullptr)
    , m_layout(nullptr)
    , m_infoLabel(nullptr)
    , m_errorLabel(nullptr)
    , m_prevButton(nullptr)
    , m_nextButton(nullptr)
{
    connect(&m_watcher, &QFutureWatcher<LoadedImage>::finished, this, &ImageDisplayNode::onImageLoaded);
}

ImageDisplayNode::~ImageDisplayNode()
{
    m_watcher.cancel();
    m_watcher.waitForFinished();
}

QString ImageDisplayNode::caption() const
{
    return QStringLiteral("Image Preview");
}

QString ImageDisplayNode::name() const
{
    return QStringLiteral("ImageDisplay");
}

unsigned int ImageDisplayNode::nPorts(PortType portType) const
{
    return (portType == PortType::In) ? 1 : 0;
}

NodeDataType ImageDisplayNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 0)
        return NodeDataType{"image_info", "Image"};
    return NodeDataType();
}

void ImageDisplayNode::setInData(std::shared_ptr<NodeData> data, PortIndex portIndex)
{
    auto imageInfo = std::dynamic_pointer_cast<ImageInfoData>(data);
    m_inputData = imageInfo;
    m_currentIndex = 0;

    if (imageInfo && !imageInfo->filePaths().isEmpty())
    {
        loadImageAtIndex();
    }
    else
    {
        updateInfo(tr("无数据"));
        if (m_imageView && m_imageView->scene()) {
            m_imageView->scene()->clear();
        }
        if (m_prevButton) {
            m_prevButton->setEnabled(false);
            m_prevButton->hide();
        }
        if (m_nextButton) {
            m_nextButton->setEnabled(false);
            m_nextButton->hide();
        }
    }
}

void ImageDisplayNode::loadImageAtIndex()
{
    if (!m_inputData || m_currentIndex < 0 || m_currentIndex >= m_inputData->filePaths().size()) return;

    QString filePath = m_inputData->filePaths().at(m_currentIndex);
    int total = m_inputData->filePaths().size();
    
    QString prefix = "";
    if (total > 1) {
        prefix = QString("Image %1 / %2: ").arg(m_currentIndex + 1).arg(total);
    }
    if (m_prevButton) {
        m_prevButton->setVisible(total > 1);
        m_prevButton->setEnabled(m_currentIndex > 0);
    }
    if (m_nextButton) {
        m_nextButton->setVisible(total > 1);
        m_nextButton->setEnabled(m_currentIndex < total - 1);
    }

    updateInfo(prefix + tr("正在加载..."));
    clearError();
    
    QFuture<LoadedImage> future = QtConcurrent::run(loadImageTask, filePath, m_inputData->allMetadata());
    m_watcher.setFuture(future);
}

void ImageDisplayNode::onPrevClicked()
{
    if (m_currentIndex > 0) {
        m_currentIndex--;
        loadImageAtIndex();
    }
}

void ImageDisplayNode::onNextClicked()
{
    if (m_inputData && m_currentIndex < m_inputData->filePaths().size() - 1) {
        m_currentIndex++;
        loadImageAtIndex();
    }
}

QWidget* ImageDisplayNode::embeddedWidget()
{
    if (!m_widget)
    {
        m_widget = new QWidget();
        m_layout = new QVBoxLayout(m_widget);
        m_layout->setContentsMargins(4, 4, 4, 4);
        m_layout->setSpacing(4);

        QWidget* topBar = new QWidget();
        QHBoxLayout* topLayout = new QHBoxLayout(topBar);
        topLayout->setContentsMargins(0, 0, 0, 0);
        topLayout->setSpacing(4);

        m_prevButton = new QPushButton("<");
        m_prevButton->setFixedSize(20, 20);
        m_prevButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_prevButton->setStyleSheet(
            "QPushButton { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; padding: 0px; margin: 0px; border: none; background-color: rgba(0,0,0,60%); color: white; border-radius: 3px; font-weight: bold; }"
            "QPushButton:hover:!disabled { background-color: rgba(0,0,0,85%); }"
            "QPushButton:disabled { background-color: rgba(0,0,0,20%); color: rgba(255,255,255,30%); }"
        );
        m_prevButton->setEnabled(false);
        m_prevButton->hide();
        connect(m_prevButton, &QPushButton::clicked, this, &ImageDisplayNode::onPrevClicked);

        m_infoLabel = new QLabel(tr("等待输入..."));
        m_infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_infoLabel->setStyleSheet("color: #FFFFFF; font-size: 11px; font-weight: bold; background-color: rgba(0, 0, 0, 50%); padding: 2px; border-radius: 2px;");
        m_infoLabel->setAlignment(Qt::AlignCenter);

        m_nextButton = new QPushButton(">");
        m_nextButton->setFixedSize(20, 20);
        m_nextButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_nextButton->setStyleSheet(
            "QPushButton { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; padding: 0px; margin: 0px; border: none; background-color: rgba(0,0,0,60%); color: white; border-radius: 3px; font-weight: bold; }"
            "QPushButton:hover:!disabled { background-color: rgba(0,0,0,85%); }"
            "QPushButton:disabled { background-color: rgba(0,0,0,20%); color: rgba(255,255,255,30%); }"
        );
        m_nextButton->setEnabled(false);
        m_nextButton->hide();
        connect(m_nextButton, &QPushButton::clicked, this, &ImageDisplayNode::onNextClicked);

        topLayout->addWidget(m_prevButton, 0, Qt::AlignLeft | Qt::AlignVCenter);
        topLayout->addWidget(m_infoLabel, 1);
        topLayout->addWidget(m_nextButton, 0, Qt::AlignRight | Qt::AlignVCenter);

        m_imageView = new ImageView();
        m_imageView->setMinimumSize(200, 150);
        m_imageView->setBackgroundBrush(Qt::black);
        m_imageView->setFrameStyle(QFrame::NoFrame);

        auto* scene = new QGraphicsScene(m_imageView);
        m_imageView->setScene(scene);

        m_errorLabel = new QLabel();
        m_errorLabel->setStyleSheet("color: #FF5555; font-weight: bold;");
        m_errorLabel->setAlignment(Qt::AlignCenter);
        m_errorLabel->setWordWrap(true);
        m_errorLabel->hide();

        m_layout->addWidget(topBar);
        m_layout->addWidget(m_imageView, 1);
        m_layout->addWidget(m_errorLabel);

        m_widget->setMinimumSize(250, 200);
        m_widget->installEventFilter(this);
    }
    return m_widget;
}

void ImageDisplayNode::onImageLoaded()
{
    m_currentImage = m_watcher.result();
    
    if (m_currentImage.success)
    {
        clearError();
        
        QString prefix = "";
        if (m_inputData && m_inputData->filePaths().size() > 1) {
            prefix = QString("Image %1 / %2: ").arg(m_currentIndex + 1).arg(m_inputData->filePaths().size());
        }
        updateInfo(prefix + m_currentImage.info);

        if (m_imageView && m_imageView->scene())
        {
            m_imageView->scene()->clear();
            auto* item = m_imageView->scene()->addPixmap(m_currentImage.pixmap);
            m_imageView->fitInView(item, Qt::KeepAspectRatio);
        }
    }
    else
    {
        setError(m_currentImage.errorMessage);
        if (m_imageView && m_imageView->scene())
        {
            m_imageView->scene()->clear();
        }
    }
}

ImageDisplayNode::LoadedImage ImageDisplayNode::loadImageTask(QString filePath, QMap<QString, QString> metadata)
{
    LoadedImage result;
    result.fileName = QFileInfo(filePath).fileName();

    cv::Mat mat;
    if (filePath.toLower().endsWith(".h5"))
    {
        QString dataset = metadata.value("dataset");
        if (dataset.isEmpty())
        {
            result.errorMessage = tr("H5文件需指定数据集名称(metadata['dataset'])");
            result.success = false;
            return result;
        }

        FormatConversion FC;
        if (FC.read_array_from_h5(filePath.toLocal8Bit().constData(), dataset.toLocal8Bit().constData(), mat) != 0)
        {
            result.errorMessage = tr("无法从H5读取数据集: %1").arg(dataset);
            result.success = false;
            return result;
        }
    }
    else
    {
        mat = cv::imread(filePath.toLocal8Bit().constData(), cv::IMREAD_UNCHANGED);
    }

    if (mat.empty())
    {
        result.errorMessage = tr("无法读取图像文件: %1").arg(result.fileName);
        result.success = false;
        return result;
    }

    // Downsample for performance if needed
    double maxDim = 1024.0;
    if (mat.cols > maxDim || mat.rows > maxDim)
    {
        double scale = std::min(maxDim / mat.cols, maxDim / mat.rows);
        cv::resize(mat, mat, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    // Convert cv::Mat to QPixmap
    cv::Mat displayMat;
    if (mat.channels() == 4) {
        cv::cvtColor(mat, displayMat, cv::COLOR_BGRA2RGBA);
    } else if (mat.channels() == 3) {
        cv::cvtColor(mat, displayMat, cv::COLOR_BGR2RGB);
    } else {
        // Grayscale or single channel (e.g. 16-bit)
        double min, max;
        cv::minMaxLoc(mat, &min, &max);
        if (max > min) {
            mat.convertTo(displayMat, CV_8U, 255.0 / (max - min), -min * 255.0 / (max - min));
        } else {
            mat.convertTo(displayMat, CV_8U);
        }
    }

    QImage img;
    if (displayMat.channels() == 1) {
        img = QImage(displayMat.data, displayMat.cols, displayMat.rows, displayMat.step, QImage::Format_Grayscale8);
    } else {
        img = QImage(displayMat.data, displayMat.cols, displayMat.rows, displayMat.step, QImage::Format_RGB888);
    }
    
    result.pixmap = QPixmap::fromImage(img.copy());
    result.info = QString("%1 (%2x%3)").arg(result.fileName).arg(mat.cols).arg(mat.rows);
    result.success = true;

    return result;
}

void ImageDisplayNode::setError(const QString& message)
{
    if (m_errorLabel)
    {
        m_errorLabel->setText(message);
        m_errorLabel->show();
    }
    updateInfo(tr("错误"));
}

void ImageDisplayNode::clearError()
{
    if (m_errorLabel)
    {
        m_errorLabel->hide();
    }
}

void ImageDisplayNode::updateInfo(const QString& info)
{
    if (m_infoLabel)
    {
        m_infoLabel->setText(info);
    }
}

QJsonObject ImageDisplayNode::save() const
{
    QJsonObject json = NodeDelegateModel::save();
    return json;
}

void ImageDisplayNode::load(QJsonObject const &json)
{
    NodeDelegateModel::load(json);
}

bool ImageDisplayNode::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_widget && event->type() == QEvent::Resize)
    {
        onWidgetResized();
    }
    return NodeDelegateModel::eventFilter(watched, event);
}

void ImageDisplayNode::onWidgetResized()
{
    if (m_imageView && m_imageView->scene() && !m_imageView->scene()->items().isEmpty())
    {
        m_imageView->fitInView(m_imageView->scene()->items().first(), Qt::KeepAspectRatio);
    }
}

} // namespace QtNodes

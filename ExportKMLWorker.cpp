#include "ExportKMLWorker.h"
#include "FormatConversion.h"
#include "NodeUtils.h"
#include "Utils.h"
#include "InSARLogManager.h"
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QImage>
#include <QColor>
#include <QFont>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <exception>

using namespace cv;
using namespace std;

ExportKMLWorker::ExportKMLWorker(QObject* parent)
    : BaseWorker(parent)
{
}

ExportKMLWorker::~ExportKMLWorker()
{
}

void ExportKMLWorker::exportKML(QString h5Path, QString outFolder, QString fileName)
{
    try
    {
        NodeUtils::Hdf5Locker locker;
        emit updateProcess(10, QStringLiteral("读取形变数据……"));

        Mat deformation_velocity, mask;

        QString error;
        if (!NodeUtils::readMatFromH5(h5Path, "defomation_velocity", deformation_velocity, -1, &error) ||
            !NodeUtils::readMatFromH5(h5Path, "mask", mask, -1, &error))
        {
            emit errorProcess(error);
            return;
        }
        if (deformation_velocity.empty() || deformation_velocity.dims != 2 ||
            deformation_velocity.channels() != 1 || mask.empty() || mask.dims != 2 ||
            mask.channels() != 1 || mask.size() != deformation_velocity.size())
        {
            emit errorProcess(QStringLiteral("形变数据或掩膜为空、维度无效或尺寸不一致"));
            return;
        }
        if (mask.type() != CV_32S)
        {
            mask.convertTo(mask, CV_32S);
        }

        int Rows = deformation_velocity.rows;
        int Cols = deformation_velocity.cols;

        if (deformation_velocity.type() != CV_64F)
        {
            deformation_velocity.convertTo(deformation_velocity, CV_64F);
        }

        double BottomLeft_lon = 0, BottomLeft_lat = 0, BottomRight_lon = 0, BottomRight_lat = 0,
            TopRight_lon = 0, TopRight_lat = 0, TopLeft_lon = 0, TopLeft_lat = 0, ref_lon = 0.0, ref_lat = 0.0;
        int ref_row = 0, ref_col = 0;
        if (!NodeUtils::readScalarFromH5(h5Path, "ref_row", ref_row, &error) ||
            !NodeUtils::readScalarFromH5(h5Path, "ref_col", ref_col, &error))
        {
            emit errorProcess(error);
            return;
        }
        if (ref_row < 0 || ref_row >= Rows || ref_col < 0 || ref_col >= Cols)
        {
            emit errorProcess(QStringLiteral("参考点 (%1, %2) 超出形变数据范围 (%3 行, %4 列)")
                .arg(ref_row).arg(ref_col).arg(Rows).arg(Cols));
            return;
        }

        for (const QString& dataset : { QStringLiteral("mapped_lat"), QStringLiteral("mapped_lon") })
        {
            int rows = 0, cols = 0;
            if (!NodeUtils::probeH5DatasetMetadata(h5Path, dataset, &rows, &cols, &error))
            {
                emit errorProcess(error);
                return;
            }
            if (rows != Rows || cols != Cols)
            {
                emit errorProcess(QStringLiteral("坐标数据集 %1 的尺寸 (%2, %3) 与形变数据 (%4, %5) 不一致")
                    .arg(dataset).arg(rows).arg(cols).arg(Rows).arg(Cols));
                return;
            }
        }

        FormatConversion FC;
        const QByteArray inputPath = h5Path.toUtf8();
        const auto readCoordinate = [&](const char* dataset, int row, int col, double limit, double& value)
        {
            // 每次使用独立矩阵，避免读取失败时复用上一个坐标。
            Mat sample;
            const int rc = FC.read_subarray_from_h5(inputPath.constData(), dataset, row, col, 1, 1, sample);
            if (rc != 0 || sample.empty() || sample.dims != 2 || sample.rows != 1 || sample.cols != 1 ||
                (sample.type() != CV_64FC1 && sample.type() != CV_32FC1))
            {
                error = QStringLiteral("读取坐标 %1(%2, %3) 失败或数据类型无效 (rc=%4, type=%5)")
                    .arg(QString::fromLatin1(dataset)).arg(row).arg(col).arg(rc).arg(sample.type());
                return false;
            }
            value = sample.type() == CV_64FC1 ? sample.at<double>(0, 0) : sample.at<float>(0, 0);
            if (!std::isfinite(value) || value < -limit || value > limit)
            {
                error = QStringLiteral("坐标 %1(%2, %3) 不是有效的经纬度: %4")
                    .arg(QString::fromLatin1(dataset)).arg(row).arg(col).arg(value);
                return false;
            }
            return true;
        };
        if (!readCoordinate("mapped_lat", ref_row, ref_col, 90.0, ref_lat) ||
            !readCoordinate("mapped_lon", ref_row, ref_col, 180.0, ref_lon) ||
            !readCoordinate("mapped_lat", 0, 0, 90.0, TopLeft_lat) ||
            !readCoordinate("mapped_lat", 0, Cols - 1, 90.0, TopRight_lat) ||
            !readCoordinate("mapped_lat", Rows - 1, 0, 90.0, BottomLeft_lat) ||
            !readCoordinate("mapped_lat", Rows - 1, Cols - 1, 90.0, BottomRight_lat) ||
            !readCoordinate("mapped_lon", 0, 0, 180.0, TopLeft_lon) ||
            !readCoordinate("mapped_lon", 0, Cols - 1, 180.0, TopRight_lon) ||
            !readCoordinate("mapped_lon", Rows - 1, 0, 180.0, BottomLeft_lon) ||
            !readCoordinate("mapped_lon", Rows - 1, Cols - 1, 180.0, BottomRight_lon))
        {
            emit errorProcess(error);
            return;
        }

        QString jpgPath = outFolder + "/" + fileName + ".jpg";
        QString colorbarPath = outFolder + "/Colorbar.png";
        QString kmlPath = outFolder + "/" + fileName + ".kml";

        Utils util;
        emit updateProcess(35, QStringLiteral("生成形变渲染图……"));
        if (util.savephase_white(jpgPath.toStdString().c_str(), "jet", deformation_velocity, mask) != 0)
        {
            emit errorProcess(QStringLiteral("生成形变渲染图失败: %1").arg(jpgPath));
            return;
        }

        if (QThread::currentThread()->isInterruptionRequested())
        {
            QFile::remove(jpgPath);
            emit cancelled();
            return;
        }

        double mMax = 0, mMin = 0;
        cv::minMaxIdx(deformation_velocity, &mMin, &mMax, NULL, NULL);

        emit updateProcess(60, QStringLiteral("生成图例色带……"));
        if (!paintColorbar(mMin, mMax, colorbarPath))
        {
            emit errorProcess(QStringLiteral("保存图例色带失败: %1").arg(colorbarPath));
            return;
        }

        if (QThread::currentThread()->isInterruptionRequested())
        {
            QFile::remove(jpgPath);
            QFile::remove(colorbarPath);
            emit cancelled();
            return;
        }

        emit updateProcess(80, QStringLiteral("正在计算地理坐标并生成KML……"));
        if (util.writeOverlayKML(BottomLeft_lon, BottomLeft_lat, BottomRight_lon, BottomRight_lat, TopRight_lon, TopRight_lat,
            TopLeft_lon, TopLeft_lat, ref_lon, ref_lat,
            (fileName + ".jpg").toStdString().c_str(), kmlPath.toStdString().c_str(), "Colorbar.png") != 0)
        {
            emit errorProcess(QStringLiteral("写入 KML 文件失败: %1").arg(kmlPath));
            return;
        }

        InSARLogManager::LogInfo("ExportKMLWorker", "KML export completed successfully: " + kmlPath);
        emit endProcess();
    }
    catch (const std::exception& exception)
    {
        emit errorProcess(QStringLiteral("KML 导出失败: %1").arg(QString::fromUtf8(exception.what())));
    }
}

bool ExportKMLWorker::paintColorbar(double mMin, double mMax, QString save_path)
{
    int mWidth = 150;
    int mHeight = 400;
    QImage map(mWidth, mHeight, QImage::Format_RGB32);
    map.fill(Qt::white);
    QPainter painter(&map);
    QPen Pen_frame;
    QPen Pen_color;
    Pen_frame.setColor(Qt::black);
    Pen_frame.setWidth(2);
    painter.setPen(Pen_frame);
    QPoint mPos_Right_Top(mWidth, 0);
    int margin_width = mWidth * 2 / 5;
    int margin_height = mHeight * 1 / 10 / 2;
    int Rect_width = mWidth - 2 * margin_width;
    int Rect_height = mHeight - 2 * margin_height;
    int Rect_Left = mPos_Right_Top.x() - mWidth + margin_width;
    int Rect_Right = mPos_Right_Top.x() - margin_width;
    int Rect_Top = mPos_Right_Top.y() + margin_height;
    int Rect_Bottom = mPos_Right_Top.y() + margin_height + Rect_height;

    double V_Range = mMax - mMin;
    double mInternal = V_Range / 4;
    for (int i = 0; i < Rect_height; i++)
    {
        int s = i * 255 / Rect_height;
        if (s < 32)
        {
            Pen_color.setColor(QColor(128 + s * 4, 0, 0));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s == 32)
        {
            Pen_color.setColor(QColor(255, 0, 0));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s < 96)
        {
            Pen_color.setColor(QColor(255, 4 * (s - 32), 0));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s < 159)
        {
            Pen_color.setColor(QColor(254 - 4 * (s - 96), 255, 2 + 4 * (s - 96)));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s == 159)
        {
            Pen_color.setColor(QColor(1, 255, 254));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s < 224)
        {
            Pen_color.setColor(QColor(0, 252 - 4 * (s - 160), 255));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else
        {
            Pen_color.setColor(QColor(0, 0, 252 - 4 * (s - 224)));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
    }
    painter.drawRect(Rect_Left, Rect_Top, Rect_width, Rect_height);
    for (int i = 0; i < 5; i++)
    {
        Pen_color.setColor(Qt::black);
        Pen_color.setWidth(2);
        painter.setPen(Pen_color);
        painter.drawLine(Rect_Right, Rect_Bottom - i * Rect_height / 4, Rect_Right + 10, Rect_Bottom - i * Rect_height / 4);
        QFont TextFont;
        TextFont.setPixelSize(20);
        painter.setFont(TextFont);
        painter.drawText(QPoint(Rect_Right + 10, Rect_Bottom - i * Rect_height / 4), QString::number(mMin + mInternal * i, 'f', 2));
    }
    return map.save(save_path, "PNG");
}

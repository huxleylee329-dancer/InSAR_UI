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
    NodeUtils::Hdf5Locker locker;
    emit updateProcess(10, QStringLiteral("读取形变数据……"));
    
    Mat deformation_velocity, mask;
    
    int ret = -1;
    if (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", deformation_velocity) &&
        NodeUtils::readMatFromH5(h5Path, "mask", mask))
    {
        ret = 0;
    }
    
    if (ret != 0)
    {
        emit errorProcess(QStringLiteral("读取H5数据失败"));
        return;
    }
    
    int Rows = deformation_velocity.rows;
    int Cols = deformation_velocity.cols;
    
    if (deformation_velocity.type() != CV_64F)
    {
        deformation_velocity.convertTo(deformation_velocity, CV_64F);
    }
    
    QString jpgPath = outFolder + "/" + fileName + ".jpg";
    QString colorbarPath = outFolder + "/Colorbar.png";
    QString kmlPath = outFolder + "/" + fileName + ".kml";
    
    Utils util;
    emit updateProcess(35, QStringLiteral("生成形变渲染图……"));
    util.savephase_white(jpgPath.toStdString().c_str(), "jet", deformation_velocity, mask);
    
    if (QThread::currentThread()->isInterruptionRequested())
    {
        QFile::remove(jpgPath);
        emit cancelled();
        return;
    }
    
    double mMax = 0, mMin = 0;
    cv::minMaxIdx(deformation_velocity, &mMin, &mMax, NULL, NULL);
    
    emit updateProcess(60, QStringLiteral("生成图例色带……"));
    paintColorbar(mMin, mMax, colorbarPath);
    
    if (QThread::currentThread()->isInterruptionRequested())
    {
        QFile::remove(jpgPath);
        QFile::remove(colorbarPath);
        emit cancelled();
        return;
    }
    
    emit updateProcess(80, QStringLiteral("正在计算地理坐标并生成KML……"));
    Mat tmp;
    double BottomLeft_lon = 0, BottomLeft_lat = 0, BottomRight_lon = 0, BottomRight_lat = 0,
        TopRight_lon = 0, TopRight_lat = 0, TopLeft_lon = 0, TopLeft_lat = 0, ref_lon = 0.0, ref_lat = 0.0;
    int ref_row = 0, ref_col = 0;
    
    {
        NodeUtils::Hdf5Locker locker;
        FormatConversion FC;
        NodeUtils::readScalarFromH5(h5Path, "ref_row", ref_row);
        NodeUtils::readScalarFromH5(h5Path, "ref_col", ref_col);
        
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lat", ref_row, ref_col, 1, 1, tmp);
        ref_lat = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lon", ref_row, ref_col, 1, 1, tmp);
        ref_lon = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lat", 0, 0, 1, 1, tmp);
        TopLeft_lat = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lat", 0, Cols-1, 1, 1, tmp);
        TopRight_lat = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lat", Rows-1, 0, 1, 1, tmp);
        BottomLeft_lat = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lat", Rows-1, Cols - 1, 1, 1, tmp);
        BottomRight_lat = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lon", 0, 0, 1, 1, tmp);
        TopLeft_lon = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lon", 0, Cols - 1, 1, 1, tmp);
        TopRight_lon = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lon", Rows - 1, 0, 1, 1, tmp);
        BottomLeft_lon = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
        FC.read_subarray_from_h5(h5Path.toStdString().c_str(), "mapped_lon", Rows - 1, Cols - 1, 1, 1, tmp);
        BottomRight_lon = tmp.type() == CV_64F ? tmp.at<double>(0, 0) : tmp.at<float>(0, 0);
    }
    
    util.writeOverlayKML(BottomLeft_lon, BottomLeft_lat, BottomRight_lon, BottomRight_lat, TopRight_lon, TopRight_lat,
        TopLeft_lon, TopLeft_lat, ref_lon, ref_lat,
        (fileName + ".jpg").toStdString().c_str(), kmlPath.toStdString().c_str(), "Colorbar.png");
        
    InSARLogManager::LogInfo("ExportKMLWorker", "KML export completed successfully: " + kmlPath);
    emit endProcess();
}

void ExportKMLWorker::paintColorbar(double mMin, double mMax, QString save_path)
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
    map.save(save_path, "PNG");
}

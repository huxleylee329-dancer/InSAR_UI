#include <ColorBar.h>

ColorBar::ColorBar(QWidget* parent) : QLabel(parent)
{
    this->resize(250,400);
    this->setMinimumWidth(250);
    this->setMinimumHeight(400);

    mPos_Right_Top = QPoint(this->width(),0);
    update();
    this->setWindowFlags(Qt::Dialog |Qt::FramelessWindowHint| Qt::Tool );
    setTheme("light");
}

ColorBar::~ColorBar()
{

}

void ColorBar::setTheme(const QString& theme)
{
    if (theme == "dark") {
        this->setStyleSheet("QLabel {background-color: #2d2d30; border:3px solid #555555}");
    } else {
        this->setStyleSheet("QLabel {background-color: white; border:3px solid gray}");
    }
}

int ColorBar::SetData(QString Data_path, QString Type)
{
    if (Data_path == NULL ||
        Type == NULL)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到数据,请确保数据没有被删除或移动"));
        return -1;
    }
    if (Type == "phase" ||
        Type == "coherence" ||
        Type == "dem") 
    {
        FormatConversion FC;
        Mat V;
        FC.read_array_from_h5(Data_path.toStdString().c_str(), Type.toStdString().c_str(), V);
        mType = Type;
        cv::minMaxLoc(V, &mMin, &mMax, NULL, NULL);
        update();
        return 0;
    }
    else if (Type == "SBAS")
    {
        FormatConversion FC;
        Mat V;
        FC.read_array_from_h5(Data_path.toStdString().c_str(), "defomation_velocity", V);
        mType = Type;
        cv::minMaxLoc(V, &mMin, &mMax, NULL, NULL);
        update();
        return 0;
    }
    return -1;
}

void ColorBar::resizeEvent(QResizeEvent* event)
{
    mWidth = this->width();
    mHeight = this->height();
   
    mPos_Right_Top = QPoint(this->width(), 0);
    update();
}

void ColorBar::paintEvent(QPaintEvent* event)
{
    //QLabel::paintEvent(event);
    QPainter painter(this);
    QPen Pen_frame;
    QPen Pen_color;
    Pen_frame.setColor(Qt::black);
    Pen_frame.setWidth(2);
    painter.setPen(Pen_frame);
    int margin_width = mWidth * 3 / 5 / 2;
    int margin_height = mHeight * 1 / 10 / 2;
    int Rect_width = mWidth - 2 * margin_width;
    int Rect_height = mHeight - 2 * margin_height;
    int Rect_Left = mPos_Right_Top.x() - mWidth + margin_width/2;
    int Rect_Right = mPos_Right_Top.x() - margin_width/2*3;
    int Rect_Top = mPos_Right_Top.y() + margin_height;
    int Rect_Bottom = mPos_Right_Top.y() + margin_height + Rect_height;
    
    
    if (mType == "phase" ||
        mType == "SBAS"  ||
        mType == "dem"
        )
    {
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
    }
    this->adjustSize();
}

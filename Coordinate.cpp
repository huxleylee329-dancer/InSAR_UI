#include<Coordinate.h>
#include<cstdlib>
#include<ctime>
#include"opencv2\opencv.hpp"
#define inf 0x3f3f3f3f
Coordinate::Coordinate(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Coordinate)
{
    ui->setupUi(this);
    image = QImage(wnd_width, wnd_height, QImage::Format_RGB32);  //初始化画布
    QColor backColor = qRgb(255, 255, 255);  
    image.fill(backColor);//对画布进行填充
    this->setFixedSize(wnd_width,wnd_height);
    this->setWindowTitle(QStringLiteral("基线预览"));
    mChart = new QChart();
    mChart->setTitle(QStringLiteral("小基线集时空基线组合"));
    mChartView = new QChartView(mChart, this);
    mChart = mChartView->chart();   //关联
    mAxisX = new QValueAxis();
    mAxisY = new QValueAxis();
    mChart->setAxisX(mAxisX);
    mChart->setAxisY(mAxisY);
    mChartView->setRenderHint(QPainter::Antialiasing); //抗锯齿

    mLabel = new QLabel(mChartView);
}

Coordinate::~Coordinate()
{
    delete ui;
}

void Coordinate::Paint2(
    QList<double> temporal_baseline, 
    QList<double> spatial_baseline,
    int index, double temporal_thresh, double temporal_thresh_low,
    double spatial_thresh
)
{
    cv::Mat formation_matrix(temporal_baseline.size(), temporal_baseline.size(), CV_32S);
    formation_matrix = 0;
    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        for (int j = 0; j < i; j++)
        {
            if (fabs(spatial_baseline[j] - spatial_baseline[i]) < spatial_thresh &&
                fabs((temporal_baseline[j] - temporal_baseline[i]) ) < temporal_thresh &&
                fabs((temporal_baseline[j] - temporal_baseline[i]) ) > temporal_thresh_low)
            {
                formation_matrix.at<int>(i, j) = 1;
            }
        }
    } 
    double Min_time = 0, Max_time = 0, Min_space = 0, Max_space = 0;
    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        if (Min_time > temporal_baseline.at(i))
            Min_time = temporal_baseline.at(i);
        if (Max_time < temporal_baseline.at(i))
            Max_time = temporal_baseline.at(i);
        if (Min_space > spatial_baseline.at(i))
            Min_space = spatial_baseline.at(i);
        if (Max_space < spatial_baseline.at(i))
            Max_space = spatial_baseline.at(i);

    }
    double pad_time = (Max_time - Min_time) / 10;
    double pad_space = (Max_space - Min_space) / 10;
    mAxisX->setRange(Min_time - pad_time, Max_time + pad_time);
    mAxisY->setRange(Min_space - pad_space, Max_space + pad_space);
    mAxisX->setTitleText(QStringLiteral("时间基线/（天）"));
    mAxisY->setTitleText(QStringLiteral("空间基线/（米）"));

    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        QPointF Origin(temporal_baseline.at(i), spatial_baseline.at(i));
        QScatterSeries* mPoint_edge = new QScatterSeries();
        QScatterSeries* mPoint_central = new QScatterSeries();
        mPoint_edge->setBorderColor(QColor(255, 0, 0));
        mPoint_edge->setBrush(QColor(255, 0, 0));
        mPoint_central->setMarkerSize(16);
        mPoint_edge->setMarkerSize(20);
        mPoint_edge->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        mPoint_central->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        mPoint_central->setBorderColor(Qt::white);
        mPoint_central->setBrush(Qt::white);
        mPoint_edge->append(QPointF(temporal_baseline.at(i), spatial_baseline.at(i)));
        mPoint_central->append(QPointF(temporal_baseline.at(i), spatial_baseline.at(i)));
        mChart->addSeries(mPoint_edge);
        mChart->addSeries(mPoint_central);
        mChart->setAxisX(mAxisX, mPoint_edge);
        mChart->setAxisY(mAxisY, mPoint_edge);
        mChart->setAxisX(mAxisX, mPoint_central);
        mChart->setAxisY(mAxisY, mPoint_central);
        connect(mPoint_central, &QScatterSeries::hovered, this, &Coordinate::ShowData);
        for (int j = 0; j < i; j++)
        {
            if (formation_matrix.at<int>(i, j) == 1)
            {
                QLineSeries* mLine = new QLineSeries();
                mLine->append(Origin);
                mLine->append(QPointF(temporal_baseline.at(j), spatial_baseline.at(j)));
                mChart->addSeries(mLine);
                mChart->setAxisX(mAxisX, mLine);
                mChart->setAxisY(mAxisY, mLine);
            }
        }
        mChart->legend()->setVisible(0);
        
    }
}

void Coordinate::resizeEvent(QResizeEvent* event)
{
    mChartView->resize(this->size());
}

void Coordinate::ShowData(const QPointF& point, bool state)
{
    if (state)
    {
        mLabel->setText(QString("(%1,%2)").arg(QString::number(point.x(), 'f', 1)).arg(QString::number(point.y(), 'f', 1)));
        mLabel->setStyleSheet("QLabel { background-color : rgb(129, 199, 212); color : rgb(0, 92, 175); border-radius:3px;font:20pt }");
        QPoint curPos = mapFromGlobal(QCursor::pos());
        mLabel->move(curPos.x() - mLabel->width() / 2, curPos.y() - mLabel->height() * 1.5);//移动数值 m_valueLabel->show();//显示出来
        mLabel->show();
    }
    else
        mLabel->hide();
}

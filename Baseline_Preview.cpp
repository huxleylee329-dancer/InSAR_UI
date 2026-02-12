#include<Baseline_Preview.h>

Baseline_Preview::Baseline_Preview(QWidget* parent) :
    QWidget(parent)
{
    this->resize(QSize(400, 400));
    this->setWindowTitle(QString::fromLocal8Bit("基线预览"));
    mChart = new QChart();
    mChart->setTitle(QString::fromLocal8Bit("时空基线"));
    mChartView = new QChartView(mChart, this);
    mChart = mChartView->chart();   //关联
    mAxisX = new QValueAxis();
    mAxisY = new QValueAxis();
    mChart->setAxisX(mAxisX);
    mChart->setAxisY(mAxisY);
    mChartView->setRenderHint(QPainter::Antialiasing); //抗锯齿
   
    mLabel = new QLabel(mChartView);
}

Baseline_Preview::~Baseline_Preview()
{
        
}

void Baseline_Preview::resizeEvent(QResizeEvent* event)
{
    /*窗口变化时重载图片并更改尺寸*/
    mChartView->resize(this->size());
}

void Baseline_Preview::Paint(QList<double> temporal_baseline, QList<double> spatial_baseline, int index)
{
    QPointF Origin(temporal_baseline.at(index-1), spatial_baseline.at(index-1));
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
    mAxisX->setTitleText(QString::fromLocal8Bit("时间基线/（天）"));
    mAxisY->setTitleText(QString::fromLocal8Bit("空间基线/（米）"));
    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        if (i == index - 1)
        {
            continue;
        }

        QLineSeries* mLine = new QLineSeries();
        mLine->append(Origin);
        mLine->append(QPointF(temporal_baseline.at(i), spatial_baseline.at(i)));
        mChart->addSeries(mLine);
        mChart->setAxisX(mAxisX, mLine);
        mChart->setAxisY(mAxisY, mLine);
    }
    for (int i = 0; i < temporal_baseline.size(); i++)
    {
        QScatterSeries* mPoint_edge = new QScatterSeries();
        QScatterSeries* mPoint_central = new QScatterSeries();
        if (i == index - 1)
        {
            mPoint_edge->setBorderColor(QColor(255, 0, 0));
            mPoint_edge->setBrush(QColor(255, 0, 0));
            mPoint_central->setMarkerSize(16);
            mPoint_edge->setMarkerSize(20);
        }
        else
        {
            mPoint_edge->setBorderColor(QColor(0, 0, 0));
            mPoint_edge->setBrush(QColor(0, 0, 0));
            mPoint_edge->setMarkerSize(15);
            mPoint_central->setMarkerSize(12);
        }
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
        connect(mPoint_central, &QScatterSeries::hovered, this, &Baseline_Preview::ShowData);
    }
    
    
}

void Baseline_Preview::ShowData(const QPointF& point, bool state)
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
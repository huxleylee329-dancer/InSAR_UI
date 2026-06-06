#pragma once
#include<ui_Coordinate.h>
#include<QtWidgets/QMainWindow>
#include<QtCharts/QtCharts>
#include<vector>
#include<QMouseEvent>
#include<QPaintEvent>
#include<QtGui>
#include"opencv2\opencv.hpp"

using namespace std;

class Coordinate : public QWidget
{
    Q_OBJECT

public:
    explicit Coordinate( QWidget* parent = 0);
    ~Coordinate();
    QChart* chart() const { return mChart; }
public slots:
    void Paint(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
    void Paint2(QList<double> temporal_baseline, QList<double> spatial_baseline, int index, double temporal_thresh, double temporal_thresh_low, double spatial_thresh);
protected:
    void paintEvent(QPaintEvent*) {
        QPainter painter(this);
        painter.drawImage(0, 0, image);
    }

private:
    Ui::Coordinate* ui;
    int wnd_width = /*2000*/1000;
    int wnd_height = /*1500*/800;
    QImage image;
    QChart* mChart;
    QChartView* mChartView;
    QLabel* mLabel;
    QValueAxis* mAxisX;
    QValueAxis* mAxisY;
    void resizeEvent(QResizeEvent* event);
private slots:
    void ShowData(const QPointF& point, bool state);
};
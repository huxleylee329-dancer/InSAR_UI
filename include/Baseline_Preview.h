#pragma once
#include<QtCharts/QtCharts>

QT_CHARTS_USE_NAMESPACE
#ifdef DEBUG
#pragma comment(lib, "Qt5Chartsd.lib")
#else
#pragma comment(lib, "Qt5Charts.lib")
#endif // DEBUG

class Baseline_Preview : public QWidget
{
	Q_OBJECT
public:
	Baseline_Preview(QWidget* parent = Q_NULLPTR);
	~Baseline_Preview();
	void resizeEvent(QResizeEvent* event);
public slots:
	void Paint(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
protected:

private:
	QChart* mChart;
	QChartView* mChartView;
	QLabel* mLabel;
	QValueAxis* mAxisX;
	QValueAxis* mAxisY;
private slots:
	void ShowData(const QPointF& point, bool state);
};
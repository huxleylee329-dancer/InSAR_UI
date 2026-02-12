#pragma once
#include<Baseline_Preview.h>
#include<Utils.h>
#include<FormatConversion.h>

#ifdef DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif // DEBUG

class ColorBar : public QLabel
{
	Q_OBJECT
public:
	ColorBar(QWidget* parent = Q_NULLPTR);
	~ColorBar();
	int SetData(QString Data_path, QString Type);
	void resizeEvent(QResizeEvent* event);
protected:
	void paintEvent(QPaintEvent* event);
private:
	int mHeight;
	int mWidth;
	double mMin;
	double mMax;
	QString mType;
	QPoint mPos_Right_Top;
};
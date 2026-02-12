#pragma once
#include<Baseline_Preview.h>
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Export_KML.h"
#include"MyThread.h"

class Export_KML : public QWidget
{
    Q_OBJECT
public:
    explicit Export_KML(QWidget* parent = Q_NULLPTR);
    ~Export_KML();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void Paint_Colorbar(double mMin, double mMax, QString save_path);
private:
    Ui::Export_KML* ui;
    QStandardItemModel* copy;
private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();
    void on_Browse_pressed();
    void on_Export_pressed();
    void ChangeVision(bool Editable);
}; 

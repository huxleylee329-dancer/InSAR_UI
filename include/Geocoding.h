#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Geocoding.h"
#include "GeocodingWorker.h"

class Geocoding : public QWidget
{
    Q_OBJECT
public:
    explicit Geocoding(QWidget* parent = Q_NULLPTR);
    ~Geocoding();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
signals:
    void operate(int, int, int, QString, QString, QString, QStandardItemModel*, QString dem_path);
    void sendCopy(QStandardItemModel*);

private:
    Ui::Geocoding* ui;
    QStandardItemModel* copy;
    GeocodingWorker* Geocoding_thread;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);

    QLabel* m_demPathLabel1 = nullptr;
    QLineEdit* m_demPathEdit1 = nullptr;
    QPushButton* m_demBrowseBtn1 = nullptr;

    QLabel* m_demPathLabel2 = nullptr;
    QLineEdit* m_demPathEdit2 = nullptr;
    QPushButton* m_demBrowseBtn2 = nullptr;
private slots:
    /*工程选择按钮响应函数*/
    void on_comboBox_project1_currentIndexChanged();
    /*工程选择按钮响应函数*/
    void on_comboBox_project2_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_buttonBox_2_accepted();
    void on_buttonBox_2_rejected();
};
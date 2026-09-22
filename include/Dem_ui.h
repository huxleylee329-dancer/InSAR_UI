#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Dem.h"
#include "DemWorker.h"
#include <QPointer>

class Dem_ui : public QWidget
{
    Q_OBJECT
public:
    explicit Dem_ui(QWidget* parent = Q_NULLPTR);
    ~Dem_ui();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
private:
    Ui::Dem* ui = nullptr;
    QStandardItemModel* copy = nullptr;
    QPointer<DemWorker> Dem_thread;
    QString save_path;
    int method = 0;
    int image_number = 0;
    void ChangeVision(bool Editable);
    void persistDemResult(const DemFileResult& result, int times,
                          const QString& projectName, const QString& savePath);
signals:
    void operate(int, int, QString, QString, QStringList, QStringList);
    void sendCopy(QStandardItemModel*);
private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();
    // void on_masterpushButton_pressed();
     //void on_slavepushButton_pressed()
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void Change_Setting();
};

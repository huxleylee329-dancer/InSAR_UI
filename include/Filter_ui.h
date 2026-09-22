#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Filter.h"
#include "DenoiseWorker.h"
#include <QPointer>

class Filter_ui : public QWidget
{
    Q_OBJECT
public:
    explicit Filter_ui(QWidget* parent = Q_NULLPTR);
    ~Filter_ui();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
private:
    Ui::Filter* ui = nullptr;
    QStandardItemModel* copy = nullptr;
    QPointer<DenoiseWorker> Filter_thread;
    QString save_path;
    int method = 0;
    int image_number = 0;
    void ChangeVision(bool Editable);
    void persistDenoiseResult(const DenoiseFileResult& result, const QList<int>& para,
                              double alpha, const QString& projectName, const QString& savePath);
signals:
    void operate(QList<int>, double, QString, QString, QStringList, QStringList);
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

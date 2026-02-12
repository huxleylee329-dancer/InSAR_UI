#pragma once
#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_NewProject.h"

class NewProject : public QWidget
{
    Q_OBJECT
public:
    explicit NewProject(QWidget* parent = Q_NULLPTR);
    ~NewProject();
    QString project;
    QString save;
private:
    Ui::NewProject* ui;
    
    void saveProjectSettings();
    void saveSystemSettings();

signals:
    void sendPath(QString,QString);
public slots:
    void ReceiveModel(QStandardItemModel*);
private slots:
   // void on_masterpushButton_pressed();
    //void on_slavepushButton_pressed();
    void on_savepushButton_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    
private:
    QStandardItemModel* copy_model;
};

#pragma once
#include <QtWidgets/QMainWindow>
#include "ui_OpenProject.h"
#include<QStandardItem>
#include<FormatConversion.h>

class OpenProject : public QWidget
{
    Q_OBJECT
public:
    explicit OpenProject(QWidget* parent = Q_NULLPTR);
    ~OpenProject();
    QStandardItemModel* model;
public slots:
    void LoadModel(QStandardItemModel*);
private:
    Ui::OpenProject* ui;
    XMLFile* project;

signals:
    void sendModel(QStandardItemModel* );
private slots:
    void on_BrowseButton_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
};
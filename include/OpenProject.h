#pragma once
#include <QtWidgets/QMainWindow>
#include "ui_OpenProject.h"
#include <QDialog>
#include<QStandardItem>
#include<FormatConversion.h>

class OpenProject : public QDialog
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
    void projectOpened(const QString& filePath);
    void aboutToLoadProject();
private slots:
    void on_BrowseButton_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
};
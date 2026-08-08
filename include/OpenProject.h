#pragma once
#include <QtWidgets/QMainWindow>
#include "ui_OpenProject.h"
#include <QDialog>

class OpenProject : public QDialog
{
    Q_OBJECT
public:
    explicit OpenProject(QWidget* parent = Q_NULLPTR);
    ~OpenProject();
private:
    Ui::OpenProject* ui;

signals:
    void projectSelected(const QString& filePath);
private slots:
    void on_BrowseButton_pressed();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
};

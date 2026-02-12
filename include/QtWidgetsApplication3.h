#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_QtWidgetsApplication3.h"

class QtWidgetsApplication3 : public QMainWindow
{
    Q_OBJECT

public:
    QtWidgetsApplication3(QWidget *parent = Q_NULLPTR);

private:
    Ui::QtWidgetsApplication3Class ui;
};

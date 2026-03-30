#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_SatExplorer.h"

class SatExplorer : public QMainWindow
{
    Q_OBJECT

public:
    SatExplorer(QWidget *parent = Q_NULLPTR);

private:
    Ui::SatExplorerClass ui;
};

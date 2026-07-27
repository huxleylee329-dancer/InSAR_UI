#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include <QStringList>
#include "ui_Registration.h"
#include "CoregistrationWorker.h"

class Registration_ui : public QWidget
{
    Q_OBJECT
public:
    explicit Registration_ui(QWidget* parent = Q_NULLPTR);
    ~Registration_ui();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::Registration* ui;
    QStandardItemModel* copy;
    CoregistrationWorker* Registration_thread;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);
    void persistGeneratedOutput(const QStringList& outputNames, const QStringList& outputPaths,
                                const QList<int>& offsetRows, const QList<int>& offsetCols,
                                const QString& temporalBaseline, const QString& effectiveBaseline,
                                const QString& parallelBaseline);
    QString m_activeProjectName;
    QString m_activeOutputNode;
    int m_activeMasterIndex = 1;
    int m_activeInterpTimes = -1;
    int m_activeBlockSize = -1;
signals:
    void operate(QList<int>, QString, QString, QString, QStringList);
    void operate2(int, QString, QString, QString, QStringList);
    void sendCopy(QStandardItemModel*);
private slots:
    void on_comboBox_currentIndexChanged();
    /*DEM辅助配准工程选择响应函数*/
    void on_comboBox_project_currentIndexChanged();
    /*DEM辅助数据节点工程选择响应函数*/
    void on_comboBox_node_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();
    // void on_masterpushButton_pressed();
     //void on_slavepushButton_pressed()
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_buttonBox_2_accepted();
    void on_buttonBox_2_rejected();
};

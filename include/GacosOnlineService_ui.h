#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_GacosOnlineService.h"
#include "GacosOnlineServiceWorker.h"

class GacosOnlineService_ui : public QWidget
{
    Q_OBJECT
public:
    explicit GacosOnlineService_ui(QWidget* parent = nullptr);
    ~GacosOnlineService_ui();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void updateProcess(int value, QString information);
    void endProcess();
    void StopThread();

signals:
    void operate(QString apiKey, QString email, int dataFormat,
                 QString save_path, QString project_name,
                 QString file_name, QStringList inputPaths);
    void sendCopy(QStandardItemModel* model);

private slots:
    void on_comboBox_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void handleResults(const QString& dstNode, const QStringList& outputNames,
                       const QStringList& outputPaths, const QString& savePath,
                       const QString& projectName);

private:
    Ui::GacosOnlineService* ui;
    QStandardItemModel* copy;
    GacosOnlineServiceWorker* m_worker;
    QString save_path;

    void ChangeVision(bool Editable);
};

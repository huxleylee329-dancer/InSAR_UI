#include"Export_KML.h"
#include"ExportKMLWorker.h"
#include"Coordinate.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<Utils.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
Export_KML::Export_KML(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ExportKml),
    m_thread(nullptr),
    m_worker(nullptr)
{
    ui->setupUi(this);

}
Export_KML::~Export_KML()
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
    }
}

void Export_KML::updateProcess(int progress, QString message)
{
    ui->Export->setText(QString("%1%: %2").arg(progress).arg(message));
}

void Export_KML::endProcess()
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
    }
    m_worker = nullptr;
    QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("KML数据导出成功！"));
    this->close();
}

void Export_KML::errorProcess(QString error_msg)
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
    }
    m_worker = nullptr;
    QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("导出失败: ") + error_msg);
    ChangeVision(true);
    ui->Export->setText(QStringLiteral("开始导出"));
}

void Export_KML::Paint_Colorbar(double mMin, double mMax, QString save_path)
{
    int mWidth = 150;
    int mHeight = 400;
    QPixmap map(mWidth, mHeight);
    map.fill(Qt::white);
    QPainter painter(&map);
    QPen Pen_frame;
    QPen Pen_color;
    Pen_frame.setColor(Qt::black);
    Pen_frame.setWidth(2);
    painter.setPen(Pen_frame);
    QPoint mPos_Right_Top(mWidth, 0);
    int margin_width = mWidth * 2 / 5  ;
    int margin_height = mHeight * 1 / 10 / 2;
    int Rect_width = mWidth - 2 * margin_width;
    int Rect_height = mHeight - 2 * margin_height;
    int Rect_Left = mPos_Right_Top.x() - mWidth + margin_width;
    int Rect_Right = mPos_Right_Top.x() - margin_width;
    int Rect_Top = mPos_Right_Top.y() + margin_height;
    int Rect_Bottom = mPos_Right_Top.y() + margin_height + Rect_height;

    double V_Range = mMax - mMin;
    double mInternal = V_Range / 4;
    for (int i = 0; i < Rect_height; i++)
    {
        int s = i * 255 / Rect_height;
        if (s < 32)
        {
            Pen_color.setColor(QColor(128 + s * 4, 0, 0));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s == 32)
        {
            Pen_color.setColor(QColor(255, 0, 0));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s < 96)
        {
            Pen_color.setColor(QColor(255, 4 * (s - 32), 0));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s < 159)
        {
            Pen_color.setColor(QColor(254 - 4 * (s - 96), 255, 2 + 4 * (s - 96)));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s == 159)
        {
            Pen_color.setColor(QColor(1, 255, 254));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else if (s < 224)
        {
            Pen_color.setColor(QColor(0, 252 - 4 * (s - 160), 255));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
        else
        {
            Pen_color.setColor(QColor(0, 0, 252 - 4 * (s - 224)));
            painter.setPen(Pen_color);
            painter.drawLine(Rect_Left, Rect_Top + i, Rect_Right, Rect_Top + i);
        }
    }
    painter.drawRect(Rect_Left, Rect_Top, Rect_width, Rect_height);
    for (int i = 0; i < 5; i++)
    {
        Pen_color.setColor(Qt::black);
        Pen_color.setWidth(2);
        painter.setPen(Pen_color);
        painter.drawLine(Rect_Right, Rect_Bottom - i * Rect_height / 4, Rect_Right + 10, Rect_Bottom - i * Rect_height / 4);
        QFont TextFont;
        TextFont.setPixelSize(20);
        painter.setFont(TextFont);
        painter.drawText(QPoint(Rect_Right + 10, Rect_Bottom - i * Rect_height / 4), QString::number(mMin + mInternal * i, 'f', 2));
    }
    map.save(save_path, "PNG");
}


void Export_KML::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }

    QStandardItem* project = NULL;
    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        if (model->item(i, 0)->rowCount() != 0)
        {
            count = model->item(i, 0)->rowCount();
            project = model->item(i, 0);
            ui->comboBox->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        ui->comboBox_2->clear();
        this->deleteLater();
    }
    QStandardItem* node = NULL;
    bool isnodefound = false;
    ui->comboBox_2->clear();
    for (int i = 0; i < count; i++)
    {
        if (project->child(i, 1)->text() == QString("SBAS-1.0"))
        {
            ui->comboBox_2->addItem(project->child(i, 0)->text());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }

        }
    }
    if (!node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
        ui->comboBox_2->clear();
        this->deleteLater();
    }
    else
    {
        ui->comboBox_2->setCurrentIndex(0);
    }
}
void Export_KML::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        bool isnodefound = false;
        QStandardItem* node = NULL;
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        ui->comboBox_2->clear();
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 1)->text() == QString("SBAS-1.0"))
            {
                ui->comboBox_2->addItem(project->child(i, 0)->text());
                if (!isnodefound)
                {
                    node = project->child(i, 0);
                    isnodefound = true;
                }
            }
        }
        if (!isnodefound)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
            ui->comboBox_2->clear();
            return;
        }
    }
}



void Export_KML::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->File_path->setDisabled(0);
        ui->File_name->setDisabled(0);
        ui->Browse->setDisabled(0);
        ui->Export->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->File_path->setDisabled(1);
        ui->File_name->setDisabled(1);
        ui->Browse->setDisabled(1);
        ui->Export->setDisabled(1);
    }


}


void Export_KML::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() > 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QStandardItem* node = NULL;
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QString temp = project->child(i, 0)->text();
            if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
            {
                node = project->child(i, 0); break;
            }
        }

        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无数据！"));
            return;
        }
    }
}

void Export_KML::on_Browse_pressed()
{
    QString folder;
    folder = QFileDialog::getExistingDirectory(this, "Path of project", "");
    ui->File_path->setText(folder);
}

void Export_KML::on_Export_pressed()
{
    bool bFlag = false;
    if (copy->item(ui->comboBox->currentIndex(), 0)->rowCount() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程下未检测到数据！请先导入图像或更换工程！"));
        return;
    }
    if (ui->File_path->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请选择存储路径！"));
        return;
    }
    bFlag = ui->File_path->text().contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("路径中不应包含中文或特殊符号！"));
        return;
    }
    if (ui->File_name->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入文件名！"));
        return;
    }
    bFlag = ui->File_name->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("文件名中不应包含中文或特殊符号！"));
        return;
    }

    QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
    QString h5_path;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
        {
            h5_path = project->child(i, 0)->child(0, 1)->text();
            break;
        }
    }

    ChangeVision(false);
    ui->Export->setText(QStringLiteral("正在导出……"));

    m_worker = new ExportKMLWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ExportKMLWorker::updateProcess, this, &Export_KML::updateProcess);
    connect(m_worker, &ExportKMLWorker::endProcess, this, &Export_KML::endProcess);
    connect(m_worker, &ExportKMLWorker::errorProcess, this, &Export_KML::errorProcess);

    m_thread->start();

    QMetaObject::invokeMethod(m_worker, "exportKML", Qt::QueuedConnection,
                              Q_ARG(QString, h5_path),
                              Q_ARG(QString, ui->File_path->text()),
                              Q_ARG(QString, ui->File_name->text()));
}


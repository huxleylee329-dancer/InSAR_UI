#pragma once
#include <QString>
#include <QStringList>
#include <vector>

// 封装单个影像的导入任务参数
struct ImportTask {
    QString filename;        // 目标导入名称（如 "csk_image_01"）
    QStringList arguments;   // 传入的源文件路径列表或参数串
};

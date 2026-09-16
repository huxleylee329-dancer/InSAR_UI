#include "ExportKMLWorker.h"
#include "Hdf5IO.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QThread>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <windows.h>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Fixture
{
    cv::Mat velocity = (cv::Mat_<double>(3, 4) << -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5);
    cv::Mat mask = cv::Mat::ones(3, 4, CV_32S);
    cv::Mat lat = (cv::Mat_<double>(3, 4) << 30, 30, 30, 30, 29, 29, 29, 29, 28, 28, 28, 28);
    cv::Mat lon = (cv::Mat_<double>(3, 4) << 110, 111, 112, 113, 110, 111, 112, 113, 110, 111, 112, 113);
    int refRow = 1;
    int refCol = 2;
    QString omit;

    void write(const QString& path)
    {
        const QByteArray file = path.toUtf8();
        require(Hdf5IO::createFile(file.constData()) == 0, "create fixture");
        const auto array = [&](const char* name, const cv::Mat& data)
        {
            if (omit != name) require(Hdf5IO::writeArray(file.constData(), name, data) == 0, name);
        };
        array("defomation_velocity", velocity);
        array("mask", mask);
        array("mapped_lat", lat);
        array("mapped_lon", lon);
        if (omit != "ref_row") require(Hdf5IO::writeInt(file.constData(), "ref_row", refRow) == 0, "ref_row");
        if (omit != "ref_col") require(Hdf5IO::writeInt(file.constData(), "ref_col", refCol) == 0, "ref_col");
    }
};

void validateOutput(const QString& folder)
{
    require(!QImage(folder + "/result.jpg").isNull(), "read JPEG output");
    require(QImage(folder + "/Colorbar.png").size() == QSize(150, 400), "read colorbar output");
    QFile file(folder + "/result.kml");
    require(file.open(QIODevice::ReadOnly), "open KML output");
    QXmlStreamReader xml(&file);
    QStringList coordinates, images;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == "coordinates") coordinates << xml.readElementText().simplified();
        if (xml.isStartElement() && xml.name() == "href") images << xml.readElementText();
    }
    require(!xml.hasError(), "valid KML XML");
    require(coordinates.size() == 2, "corner and reference coordinates");
    require(coordinates[0] == "110.000000,28.000000 113.000000,28.000000 113.000000,30.000000 110.000000,30.000000", "correct corner order and values");
    require(coordinates[1] == "112.000000,29.000000", "correct reference point");
    require(images == (QStringList() << "result.jpg" << "Colorbar.png"), "relative image links");
}

int passed = 0;
void run(const char* name, const std::function<void(Fixture&)>& setup,
         const QString& expectedError = QString(), bool outputExists = true)
{
    QTemporaryDir dir;
    require(dir.isValid(), "temporary directory");
    Fixture fixture;
    setup(fixture);
    const QString input = dir.path() + "/input.h5";
    fixture.write(input);
    const QString output = dir.path() + "/output";
    if (outputExists) require(QDir().mkpath(output), "output directory");
    ExportKMLWorker worker;
    int completed = 0, errors = 0;
    QString message;
    QObject::connect(&worker, &BaseWorker::endProcess, [&]() { ++completed; });
    QObject::connect(&worker, &BaseWorker::errorProcess, [&](const QString& error) { ++errors; message = error; });
    worker.exportKML(input, output, "result");
    if (expectedError.isEmpty())
    {
        if (!message.isEmpty()) std::fprintf(stderr, "%s\n", message.toUtf8().constData());
        require(completed == 1 && errors == 0, "exactly one success signal");
        validateOutput(output);
    }
    else
    {
        if (!message.contains(expectedError)) std::fprintf(stderr, "%s\n", message.toUtf8().constData());
        require(completed == 0 && errors == 1 && message.contains(expectedError), "one descriptive error, no success signal");
        require(QDir(output).entryList(QDir::Files).isEmpty(), "failed validation leaves no output files");
    }
    ++passed;
    std::printf("PASS %s\n", name);
}
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    try
    {
        if (app.arguments().contains("--missing-lat"))
        {
            run("missing latitude crash reproduction", [](Fixture& f) { f.omit = "mapped_lat"; }, "mapped_lat");
            return 0;
        }
        run("float64 coordinates", [](Fixture&) {});
        run("float32 coordinates and byte mask", [](Fixture& f)
        {
            f.lat.convertTo(f.lat, CV_32F); f.lon.convertTo(f.lon, CV_32F);
            f.velocity.convertTo(f.velocity, CV_32F); f.mask.convertTo(f.mask, CV_8U);
        });
        for (const char* dataset : { "mapped_lat", "mapped_lon", "ref_row", "ref_col", "mask", "defomation_velocity" })
        {
            run(dataset, [=](Fixture& f) { f.omit = dataset; }, QString::fromLatin1(dataset));
        }
        run("negative row", [](Fixture& f) { f.refRow = -1; }, QStringLiteral("超出"));
        run("row at upper bound", [](Fixture& f) { f.refRow = 3; }, QStringLiteral("超出"));
        run("negative column", [](Fixture& f) { f.refCol = -1; }, QStringLiteral("超出"));
        run("column at upper bound", [](Fixture& f) { f.refCol = 4; }, QStringLiteral("超出"));
        run("latitude shape mismatch", [](Fixture& f) { f.lat = f.lat.row(0).clone(); }, "mapped_lat");
        run("longitude shape mismatch", [](Fixture& f) { f.lon = f.lon.col(0).clone(); }, "mapped_lon");
        run("mask shape mismatch", [](Fixture& f) { f.mask = f.mask.row(0).clone(); }, QStringLiteral("尺寸"));
        run("integer latitude", [](Fixture& f) { f.lat.convertTo(f.lat, CV_32S); }, "mapped_lat");
        run("integer longitude after valid latitude", [](Fixture& f) { f.lon.convertTo(f.lon, CV_32S); }, "mapped_lon");
        const int rows[] = { 1, 0, 0, 2, 2 };
        const int cols[] = { 2, 0, 3, 0, 3 };
        for (int point = 0; point < 5; ++point)
        {
            run("NaN at each latitude sample", [=](Fixture& f) { f.lat.at<double>(rows[point], cols[point]) = std::numeric_limits<double>::quiet_NaN(); }, "mapped_lat");
            run("infinity at each longitude sample", [=](Fixture& f) { f.lon.at<double>(rows[point], cols[point]) = std::numeric_limits<double>::infinity(); }, "mapped_lon");
        }
        run("latitude outside range", [](Fixture& f) { f.lat.at<double>(1, 2) = 91; }, "mapped_lat");
        run("longitude outside range", [](Fixture& f) { f.lon.at<double>(1, 2) = 181; }, "mapped_lon");
        run("constant deformation rendering failure", [](Fixture& f) { f.velocity.setTo(1); }, QStringLiteral("渲染图失败"));
        run("unwritable output path", [](Fixture&) {}, QStringLiteral("渲染图失败"), false);
        for (int cancelProgress : { 35, 60 })
        {
            QTemporaryDir dir;
            require(dir.isValid(), "cancellation temporary directory");
            const QString input = dir.path() + "/input.h5";
            const QString output = dir.path() + "/output";
            Fixture fixture;
            fixture.write(input);
            require(QDir().mkpath(output), "cancellation output directory");
            int completed = 0, errors = 0, cancelled = 0;
            QThread* thread = QThread::create([&]()
            {
                ExportKMLWorker worker;
                QObject::connect(&worker, &BaseWorker::updateProcess, [&](int progress, const QString&)
                {
                    if (progress == cancelProgress) QThread::currentThread()->requestInterruption();
                });
                QObject::connect(&worker, &BaseWorker::endProcess, [&]() { ++completed; });
                QObject::connect(&worker, &BaseWorker::errorProcess, [&](const QString&) { ++errors; });
                QObject::connect(&worker, &ExportKMLWorker::cancelled, [&]() { ++cancelled; });
                worker.exportKML(input, output, "result");
            });
            thread->start();
            thread->wait();
            delete thread;
            require(completed == 0 && errors == 0 && cancelled == 1, "cancellation signal");
            require(QDir(output).entryList(QDir::Files).isEmpty(), "cancelled output cleanup");
            ++passed;
            std::printf("PASS cancellation at %d percent\n", cancelProgress);
        }
        std::printf("All %d regression cases passed.\n", passed);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL after %d cases: %s\n", passed, error.what());
        return 1;
    }
}

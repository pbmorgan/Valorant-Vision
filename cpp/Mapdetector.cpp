#include "../headers/mapdetector.h"
#include <opencv2/opencv.hpp>
#include <QString>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QStandardPaths>

static QString layoutPathForName(const QString& name) {
    if (name == "abyss")    return "map_layouts/Abyss_layout.png";
    if (name == "ascent")   return "map_layouts/Ascent_layout.png";
    if (name == "bind")     return "map_layouts/Bind_layout.png";
    if (name == "breeze")   return "map_layouts/Breeze_layout.png";
    if (name == "fracture") return "map_layouts/Fracture_layout.png";
    if (name == "haven")    return "map_layouts/Haven_layout.png";
    if (name == "icebox")   return "map_layouts/Icebox_layout.png";
    if (name == "lotus")    return "map_layouts/Lotus_layout.png";
    if (name == "pearl")    return "map_layouts/Pearl_layout.png";
    if (name == "split")    return "map_layouts/Split_layout.png";
    if (name == "sunset")   return "map_layouts/Sunset_layout.png";
    return "";
}

QString detectMapFromVideo(const std::string& videoPath) {

    cv::VideoCapture cap(videoPath);
    if (!cap.isOpened()) {
        qDebug() << "[MapDetector] Could not open video.";
        return "";
    }

    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) fps = 30.0;
    double totalSeconds = cap.get(cv::CAP_PROP_FRAME_COUNT) / fps;
    qDebug() << "[MapDetector] Video length:" << (int)totalSeconds << "seconds";

    // Scan every 30 seconds up to 10 minutes looking for the broadcast bar
    double maxSeek = std::min(totalSeconds, 600.0);

    for (double seekSec = 10.0; seekSec <= maxSeek; seekSec += 30.0) {
        cap.set(cv::CAP_PROP_POS_FRAMES, seekSec * fps);
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) continue;

        qDebug() << "[MapDetector] Checking frame at" << (int)seekSec << "seconds...";

        // Crop the broadcast bar: "CURRENT: HAVEN" in the top-left strip
        double sx = frame.cols / 1280.0;
        double sy = frame.rows / 720.0;
        int cropW = std::min((int)(400 * sx), frame.cols);
        int cropH = std::min((int)(22 * sy), frame.rows);
        cv::Mat bar = frame(cv::Rect(0, 0, cropW, cropH));

        // Upscale 3x so Tesseract can read the small text
        cv::Mat upscaled;
        cv::resize(bar, upscaled, cv::Size(), 3, 3, cv::INTER_CUBIC);

        // Write to system temp folder - works on any PC
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString tempImage = tempDir + "/vv_bar.png";
        QString tempText = tempDir + "/vv_bar";
        cv::imwrite(tempImage.toStdString(), upscaled);

        // Run Tesseract
        QProcess process;
        QString tesseractExe = "tesseract";
        if (!QFile::exists("tesseract")) {
            // Common install locations on Windows
            for (const QString& path : {
                "C:/Program Files/Tesseract-OCR/tesseract.exe",
                "C:/Program Files (x86)/Tesseract-OCR/tesseract.exe"
            }) {
                if (QFile::exists(path)) { tesseractExe = path; break; }
            }
        }
        process.start(tesseractExe, {
            tempImage, tempText,
            "--psm", "7",
            "-c", "tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZ: "
            });
        process.waitForFinished(5000);

        // Read the output
        QFile file(tempText + ".txt");
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString text = file.readAll().toUpper().simplified();
        file.close();

        qDebug() << "[MapDetector] OCR read:" << text;

        QStringList knownMaps = {
            "abyss","ascent","bind","breeze","fracture",
            "haven","icebox","lotus","pearl","split","sunset"
        };

        // Look for map name right after "CURRENT:"
        int idx = text.indexOf("CURRENT");
        if (idx >= 0) {
            QString after = text.mid(idx + 7).simplified();
            if (after.startsWith(":")) after = after.mid(1).simplified();
            QString firstWord = after.split(" ").first().toLower();
            if (knownMaps.contains(firstWord)) {
                cap.release();
                qDebug() << "[MapDetector] Detected:" << firstWord;
                return layoutPathForName(firstWord);
            }
        }

        // Fallback: scan whole text for any known map name
        for (const QString& map : knownMaps) {
            if (text.contains(map.toUpper())) {
                cap.release();
                qDebug() << "[MapDetector] Detected:" << map;
                return layoutPathForName(map);
            }
        }
    }

    cap.release();
    qDebug() << "[MapDetector] Could not detect map - broadcast bar not found.";
    return "";
}

void saveMapNameReference(const std::string& videoPath, const std::string& mapName) {
    Q_UNUSED(videoPath);
    Q_UNUSED(mapName);
}
#include "../headers/mapdetector.h"

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

#include <QString>
#include <QStringList>
#include <QDebug>

#include <algorithm>


// -----------------------------------------------------------------------------
// Known Valorant maps
//
// Keep this list in one place instead of recreating it every time we check
// another video frame.
// -----------------------------------------------------------------------------
static const QStringList KNOWN_MAPS = {
    "abyss",
    "ascent",
    "bind",
    "breeze",
    "corrode",
    "fracture",
    "haven",
    "icebox",
    "lotus",
    "pearl",
    "split",
    "summit",
    "sunset"
};


// -----------------------------------------------------------------------------
// Convert a detected map name into the corresponding minimap image path.
// -----------------------------------------------------------------------------
static QString layoutPathForName(const QString& name)
{
    if (name == "abyss")    return "map_layouts/Abyss_layout.png";
    if (name == "ascent")   return "map_layouts/Ascent_layout.png";
    if (name == "bind")     return "map_layouts/Bind_layout.png";
    if (name == "breeze")   return "map_layouts/Breeze_layout.png";
    if (name == "corrode")  return "map_layouts/Corrode_layout.png";
    if (name == "fracture") return "map_layouts/Fracture_layout.png";
    if (name == "haven")    return "map_layouts/Haven_layout.png";
    if (name == "icebox")   return "map_layouts/Icebox_layout.png";
    if (name == "lotus")    return "map_layouts/Lotus_layout.png";
    if (name == "pearl")    return "map_layouts/Pearl_layout.png";
    if (name == "split")    return "map_layouts/Split_layout.png";
    if (name == "summit")   return "map_layouts/Summit_layout.png";
    if (name == "sunset")   return "map_layouts/Sunset_layout.png";

    return "";
}


// -----------------------------------------------------------------------------
// OCR one cropped broadcast-bar image.
//
// OLD METHOD:
//      cv::Mat
//          -> save PNG
//          -> launch tesseract.exe
//          -> create TXT file
//          -> reopen TXT file
//
// NEW METHOD:
//      cv::Mat
//          -> Tesseract C++ API
//          -> QString
//
// This avoids disk I/O and avoids launching a new process for every frame.
// -----------------------------------------------------------------------------
static QString readMapBarWithTesseract(
    tesseract::TessBaseAPI& tess,
    const cv::Mat& bar)
{
    if (bar.empty())
    {
        return "";
    }

    // Convert OpenCV's BGR image to grayscale.
    //
    // Tesseract does not need the color information for this text,
    // and grayscale avoids any BGR/RGB channel-order issues.
    cv::Mat gray;

    if (bar.channels() == 3)
    {
        cv::cvtColor(
            bar,
            gray,
            cv::COLOR_BGR2GRAY
        );
    }
    else
    {
        gray = bar.clone();
    }


    // The broadcast text is very small.
    // Keep the same 3x enlargement your previous detector used.
    cv::Mat upscaled;

    cv::resize(
        gray,
        upscaled,
        cv::Size(),
        3.0,
        3.0,
        cv::INTER_CUBIC
    );


    // Give the image directly to Tesseract in memory.
    //
    // Because this is an 8-bit grayscale image:
    // bytes-per-pixel = 1.
    tess.SetImage(
        upscaled.data,
        upscaled.cols,
        upscaled.rows,
        1,
        static_cast<int>(upscaled.step)
    );


    // Ask Tesseract to recognize the current image.
    char* rawOutput = tess.GetUTF8Text();

    QString text;

    if (rawOutput)
    {
        text = QString::fromUtf8(rawOutput);

        // GetUTF8Text() allocates this buffer.
        delete[] rawOutput;
    }


    // Normalize the OCR result so the existing map-name matching
    // behaves the same way as before.
    return text.toUpper().simplified();
}


// -----------------------------------------------------------------------------
// Detect which Valorant map appears in the selected video.
// -----------------------------------------------------------------------------
QString detectMapFromVideo(const std::string& videoPath)
{
    // Open the video once for this map-detection pass.
    cv::VideoCapture cap(videoPath);

    if (!cap.isOpened())
    {
        qDebug() << "[MapDetector] Could not open video.";
        return "";
    }


    // -------------------------------------------------------------------------
    // Initialize Tesseract ONCE.
    //
    // Previously a new tesseract.exe process was launched for every checked
    // frame. Now one Tesseract instance stays alive for this entire function.
    // -------------------------------------------------------------------------
    tesseract::TessBaseAPI tess;

    if (tess.Init("tessdata", "eng") != 0)
    {
        qDebug() << "[MapDetector] Could not initialize Tesseract.";

        cap.release();
        return "";
    }


    // The broadcast bar should contain one short line such as:
    //
    // CURRENT: HAVEN
    //
    // Therefore tell Tesseract to treat the crop as one text line.
    tess.SetPageSegMode(
        tesseract::PSM_SINGLE_LINE
    );


    // Restrict recognition to characters that can actually appear
    // in the map-name broadcast bar.
    tess.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ: "
    );


    // -------------------------------------------------------------------------
    // Read basic video information.
    // -------------------------------------------------------------------------
    double fps = cap.get(cv::CAP_PROP_FPS);

    if (fps <= 0)
    {
        fps = 30.0;
    }


    double frameCount =
        cap.get(cv::CAP_PROP_FRAME_COUNT);

    double totalSeconds =
        frameCount / fps;


    qDebug()
        << "[MapDetector] Video length:"
        << static_cast<int>(totalSeconds)
        << "seconds";


    // For Stage 1 we are deliberately keeping your existing scan behavior.
    //
    // Later we will optimize this too.
    double maxSeek =
        std::min(totalSeconds, 600.0);


    // -------------------------------------------------------------------------
    // Search the early portion of the video for the broadcast map-name bar.
    //
    // Keep the original timing:
    //     10 sec
    //     40 sec
    //     70 sec
    //     ...
    //
    // We will improve this strategy after confirming the new OCR path works.
    // -------------------------------------------------------------------------
    for (
        double seekSec = 10.0;
        seekSec <= maxSeek;
        seekSec += 30.0
        )
    {
        // Seek to the requested point in the video.
        cap.set(
            cv::CAP_PROP_POS_FRAMES,
            seekSec * fps
        );


        cv::Mat frame;

        cap >> frame;


        if (frame.empty())
        {
            continue;
        }


        qDebug()
            << "[MapDetector] Checking frame at"
            << static_cast<int>(seekSec)
            << "seconds...";


        // ---------------------------------------------------------------------
        // Crop the same top-left broadcast-bar region used by the old detector.
        //
        // These coordinates were designed around a 1280x720 reference video,
        // so scale them to the actual frame resolution.
        // ---------------------------------------------------------------------
        double sx =
            frame.cols / 1280.0;

        double sy =
            frame.rows / 720.0;


        int cropW =
            std::min(
                static_cast<int>(400 * sx),
                frame.cols
            );


        int cropH =
            std::min(
                static_cast<int>(22 * sy),
                frame.rows
            );


        // Make sure OpenCV never receives an invalid crop.
        if (cropW <= 0 || cropH <= 0)
        {
            continue;
        }


        cv::Rect broadcastBarRect(
            0,
            0,
            cropW,
            cropH
        );


        cv::Mat bar =
            frame(broadcastBarRect);


        // ---------------------------------------------------------------------
        // OCR the crop directly in memory.
        // ---------------------------------------------------------------------
        QString text =
            readMapBarWithTesseract(
                tess,
                bar
            );


        qDebug()
            << "[MapDetector] OCR read:"
            << text;


        // ---------------------------------------------------------------------
        // Primary detection:
        //
        // Prefer the word immediately after:
        //
        // CURRENT:
        // ---------------------------------------------------------------------
        int currentIndex =
            text.indexOf("CURRENT");


        if (currentIndex >= 0)
        {
            QString afterCurrent =
                text.mid(currentIndex + 7)
                .simplified();


            // Handle both:
            //
            // CURRENT: HAVEN
            //
            // and:
            //
            // CURRENT HAVEN
            if (afterCurrent.startsWith(":"))
            {
                afterCurrent =
                    afterCurrent.mid(1)
                    .simplified();
            }


            QString firstWord =
                afterCurrent
                .split(" ")
                .first()
                .toLower();


            if (KNOWN_MAPS.contains(firstWord))
            {
                qDebug()
                    << "[MapDetector] Detected:"
                    << firstWord;


                tess.End();
                cap.release();

                return layoutPathForName(
                    firstWord
                );
            }
        }


        // ---------------------------------------------------------------------
        // Fallback:
        //
        // If Tesseract did not read "CURRENT" correctly, look anywhere in
        // the recognized text for a known map name.
        // ---------------------------------------------------------------------
        for (const QString& map : KNOWN_MAPS)
        {
            if (text.contains(map.toUpper()))
            {
                qDebug()
                    << "[MapDetector] Detected:"
                    << map;


                tess.End();
                cap.release();

                return layoutPathForName(
                    map
                );
            }
        }
    }


    // Nothing was confidently detected.
    tess.End();
    cap.release();


    qDebug()
        << "[MapDetector] Could not detect map - "
        "broadcast bar not found.";


    return "";
}


// -----------------------------------------------------------------------------
// This function is currently unused.
//
// We are leaving it here because it is still declared in mapdetector.h.
// We can either implement it or remove it during a later cleanup stage.
// -----------------------------------------------------------------------------
void saveMapNameReference(
    const std::string& videoPath,
    const std::string& mapName)
{
    Q_UNUSED(videoPath);
    Q_UNUSED(mapName);
}
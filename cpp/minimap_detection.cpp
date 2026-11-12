#include "../headers/minimap.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <algorithm>

QString Minimap::checkMap(const cv::Mat& inputFrame)
{
    // Make sure we actually received a frame
    if (inputFrame.empty())
    {
        qWarning("Minimap: received an empty frame");
        return QString();
    }

    // --------------------------------------------------
    // 1. Crop the top-left area where the minimap appears
    // --------------------------------------------------

    // Use 35% of the video's height.
    // At 1080p, this gives us about a 378x378 region.
    // Crop the part of the top-left corner where
// the Valorant broadcast minimap is located.
    int x = static_cast<int>(inputFrame.cols * 0.02);
    int y = static_cast<int>(inputFrame.rows * 0.04);

    int width = static_cast<int>(inputFrame.cols * 0.20);
    int height = static_cast<int>(inputFrame.rows * 0.35);

    // Make sure the crop stays inside the video frame
    width = std::min(width, inputFrame.cols - x);
    height = std::min(height, inputFrame.rows - y);

    cv::Rect minimapRegion(
        x,
        y,
        width,
        height
    );

    cv::Mat minimapCrop =
        inputFrame(minimapRegion).clone();


    // --------------------------------------------------
    // 2. Convert the cropped area to grayscale
    // --------------------------------------------------

    cv::Mat grayMinimap;

    if (minimapCrop.channels() == 3)
    {
        cv::cvtColor(
            minimapCrop,
            grayMinimap,
            cv::COLOR_BGR2GRAY
        );
    }
    else if (minimapCrop.channels() == 4)
    {
        cv::cvtColor(
            minimapCrop,
            grayMinimap,
            cv::COLOR_BGRA2GRAY
        );
    }
    else if (minimapCrop.channels() == 1)
    {
        grayMinimap = minimapCrop;
    }
    else
    {
        qWarning("Minimap: unsupported image format");
        return QString();
    }


    // --------------------------------------------------
    // 3. Maps we want to compare against
    // --------------------------------------------------

    static const std::vector<std::string> mapNames = {
        "Abyss",
        "Ascent",
        "Bind",
        "Breeze",
        "Fracture",
        "Haven",
        "Icebox",
        "Lotus",
        "Pearl",
        "Split",
        "Sunset",
        "Summit"
    };


    // --------------------------------------------------
    // 4. Load the map templates only once
    // --------------------------------------------------

    static std::vector<cv::Mat> mapTemplates;

    if (mapTemplates.empty())
    {
        qDebug() << "Minimap: loading map templates...";

        for (const std::string& mapName : mapNames)
        {
            std::string templatePath =
                "map_layouts/" +
                mapName +
                "_layout.png";

            cv::Mat mapTemplate =
                cv::imread(
                    templatePath,
                    cv::IMREAD_GRAYSCALE
                );

            if (mapTemplate.empty())
            {
                qWarning(
                    "Minimap: could not load map template '%s'",
                    templatePath.c_str()
                );
            }

            // Keep the vector indexes aligned with mapNames
            mapTemplates.push_back(mapTemplate);
        }
    }


    // --------------------------------------------------
    // 5. Track the best and second-best map
    // --------------------------------------------------

    double highestConfidence = -1.0;
    double secondHighestConfidence = -1.0;

    std::string bestMap = "";


    // --------------------------------------------------
    // 6. Compare crop against every map
    // --------------------------------------------------

    for (size_t i = 0; i < mapNames.size(); i++)
    {
        const std::string& mapName =
            mapNames[i];

        const cv::Mat& originalTemplate =
            mapTemplates[i];

        if (originalTemplate.empty())
        {
            continue;
        }


        // Resize this map layout to the same size
        // as the portion of the screen we cropped.
        cv::Mat resizedTemplate;

        cv::resize(
            originalTemplate,
            resizedTemplate,
            grayMinimap.size()
        );


        // Compare the cropped screen against this map
        cv::Mat result;

        cv::matchTemplate(
            grayMinimap,
            resizedTemplate,
            result,
            cv::TM_CCOEFF_NORMED
        );


        // Get the confidence score
        double minConfidence = 0.0;
        double maxConfidence = 0.0;

        cv::minMaxLoc(
            result,
            &minConfidence,
            &maxConfidence
        );


        // Print each map's confidence while testing
        qDebug(
            "Map: %s | Confidence: %.3f",
            mapName.c_str(),
            maxConfidence
        );


        // ----------------------------------------------
        // Keep track of first and second place
        // ----------------------------------------------

        if (maxConfidence > highestConfidence)
        {
            secondHighestConfidence =
                highestConfidence;

            highestConfidence =
                maxConfidence;

            bestMap =
                mapName;
        }
        else if (maxConfidence > secondHighestConfidence)
        {
            secondHighestConfidence =
                maxConfidence;
        }
    }


    // --------------------------------------------------
    // 7. Make sure we compared at least one map
    // --------------------------------------------------

    if (bestMap.empty())
    {
        qWarning(
            "Minimap: no valid map templates could be compared"
        );

        return QString();
    }


    qDebug(
        "Best map: %s | Confidence: %.3f | Second best: %.3f",
        bestMap.c_str(),
        highestConfidence,
        secondHighestConfidence
    );


    // --------------------------------------------------
    // 8. Decide whether this frame is trustworthy
    // --------------------------------------------------

    // We will tune these after seeing real results.
    const double confidenceThreshold = 0.35;
    const double minimumDifference = 0.10;

    double confidenceDifference =
        highestConfidence -
        secondHighestConfidence;


    // Nothing looks enough like a minimap yet
    if (highestConfidence < confidenceThreshold)
    {
        qDebug(
            "No map detected yet. Best confidence: %.3f",
            highestConfidence
        );

        return QString();
    }


    // Winner and second place are too close
    if (confidenceDifference < minimumDifference)
    {
        qDebug(
            "Map detection uncertain. "
            "First/second difference: %.3f",
            confidenceDifference
        );

        return QString();
    }


    // --------------------------------------------------
    // 9. Confident detection
    // --------------------------------------------------

    qDebug(
        "MAP DETECTED: %s | Confidence: %.3f",
        bestMap.c_str(),
        highestConfidence
    );


    QString detectedFile =
        QString::fromStdString(
            "map_layouts/" +
            bestMap +
            "_layout.png"
        );


    // Do NOT load the image here.
    // MainWindow will decide when to display it.
    return detectedFile;
}
#include "../headers/minimap.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <algorithm>
#include <cmath>
#include <utility>

Minimap::Minimap(QWidget *parentAddress)
    : QDockWidget("Minimap", parentAddress)
{
    // creates a widget for the minimap
    minimap_wid = new QLabel(this);
    // makes the minimap stay in the centre
    minimap_wid->setAlignment(Qt::AlignCenter);
    // sets the minimum window size 
    minimap_wid->setMinimumSize(250, 250);
    // doesn't allow the map to be stretched from all the ways
    minimap_wid->setScaledContents(false);
    // allows the window to be resized as user wants
    minimap_wid->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // allows the window be popped out and free to move
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    // only allows to be docked on the right side right now!!!
    setAllowedAreas(Qt::RightDockWidgetArea);
    // draws the windows
    setWidget(minimap_wid);

    //Open up mapping file and parse it as json
    std::ifstream f("id_to_name.json");
    if (!f.is_open()) { // was crashing when it couldn't find the file, so added this check and warning message
        qWarning("Minimap: could not open id_to_name.json � check working directory");
        return;
    }
    nlohmann::json idToNameJSON = nlohmann::json::parse(f);
    f.close();

    //Create a mapping of ids to names
    int numClasses = idToNameJSON.size();
    for (int i = 0; i < numClasses; i++) {
        idToName.push_back(idToNameJSON[i]["name"]);
    }

    agentIcons.push_back(cv::Mat()); //Pushing back an empty image since IDs start at 1, not 0 (should change this later...)
    //Add each agent image to a vector
    for (int i = 0; i < numClasses; i++) {
        cv::Mat img = cv::imread("agent_icons/" + idToName[i] + ".png", cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            qWarning("Minimap: could not load agent icon for '%s'", idToName[i].c_str());
            agentIcons.push_back(cv::Mat());
            continue;
        }
        cv::Mat resizedImg;
        cv::resize(img, resizedImg, cv::Size(16, 16)); //This isn't *really* hardcoding, as this will get layered onto the image when it's resized to match the model's predictions
        agentIcons.push_back(resizedImg);
    }
    // ABILITY_BETA_INTEGRATION: names are indexed by their utility class ID.
    std::ifstream utilsFile("utility_id_to_name.json");
    if (utilsFile.is_open())
    {
        try
        {
            auto entries = nlohmann::json::parse(utilsFile);
            utilityNames.resize(1); // class 0 is background
            for (const auto& entry : entries)
            {
                int id = entry.at("id").get<int>();
                if (id <= 0 || id > 100) continue;
                if (static_cast<size_t>(id) >= utilityNames.size())
                    utilityNames.resize(id + 1);
                utilityNames[id] = entry.at("name").get<std::string>();
            }
        }
        catch (const std::exception& e)
        {
            qWarning("Minimap: invalid utility_id_to_name.json: %s", e.what());
        }
    }
    else
    {
        qWarning("Minimap: utility_id_to_name.json missing; displaying utility IDs.");
    }
}

void Minimap::loadImage(const QString& filePath)
{
    mapFile = filePath;
    lastPredictions.resize(0, 0);
    lastUtilityPredictions.resize(0, 0);

    if (!pixmap.load(filePath)) {
        // loads split as default if path error
        pixmap.load("map_layouts/Sunset_layout.png");

        mapFile = filePath;
        minimap_wid->setPixmap(pixmap);
    }
    // loads the image after image selected
    minimap_wid->setPixmap(pixmap);
}

void Minimap::resizeEvent(QResizeEvent *event)
{
    // if image present, resize without losing qaulity
    if (!pixmap.isNull()) {
        minimap_wid->setPixmap(
            pixmap.scaled(
                minimap_wid->size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            )
        );
    }

    QDockWidget::resizeEvent(event);
}

QPixmap Minimap::cvMatToQPixmap(const cv::Mat &inMat) {
    cv::Mat rgbMat;
    // OpenCV uses BGR, Qt uses RGB
    cv::cvtColor(inMat, rgbMat, cv::COLOR_BGR2RGB);

    QImage qtImage((const unsigned char *)rgbMat.data,
        rgbMat.cols, rgbMat.rows,
        rgbMat.step,
        QImage::Format_RGB888);

    return QPixmap::fromImage(qtImage).copy();
}

// ABILITY_BETA_INTEGRATION: cache both models' results and render them together.
void Minimap::redrawAgents(Eigen::MatrixXf frameData)
{
    if (frameData.rows() == lastPredictions.rows() &&
        frameData.cols() == lastPredictions.cols() &&
        (frameData.size() == 0 || frameData.isApprox(lastPredictions))) return;
    lastPredictions = frameData;
    renderPredictions();
}

void Minimap::redrawUtilities(Eigen::MatrixXf utilityData)
{
    if (utilityData.rows() == lastUtilityPredictions.rows() &&
        utilityData.cols() == lastUtilityPredictions.cols() &&
        (utilityData.size() == 0 || utilityData.isApprox(lastUtilityPredictions))) return;
    lastUtilityPredictions = utilityData;
    renderPredictions();
}

void Minimap::renderPredictions()
{
    if (mapFile.isEmpty()) return;
    cv::Mat background = cv::imread(mapFile.toStdString());
    if (background.empty()) return;
    cv::Mat image;
    cv::resize(background, image, cv::Size(384, 384));

    // Keep the original agent rendering behavior (with safety checks).
    if (lastPredictions.cols() >= 7)
    {
        for (int i = 0; i < lastPredictions.rows(); ++i)
        {
            int x = static_cast<int>(lastPredictions(i, 0));
            int y = static_cast<int>(lastPredictions(i, 1));
            int classID = static_cast<int>(lastPredictions(i, 6));
            if (classID <= 0 || static_cast<size_t>(classID) >= agentIcons.size()) continue;
            const cv::Mat& agent = agentIcons[classID];
            if (agent.empty()) continue;
            if (x < 0 || y < 0 || x + agent.cols > image.cols || y + agent.rows > image.rows) continue;
            cv::Mat destination = image(cv::Rect(x, y, agent.cols, agent.rows));
            if (agent.channels() == 4)
            {
                std::vector<cv::Mat> channels;
                cv::split(agent, channels);
                cv::Mat bgr;
                cv::merge(std::vector<cv::Mat>{channels[0], channels[1], channels[2]}, bgr);
                bgr.copyTo(destination, channels[3]);
            }
            else if (agent.channels() == 3)
            {
                agent.copyTo(destination);
            }
        }
    }

    // The utility model returns x1, y1, x2, y2, score, classID.
    // Boxes are intentionally left as a baseline visualization; accuracy is not final.
    std::vector<std::pair<float, int>> matches;
    if (lastUtilityPredictions.cols() >= 6)
    {
        for (int i = 0; i < lastUtilityPredictions.rows(); ++i)
        {
            float score = lastUtilityPredictions(i, 4);
            int id = static_cast<int>(lastUtilityPredictions(i, 5));
            if (std::isfinite(score) && score >= 0.75f && id > 0 && id <= 11)
                matches.emplace_back(score, i);
        }
    }
    std::sort(matches.begin(), matches.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    int rendered = 0;
    for (const auto& match : matches)
    {
        if (rendered >= 12) break; // Avoid overwhelming the minimap with boxes.
        int i = match.second;
        int id = static_cast<int>(lastUtilityPredictions(i, 5));
        float fx1 = lastUtilityPredictions(i, 0);
        float fy1 = lastUtilityPredictions(i, 1);
        float fx2 = lastUtilityPredictions(i, 2);
        float fy2 = lastUtilityPredictions(i, 3);
        if (!std::isfinite(fx1) || !std::isfinite(fy1) ||
            !std::isfinite(fx2) || !std::isfinite(fy2)) continue;
        int x1 = std::clamp(static_cast<int>(fx1), 0, 383);
        int y1 = std::clamp(static_cast<int>(fy1), 0, 383);
        int x2 = std::clamp(static_cast<int>(fx2), 0, 383);
        int y2 = std::clamp(static_cast<int>(fy2), 0, 383);
        if (x2 <= x1 || y2 <= y1) continue;

        cv::rectangle(image, cv::Rect(x1, y1, x2 - x1, y2 - y1),
                      cv::Scalar(0, 215, 255), 2);
        std::string label = "Utility " + std::to_string(id);
        if (static_cast<size_t>(id) < utilityNames.size() && !utilityNames[id].empty())
            label = utilityNames[id];
        cv::putText(image, label, cv::Point(x1, std::max(12, y1 - 3)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.33, cv::Scalar(0, 0, 0), 2);
        cv::putText(image, label, cv::Point(x1, std::max(12, y1 - 3)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.33, cv::Scalar(0, 215, 255), 1);
        ++rendered;
    }

    QPixmap newPixmap = cvMatToQPixmap(image);
    newPixmap = newPixmap.scaled(minimap_wid->size(), Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    pixmap = newPixmap;
    minimap_wid->setPixmap(pixmap);
}

//:mapFile
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
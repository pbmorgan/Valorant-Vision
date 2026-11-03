#include "../headers/minimap.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <algorithm>

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
}

void Minimap::loadImage(const QString& filePath)
{
    mapFile = filePath;

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
void Minimap::updateScore(
    QString leftTeam,
    int newLeftScore,
    QString rightTeam,
    int newRightScore)
{
    // Avoid redrawing when OCR reports the same score again.
    if (leftTeamName == leftTeam &&
        rightTeamName == rightTeam &&
        leftScore == newLeftScore &&
        rightScore == newRightScore)
    {
        return;
    }

    // Save the newest team names and score.
    leftTeamName = leftTeam;
    rightTeamName = rightTeam;
    leftScore = newLeftScore;
    rightScore = newRightScore;

    // Redraw so score changes appear even when agents have not moved.
    redrawMinimap();
}


void Minimap::updateRoundTimer(QString timer)
{
    // OCR may report the same timer value more than once.
    if (currentRoundTimer == timer)
    {
        return;
    }

    // Save the newest round timer.
    currentRoundTimer = timer;

    // Redraw independently of agent movement.
    redrawMinimap();
}

void Minimap::updateSpikeTimer(int secondsRemaining)
{
    // Avoid redrawing the minimap if the Spike timer
    // has not actually changed.
    if (spikeSecondsRemaining == secondsRemaining)
    {
        return;
    }

    spikeSecondsRemaining = secondsRemaining;

    // Redraw so the updated Spike countdown appears
    // immediately on the minimap.
    redrawMinimap();
}



void Minimap::setReplayActive(bool active)
{
    // Do nothing if the replay state did not actually change.
    if (replayActive == active)
    {
        return;
    }

    replayActive = active;

    if (!replayActive)
    {
        // Replay ended:
        // force the next live prediction to redraw.
        lastPredictions.resize(0, 0);
    }

    // Redraw the complete minimap.
    //
    // During replay, redrawMinimap() will hide the agents
    // while keeping the current score and timer visible.
    redrawMinimap();
}

void Minimap::redrawAgents(Eigen::MatrixXf frameData)
{
    // Never use agent predictions taken from replay footage.
    if (replayActive)
    {
        return;
    }

    // Keep the existing behavior when there are no predictions.
    if (frameData.rows() == 0)
    {
        return;
    }

    // Do not redraw if the predictions have not changed.
    if ((frameData.rows() == lastPredictions.rows()) &&
        (frameData.cols() == lastPredictions.cols()) &&
        frameData.isApprox(lastPredictions))
    {
        return;
    }

    // Save the newest agent positions.
    lastPredictions = frameData;

    // Redraw the complete minimap using the newest information.
    redrawMinimap();
}

void Minimap::redrawMinimap()
{
    // Cannot draw anything until a map has been detected.
    if (mapFile.isEmpty())
    {
        return;
    }

    // Always start from a clean copy of the map.
    cv::Mat image = cv::imread(mapFile.toStdString());

    if (image.empty())
    {
        qWarning("Minimap: could not redraw map image");
        return;
    }

    cv::Mat resizedImg;

    // Model coordinates are based on a 384x384 image.
    cv::resize(
        image,
        resizedImg,
        cv::Size(384, 384)
    );


    // --------------------------------------------------
    // Draw agent icons
    // --------------------------------------------------

    // During replay footage, agents are intentionally hidden.
    if (!replayActive && lastPredictions.rows() > 0)
    {
        for (int i = 0; i < lastPredictions.rows(); i++)
        {
            int xmin = lastPredictions(i, 0);
            int ymin = lastPredictions(i, 1);
            int xmax = lastPredictions(i, 2);
            int ymax = lastPredictions(i, 3);
            int classID = lastPredictions(i, 6);


            // Make sure this prediction has a valid agent icon.
            if (classID <= 0 ||
                classID >= static_cast<int>(agentIcons.size()))
            {
                continue;
            }

            cv::Mat agent = agentIcons[classID];

            if (agent.empty() || agent.channels() != 4)
            {
                continue;
            }


            // Separate the PNG transparency channel.
            std::vector<cv::Mat> channels;
            cv::split(agent, channels);

            cv::Mat alpha = channels[3];

            cv::Mat bgrForeground;

            cv::merge(
                std::vector<cv::Mat>{
                channels[0],
                    channels[1],
                    channels[2]
            },
                bgrForeground
            );


            // Find center of the detected bounding box.
            int centerX = (xmin + xmax) / 2;
            int centerY = (ymin + ymax) / 2;


            // Previously calibrated transformation from the
            // broadcast minimap to the clean map.
            constexpr float MAP_SCALE_X = 1.10f;
            constexpr float MAP_SCALE_Y = 1.10f;

            constexpr float MAP_OFFSET_X = 6.0f;
            constexpr float MAP_OFFSET_Y = 6.0f;


            int mappedCenterX =
                static_cast<int>(
                    (centerX * MAP_SCALE_X) +
                    MAP_OFFSET_X
                    );

            int mappedCenterY =
                static_cast<int>(
                    (centerY * MAP_SCALE_Y) +
                    MAP_OFFSET_Y
                    );


            int drawX =
                mappedCenterX - (agent.cols / 2);

            int drawY =
                mappedCenterY - (agent.rows / 2);


            // Make sure the agent icon stays inside the map.
            if (drawX < 0 ||
                drawY < 0 ||
                drawX + agent.cols > resizedImg.cols ||
                drawY + agent.rows > resizedImg.rows)
            {
                continue;
            }


            cv::Rect rect(
                drawX,
                drawY,
                agent.cols,
                agent.rows
            );

            cv::Mat ROI =
                resizedImg(rect);

            bgrForeground.copyTo(
                ROI,
                alpha
            );
        }
    }


    // --------------------------------------------------
    // Draw score and round timer
    // --------------------------------------------------

    bool haveScore =
        leftScore >= 0 &&
        rightScore >= 0;

    bool haveTimer =
        !currentRoundTimer.isEmpty();


    if (haveScore || haveTimer)
    {
        // Create a semi-transparent dark HUD area
        // at the top-center of the minimap.
        cv::Mat hudOverlay =
            resizedImg.clone();

        cv::rectangle(
            hudOverlay,
            cv::Point(65, 5),
            cv::Point(319, 60),
            cv::Scalar(15, 15, 15),
            cv::FILLED
        );

        cv::addWeighted(
            hudOverlay,
            0.75,
            resizedImg,
            0.25,
            0.0,
            resizedImg
        );


        // Helper for drawing centered text.
        auto drawCenteredText =
            [&resizedImg](
                const std::string& text,
                int y,
                double fontScale,
                int thickness)
            {
                int baseline = 0;

                cv::Size textSize =
                    cv::getTextSize(
                        text,
                        cv::FONT_HERSHEY_SIMPLEX,
                        fontScale,
                        thickness,
                        &baseline
                    );

                int x =
                    (resizedImg.cols -
                        textSize.width) / 2;


                // Black outline.
                cv::putText(
                    resizedImg,
                    text,
                    cv::Point(x, y),
                    cv::FONT_HERSHEY_SIMPLEX,
                    fontScale,
                    cv::Scalar(0, 0, 0),
                    thickness + 2,
                    cv::LINE_AA
                );


                // White foreground.
                cv::putText(
                    resizedImg,
                    text,
                    cv::Point(x, y),
                    cv::FONT_HERSHEY_SIMPLEX,
                    fontScale,
                    cv::Scalar(255, 255, 255),
                    thickness,
                    cv::LINE_AA
                );
            };


        // Example:
        // SEN  5 - 4  G2
        if (haveScore)
        {
            QString scoreText;

            if (!leftTeamName.isEmpty() &&
                !rightTeamName.isEmpty())
            {
                scoreText =
                    QString("%1  %2 - %3  %4")
                    .arg(leftTeamName)
                    .arg(leftScore)
                    .arg(rightScore)
                    .arg(rightTeamName);
            }
            else
            {
                scoreText =
                    QString("%1 - %2")
                    .arg(leftScore)
                    .arg(rightScore);
            }

            drawCenteredText(
                scoreText.toStdString(),
                27,
                0.55,
                1
            );
        }


        // Example:
        // 1:23
        if (haveTimer)
        {
            drawCenteredText(
                currentRoundTimer.toStdString(),
                52,
                0.65,
                2
            );
        }
    }
    // --------------------------------------------------
// Draw planted Spike countdown
// --------------------------------------------------

    if (spikeSecondsRemaining >= 0)
    {
        // Create a separate dark HUD area at the
        // bottom-center of the minimap.
        cv::Mat spikeOverlay =
            resizedImg.clone();

        cv::rectangle(
            spikeOverlay,
            cv::Point(105, 338),
            cv::Point(279, 378),
            cv::Scalar(15, 15, 15),
            cv::FILLED
        );

        // Blend the dark box with the minimap underneath.
        cv::addWeighted(
            spikeOverlay,
            0.80,
            resizedImg,
            0.20,
            0.0,
            resizedImg
        );

        // Convert the remaining number of seconds into
        // a timer such as 0:45, 0:32, or 0:05.
        int spikeMinutes =
            spikeSecondsRemaining / 60;

        int spikeSeconds =
            spikeSecondsRemaining % 60;

        QString spikeText =
            QString("SPIKE  %1:%2")
            .arg(spikeMinutes)
            .arg(
                spikeSeconds,
                2,
                10,
                QChar('0')
            );

        std::string text =
            spikeText.toStdString();

        int baseline = 0;

        cv::Size textSize =
            cv::getTextSize(
                text,
                cv::FONT_HERSHEY_SIMPLEX,
                0.55,
                2,
                &baseline
            );

        int x =
            (resizedImg.cols - textSize.width) / 2;

        // Black outline around the text.
        cv::putText(
            resizedImg,
            text,
            cv::Point(x, 365),
            cv::FONT_HERSHEY_SIMPLEX,
            0.55,
            cv::Scalar(0, 0, 0),
            4,
            cv::LINE_AA
        );

        // Bright red/pink foreground to distinguish
        // the Spike timer from the normal round timer.
        cv::putText(
            resizedImg,
            text,
            cv::Point(x, 365),
            cv::FONT_HERSHEY_SIMPLEX,
            0.55,
            cv::Scalar(80, 100, 255),
            2,
            cv::LINE_AA
        );
    }


    // --------------------------------------------------
    // Display final minimap
    // --------------------------------------------------

    pixmap =
        cvMatToQPixmap(resizedImg);

    minimap_wid->setPixmap(
        pixmap.scaled(
            minimap_wid->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        )
    );
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
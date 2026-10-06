#include "../headers/minimap.h"

#include <nlohmann/json.hpp>

#include <fstream>

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

void Minimap::redrawAgents(Eigen::MatrixXf frameData) {

    //Don't redraw if there are no predictions or if the predictions are unchanged from the last frame
    if (frameData.rows() == 0) { return; }
    if ((frameData.rows() == lastPredictions.rows()) && 
        (frameData.cols() == lastPredictions.cols()) &&  
        (frameData.isApprox(lastPredictions))) { return; }

    //Store the new predictions as the last predictions for then ext frame
    lastPredictions = frameData;

    //Reload the original minimap
    cv::Mat image = cv::imread(mapFile.toStdString());
    cv::Mat resizedImg;
    cv::resize(image, resizedImg, cv::Size(384, 384)); //Resize to 384x384 since that's what the model's predictions are based on


    for (int i = 0; i < frameData.rows(); i++) {

        //Get prediction data
        int xmin = frameData(i, 0);
        int ymin = frameData(i, 1);
        int xmax = frameData(i, 2);
        int ymax = frameData(i, 3);
        int trackID = frameData(i, 4);
        int classID = frameData(i, 6);

        //Get the right agent
        cv::Mat agent = agentIcons[classID];

        std::vector<cv::Mat> channels;
        cv::split(agent, channels);

        cv::Mat alpha;
        alpha = channels[3]; // Extract the 4th channel
        cv::Mat bgr_foreground;
        cv::merge(std::vector<cv::Mat>{channels[0], channels[1], channels[2]}, bgr_foreground);

        //If the area to draw on is OOB, don't do it (should change it later to just shift it to fit within bounds) 
        if (xmin < 0 || ymin < 0 || xmin + agent.cols > resizedImg.cols || ymin + agent.rows > resizedImg.rows) {
            continue;
        }
        cv::Rect rect(xmin, ymin, agent.cols, agent.rows);
        cv::Mat ROI = resizedImg(rect);

        bgr_foreground.copyTo(ROI, alpha);

       // cv::String text = std::to_string(trackID);
       // cv::putText(resizedImg, text, cv::Point(xmin, ymin - 10), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
       // cv::putText(resizedImg, text, cv::Point(xmin, ymin - 10), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
    }

    // Update the displayed minimap
    QPixmap newPixmap = cvMatToQPixmap(resizedImg);
    if (!pixmap.isNull()) {
        newPixmap = newPixmap.scaled(minimap_wid->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    if (newPixmap.toImage() != pixmap.toImage()) {
        pixmap = newPixmap;
        minimap_wid->setPixmap(pixmap);
    }

}

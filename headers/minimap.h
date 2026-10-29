#ifndef MINIMAP_H
#define MINIMAP_H

#include <QDockWidget>
#include <QLabel>
#include <QPixmap>

#include <Eigen/Dense>
#include <opencv2/opencv.hpp>

#include <vector>
#include <string>


#include <nlohmann/json.hpp>

// This class is used by the side bar to show logs and reset bar
class Minimap : public QDockWidget {

private:
	QLabel* minimap_wid;
	QPixmap pixmap;

	QPixmap cvMatToQPixmap(const cv::Mat &inMat);
	QString mapFile = "";

	std::vector<std::string> idToName;
	std::vector<cv::Mat> agentIcons;

	Eigen::MatrixXf lastPredictions; //We store this so that we don't have to redraw the minimap if nothing has changed
	// True while replay footage is active.
	// Agent positions should not be displayed during a replay.
	bool replayActive = false;

public:
	// sets the parent to nullptr if no main parent
	explicit Minimap(QWidget* parent = nullptr);

	// allows to load an image to the minimap widget
	void loadImage(const QString &file);

	// allows the image in the widget to resize without losing the quality
	void resizeEvent(QResizeEvent *event);

	void redrawAgents(Eigen::MatrixXf frameData);

	// Enables or disables agent placement during replay footage.
	void setReplayActive(bool active);

	// Image Detection 2.0
	// Returns the detected map layout path.
	// Returns an empty QString if the frame is not confident enough.
	QString checkMap(const cv::Mat& inputFrame);
	
};

#endif